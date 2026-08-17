"""Translate compositional MIDI into the contract of the loaded Live instrument."""

import bisect
import os

from .sound_matcher import tokens


RHYTHM_CATALOGS = {
    "kick_drum", "snare_clap", "hi_hats", "timpani", "taiko_ensemble",
    "latin_percussion", "shakers", "cymbals", "orchestral_percussion", "production_drums",
}

DURATION_CAPS = {
    "kick_drum": 0.25, "snare_clap": 0.25, "hi_hats": 0.25,
    "timpani": 0.5, "taiko_ensemble": 0.5, "latin_percussion": 0.25,
    "shakers": 0.25, "cymbals": 0.5, "orchestral_percussion": 0.5,
    "marimba": 1.0, "vibraphone": 1.5, "celesta": 1.0, "tubular_bells": 0.5,
    "flute": 2.5, "piccolo": 2.0, "alto_flute": 2.5, "oboe": 2.5,
    "english_horn": 3.0, "clarinet": 2.5, "bass_clarinet": 3.0,
    "bassoon": 3.0, "contrabassoon": 3.0, "woodwind_ensemble": 3.0,
    "french_horns": 3.5, "trumpets": 3.0, "trombones": 3.5,
    "bass_trombone": 3.5, "tuba": 3.5, "brass_ensemble": 3.5,
    "piano": 2.0, "harp": 1.5, "guitar": 1.5,
}

# Extremely short note-offs can choke one-shots and make an otherwise valid percussion
# gesture disappear. Floors are intentionally smaller than the musical grid: they protect
# playback without moving attacks or flattening articulation.
DURATION_FLOORS = {
    "kick_drum": 1.0 / 16.0, "snare_clap": 1.0 / 16.0,
    "hi_hats": 1.0 / 16.0, "shakers": 1.0 / 16.0,
    "latin_percussion": 1.0 / 8.0, "timpani": 1.0 / 8.0,
    "taiko_ensemble": 1.0 / 8.0, "orchestral_percussion": 1.0 / 8.0,
    "cymbals": 1.0 / 8.0,
}

AUDIBLE_VARIANT_IDENTITIES = {
    "clap", "snare", "side stick", "closed hat", "pedal hat", "open hat",
    "maracas", "shaker",
}

CRITICAL_TIMBRE_CATALOGS = {
    "kick_drum", "sub_synth", "electric_bass", "lead_synth",
}

# Chromatic instruments need enough gate time to produce a stable pitch and a deliberate
# release. These limits are deliberately role-specific: a bass pluck may be short, while
# a bowed string, pad or texture must not receive a 31 ms repair fragment.
MUSICAL_DURATION_FLOORS = {
    "sub_synth": 1.0 / 8.0, "electric_bass": 1.0 / 8.0,
    "piano": 1.0 / 8.0, "harp": 1.0 / 8.0, "guitar": 1.0 / 8.0,
    "marimba": 1.0 / 8.0, "vibraphone": 1.0 / 8.0,
    "celesta": 1.0 / 8.0, "tubular_bells": 1.0 / 8.0,
    "lead_synth": 1.0 / 8.0, "poly_synth": 1.0 / 8.0,
    "analog_pad": 1.0 / 2.0, "ambient_texture": 1.0 / 2.0,
    "violin_1": 1.0 / 4.0, "violin_2": 1.0 / 4.0,
    "viola": 1.0 / 4.0, "cello": 1.0 / 4.0,
    "contrabass": 1.0 / 4.0, "string_ensemble": 1.0 / 4.0,
    "chamber_strings": 1.0 / 4.0, "flute": 1.0 / 4.0,
    "piccolo": 1.0 / 4.0, "alto_flute": 1.0 / 4.0,
    "oboe": 1.0 / 4.0, "english_horn": 1.0 / 4.0,
    "clarinet": 1.0 / 4.0, "bass_clarinet": 1.0 / 4.0,
    "bassoon": 1.0 / 4.0, "contrabassoon": 1.0 / 4.0,
    "woodwind_ensemble": 1.0 / 4.0, "french_horns": 1.0 / 4.0,
    "trumpets": 1.0 / 4.0, "trombones": 1.0 / 4.0,
    "bass_trombone": 1.0 / 4.0, "tuba": 1.0 / 4.0,
    "brass_ensemble": 1.0 / 4.0, "choir": 1.0 / 4.0,
}


