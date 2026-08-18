"""Role-aware matching for playable Ableton browser items.

Musical identity is a hard gate. Descriptive words ("solo", "warm", "section")
can rank two valid cellos, but can never turn a bass or riser into a cello.
"""

import hashlib
import re
import unicodedata


EMPTY_CONTAINERS = {
    "drum rack", "instrument rack", "sampler", "simpler", "drum sampler",
    "impulse", "external instrument",
}

GENERIC_TOKENS = {
    "acoustic", "aire", "air", "amplio", "balanced", "calido", "clean",
    "cola", "corta", "corto", "dark", "deep", "dry", "electronic", "ensemble",
    "evolving", "expressive", "granulado", "grave", "graves", "intimate", "intimo",
    "kit", "largo", "long", "mate", "modern", "movil", "natural", "noble",
    "orchestral", "oscura", "oscuro", "processed", "profundo", "redondo",
    "resonancia", "seco", "secos", "section", "short", "soft", "solo", "warm", "wide",
}

# Each tier is tried in order. Later tiers are declared, audible family fallbacks.
# Aliases are phrases; every word in one alias must occur as a complete token.
IDENTITY_TIERS = {
    "kick_drum": (("kick",),),
    "snare_clap": (("snare", "clap", "sidestick", "rim"),),
    "hi_hats": (("hi hat", "hihat", "hat"),),
    "timpani": (("timpani",), ("orchestral percussion", "percussion")),
    "taiko_ensemble": (("taiko",), ("tom", "percussion")),
    "latin_percussion": (("latin percussion", "conga", "bongo", "cowbell", "tambourine"),
                         ("percussion",)),
    "shakers": (("shaker", "cabasa", "maraca"), ("percussion",)),
    "cymbals": (("cymbal", "crash", "ride"),),
    "orchestral_percussion": (("orchestral percussion", "tom", "timpani"), ("percussion",)),
    "piano": (("piano",), ("keys",)),
    "harp": (("harp",), ("pluck", "strings")),
    "violin_1": (("violin",), ("strings",)),
    "violin_2": (("violin",), ("strings",)),
    "viola": (("viola",), ("strings",)),
    "cello": (("cello",), ("strings",)),
    "contrabass": (("contrabass", "double bass"), ("low strings", "strings")),
    "string_ensemble": (("string ensemble", "strings",),),
    "chamber_strings": (("chamber strings", "strings",),),
    "flute": (("flute",), ("wind", "winds")),
    "piccolo": (("piccolo",), ("flute", "wind")),
    "alto_flute": (("alto flute", "flute"), ("wind",)),
    "oboe": (("oboe",), ("wind", "winds", "reed")),
    "english_horn": (("english horn",), ("horn", "wind", "reed")),
    "clarinet": (("clarinet",), ("wind", "reed")),
    "bass_clarinet": (("bass clarinet", "clarinet"), ("wind", "reed")),
    "bassoon": (("bassoon",), ("wind", "reed")),
    "contrabassoon": (("contrabassoon", "bassoon"), ("wind", "reed")),
    "woodwind_ensemble": (("woodwind", "winds"), ("flute", "clarinet", "reed")),
    "french_horns": (("french horn", "horns", "horn"), ("brass",)),
    "trumpets": (("trumpet",), ("brass",)),
    "trombones": (("trombone",), ("brass",)),
    "bass_trombone": (("bass trombone", "trombone"), ("brass",)),
    "tuba": (("tuba",), ("brass",)),
    "brass_ensemble": (("brass ensemble", "brass"),),
    "choir": (("choir", "choral", "voice"),),
    "mallets": (("mallet",), ("marimba", "vibraphone", "bells")),
    "celesta": (("celesta",), ("bells", "keys")),
    "vibraphone": (("vibraphone", "vibes"), ("mallet",)),
    "marimba": (("marimba",), ("mallet",)),
    "tubular_bells": (("tubular bells",), ("bells",)),
    "electric_bass": (("electric bass", "bass guitar"), ("bass",)),
    "sub_synth": (("sub bass", "sub",), ("bass",)),
    "analog_pad": (("analog pad", "pad"),),
    "poly_synth": (("poly synth", "synth keys"), ("synth",)),
    "lead_synth": (("synth lead", "lead"), ("synth",)),
    "guitar": (("guitar",), ("pluck",)),
    "ambient_texture": (("ambient texture", "texture", "granular"), ("ambient",)),
    "production_drums": (("core kit", "drum kit", "kit"),),
    "harmonic_ensemble": (("strings", "pad", "piano", "keys"),),
    "foreground_voice": (("lead", "violin", "cello", "flute"),),
}

