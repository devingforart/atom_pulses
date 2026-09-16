"""Deterministic, role-aware audition sounds for editable PULSO MIDI.

The audition layer deliberately does not interpret poetic preset descriptions.  Its only
job is to make the written notes immediately audible through a small, stable set of Live
devices.  Sound design remains metadata and can be replaced by the producer afterwards.
"""


PROFILE_DEVICES = {
    "drum": ("909 Core Kit.adg", "808 Core Kit.adg"),
    "sub": ("Operator", "Drift", "Wavetable"),
    "bass": ("Operator", "Drift", "Wavetable"),
    "pluck": ("Operator", "Drift", "Wavetable"),
    "arp": ("Operator", "Drift", "Wavetable"),
    "chord": ("Drift", "Operator", "Wavetable"),
    "pad": ("Wavetable", "Drift", "Operator"),
    "lead": ("Wavetable", "Drift", "Operator"),
    "texture": ("Wavetable", "Drift", "Operator"),
}

PROFILE_PATCHES = {
    "drum": {"attack": 0.0, "release": 0.22, "cutoff": 0.62, "motion": 0.0,
             "width": 0.18},
    "sub": {"attack": 0.01, "release": 0.12, "cutoff": 0.28, "motion": 0.0,
            "width": 0.0},
    "bass": {"attack": 0.01, "release": 0.18, "cutoff": 0.38, "motion": 0.02,
             "width": 0.04},
    "pluck": {"attack": 0.0, "release": 0.12, "cutoff": 0.55, "motion": 0.0,
              "width": 0.12},
    "arp": {"attack": 0.0, "release": 0.10, "cutoff": 0.58, "motion": 0.0,
            "width": 0.10},
    "chord": {"attack": 0.04, "release": 0.28, "cutoff": 0.48, "motion": 0.02,
              "width": 0.26},
    "pad": {"attack": 0.18, "release": 0.58, "cutoff": 0.44, "motion": 0.08,
            "width": 0.38},
    "lead": {"attack": 0.02, "release": 0.24, "cutoff": 0.60, "motion": 0.03,
             "width": 0.16},
    "texture": {"attack": 0.24, "release": 0.72, "cutoff": 0.50, "motion": 0.10,
                "width": 0.42},
}


def audition_profile(spec):
    """Resolve one stable monitoring profile from musical function, never from chance."""
    department = str(spec.get("department", "harmony")).casefold()
    catalog = str(spec.get("catalog_id", "")).casefold()
    function = str(spec.get("orchestral_function", "")).casefold()
    role = str(spec.get("role", "")).casefold()
    intent = str(spec.get("authored_preset_intent", spec.get("preset_intent", ""))).casefold()
    words = " ".join((catalog.replace("_", " "), function, role, intent))
    if department == "rhythm":
        return "drum"
    if catalog == "sub_synth":
        return "sub"
    if catalog in {"ambient_texture", "granular_pad", "spectral_drone", "noise_riser",
                   "shimmer_tail", "vocal_chop_texture"}:
        return "texture"
    if catalog in {"analog_pad", "string_ensemble", "chamber_strings", "choir"}:
        return "pad"
    if any(value in words for value in ("texture", "noise", "riser", "shimmer", "spectral",
                                        "transition", "atmosphere", "ambient", "upper air")):
        return "texture"
    if any(value in words for value in ("pad", "drone", "sustained", "foundation", "pedal",
                                        "floor", "continuum", "harmonic field")):
        return "pad"
    if catalog in {"electric_bass", "rolling_mid_bass", "reese_layer", "contrabass",
                   "contrabassoon"} or any(value in words for value in ("bass line", "low end")):
        return "bass"
    if any(value in words for value in ("arpeggio", "arpeggiated", " arp", "ostinato",
                                        "sequence", "pulse")):
        return "arp"
    if any(value in words for value in ("pluck", "staccato", "stab", "detached", "mallet",
                                        "piano", "harp", "guitar")):
        return "pluck"
    if department == "melody" or any(value in words for value in
                                      ("lead", "protagonist", "foreground", "countermelody")):
        return "lead"
    return "chord"


def is_audition_fallback(quality):
    """True only when Live could not load the intended or neutral audition sound."""
    return str(quality or "").casefold() not in {"identity", "neutral_audition"}


def apply_neutral_contract(source):
    """Return an export spec whose sound is auditable and whose authored intent is retained."""
    spec = dict(source)
    profile = audition_profile(spec)
    candidates = PROFILE_DEVICES[profile]
    spec.setdefault("authored_native_device", str(spec.get("native_device", "auto")))
    spec.setdefault("authored_preset_intent", str(spec.get("preset_intent", "")))
    spec["audition_policy"] = "neutral_role_v1"
    spec["audition_profile"] = profile
    spec["native_device"] = "Drum Rack" if profile == "drum" else candidates[0]
    spec["device_candidates"] = list(candidates)
    spec["preset_intent"] = "PULSO neutral {} audition".format(profile)
    spec["minimum_intent_fidelity"] = 0.0
    spec["strict_timbre_gate"] = False
    spec["sound_locked"] = False
    spec["sound_variation"] = 0
    spec["audible_variant_count"] = 1
    return spec


def select_neutral_sound(items, spec):
    """Select the first installed device in the versioned profile fallback order."""
    profile = audition_profile(spec)
    for wanted in PROFILE_DEVICES[profile]:
        exact = []
        for name, path, item in items:
            if str(name).strip().casefold() != wanted.casefold():
                continue
            normalized_path = str(path).casefold().replace("\\", "/")
            # Prefer the factory instrument itself over a coincidentally named user preset.
            instrument_root = "/instruments/" in normalized_path or normalized_path.startswith("instruments/")
            exact.append((not instrument_root, normalized_path, name, path, item))
        if exact:
            exact.sort(key=lambda value: value[:2])
            _, _, name, path, item = exact[0]
            return name, path, item, "neutral_audition", False
    return None


def neutral_patch(profile):
    return dict(PROFILE_PATCHES.get(str(profile).casefold(), PROFILE_PATCHES["chord"]))