def is_one_shot_path(path):
    return os.path.splitext(str(path).casefold())[1] in (".wav", ".aif", ".aiff", ".flac")


def drum_semantics(catalog_id, pitch):
    catalog_id = str(catalog_id).casefold()
    pitch = int(pitch)
    if catalog_id == "kick_drum":
        return ("kick",)
    if catalog_id == "snare_clap":
        if pitch == 39:
            return ("clap",)
        if pitch == 37:
            return ("side stick", "sidestick", "rim")
        return ("snare", "clap")
    if catalog_id == "hi_hats":
        if pitch == 46:
            return ("open hat", "open hihat")
        if pitch == 44:
            return ("pedal hat", "pedal hihat")
        return ("closed hat", "closed hihat", "hat")
    if catalog_id == "shakers":
        if pitch == 46:
            return ("open hat", "open hihat")
        if pitch == 58:
            return ("vibraslap", "shaker")
        if pitch == 70:
            return ("maracas", "shaker")
        if pitch == 74:
            return ("long guiro", "guiro")
        return ("maracas", "shaker", "cabasa")
    if catalog_id == "timpani":
        return ("timpani",)
    if catalog_id == "taiko_ensemble":
        return ("low tom", "floor tom", "tom") if pitch <= 45 else ("mid tom", "tom")
    if catalog_id == "orchestral_percussion":
        meanings = {
            37: ("side stick", "sidestick", "rim"),
            41: ("low floor tom", "floor tom", "tom"), 43: ("high floor tom", "floor tom", "tom"),
            45: ("low tom", "tom"), 47: ("low mid tom", "tom"),
            48: ("high mid tom", "tom"), 50: ("high tom", "tom"),
            49: ("crash", "cymbal"), 51: ("ride", "cymbal"),
            52: ("chinese cymbal", "china cymbal", "cymbal"),
            53: ("ride bell", "ride", "cymbal"), 55: ("splash", "cymbal"),
            57: ("crash", "cymbal"), 59: ("ride", "cymbal"),
            54: ("tambourine",), 56: ("cowbell",),
            60: ("high bongo", "bongo"), 61: ("low bongo", "bongo"),
            62: ("mute high conga", "muted conga", "conga"),
            63: ("open high conga", "open conga", "conga"), 64: ("low conga", "conga"),
            65: ("high timbale", "timbale"), 66: ("low timbale", "timbale"),
            67: ("high agogo", "agogo"), 68: ("low agogo", "agogo"),
            69: ("cabasa",), 70: ("maracas", "shaker"),
            75: ("claves", "clave"), 76: ("high wood block", "wood block"),
            77: ("low wood block", "wood block"), 80: ("mute triangle", "triangle"),
            81: ("open triangle", "triangle"),
        }
        return meanings.get(pitch, ("percussion",))
    if catalog_id == "cymbals":
        if pitch in (51, 53, 59):
            return ("ride", "cymbal")
        if pitch == 52:
            return ("chinese cymbal", "china cymbal", "cymbal")
        if pitch == 55:
            return ("splash cymbal", "splash")
        return ("crash", "cymbal")
    if catalog_id == "latin_percussion":
        meanings = {
            41: ("low floor tom", "floor tom", "tom"),
            43: ("high floor tom", "floor tom", "tom"),
            45: ("low tom", "tom"), 47: ("low mid tom", "tom"),
            48: ("high mid tom", "tom"), 50: ("high tom", "tom"),
            57: ("crash", "cymbal"),
            60: ("high bongo", "bongo"), 61: ("low bongo", "bongo"),
            62: ("mute high conga", "muted conga", "conga"),
            63: ("open high conga", "open conga", "conga"),
            64: ("low conga", "conga"),
            65: ("high timbale", "timbale"), 66: ("low timbale", "timbale"),
            69: ("cabasa",), 70: ("maracas", "shaker"),
            75: ("claves", "clave"), 76: ("high wood block", "wood block"),
            77: ("low wood block", "wood block"),
        }
        return meanings.get(pitch, ("percussion",))
    return ()