FAMILY_PATH_HINTS = {
    "violin_1": ("/strings/",), "violin_2": ("/strings/",),
    "viola": ("/strings/",), "cello": ("/strings/",),
    "contrabass": ("/strings/",), "string_ensemble": ("/strings/",),
    "chamber_strings": ("/strings/",),
    "flute": ("/winds/",), "piccolo": ("/winds/",), "alto_flute": ("/winds/",),
    "oboe": ("/winds/",), "english_horn": ("/winds/",), "clarinet": ("/winds/",),
    "bass_clarinet": ("/winds/",), "bassoon": ("/winds/",),
    "contrabassoon": ("/winds/",), "woodwind_ensemble": ("/winds/",),
    "french_horns": ("/brass/",), "trumpets": ("/brass/",),
    "trombones": ("/brass/",), "bass_trombone": ("/brass/",),
    "tuba": ("/brass/",), "brass_ensemble": ("/brass/",),
    "kick_drum": ("/kick/", "/percussive/"), "snare_clap": ("/snare/", "/percussive/"),
    "hi_hats": ("/hihat/", "/percussive/"), "timpani": ("/misc percussion/", "/percussive/"),
    "taiko_ensemble": ("/tom/", "/percussive/"),
    "latin_percussion": ("/conga/", "/bongo/", "/bell/", "/misc percussion/", "/percussive/"),
    "shakers": ("/shaker/", "/misc percussion/", "/percussive/"),
    "cymbals": ("/cymbal/", "/percussive/"),
    "orchestral_percussion": ("/tom/", "/misc percussion/", "/percussive/"),
    "piano": ("/piano & keys/",), "electric_bass": ("/bass/",),
    "sub_synth": ("/bass/",), "analog_pad": ("/pad/",),
    "lead_synth": ("/synth lead/",), "ambient_texture": ("/ambient & evolving/",),
}

KIT_REQUIRED_CATALOGS = {"production_drums"}

# A wrong named solo instrument is more misleading than a neutral synth fallback. Ensemble
# strings may substitute within their family, but solo winds retain exact identity.
STRICT_IDENTITY_CATALOGS = {
    "flute", "piccolo", "oboe", "english_horn", "clarinet", "bass_clarinet",
    "bassoon", "contrabassoon", "french_horns", "trumpets", "trombones",
    "bass_trombone", "tuba", "contrabass",
}

# These roles must remain timbrally independent. Sharing the sub preset with the movement
# bass collapses the low-end arrangement into one voice and defeats the AI's orchestration.
EXCLUSIVE_SOUND_CATALOGS = {
    "sub_synth", "electric_bass", "analog_pad", "poly_synth", "lead_synth",
    "ambient_texture",
}

RHYTHM_CATALOGS = {
    "kick_drum", "snare_clap", "hi_hats", "timpani", "taiko_ensemble",
    "latin_percussion", "shakers", "cymbals", "orchestral_percussion", "production_drums",
}

ONE_SHOT_PREFERRED_CATALOGS = {
    "kick_drum", "snare_clap", "hi_hats", "latin_percussion", "shakers", "cymbals",
}

RAW_AUDIO_EXTENSIONS = (".wav", ".aif", ".aiff", ".flac")

ARTICULATION_GROUPS = {
    "kick": {"kick"}, "snare": {"snare", "sidestick", "rim"},
    "clap": {"clap"}, "hat": {"hat", "hihat"},
    "ride": {"ride"}, "crash": {"crash"}, "splash": {"splash"},
    "china": {"china", "chinese"}, "conga": {"conga"},
    "bongo": {"bongo"}, "timbale": {"timbale"}, "tom": {"tom"},
    "clave": {"clave", "claves"}, "cowbell": {"cowbell"},
    "tambourine": {"tambourine", "tamb"}, "shaker": {"shaker"},
    "maraca": {"maraca", "maracas"}, "vibraslap": {"vibraslap"},
    "guiro": {"guiro"}, "cabasa": {"cabasa"},
    "wood block": {"wood", "block"}, "triangle": {"triangle"},
}

