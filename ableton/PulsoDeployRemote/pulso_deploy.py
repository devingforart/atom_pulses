"""PULSO -> Ableton Live native arrangement and sound deployment bridge.

The bridge only indexes Ableton instruments, Racks, Max for Live devices and the
User Library. It deliberately never visits the Plug-Ins browser root.
"""

import json
import os
import traceback

import Live  # pyright: ignore[reportMissingImports]
from _Framework.ControlSurface import ControlSurface  # pyright: ignore[reportMissingImports]
from .request_guard import RequestGuard
from .playback_adapter import (adapt_notes, build_drum_pitch_map, deployment_outcome,
                               expand_percussion_specs, is_one_shot_path,
                               note_specification_arguments, project_expression)
from .sound_matcher import catalog_capabilities, select_track_sound


class PulsoDeployRemote(ControlSurface):
    POLL_TICKS = 45
    INVENTORY_BATCH = 80
    INVENTORY_LIMIT = 6000

    def __init__(self, c_instance):
        super(PulsoDeployRemote, self).__init__(c_instance)
        local_data = os.environ.get("LOCALAPPDATA", "")
        self._bridge_dir = os.path.join(local_data, "PULSO", "LiveBridge")
        self._request_file = os.path.join(self._bridge_dir, "request.json")
        self._status_file = os.path.join(self._bridge_dir, "status.json")
        self._heartbeat_file = os.path.join(self._bridge_dir, "heartbeat.json")
        self._inventory_file = os.path.join(self._bridge_dir, "inventory.json")
        # request.json intentionally remains on disk as the latest deployment snapshot.
        # Adopt it before the first poll so opening PULSO or restarting Live can never
        # replay an old command. Only CREATE IN LIVE writes a different UUID.
        self._request_guard = RequestGuard(self._request_file)
        self._deployed_tracks = []
        self._previous_deployed_tracks = []
        self._verified_tracks = []
        self._failed_tracks = []
        self._device_queue = []
        self._native_items = []
        self._inventory_queue = []
        self._inventory_complete = False
        self._loaded_devices = 0
        self._fallback_devices = 0
        self._missing_devices = []
        self._sound_report = []
        self._deployment_total = 0
        self._deployment_busy = False
        self._running = True
        self.schedule_message(2, self._start_inventory)
        self.schedule_message(10, self._poll)
        self._write_status("ready", "LIVE NATIVE BRIDGE READY")

    def disconnect(self):
        self._running = False
        super(PulsoDeployRemote, self).disconnect()

    def _poll(self):
        if not self._running:
            return
        if os.path.isfile(self._request_file):
            try:
                with open(self._request_file, "r", encoding="utf-8") as source:
                    request = json.load(source)
                if self._request_guard.claim(request):
                    self._deploy(request)
            except Exception as error:
                self._write_status("error", "DEPLOY FAILED: {}".format(error), traceback.format_exc())
        self._write_heartbeat()
        self.schedule_message(self.POLL_TICKS, self._poll)

    def _start_inventory(self):
        browser = getattr(self.application(), "browser", None)
        if browser is None:
            self._write_status("error", "LIVE BROWSER API UNAVAILABLE")
            return
        roots = []
        # Never add browser.plugins here. Native-only is a product guarantee.
        for attribute in ("sounds", "drums", "instruments", "max_for_live", "user_library"):
            root = getattr(browser, attribute, None)
            if root is not None:
                roots.append((root, attribute, 0))
        self._inventory_queue = roots
        self._native_items = []
        self.schedule_message(1, self._scan_inventory_batch)

    def _scan_inventory_batch(self):
        processed = 0
        while self._inventory_queue and processed < self.INVENTORY_BATCH and len(self._native_items) < self.INVENTORY_LIMIT:
            item, parent_path, depth = self._inventory_queue.pop(0)
            processed += 1
            name = str(getattr(item, "name", "")).strip()
            path = parent_path + ("/" + name if name else "")
            if bool(getattr(item, "is_loadable", False)):
                self._native_items.append((name, path, item))
            if depth < 9:
                for child in getattr(item, "children", ()):
                    self._inventory_queue.append((child, path, depth + 1))
        if self._inventory_queue and len(self._native_items) < self.INVENTORY_LIMIT:
            self.schedule_message(1, self._scan_inventory_batch)
            return
        self._inventory_complete = True
        self._write_inventory()
        self._write_status("ready", "LIVE NATIVE READY - {} SOUNDS INDEXED".format(len(self._native_items)))

    def _write_inventory(self):
        try:
            os.makedirs(self._bridge_dir, exist_ok=True)
            payload = {
                "schema_version": 2,
                "source": "ableton_live_native",
                "complete": self._inventory_complete,
                "loadable_count": len(self._native_items),
                "items": [{"name": name, "path": path} for name, path, _ in self._native_items],
                "capabilities": catalog_capabilities(self._native_items),
            }
            temp = self._inventory_file + ".pending"
            with open(temp, "w", encoding="utf-8") as target:
                json.dump(payload, target, ensure_ascii=False)
            os.replace(temp, self._inventory_file)
        except Exception:
            pass

    def _deploy(self, request):
        if self._deployment_busy:
            self._write_status("busy", "LIVE DEPLOYMENT ALREADY IN PROGRESS")
            return
        tracks = expand_percussion_specs(request.get("tracks", []))
        if request.get("schema_version") not in (2, 3, 4, 5, 6) or not tracks:
            raise RuntimeError("invalid or empty deployment request")
        if request.get("sound_engine", "ableton_live_native") != "ableton_live_native":
            raise RuntimeError("unsupported sound engine")
        # Resolve the complete playback contract before touching the Set. This catches
        # unavailable identities and empty-container fallbacks without creating tracks.
        resolved = []
        used_paths = set()
        unresolved = []
        for spec in tracks:
            match = select_track_sound(self._native_items, spec, used_paths)
            if match is None:
                unresolved.append(str(spec.get("name", "PULSO Part")))
                continue
            used_paths.add(str(match[1]).casefold())
            resolved.append((spec, match))
        if not resolved:
            self._write_status("rejected", "LIVE PREFLIGHT REJECTED - NO AUDIBLE SOUNDS AVAILABLE",
                               {"missing": unresolved, "committed_tracks": 0,
                                "previous_deployment_preserved": bool(self._deployed_tracks)})
            return
        song = self.song()
        self._previous_deployed_tracks = list(self._deployed_tracks)
        self._device_queue = []
        self._deployed_tracks = []
        self._verified_tracks = []
        self._failed_tracks = []
        self._loaded_devices = 0
        self._fallback_devices = 0
        self._missing_devices = list(unresolved)
        self._sound_report = [{"track": name, "state": "preflight_no_playable_match"}
                              for name in unresolved]
        self._deployment_total = len(tracks)
        self._deployment_busy = True
        if hasattr(song, "tempo"):
            song.tempo = max(20.0, min(999.0, float(request.get("bpm", song.tempo))))
        try:
            for spec, match in resolved:
                song.create_midi_track(-1)
                track_index = len(song.tracks) - 1
                track = song.tracks[track_index]
                track.name = str(spec.get("name", "PULSO Part"))[:120]
                track.mute = False
                self._deployed_tracks.append(track)
                # Live can invalidate a Python Track wrapper while Browser.load_item() is
                # completing. Keep the stable Set index across the asynchronous boundary and
                # reacquire a fresh wrapper at every callback.
                self._device_queue.append((track_index, spec, match, float(request.get("length_beats", 4.0))))
        except Exception:
            # Track creation is a transaction-level Live API failure. This is the one class
            # of error that rolls the complete staging set back.
            self._remove_tracks(song, self._deployed_tracks)
            self._deployed_tracks = self._previous_deployed_tracks
            self._previous_deployed_tracks = []
            self._device_queue = []
            self._deployment_busy = False
            raise
        self._write_status("loading", "STAGING {} TRACKS - LOADING VERIFIED SOUNDS".format(len(resolved)))
        self.schedule_message(2, self._load_next_device)

    def _load_next_device(self):
        if not self._running:
            return
        total = self._deployment_total
        if not self._device_queue:
            details = {"loaded": self._loaded_devices, "fallbacks": self._fallback_devices,
                       "missing": self._missing_devices, "sounds": self._sound_report}
            if self._loaded_devices == 0:
                self._remove_tracks(self.song(), self._deployed_tracks)
                self._deployed_tracks = self._previous_deployed_tracks
                self._previous_deployed_tracks = []
                self._deployment_busy = False
                details["committed_tracks"] = 0
                details["previous_deployment_preserved"] = bool(self._deployed_tracks)
                self._write_status("rejected", "LIVE DEPLOYMENT REJECTED - NO AUDIBLE TRACKS",
                                   details)
                return
            self._remove_tracks(self.song(), self._failed_tracks)
            self._deployed_tracks = list(self._verified_tracks)
            state, message = deployment_outcome(self._loaded_devices, total,
                                                len(self._missing_devices), self._fallback_devices)
            details["committed_tracks"] = self._loaded_devices
            details["skipped_tracks"] = len(self._missing_devices)
            details["previous_deployment_preserved"] = False
            self._remove_tracks(self.song(), self._previous_deployed_tracks)
            self._previous_deployed_tracks = []
            self._deployment_busy = False
            self._write_status(state, message, details)
            return
        track_index, spec, match, length_beats = self._device_queue.pop(0)
        track = self._track_at(track_index)
        if track is None:
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
            self._sound_report.append({"track": str(spec.get("name", "PULSO Part")),
                                       "state": "staging_track_disappeared"})
            self.schedule_message(2, self._load_next_device)
            return
        self.song().view.selected_track = track
        matched = self._load_native_sound(match)
        if matched is None:
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
            self._failed_tracks.append(track)
            self._sound_report.append({"track": str(spec.get("name", "PULSO Part")),
                                       "state": "no_playable_match"})
            self.schedule_message(2, self._load_next_device)
            return
        quality, shared, matched_name, matched_path, before_count = matched
        # Browser loads are asynchronous. Verify the new track actually received a device
        # instead of treating load_item() returning as proof of audible sound.
        self.schedule_message(8, lambda: self._verify_loaded_sound(
            track_index, spec, quality, shared, matched_name, matched_path, before_count,
            length_beats, 0))

    def _verify_loaded_sound(self, track_index, spec, quality, shared, matched_name, matched_path,
                             before_count, length_beats, attempt):
        if not self._running:
            return
        track = self._track_at(track_index)
        if track is None:
            if attempt < 2:
                self.schedule_message(6, lambda: self._verify_loaded_sound(
                    track_index, spec, quality, shared, matched_name, matched_path,
                    before_count, length_beats, attempt + 1))
                return
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
            self._sound_report.append({"track": str(spec.get("name", "PULSO Part")),
                                       "state": "staging_track_disappeared"})
            self.schedule_message(2, self._load_next_device)
            return
        valid = False
        reason = "device_not_created"
        notes = []
        adaptation = None
        try:
            devices = list(track.devices)
            valid = len(devices) > before_count
            if valid and devices:
                device = devices[-1]
                if bool(getattr(device, "can_have_chains", False)) and len(list(device.chains)) == 0:
                    valid = False
                    reason = "empty_rack"
                if valid and bool(getattr(device, "can_have_drum_pads", False)):
                    populated = []
                    for pad in device.drum_pads:
                        chains = list(pad.chains)
                        if not chains:
                            continue
                        label = str(getattr(pad, "name", "")) + " " + " ".join(
                            str(getattr(chain, "name", "")) for chain in chains)
                        populated.append((int(pad.note), label))
                    pitch_map = build_drum_pitch_map(spec, populated)
                    if pitch_map is None:
                        valid = False
                        reason = "no_semantic_drum_pad"
                    else:
                        notes, adaptation = adapt_notes(spec, "drum_rack", pitch_map=pitch_map)
                elif valid and is_one_shot_path(matched_path):
                    notes, adaptation = adapt_notes(spec, "one_shot", root_note=60)
                elif valid:
                    notes, adaptation = adapt_notes(spec, "chromatic")
                if valid:
                    notes, expression_report = project_expression(
                        spec, notes, adaptation.get("source_kind", "chromatic"))
                    adaptation["expression"] = expression_report
                    adapted_spec = dict(spec)
                    adapted_spec["notes"] = notes
                    insertion, insertion_error = self._create_arrangement_clip(
                        track, adapted_spec, length_beats)
                    adaptation["note_insertion"] = insertion
                    if insertion_error:
                        adaptation["modern_note_error"] = insertion_error
        except Exception as error:
            # A Browser load may briefly leave Live's Python wrapper without its native
            # Track handle. A fresh wrapper on a later tick is safe; rejecting the whole
            # transaction immediately is not.
            if attempt < 2 and ("TrackPyHandle" in str(error) or "None.None(Track)" in str(error)):
                self.schedule_message(6, lambda: self._verify_loaded_sound(
                    track_index, spec, quality, shared, matched_name, matched_path,
                    before_count, length_beats, attempt + 1))
                return
            valid = False
            reason = "playback_adaptation_error:" + str(error)[:120]
        report = {"track": str(spec.get("name", "PULSO Part")),
                  "catalog_id": str(spec.get("catalog_id", "")), "matched": matched_name,
                  "path": matched_path, "quality": quality, "shared_sound": bool(shared),
                  "adaptation": adaptation,
                  "state": "verified" if valid else reason}
        self._sound_report.append(report)
        if valid:
            self._loaded_devices += 1
            self._verified_tracks.append(track)
            if quality != "identity":
                self._fallback_devices += 1
            track.name = (str(spec.get("name", track.name)) + " | " + matched_name)[:120]
            self._apply_mixer_defaults(track, spec)
        else:
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
            self._failed_tracks.append(track)
        completed = len(self._deployed_tracks) - len(self._device_queue)
        self._write_status("loading", "VERIFYING LIVE SOUNDS {}/{}".format(
            completed, len(self._deployed_tracks)), {"sounds": self._sound_report})
        self.schedule_message(2, self._load_next_device)

    def _track_at(self, index):
        try:
            tracks = list(self.song().tracks)
            return tracks[index] if 0 <= index < len(tracks) else None
        except (RuntimeError, TypeError):
            return None

    def _load_native_sound(self, item):
        browser = getattr(self.application(), "browser", None)
        if browser is None or not hasattr(browser, "load_item"):
            return None
        if item is not None:
            try:
                before_count = len(list(self.song().view.selected_track.devices))
                browser.load_item(item[2])
                return item[3], item[4], item[0], item[1], before_count
            except Exception:
                return None
        return None

    @staticmethod
    def _apply_mixer_defaults(track, spec):
        try:
            gain_db = max(-18.0, min(0.0, float(spec.get("mixer_gain_db", -6.0))))
            # Live's track volume uses a normalized/device-specific range; this conservative
            # mapping leaves headroom without depending on an undocumented dB conversion.
            track.mixer_device.volume.value = 0.58 + (gain_db + 18.0) / 18.0 * 0.18
        except Exception:
            pass

    def _remove_previous_deployment(self, song):
        self._remove_tracks(song, self._deployed_tracks)
        self._deployed_tracks = []

    @staticmethod
    def _remove_tracks(song, tracks):
        current_tracks = list(song.tracks)
        indices = []
        for deployed in tracks:
            try:
                indices.append(current_tracks.index(deployed))
            except (ValueError, RuntimeError):
                pass
        for index in sorted(set(indices), reverse=True):
            try:
                song.delete_track(index)
            except RuntimeError:
                pass

    @staticmethod
    def _create_arrangement_clip(track, spec, length_beats):
        if not hasattr(track, "create_midi_clip"):
            raise RuntimeError("this Live version does not expose Arrangement clip creation")
        clip = track.create_midi_clip(0.0, max(0.25, length_beats))
        clip.name = str(spec.get("name", "PULSO Part"))[:120]
        notes = []
        extended_note_args = []
        for item in spec.get("notes", []):
            pitch = max(0, min(127, int(item.get("pitch", 60))))
            start = max(0.0, float(item.get("start", 0.0)))
            duration = max(1.0 / 960.0, float(item.get("duration", 0.25)))
            velocity = max(1, min(127, int(item.get("velocity", 100))))
            notes.append((pitch, start, duration, velocity, False))
            extended_note_args.append(note_specification_arguments(item))
        modern_error = None
        if extended_note_args and hasattr(clip, "add_new_notes"):
            try:
                # The Python Remote Script runtime differs from Max's dictionary LOM:
                # it consumes MidiNoteSpecification objects directly.
                extended_notes = tuple(Live.Clip.MidiNoteSpecification(*arguments)
                                       for arguments in extended_note_args)
                clip.add_new_notes(tuple(extended_notes))
                if hasattr(clip, "deselect_all_notes"):
                    clip.deselect_all_notes()
                return "live12_midi_note_specifications", None
            except Exception as error:
                modern_error = "{}: {}".format(type(error).__name__, str(error))[:180]
        if notes and hasattr(clip, "set_notes"):
            clip.set_notes(tuple(notes))
            if hasattr(clip, "deselect_all_notes"):
                clip.deselect_all_notes()
            return "legacy_notes_with_baked_expression", modern_error
        if notes:
            raise RuntimeError("this Live version does not expose MIDI note insertion")

    def _write_heartbeat(self):
        try:
            os.makedirs(self._bridge_dir, exist_ok=True)
            temp = self._heartbeat_file + ".pending"
            with open(temp, "w", encoding="utf-8") as target:
                json.dump({"alive": True, "native_inventory": len(self._native_items)}, target)
            os.replace(temp, self._heartbeat_file)
        except Exception:
            pass

    def _write_status(self, state, message, details=None):
        try:
            os.makedirs(self._bridge_dir, exist_ok=True)
            temp = self._status_file + ".pending"
            payload = {"state": state, "message": message}
            if details is not None:
                payload["details"] = details
            with open(temp, "w", encoding="utf-8") as target:
                json.dump(payload, target, ensure_ascii=False)
            os.replace(temp, self._status_file)
        except Exception:
            pass
