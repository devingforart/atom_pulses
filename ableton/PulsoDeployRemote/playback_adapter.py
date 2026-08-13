"""Translate compositional MIDI into the contract of the loaded Live instrument."""

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
        return ("maracas", "shaker", "cabasa")
    if catalog_id == "timpani":
        return ("timpani",)
    if catalog_id in ("taiko_ensemble", "orchestral_percussion"):
        return ("low tom", "floor tom", "tom") if pitch <= 45 else ("mid tom", "tom")
    if catalog_id == "cymbals":
        return ("ride", "cymbal") if pitch in (51, 53, 59) else ("crash", "cymbal")
    if catalog_id == "latin_percussion":
        meanings = {
            60: ("high bongo", "bongo"), 61: ("low bongo", "bongo"),
            62: ("mute high conga", "muted conga", "conga"),
            63: ("open high conga", "open conga", "conga"),
            64: ("low conga", "conga"), 69: ("cabasa",), 70: ("maracas", "shaker"),
            75: ("claves", "clave"), 76: ("high wood block", "wood block"),
            77: ("low wood block", "wood block"),
        }
        return meanings.get(pitch, ("percussion",))
    return ()


def expand_percussion_specs(specs):
    """Split incompatible articulations before sound selection when one-shot playback is likely."""
    split_catalogs = {"snare_clap", "hi_hats", "taiko_ensemble", "orchestral_percussion"}
    expanded = []
    for source in specs:
        catalog_id = str(source.get("catalog_id", "")).casefold()
        pitches = sorted(set(int(note.get("pitch", 60)) for note in source.get("notes", [])))
        if catalog_id not in split_catalogs or len(pitches) <= 1:
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
            item["track_key"] = "{}:pitch:{}".format(source.get("track_key", "part"), pitch)
            expanded.append(item)
    return expanded


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
    adapted = []
    remapped = {}
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
        duration = max(1.0 / 960.0, float(note.get("duration", 0.25)))
        note["duration"] = min(duration, cap) if cap is not None else duration
        note["velocity"] = max(1, min(127, int(note.get("velocity", 100))))
        adapted.append(note)
    adapted.sort(key=lambda item: (item["start"], item["pitch"]))

    # Overlapping equal pitches are undefined in channel MIDI: the first note-off can
    # terminate the newer note. Trim the earlier event while preserving legato elsewhere.
    overlap_repairs = 0
    previous_by_pitch = {}
    for note in adapted:
        previous = previous_by_pitch.get(note["pitch"])
        if previous is not None:
            previous_end = previous["start"] + previous["duration"]
            if previous_end > note["start"]:
                previous["duration"] = max(1.0 / 960.0, note["start"] - previous["start"])
                overlap_repairs += 1
        previous_by_pitch[note["pitch"]] = note
    return adapted, {
        "source_kind": source_kind,
        "root_note": int(root_note) if source_kind == "one_shot" else None,
        "pitch_remap": {str(source): target for source, target in sorted(remapped.items())},
        "duration_cap": cap,
        "overlap_repairs": overlap_repairs,
    }