# Words in live_preset_intent are part of the audible contract, not decorative prose.
# We deliberately keep this vocabulary small and perceptually meaningful: it is better to
# admit that a generic flute is only a character fallback than to claim it is the requested
# breathy alto flute merely because both filenames contain "flute".
CHARACTER_GROUPS = {
    "breath": {"breath", "breathy", "airy"},
    "glass": {"glass", "glassy", "crystal", "crystalline"},
    "felt": {"felt"},
    "mute": {"mute", "muted", "damped"},
    "bright": {"bright", "luminous", "brilliant", "brillante", "luminoso"},
    "dark": {"dark", "dusky", "shadow", "oscuro", "oscura"},
    "warm": {"warm", "rounded", "mellow", "sweet", "vintage", "calido", "calida"},
    "cold": {"cold", "icy", "metallic"},
    "high": {"high", "upper", "treble", "piccolo", "alto", "agudo"},
    "low": {"low", "lower", "bass", "deep", "grave", "profundo", "profunda"},
    "alto": {"alto"},
    "soft": {"soft", "gentle", "delicate", "mellow", "suave"},
    "hard": {"hard", "aggressive", "punchy", "duro", "agresivo"},
    "dry": {"dry", "close", "intimate", "seco", "seca"},
    "wet": {"wet", "reverb", "spacious", "ambient", "humedo", "reverberante"},
    "short": {"short", "tight", "pluck", "plucked", "corto", "corta"},
    "long": {"long", "sustain", "sustained", "evolving", "largo", "larga", "sostenido"},
    "sine": {"sine", "sinusoidal"},
    "granular": {"granular", "grain", "granulator"},
}

CHARACTER_OPPOSITES = {
    "bright": "dark", "dark": "bright", "warm": "cold", "cold": "warm",
    "high": "low", "low": "high", "soft": "hard", "hard": "soft",
    "dry": "wet", "wet": "dry", "short": "long", "long": "short",
}

NEGATION_WORDS = {"no", "not", "without", "sin", "sans", "kein", "keine"}
TIMING_WORDS = {
    "gate", "gated", "gating", "attack", "release", "decay", "duration",
    "millisecond", "milliseconds", "second", "seconds", "tail", "envelope",
}


def _normalized_words(value):
    """Return unfiltered words so negation and numeric timing context survive."""
    normalized = unicodedata.normalize("NFKD", str(value).casefold())
    ascii_value = "".join(character for character in normalized
                          if not unicodedata.combining(character))
    return re.sub(r"[^a-z0-9]+", " ", ascii_value).split()


def _requested_character_groups(value):
    """Extract positive audible traits without mistaking prose for a contract.

    GPT often writes execution instructions beside timbre, for example
    ``hard 180 ms gate; no reverb``. ``hard`` there describes the gate operation and
    ``reverb`` is explicitly prohibited. Treating both as positive sound character
    produced false critical deployment failures.
    """
    words = _normalized_words(value)
    requested = set()
    for index, word in enumerate(words):
        groups = [group for group, aliases in CHARACTER_GROUPS.items() if word in aliases]
        if not groups:
            continue
        preceding = words[max(0, index - 3):index]
        nearby = words[max(0, index - 2):min(len(words), index + 5)]
        negated = any(token in NEGATION_WORDS for token in preceding)
        has_timing_word = any(token in TIMING_WORDS for token in nearby)
        has_numeric_timing = any(token.isdigit() for token in nearby) and (
            "ms" in nearby or "millisecond" in nearby or "milliseconds" in nearby or
            "second" in nearby or "seconds" in nearby)
        for group in groups:
            # Short/long beside a gate or release belongs to playback adaptation, not
            # preset character. Hard/soft is ignored only for an explicit numeric gate.
            technical = ((group in {"short", "long"} and has_timing_word) or
                         (group in {"hard", "soft"} and
                          (has_numeric_timing or "gate" in nearby or "gated" in nearby)))
            if technical:
                continue
            if negated:
                opposite = CHARACTER_OPPOSITES.get(group)
                if opposite:
                    requested.add(opposite)
                continue
            requested.add(group)
    return requested