def expand_percussion_specs(specs):
    """Split incompatible articulations before sound selection when one-shot playback is likely."""
    split_catalogs = {"snare_clap", "hi_hats", "shakers", "taiko_ensemble",
                      "latin_percussion", "orchestral_percussion", "cymbals"}
    expanded = []
    for source in specs:
        catalog_id = str(source.get("catalog_id", "")).casefold()
        pitches = sorted(set(int(note.get("pitch", 60)) for note in source.get("notes", [])))
        if catalog_id not in split_catalogs or not pitches:
            expanded.append(source)
            continue
        for pitch in pitches:
            item = dict(source)
            item["notes"] = [dict(note) for note in source.get("notes", [])
                             if int(note.get("pitch", 60)) == pitch]
            aliases = drum_semantics(catalog_id, pitch)
            identity = aliases[0] if aliases else "articulation {}".format(pitch)
            item["name"] = "{} | {}".format(source.get("name", "PULSO Part"), identity.title())
            item["preset_intent"] = identity
            item["articulation_identity"] = identity
            item["articulation_aliases"] = list(aliases)
            item["track_key"] = "{}:pitch:{}".format(source.get("track_key", "part"), pitch)
            expanded.append(item)
    return expanded


def requested_audible_variants(spec):
    """Return a bounded number of independently sounded lanes for repeated attacks.

    Variation is deliberately limited to identities for which alternating samples is a
    normal production technique. A kick, ride or orchestral hit keeps one stable identity.
    """
    identity = str(spec.get("articulation_identity", "")).strip().casefold()
    notes = list(spec.get("notes", ()))
    if identity not in AUDIBLE_VARIANT_IDENTITIES or len(notes) < 16:
        return 1
    return 3 if len(notes) >= 256 else 2


