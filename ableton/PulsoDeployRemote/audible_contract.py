"""Audible realization contracts that operate on Live's public object model.

The Remote Script API exposes device parameters and per-track output meters, but not an
offline audio bounce or spectral buffers. This module therefore measures only claims it can
prove: envelope bounds, audible output during authored notes, and residual output after the
role-specific release window.
"""

import re


RELEASE_SECONDS = {
    "kick_drum": 0.14, "snare_clap": 0.30, "hi_hats": 0.16,
    "shakers": 0.22, "latin_percussion": 0.28,
    "orchestral_percussion": 0.55, "cymbals": 1.40,
    "sub_synth": 0.16, "electric_bass": 0.24,
    "poly_synth": 0.22, "piano": 0.55, "guitar": 0.42,
    "lead_synth": 0.42, "flute": 0.55, "alto_flute": 0.60,
    "piccolo": 0.48, "analog_pad": 0.95, "ambient_texture": 1.35,
}

EFFECT_DEVICE_WORDS = {
    "compressor", "limiter", "reverb", "delay", "echo", "gate", "utility",
    "saturator", "chorus", "flanger", "phaser",
}


def release_target_seconds(spec):
    explicit = spec.get("release_max_seconds")
    if explicit is not None:
        return max(0.04, min(4.0, float(explicit)))
    catalog = str(spec.get("catalog_id", "")).strip().casefold()
    target = RELEASE_SECONDS.get(catalog, 0.65)
    identity = str(spec.get("articulation_identity", "")).strip().casefold()
    if "closed hat" in identity or "pedal hat" in identity:
        target = min(target, 0.12)
    elif "open hat" in identity:
        target = max(target, 0.32)
    elif any(word in identity for word in ("ride", "crash", "splash", "china")):
        target = max(target, 1.20)
    return target


def _parse_time_seconds(value):
    text = str(value).strip().casefold().replace(",", ".")
    match = re.search(r"(-?\d+(?:\.\d+)?)\s*(ms|msec|s|sec|seconds?)\b", text)
    if not match:
        return None
    amount = float(match.group(1))
    return amount / 1000.0 if match.group(2) in ("ms", "msec") else amount


def _parameter_display(parameter, value):
    formatter = getattr(parameter, "str_for_value", None)
    if callable(formatter):
        try:
            return formatter(value)
        except (RuntimeError, TypeError, ValueError):
            return ""
    return str(getattr(parameter, "display_value", ""))


def _cap_time_parameter(parameter, target_seconds):
    try:
        minimum = float(parameter.min)
        maximum = float(parameter.max)
        current = float(parameter.value)
    except (AttributeError, TypeError, ValueError):
        return False, "parameter_range_unavailable"
    current_seconds = _parse_time_seconds(_parameter_display(parameter, current))
    if current_seconds is None:
        return False, "parameter_time_unit_unavailable"
    if current_seconds <= target_seconds + 0.005:
        return False, "already_safe"
    low, high = minimum, maximum
    best = minimum
    parsed = False
    for _ in range(28):
        middle = (low + high) * 0.5
        seconds = _parse_time_seconds(_parameter_display(parameter, middle))
        if seconds is None:
            break
        parsed = True
        if seconds <= target_seconds:
            best = middle
            low = middle
        else:
            high = middle
    if not parsed:
        return False, "parameter_curve_unavailable"
    try:
        parameter.value = best
        return True, "capped"
    except (RuntimeError, TypeError, ValueError):
        return False, "parameter_write_failed"


def _walk_devices(device, depth=0):
    if device is None or depth > 4:
        return
    yield device
    for chain in getattr(device, "chains", ()):
        for child in getattr(chain, "devices", ()):
            for nested in _walk_devices(child, depth + 1):
                yield nested


def apply_release_contract(device, spec):
    target = release_target_seconds(spec)
    report = {
        "target_seconds": target,
        "parameters_found": 0,
        "parameters_capped": 0,
        "already_safe": 0,
        "unresolved_parameters": 0,
        "status": "no_release_parameter",
    }
    for candidate in _walk_devices(device):
        device_name = str(getattr(candidate, "name", "")).casefold()
        if any(word in device_name for word in EFFECT_DEVICE_WORDS):
            continue
        for parameter in getattr(candidate, "parameters", ()):
            name = str(getattr(parameter, "name", "")).strip().casefold()
            if not re.search(r"(^|\s)(amp\s+)?release($|\s)", name):
                continue
            report["parameters_found"] += 1
            changed, reason = _cap_time_parameter(parameter, target)
            if changed:
                report["parameters_capped"] += 1
            elif reason == "already_safe":
                report["already_safe"] += 1
            else:
                report["unresolved_parameters"] += 1
    if report["parameters_capped"]:
        report["status"] = "capped"
    elif report["parameters_found"] and not report["unresolved_parameters"]:
        report["status"] = "already_safe"
    elif report["parameters_found"]:
        report["status"] = "partially_unresolved"
    return report


def meter_snapshot(spec, meter_level, beat, bpm=120.0):
    """Classify one real output-meter observation against authored note activity."""
    beat = float(beat)
    meter = max(0.0, float(meter_level))
    notes = list(spec.get("notes", ()))
    active = any(float(note.get("start", 0.0)) <= beat <
                 float(note.get("start", 0.0)) + float(note.get("duration", 0.25))
                 for note in notes)
    previous_ends = [float(note.get("start", 0.0)) + float(note.get("duration", 0.25))
                     for note in notes
                     if float(note.get("start", 0.0)) + float(note.get("duration", 0.25)) <= beat]
    last_end = max(previous_ends) if previous_ends else None
    release_beats = release_target_seconds(spec) * max(20.0, float(bpm)) / 60.0
    tail_window_over = last_end is not None and beat > last_end + release_beats + 0.125
    audible = meter >= 0.001
    return {
        "expected_active": active,
        "audible": audible,
        "silent_while_active": bool(active and not audible),
        "tail_violation": bool(not active and tail_window_over and audible),
        "meter_level": meter,
    }


def aggregate_meter_snapshots(snapshots):
    snapshots = list(snapshots)
    expected = sum(1 for item in snapshots if item.get("expected_active"))
    audible_expected = sum(1 for item in snapshots
                           if item.get("expected_active") and item.get("audible"))
    tail_violations = sum(1 for item in snapshots if item.get("tail_violation"))
    return {
        "observations": len(snapshots),
        "expected_active_observations": expected,
        "audible_active_observations": audible_expected,
        "audible_presence_ratio": (float(audible_expected) / expected) if expected else 1.0,
        "tail_violations": tail_violations,
        "spectral_analysis_available": False,
        "spectral_analysis_reason": "live_remote_script_exposes_meters_not_audio_buffers",
    }