def tokens(value):
    normalized = unicodedata.normalize("NFKD", str(value).casefold())
    ascii_value = "".join(character for character in normalized if not unicodedata.combining(character))
    result = []
    for token in re.sub(r"[^a-z0-9]+", " ", ascii_value).split():
        if token in ("hi", "high"):
            result.append("high")
            continue
        if token in ("lo", "low"):
            result.append("low")
            continue
        if token in ("mute", "muted"):
            result.append("mute")
            continue
        if token in ("close", "closed"):
            result.append("closed")
            continue
        if len(token) <= 2:
            continue
        result.append(token)
        # Browser labels mix singular/plural freely (Tom/Toms, Cymbal/Cymbals).
        if len(token) >= 4 and token.endswith("s") and not token.endswith("ss"):
            result.append(token[:-1])
    return result


def intent_fidelity(name, path, spec):
    """Measure whether a playable family match also realizes its requested character.

    A score of 1 means either no binding character was requested or every requested
    descriptor is present. Missing words are reported honestly; an explicit opposite is
    penalized further. Instrument-family identity continues to be enforced separately.
    """
    available_tokens = set(tokens("{} {}".format(name, path)))
    catalog_id = str(spec.get("catalog_id", "")).strip().casefold()
    lowered_name = str(name).casefold()
    # Browser filenames omit obvious physical properties. A raw isolated drum hit is dry
    # and short unless explicitly labelled wet/long; a kick is inherently low. Encoding
    # those observable facts avoids punishing a suitable 909 merely because its filename
    # does not repeat GPT's adjectives.
    if catalog_id == "kick_drum":
        available_tokens.update(("low", "deep"))
    if str(name).casefold().endswith(RAW_AUDIO_EXTENSIONS) and catalog_id in {
            "kick_drum", "snare_clap", "hi_hats", "shakers", "latin_percussion"}:
        if not available_tokens.intersection(CHARACTER_GROUPS["wet"]):
            available_tokens.add("dry")
        if not available_tokens.intersection(CHARACTER_GROUPS["long"]):
            available_tokens.add("short")
    if catalog_id == "sub_synth" and "sine" in available_tokens:
        available_tokens.update(("clean", "low", "deep"))
    requested_groups = sorted(_requested_character_groups(spec.get("preset_intent", "")))
    if not requested_groups:
        return 1.0
    matched = sum(1 for group in requested_groups
                  if available_tokens.intersection(CHARACTER_GROUPS[group]))
    contradictions = sum(1 for group in requested_groups
                         if CHARACTER_OPPOSITES.get(group) and
                         available_tokens.intersection(
                             CHARACTER_GROUPS[CHARACTER_OPPOSITES[group]]))
    score = 0.35 + 0.65 * matched / len(requested_groups) - 0.20 * contradictions
    return max(0.0, min(1.0, score))


def select_track_sound_variants(items, spec, count=1, used_paths=None):
    """Choose distinct audible realizations for one semantic articulation."""
    count = max(1, min(3, int(count)))
    used = set(str(value).casefold() for value in (used_paths or ()))
    selected = []
    selected_identities = set()
    for _ in range(count):
        match = select_track_sound(items, spec, used)
        if match is None:
            break
        identity = (str(match[0]).casefold(), str(match[1]).casefold())
        if identity in selected_identities:
            break
        selected.append(match)
        selected_identities.add(identity)
        used.add(identity[1])
        used.add("name:" + identity[0])
    return selected


def spec_intent_consistency(spec):
    """Detect contradictory character authorship before the Browser hides it.

    Track names may be poetic, so a score is reduced only when both the visible identity
    and preset intent contain concrete character groups and none of them agree.
    """
    # Role prose describes musical behaviour and often contains negated context (for
    # example "absent during dry attacks"). It must not redefine the track's own timbre.
    # The visible track name is the authored identity compared with preset intent.
    label_tokens = set(tokens(spec.get("name", "")))
    intent_tokens = set(tokens(spec.get("preset_intent", "")))
    label_groups = {group for group, values in CHARACTER_GROUPS.items()
                    if label_tokens.intersection(values)}
    intent_groups = {group for group, values in CHARACTER_GROUPS.items()
                     if intent_tokens.intersection(values)}
    if not label_groups or not intent_groups:
        return 1.0
    if label_groups.intersection(intent_groups):
        return 1.0
    for group in label_groups:
        if CHARACTER_OPPOSITES.get(group) in intent_groups:
            return 0.0
    return 0.5


