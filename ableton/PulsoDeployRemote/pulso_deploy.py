"""PULSO -> Ableton Live native arrangement and sound deployment bridge.

The bridge only indexes Ableton instruments, Racks, Max for Live devices and the
User Library. It deliberately never visits the Plug-Ins browser root.
"""

import json
import os
import traceback

from _Framework.ControlSurface import ControlSurface
from .request_guard import RequestGuard
from .sound_matcher import EMPTY_CONTAINERS, best_inventory_match


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
        self._device_queue = []
        self._native_items = []
        self._inventory_queue = []
        self._inventory_complete = False
        self._loaded_devices = 0
        self._fallback_devices = 0
        self._missing_devices = []
        self._sound_report = []
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
                "schema_version": 1,
                "source": "ableton_live_native",
                "complete": self._inventory_complete,
                "loadable_count": len(self._native_items),
                "items": [{"name": name, "path": path} for name, path, _ in self._native_items],
            }
            temp = self._inventory_file + ".pending"
            with open(temp, "w", encoding="utf-8") as target:
                json.dump(payload, target, ensure_ascii=False)
            os.replace(temp, self._inventory_file)
        except Exception:
            pass

    def _deploy(self, request):
        tracks = request.get("tracks", [])
        if request.get("schema_version") not in (2, 3) or not tracks:
            raise RuntimeError("invalid or empty deployment request")
        if request.get("sound_engine", "ableton_live_native") != "ableton_live_native":
            raise RuntimeError("unsupported sound engine")
        song = self.song()
        self._remove_previous_deployment(song)
        self._device_queue = []
        self._deployed_tracks = []
        self._loaded_devices = 0
        self._fallback_devices = 0
        self._missing_devices = []
        self._sound_report = []
        if hasattr(song, "tempo"):
            song.tempo = max(20.0, min(999.0, float(request.get("bpm", song.tempo))))
        for spec in tracks:
            song.create_midi_track(-1)
            track = song.tracks[-1]
            track.name = str(spec.get("name", "PULSO Part"))[:120]
            track.mute = False
            self._deployed_tracks.append(track)
            self._create_arrangement_clip(track, spec, float(request.get("length_beats", 4.0)))
            self._device_queue.append((track, spec))
        self._write_status("loading", "CREATED {} TRACKS - MATCHING LIVE SOUNDS".format(len(tracks)))
        self.schedule_message(2, self._load_next_device)

    def _load_next_device(self):
        if not self._running:
            return
        total = len(self._deployed_tracks)
        if not self._device_queue:
            message = "LIVE NATIVE COMPLETE - {}/{} SOUNDS LOADED".format(self._loaded_devices, total)
            if self._fallback_devices:
                message += " - {} FALLBACKS".format(self._fallback_devices)
            if self._missing_devices:
                message += " - {} MISSING".format(len(self._missing_devices))
            self._write_status("complete" if not self._missing_devices else "partial", message,
                               {"loaded": self._loaded_devices, "fallbacks": self._fallback_devices,
                                "missing": self._missing_devices, "sounds": self._sound_report})
            return
        track, spec = self._device_queue.pop(0)
        self.song().view.selected_track = track
        matched = self._load_native_sound(spec)
        if matched is None:
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
            self._sound_report.append({"track": str(spec.get("name", "PULSO Part")),
                                       "state": "no_playable_match"})
            self.schedule_message(2, self._load_next_device)
            return
        used_fallback, matched_name, matched_path, before_count = matched
        # Browser loads are asynchronous. Verify the new track actually received a device
        # instead of treating load_item() returning as proof of audible sound.
        self.schedule_message(8, lambda: self._verify_loaded_sound(
            track, spec, used_fallback, matched_name, matched_path, before_count))

    def _verify_loaded_sound(self, track, spec, used_fallback, matched_name, matched_path, before_count):
        if not self._running:
            return
        valid = False
        reason = "device_not_created"
        try:
            devices = list(track.devices)
            valid = len(devices) > before_count
            if valid and devices:
                device = devices[-1]
                if bool(getattr(device, "can_have_chains", False)) and len(list(device.chains)) == 0:
                    valid = False
                    reason = "empty_rack"
        except (RuntimeError, AttributeError):
            valid = False
            reason = "track_unavailable"
        report = {"track": str(spec.get("name", "PULSO Part")), "matched": matched_name,
                  "path": matched_path, "state": "verified" if valid else reason}
        self._sound_report.append(report)
        if valid:
            self._loaded_devices += 1
            if used_fallback:
                self._fallback_devices += 1
            track.name = (str(spec.get("name", track.name)) + " | " + matched_name)[:120]
            self._apply_mixer_defaults(track, spec)
        else:
            self._missing_devices.append(str(spec.get("name", "PULSO Part")))
        completed = len(self._deployed_tracks) - len(self._device_queue)
        self._write_status("loading", "VERIFYING LIVE SOUNDS {}/{}".format(
            completed, len(self._deployed_tracks)), {"sounds": self._sound_report})
        self.schedule_message(2, self._load_next_device)

    def _load_native_sound(self, spec):
        browser = getattr(self.application(), "browser", None)
        if browser is None or not hasattr(browser, "load_item"):
            return None
        candidates = [str(value).strip() for value in spec.get("device_candidates", []) if str(value).strip()]
        intent = str(spec.get("preset_intent", "")).strip()
        device = str(spec.get("native_device", "Instrument Rack")).strip()
        if intent and intent not in candidates:
            candidates.insert(0, intent)
        if device and device not in candidates:
            candidates.append(device)
        for index, query in enumerate(candidates):
            if query.casefold() in EMPTY_CONTAINERS:
                continue
            item = best_inventory_match(self._native_items, query, device if index == 0 else "")
            if item is None:
                continue
            try:
                before_count = len(list(self.song().view.selected_track.devices))
                browser.load_item(item[2])
                return index > 0, item[0], item[1], before_count
            except Exception:
                continue
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
        current_tracks = list(song.tracks)
        indices = []
        for deployed in self._deployed_tracks:
            try:
                indices.append(current_tracks.index(deployed))
            except (ValueError, RuntimeError):
                pass
        for index in sorted(set(indices), reverse=True):
            try:
                song.delete_track(index)
            except RuntimeError:
                pass
        self._deployed_tracks = []

    @staticmethod
    def _create_arrangement_clip(track, spec, length_beats):
        if not hasattr(track, "create_midi_clip"):
            raise RuntimeError("this Live version does not expose Arrangement clip creation")
        clip = track.create_midi_clip(0.0, max(0.25, length_beats))
        clip.name = str(spec.get("name", "PULSO Part"))[:120]
        notes = []
        for item in spec.get("notes", []):
            notes.append((max(0, min(127, int(item.get("pitch", 60)))),
                          max(0.0, float(item.get("start", 0.0))),
                          max(1.0 / 960.0, float(item.get("duration", 0.25))),
                          max(1, min(127, int(item.get("velocity", 100)))), False))
        if notes and hasattr(clip, "set_notes"):
            clip.set_notes(tuple(notes))
            if hasattr(clip, "deselect_all_notes"):
                clip.deselect_all_notes()
            return
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
