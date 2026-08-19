"""Auditable creative-quality contract for PULSO's rendered MIDI.

This gate never deletes a technically valid arrangement.  It tells Live and the musician
whether the audible score is genuinely AI-authored and structurally convincing, keeping
MIDI integrity separate from artistic acceptance.
"""


def _number(payload, key, default=0.0):
    try:
        return float(payload.get(key, default))
    except (TypeError, ValueError):
        return float(default)


def evaluate_creative_quality(request):
    audited = bool(request.get("narrative_audited", False))
    if not audited:
        return {"audited": False, "passed": True, "codes": [], "score": 1.0}

    domain = str(request.get("production_domain", "adaptive")).casefold()
    foreground = _number(request, "foreground_ai_authorship_ratio", 0.0)
    movement_bass = _number(request, "movement_bass_ai_authorship_ratio", 0.0)
    foreground_notes = int(_number(request, "foreground_note_count", 0.0))
    movement_bass_notes = int(_number(request, "movement_bass_note_count", 0.0))
    groove = _number(request, "groove_authorship_coverage", 0.0)
    bass_continuity = _number(request, "bass_phrase_continuity", 0.0)
    scalar_run = int(_number(request, "maximum_melodic_step_run", 0.0))
    drum_gap = int(_number(request, "maximum_club_drum_gap_bars", 0.0))
    low_end_gap = int(_number(request, "maximum_club_low_end_gap_bars", 0.0))
    score = _number(request, "creative_score", request.get("narrative_score", 0.0))

    codes = []
    if foreground_notes >= 8 and foreground < 0.85:
        codes.append("procedural_foreground")
    if movement_bass_notes >= 8 and movement_bass < 0.75:
        codes.append("procedural_movement_bass")
    if movement_bass_notes >= 8 and bass_continuity < 0.60:
        codes.append("fragmented_bass_narrative")
    if scalar_run > 5:
        codes.append("scalar_melody_without_speech")
    if domain == "club_electronic":
        if groove < 0.45:
            codes.append("groove_not_ai_authored")
        if drum_gap > 16:
            codes.append("club_pulse_absent_too_long")
        if low_end_gap > 16:
            codes.append("low_end_absent_too_long")
    if score < 0.76:
        codes.append("creative_score_below_gate")
    if request.get("creative_ready") is False and "creative_score_below_gate" not in codes:
        codes.append("composer_marked_for_revision")

    return {
        "audited": True,
        "passed": not codes and bool(request.get("creative_ready", True)),
        "codes": codes,
        "score": score,
        "foreground_ai_authorship_ratio": foreground,
        "movement_bass_ai_authorship_ratio": movement_bass,
        "foreground_note_count": foreground_notes,
        "movement_bass_note_count": movement_bass_notes,
        "groove_authorship_coverage": groove,
        "bass_phrase_continuity": bass_continuity,
        "maximum_melodic_step_run": scalar_run,
        "maximum_club_drum_gap_bars": drum_gap,
        "maximum_club_low_end_gap_bars": low_end_gap,
    }