def is_playable_item(name):
    return str(name).strip().casefold() not in EMPTY_CONTAINERS


def _matches_alias(haystack, alias):
    return set(tokens(alias)).issubset(haystack)


def _identity_tier(catalog_id, haystack):
    tiers = IDENTITY_TIERS.get(str(catalog_id).strip().casefold(), ())
    for index, aliases in enumerate(tiers):
        if any(_matches_alias(haystack, alias) for alias in aliases):
            return index
    return None


def _requested_articulation_group(articulation_aliases):
    alias_values = [str(value).casefold() for value in articulation_aliases]
    primary_tokens = set(tokens(alias_values[0])) if alias_values else set()
    return next((group for group, values in ARTICULATION_GROUPS.items()
                 if primary_tokens.intersection(values)), None)


def _qualifier_conflict(name, articulation_aliases):
    alias_values = [str(value).casefold() for value in articulation_aliases]
    requested = set(tokens(alias_values[0])) if alias_values else set()
    available = set(tokens(name))
    if "high" in requested and "low" in available and "high" not in available:
        return True
    if "low" in requested and "high" in available and "low" not in available:
        return True
    if "open" in requested and available.intersection({"closed", "mute"}):
        return True
    if "open" in requested and "pedal" in available and "pedal" not in requested:
        return True
    if "closed" in requested and "open" in available:
        return True
    if "mute" in requested and "open" in available:
        return True
    if "pedal" in requested and "open" in available and "pedal" not in available:
        return True
    return False


def _articulation_qualifier_score(name, articulation_aliases):
    alias_values = [str(value).casefold() for value in articulation_aliases]
    requested = set(tokens(alias_values[0])) if alias_values else set()
    available = set(tokens(name))
    score = 0
    if requested.intersection({"open", "closed", "mute", "pedal"}).intersection(available):
        score += 36
    if requested.intersection({"high", "low"}).intersection(available):
        score += 18
    return score


def _semantic_conflict(name, catalog_id, articulation_aliases=()):
    """Reject compound or contradictory one-shots before lexical ranking.

    A filename containing both "kick" and "open hat" is a valid production layer, but it
    cannot represent an isolated hat articulation in an editable multi-track arrangement.
    """
    lowered = str(name).casefold()
    item_tokens = set(tokens(name))
    rhythm = str(catalog_id).casefold() in RHYTHM_CATALOGS
    if rhythm and any(term in lowered for term in
                      (" combo", "loop", "construction kit", "full kit", "drum mix")):
        return True
    alias_values = [str(value).casefold() for value in articulation_aliases]
    aliases = " ".join(alias_values)
    if ("open hat" in aliases or "open hihat" in aliases) and \
            item_tokens.intersection({"kick", "snare", "clap", "tom"}):
        return True
    if ("closed hat" in aliases or "closed hihat" in aliases) and "open" in item_tokens:
        return True
    if "clap" in aliases and "kick" in item_tokens:
        return True
    if _qualifier_conflict(name, articulation_aliases):
        return True
    requested = _requested_articulation_group(articulation_aliases)
    if requested is not None:
        contradictory = set().union(*(values for group, values in ARTICULATION_GROUPS.items()
                                      if group != requested))
        # Libraries conventionally label a China cymbal as "Crash China".  "Crash" is
        # descriptive in that compound identity; the explicit China token still wins.
        if requested == "china":
            contradictory.difference_update(ARTICULATION_GROUPS["crash"])
        # A compound sample is not an isolated articulation even when its filename also
        # contains the requested word ("Conga and Tambourine", for example). Triggering
        # that file as one MIDI hit imports another rhythm and an uncontrolled tail.
        if item_tokens.intersection(contradictory):
            return True
    return False


def _matches_articulation_identity(haystack, articulation_aliases):
    requested = _requested_articulation_group(articulation_aliases)
    if requested is not None:
        return bool(set(haystack).intersection(ARTICULATION_GROUPS[requested]))
    return any(_matches_alias(haystack, alias) for alias in articulation_aliases)