def split_audible_variants(spec, count):
    """Distribute attacks across velocity-aware deterministic round-robin lanes."""
    count = max(1, min(3, int(count)))
    notes = sorted((dict(note) for note in spec.get("notes", ())),
                   key=lambda note: (float(note.get("start", 0.0)),
                                     int(note.get("pitch", 60))))
    if count == 1 or len(notes) < count:
        item = dict(spec)
        item["notes"] = notes
        item["audible_variant_count"] = 1
        item["audible_variant_index"] = 0
        return [item]
    velocities = sorted(int(note.get("velocity", 100)) for note in notes)
    median = velocities[len(velocities) // 2]
    lanes = [[] for _ in range(count)]
    for ordinal, note in enumerate(notes):
        phrase = int(float(note.get("start", 0.0)) // 32.0)
        velocity_band = 1 if int(note.get("velocity", 100)) > median else 0
        lane = (ordinal + phrase + velocity_band) % count
        lanes[lane].append(note)
    if any(not lane for lane in lanes):
        return split_audible_variants(spec, 1)
    result = []
    for index, lane in enumerate(lanes):
        item = dict(spec)
        item["notes"] = lane
        item["name"] = "{} | RR {}".format(spec.get("name", "PULSO Part"), index + 1)
        item["track_key"] = "{}:rr:{}".format(spec.get("track_key", "part"), index)
        item["audible_variant_group"] = str(spec.get("track_key", "part"))
        item["audible_variant_count"] = count
        item["audible_variant_index"] = index
        item["audible_variant_mode"] = "velocity_phrase_round_robin"
        result.append(item)
    return result


def timbre_contract(spec, fidelity, quality="identity"):
    """Evaluate the binding timbre contract before any Live track is created."""
    catalog = str(spec.get("catalog_id", "")).strip().casefold()
    priority = str(spec.get("timbre_priority", "")).strip().casefold()
    if not priority:
        priority = "critical" if catalog in CRITICAL_TIMBRE_CATALOGS else "support"
    default_floor = 0.65 if priority == "critical" else 0.35
    floor = max(0.0, min(1.0, float(spec.get("minimum_intent_fidelity", default_floor))))
    fidelity = max(0.0, min(1.0, float(fidelity)))
    passed = fidelity + 1.0e-9 >= floor
    return {
        "priority": priority,
        "minimum_fidelity": floor,
        "fidelity": fidelity,
        "quality": str(quality),
        "passed": passed,
        "blocking": priority == "critical" and not passed,
    }


ARTICULATION_SUBSTITUTIONS = {
    # Preserve the rhythmic gesture inside the same physical family. The authored identity
    # remains in metadata, so a deliberate substitution can never masquerade as an exact hit.
    "vibraslap": (
        ("maracas", ("maracas", "shaker")),
        ("long guiro", ("long guiro", "guiro")),
        ("cabasa", ("cabasa",)),
    ),
    "pedal hat": (("closed hat", ("closed hat", "closed hihat", "hat")),),
    "chinese cymbal": (
        ("crash", ("crash", "cymbal")),
        ("splash cymbal", ("splash cymbal", "splash")),
    ),
    "splash cymbal": (("crash", ("crash", "cymbal")),),
    "mute high conga": (("open high conga", ("open high conga", "open conga", "conga")),),
    "open high conga": (("mute high conga", ("mute high conga", "muted conga", "conga")),),
}


def articulation_substitution_specs(spec):
    """Return declared, same-family alternatives without altering MIDI attack timing."""
    authored = str(spec.get("articulation_identity", "")).strip().casefold()
    result = []
    for identity, aliases in ARTICULATION_SUBSTITUTIONS.get(authored, ()):
        candidate = dict(spec)
        candidate["notes"] = [dict(note) for note in spec.get("notes", [])]
        candidate["authored_articulation_identity"] = authored
        candidate["articulation_identity"] = identity
        candidate["articulation_aliases"] = list(aliases)
        candidate["preset_intent"] = identity
        base_name = str(spec.get("name", "PULSO Part")).split(" | ", 1)[0]
        candidate["name"] = "{} | {} (for {})".format(
            base_name, identity.title(), authored.title())
        candidate["track_key"] = "{}:sub:{}".format(
            spec.get("track_key", "part"), identity.replace(" ", "_"))
        candidate["substitution_reason"] = "authored_articulation_unavailable"
        result.append(candidate)
    return result


def note_specification_arguments(item):
    """Return the positional Live.Clip.MidiNoteSpecification constructor contract."""
    return (
        max(0, min(127, int(item.get("pitch", 60)))),
        max(0.0, float(item.get("start", item.get("start_time", 0.0)))),
        max(1.0 / 960.0, float(item.get("duration", 0.25))),
        float(max(1, min(127, int(item.get("velocity", 100))))),
        bool(item.get("mute", False)),
        max(0.0, min(1.0, float(item.get("probability", 1.0)))),
        float(max(-127, min(127, int(item.get("velocity_deviation", 0))))),
        float(max(1, min(127, int(item.get("release_velocity", 64))))),
    )


def best_pad_for_semantics(populated_pads, aliases):
    best = None
    best_score = 0
    for note, label in populated_pads:
        label_tokens = set(tokens(label))
        score = 0
        for alias in aliases:
            alias_tokens = set(tokens(alias))
            if alias_tokens and alias_tokens.issubset(label_tokens):
                score = max(score, len(alias_tokens) * 10)
        if score > best_score:
            best_score = score
            best = int(note)
    return best


def build_drum_pitch_map(spec, populated_pads):
    populated_notes = set(int(note) for note, _ in populated_pads)
    mapping = {}
    for pitch in sorted(set(int(note.get("pitch", 60)) for note in spec.get("notes", []))):
        if pitch in populated_notes:
            mapping[pitch] = pitch
            continue
        target = best_pad_for_semantics(populated_pads,
                                        drum_semantics(spec.get("catalog_id", ""), pitch))
        if target is None:
            return None
        mapping[pitch] = target
    return mapping


def adapt_notes(spec, source_kind="chromatic", pitch_map=None, root_note=60):
    """Return sanitized note dictionaries and a reproducible adaptation report."""
    catalog_id = str(spec.get("catalog_id", "")).casefold()
    cap = DURATION_CAPS.get(catalog_id)
    role_text = "{} {} {}".format(spec.get("name", ""), spec.get("role", ""),
                                   spec.get("orchestral_function", "")).casefold()
    if "chord stab" in role_text or "harmonic punctuation" in role_text:
        cap = min(1.0, cap) if cap is not None else 1.0
    floor = DURATION_FLOORS.get(
        catalog_id, MUSICAL_DURATION_FLOORS.get(catalog_id, 1.0 / 16.0))
    adapted = []
    remapped = {}
    duration_repairs = 0
    for source in spec.get("notes", []):
        note = dict(source)
        original_pitch = max(0, min(127, int(note.get("pitch", 60))))
        target_pitch = original_pitch
        if source_kind == "one_shot" and catalog_id in RHYTHM_CATALOGS:
            target_pitch = int(root_note)
        elif pitch_map is not None:
            target_pitch = int(pitch_map.get(original_pitch, original_pitch))
        if target_pitch != original_pitch:
            remapped[original_pitch] = target_pitch
        note["pitch"] = max(0, min(127, target_pitch))
        note["start"] = max(0.0, float(note.get("start", 0.0)))
        source_duration = float(note.get("duration", 0.25))
        duration = max(floor, source_duration)
        if duration > source_duration + 1.0e-9:
            duration_repairs += 1
        note["duration"] = min(duration, cap) if cap is not None else duration
        note["velocity"] = max(1, min(127, int(note.get("velocity", 100))))
        adapted.append(note)
    adapted.sort(key=lambda item: (item["start"], item["pitch"]))

    # Overlapping equal pitches are undefined in channel MIDI: the first note-off can
    # terminate the newer note. Trim the earlier event while preserving legato elsewhere.
    overlap_repairs = 0
    inaudible_notes_removed = 0
    previous_by_pitch = {}
    for note in adapted:
        previous = previous_by_pitch.get(note["pitch"])
        if previous is not None:
            previous_end = previous["start"] + previous["duration"]
            if previous_end > note["start"]:
                available = note["start"] - previous["start"]
                if available + 1.0e-9 < floor:
                    # Two same-pitch attacks closer than the audible gate cannot both be
                    # represented safely in channel MIDI. Keep the later musical attack.
                    previous["_pulso_drop"] = True
                    inaudible_notes_removed += 1
                else:
                    previous["duration"] = available
                    overlap_repairs += 1
        previous_by_pitch[note["pitch"]] = note
    adapted = [note for note in adapted if not note.pop("_pulso_drop", False)]
    return adapted, {
        "source_kind": source_kind,
        "root_note": int(root_note) if source_kind == "one_shot" else None,
        "pitch_remap": {str(source): target for source, target in sorted(remapped.items())},
        "duration_cap": cap,
        "duration_floor": floor,
        "duration_repairs": duration_repairs,
        "overlap_repairs": overlap_repairs,
        "inaudible_notes_removed": inaudible_notes_removed,
    }


def _curve(events):
    ordered = sorted(events, key=lambda item: float(item.get("beat", 0.0)))
    return ([float(item.get("beat", 0.0)) for item in ordered],
            [int(item.get("value", 0)) for item in ordered])


def _value_at(curve, beat, default):
    """Linearly sample a controller curve without depending on Live's envelope API."""
    beats, values = curve
    if not beats:
        return default
    right = bisect.bisect_right(beats, beat)
    if right == 0:
        return values[0]
    if right >= len(beats):
        return values[-1]
    start = beats[right - 1]
    end = beats[right]
    if end <= start:
        return values[right - 1]
    amount = max(0.0, min(1.0, (beat - start) / (end - start)))
    return int(round(values[right - 1] * (1.0 - amount) + values[right] * amount))


def project_expression(spec, notes, source_kind="chromatic"):
    """Bake the portable part of CC/pressure performance into Live note properties.

    Raw curves remain in the deployment contract. Velocity, release, duration and Live 12
    note-expression fields make the interpretation audible even where Arrangement clip
    MIDI-controller envelopes are not exposed by the Python API.
    """
    controls_by_cc = {}
    for event in spec.get("controls", []):
        controller = max(0, min(127, int(event.get("controller", 0))))
        controls_by_cc.setdefault(controller, []).append(event)
    curves = {controller: _curve(events) for controller, events in controls_by_cc.items()}
    pressure = [event for event in spec.get("expressions", [])
                if str(event.get("type", "")) in ("channel_pressure", "poly_aftertouch")]
    pressure_curve = _curve(pressure)
    pedal = controls_by_cc.get(64, [])
    pedal_curve = curves.get(64, ([], []))
    projected = []
    velocity_changes = 0
    sustain_extensions = 0
    for source in notes:
        note = dict(source)
        beat = float(note.get("start", 0.0))
        expression = _value_at(curves.get(11, ([], [])), beat, 100)
        modulation = _value_at(curves.get(1, ([], [])), beat, 0)
        brightness = _value_at(curves.get(74, ([], [])), beat, 64)
        pressure_value = _value_at(pressure_curve, beat, 0)
        original_velocity = int(note.get("velocity", 100))
        expression_scale = 0.55 + 0.57 * (expression / 127.0)
        shaped_velocity = max(1, min(127, int(round(
            original_velocity * expression_scale + pressure_value * 0.055))))
        if shaped_velocity != original_velocity:
            velocity_changes += 1
        note["velocity"] = shaped_velocity
        note["velocity_deviation"] = max(-127, min(127, int(round(
            (modulation - 32) * 0.18 + (brightness - 64) * 0.10))))
        note["release_velocity"] = max(1, min(127, int(round(
            48 + brightness * 0.42 + pressure_value * 0.16))))
        note["probability"] = 1.0
        if source_kind == "chromatic" and _value_at(pedal_curve, beat, 0) >= 64:
            note_end = beat + float(note.get("duration", 0.25))
            pedal_off = next((event_beat for event_beat, value in zip(*pedal_curve)
                              if event_beat > note_end and value < 64), None)
            if pedal_off is not None:
                extended = min(beat + 2.0, pedal_off) - beat
                if extended > float(note.get("duration", 0.25)):
                    note["duration"] = extended
                    sustain_extensions += 1
        projected.append(note)
    return projected, {
        "controls_received": sum(len(items) for items in controls_by_cc.values()),
        "expressions_received": len(spec.get("expressions", [])),
        "velocity_changes": velocity_changes,
        "sustain_extensions": sustain_extensions,
        "extended_note_properties": bool(projected),
    }


def audible_deployment_score(loaded, total, missing=0, fallbacks=0,
                             chromatic_duration_repairs=0, inaudible_notes_removed=0,
                             mean_intent_fidelity=1.0, mean_intent_consistency=1.0):
    """Quality of the MIDI that actually became audible, not the abstract source score."""
    total = max(1, int(total))
    verified = max(0.0, min(1.0, float(loaded) / total))
    penalty = max(0, int(fallbacks)) / total * 0.18
    penalty += min(0.12, max(0, int(chromatic_duration_repairs)) * 0.003)
    penalty += min(0.24, max(0, int(inaudible_notes_removed)) * 0.025)
    penalty += max(0, int(missing)) / total * 0.12
    penalty += (1.0 - max(0.0, min(1.0, float(mean_intent_fidelity)))) * 0.24
    penalty += (1.0 - max(0.0, min(1.0, float(mean_intent_consistency)))) * 0.12
    return max(0.0, min(1.0, verified - penalty))


def deployment_outcome(loaded, total, missing=0, fallbacks=0):
    """Pure transaction policy shared by tests and the Live callback implementation."""
    loaded = max(0, int(loaded))
    total = max(0, int(total))
    missing = max(0, int(missing))
    fallbacks = max(0, int(fallbacks))
    if loaded == 0:
        return "rejected", "LIVE DEPLOYMENT REJECTED - NO AUDIBLE TRACKS"
    state = "degraded" if missing else "complete"
    message = "LIVE NATIVE COMPLETE - {}/{} PLAYBACK CONTRACTS VERIFIED".format(loaded, total)
    if fallbacks:
        message += " - {} FALLBACKS".format(fallbacks)
    if missing:
        message += " - {} TRACK{} SKIPPED".format(missing, "" if missing == 1 else "S")
    return state, message