def _safe_isolated_articulation(name, path):
    """Accept only tempo-independent raw hits for split percussion tracks.

    Live presets can conceal a sequencer, a long envelope or a chromatically mapped rack.
    Raw files in Drum Hits have one deterministic attack and can be remapped safely by the
    playback adapter. Tempo-labelled material is a loop even when Browser categorises it
    under Drum Hits, so it is never used as an individual GM articulation.
    """
    label = "{} {}".format(name, path).casefold()
    if not str(name).casefold().endswith(RAW_AUDIO_EXTENSIONS):
        return False
    if re.search(r"\b\d{2,3}\s*bpm\b", label):
        return False
    if any(term in label for term in
           (" construction kit", " drum loop", " percussion loop", " full loop")):
        return False
    return True


def _is_used(name, path, used):
    return str(path).casefold() in used or "name:" + str(name).casefold() in used


def _rank_item(name, path, intent, requested_device, catalog_id=""):
    name_tokens = set(tokens(name))
    path_tokens = set(tokens(path))
    item_tokens = name_tokens.union(path_tokens)
    intent_tokens = set(tokens(intent)).difference(GENERIC_TOKENS)
    score = len(intent_tokens.intersection(name_tokens)) * 9
    score += len(intent_tokens.intersection(path_tokens).difference(name_tokens)) * 2
    lowered_name = str(name).casefold()
    lowered_path = str(path).casefold().replace("\\", "/")
    if lowered_name.endswith((".adg", ".adv")):
        score += 4
    if "/drum hits/" in lowered_path:
        score += 5
    if " bpm" in lowered_name:
        score -= 4
    if any(hint in lowered_path for hint in FAMILY_PATH_HINTS.get(str(catalog_id).casefold(), ())):
        score += 12
    if str(catalog_id).casefold() in ONE_SHOT_PREFERRED_CATALOGS and lowered_name.endswith(
            (".wav", ".aif", ".aiff", ".flac")):
        score += 12
    if str(catalog_id).casefold() == "piano":
        wanted = set(tokens(intent))
        if wanted.intersection({"felt", "acoustic"}):
            acoustic_character = ("prepared", "mute", "upright", "grand", "childhood", "ac piano")
            electronic_character = ("e-piano", "electric piano", "fm piano", "synth piano",
                                    "wurli", "toy piano", "analog piano")
            if any(value in lowered_name for value in acoustic_character):
                score += 28
            if any(value in lowered_name for value in electronic_character):
                score -= 36
    if str(catalog_id).casefold() == "cymbals" and ("kick" in name_tokens or "reverse" in name_tokens):
        score -= 30
    if str(catalog_id).casefold() == "kick_drum":
        if any(value in lowered_name for value in ("short", "tight", "punch", "909", "club")):
            score += 24
        if "synth bass" in lowered_name or any(value in lowered_name for value in ("long tail", "boomy")):
            score -= 60
    if str(catalog_id).casefold() == "sub_synth":
        wanted = set(tokens(intent))
        if "sub" in name_tokens:
            score += 10
        if "sine" in wanted:
            score += 42 if "sine" in name_tokens else -42 if name_tokens.intersection(
                {"saw", "complex", "wobble", "drive", "growl"}) else 0
        if wanted.intersection({"clean", "mono"}):
            score += 24 if name_tokens.intersection({"basic", "clean", "sine", "pure"}) else 0
            if name_tokens.intersection({"complex", "wobble", "drive", "growl", "itchy"}):
                score -= 32
        if any(value in lowered_name for value in ("electric", "finger", "pluck", "growl")):
            score -= 32
    if str(catalog_id).casefold() == "electric_bass":
        if any(value in lowered_name for value in ("electric", "finger", "pluck", "muted", "groove")):
            score += 30
        if any(value in lowered_name for value in ("sub sine", "pure sine", "clean sub")):
            score -= 55
    if str(catalog_id).casefold() in {"analog_pad", "poly_synth", "lead_synth", "ambient_texture"}:
        wanted = set(tokens(intent))
        high_role = bool(wanted.intersection({"high", "upper", "bright", "air", "luminous", "treble"}))
        low_role = bool(wanted.intersection({"low", "lower", "dark", "body", "foundation", "bass"}))
        named_high = bool(name_tokens.intersection({"high", "upper", "bright", "air"}))
        named_low = bool(name_tokens.intersection({"low", "lower", "dark", "bass"}))
        if high_role and named_high:
            score += 24
        if low_role and named_low:
            score += 24
        if high_role and named_low:
            score -= 36
        if low_role and named_high:
            score -= 36
    device_tokens = set(tokens(requested_device))
    if device_tokens and device_tokens.issubset(item_tokens):
        score += 2
    return score


def select_track_sound(items, spec, used_paths=None):
    """Return (name, path, opaque_item, quality, shared) or None for one track spec."""
    catalog_id = str(spec.get("catalog_id", "")).strip().casefold()
    # Identity remains the hard contract; the shared palette resolves ties so a large
    # ensemble inhabits one sound world instead of selecting unrelated presets.
    # Per-track intent owns sound selection. The global palette is already reflected in
    # GPT's authored intent and must never leak words such as "muted" from a bass into the
    # lead, or "glassy" from a piano into a pad.
    signature = spec.get("timbre_signature", {}) or {}
    signature_words = " ".join(str(signature.get(key, "")) for key in
                               ("source", "envelope", "spectrum", "motion", "space", "texture"))
    intent = (str(spec.get("preset_intent", "")).strip() + " " + signature_words).strip()
    requested_device = str(spec.get("native_device", ""))
    articulation_aliases = tuple(str(value).strip() for value in
                                 spec.get("articulation_aliases", ()) if str(value).strip())
    used = set(str(path).casefold() for path in (used_paths or ()))
    tiered = []
    for name, path, item in items:
        if not is_playable_item(name):
            continue
        if _semantic_conflict(name, catalog_id, articulation_aliases):
            continue
        if articulation_aliases and catalog_id in RHYTHM_CATALOGS and \
                not _safe_isolated_articulation(name, path):
            continue
        lowered_name = str(name).casefold()
        if catalog_id == "kick_drum" and ("synth bass" in lowered_name or
                any(value in lowered_name for value in
                    ("click layer", "top layer", "transient layer", "open hat combo"))):
            continue
        if catalog_id in KIT_REQUIRED_CATALOGS and not (lowered_name.endswith(".adg") and "kit" in tokens(name)):
            continue
        haystack = set(tokens(str(name) + " " + str(path)))
        articulation_match = articulation_aliases and _matches_articulation_identity(
            haystack, articulation_aliases)
        identity_tier = _identity_tier(catalog_id, haystack)
        tier = 0 if articulation_match else (
            identity_tier + 1 if articulation_aliases and identity_tier is not None
            else identity_tier)
        if tier is not None:
            fidelity = intent_fidelity(name, path, spec)
            tiered.append((tier, _is_used(name, path, used),
                           -(_rank_item(name, path, intent, requested_device, catalog_id) +
                             _articulation_qualifier_score(name, articulation_aliases) +
                             fidelity * 48.0),
                           str(path).casefold(), name, path, item))
    if tiered:
        if articulation_aliases and catalog_id in RHYTHM_CATALOGS:
            # A split GM articulation is an exact audible identity, not a suggestion.
            # Skipping an unavailable ride is safer than silently turning it into a clap.
            tiered = [candidate for candidate in tiered if candidate[0] == 0]
        if catalog_id in STRICT_IDENTITY_CATALOGS:
            tiered = [candidate for candidate in tiered if candidate[0] == 0]
        elif catalog_id in RHYTHM_CATALOGS and catalog_id != "production_drums" and \
                not any(candidate[0] == 0 for candidate in tiered):
            tiered = [candidate for candidate in tiered
                      if str(candidate[5]).casefold().endswith(RAW_AUDIO_EXTENSIONS)]
        if catalog_id in EXCLUSIVE_SOUND_CATALOGS:
            tiered = [candidate for candidate in tiered if not candidate[1]]
    if tiered:
        tiered.sort(key=lambda value: value[:4])
        chosen = tiered[0]
        locked_path = str(spec.get("locked_sound_path", "")).casefold()
        if bool(spec.get("sound_locked", False)) and locked_path:
            locked = next((candidate for candidate in tiered
                           if str(candidate[5]).casefold() == locked_path), None)
            if locked is not None:
                chosen = locked
        elif spec.get("sound_selection_seed") is not None:
            best_tier, best_used, best_rank = tiered[0][:3]
            floor = float(spec.get("minimum_intent_fidelity", 0.0))
            recent = {str(value).casefold() for value in spec.get("recent_sound_paths", ())}
            eligible = [candidate for candidate in tiered
                        if candidate[0] == best_tier and candidate[1] == best_used and
                        candidate[2] <= best_rank + 20.0 and
                        intent_fidelity(candidate[4], candidate[5], spec) >= floor]
            fresh = [candidate for candidate in eligible
                     if str(candidate[5]).casefold() not in recent]
            if fresh:
                eligible = fresh
            uniqueness = max(0.0, min(1.0, float(signature.get("uniqueness", 0.5))))
            top_k = max(1, min(len(eligible), 1 + int(round(uniqueness * 7.0))))
            if top_k:
                seed = "{}:{}:{}:{}".format(spec.get("sound_selection_seed", "0"),
                                             spec.get("sound_variation", 0), catalog_id,
                                             spec.get("track_key", spec.get("name", "")))
                index = int(hashlib.sha256(seed.encode("utf-8")).hexdigest()[:16], 16) % top_k
                chosen = eligible[index]
        tier, shared, _, _, name, path, item = chosen
        fidelity = intent_fidelity(name, path, spec)
        quality = "identity" if tier == 0 else "family_fallback"
        if fidelity < 0.78:
            quality = "character_fallback"
        return name, path, item, quality, shared

    if articulation_aliases and catalog_id in RHYTHM_CATALOGS:
        return None

    # Only explicit audible native instruments may rescue an unavailable family.
    # Empty Rack/Sampler containers never qualify.
    candidates = [str(value).strip() for value in spec.get("device_candidates", [])]
    if requested_device:
        candidates.append(requested_device)
    for candidate in candidates:
        if not candidate or candidate.casefold() in EMPTY_CONTAINERS:
            continue
        for name, path, item in items:
            if is_playable_item(name) and candidate.casefold() == str(name).strip().casefold():
                return name, path, item, "device_fallback", _is_used(name, path, used)

    # Last-resort playback is explicit and audible, never an empty Rack. Rhythm receives a
    # one-shot from Drum Hits; pitched parts receive a neutral native synth rather than a
    # falsely labelled orchestral substitute.
    if catalog_id in RHYTHM_CATALOGS:
        emergency = []
        for name, path, item in items:
            lowered_path = str(path).casefold().replace("\\", "/")
            if _semantic_conflict(name, catalog_id, articulation_aliases):
                continue
            if not str(name).casefold().endswith(RAW_AUDIO_EXTENSIONS) or \
                    "/drum hits/" not in lowered_path:
                continue
            emergency.append((-_rank_item(name, path, intent, requested_device, catalog_id),
                              _is_used(name, path, used), str(path).casefold(), name, path, item))
        if emergency:
            emergency.sort(key=lambda value: value[:3])
            _, shared, _, name, path, item = emergency[0]
            return name, path, item, "emergency_one_shot", shared
    else:
        for safe_name in ("Drift", "Wavetable", "Operator"):
            for name, path, item in items:
                if str(name).strip().casefold() == safe_name.casefold() and is_playable_item(name):
                    return name, path, item, "emergency_instrument", _is_used(name, path, used)
    return None


def catalog_capabilities(items):
    """Summarize exact and family-only playback identities in the installed inventory."""
    exact = []
    family = []
    unavailable = []
    for catalog_id in sorted(IDENTITY_TIERS):
        if catalog_id in ("production_drums", "harmonic_ensemble", "foreground_voice"):
            continue
        tiers = []
        for name, path, _ in items:
            if not is_playable_item(name):
                continue
            tier = _identity_tier(catalog_id, set(tokens(str(name) + " " + str(path))))
            if tier is not None:
                tiers.append(tier)
        if 0 in tiers:
            exact.append(catalog_id)
        elif tiers:
            family.append(catalog_id)
        else:
            unavailable.append(catalog_id)
    return {"exact": exact, "family_fallback": family, "unavailable": unavailable}


def best_inventory_match(items, query, preferred_device=""):
    """Compatibility helper for generic searches; requires a real complete-word overlap."""
    query_tokens = set(tokens(query)).difference(GENERIC_TOKENS)
    if not query_tokens:
        return None
    matches = []
    for name, path, item in items:
        if not is_playable_item(name):
            continue
        haystack = set(tokens(str(name) + " " + str(path)))
        overlap = query_tokens.intersection(haystack)
        if overlap:
            matches.append((-len(overlap), -_rank_item(name, path, query, preferred_device),
                            str(path).casefold(), name, path, item))
    if not matches:
        return None
    matches.sort(key=lambda value: value[:3])
    return matches[0][3:]
