"""Role-aware matching for playable Ableton browser items.

Musical identity is a hard gate. Descriptive words ("solo", "warm", "section")
can rank two valid cellos, but can never turn a bass or riser into a cello.
"""

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
    "electric_bass": (("electric bass", "bass"),),
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
    "latin_percussion": ("/percussion/", "/percussive/"),
    "shakers": ("/shaker/", "/misc percussion/", "/percussive/"),
    "cymbals": ("/cymbal/", "/percussive/"),
    "orchestral_percussion": ("/tom/", "/misc percussion/", "/percussive/"),
    "piano": ("/piano & keys/",), "electric_bass": ("/bass/",),
    "sub_synth": ("/bass/",), "analog_pad": ("/pad/",),
    "lead_synth": ("/synth lead/",), "ambient_texture": ("/ambient & evolving/",),
}

KIT_REQUIRED_CATALOGS = {"latin_percussion", "production_drums"}


def tokens(value):
    normalized = unicodedata.normalize("NFKD", str(value).casefold())
    ascii_value = "".join(character for character in normalized if not unicodedata.combining(character))
    result = []
    for token in re.sub(r"[^a-z0-9]+", " ", ascii_value).split():
        if len(token) <= 2:
            continue
        result.append(token)
        # Browser labels mix singular/plural freely (Tom/Toms, Cymbal/Cymbals).
        if len(token) >= 4 and token.endswith("s") and not token.endswith("ss"):
            result.append(token[:-1])
    return result


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
    device_tokens = set(tokens(requested_device))
    if device_tokens and device_tokens.issubset(item_tokens):
        score += 2
    return score


def select_track_sound(items, spec):
    """Return (name, path, opaque_item, quality) or None for one track spec."""
    catalog_id = str(spec.get("catalog_id", "")).strip().casefold()
    intent = str(spec.get("preset_intent", ""))
    requested_device = str(spec.get("native_device", ""))
    tiered = []
    for name, path, item in items:
        if not is_playable_item(name):
            continue
        lowered_name = str(name).casefold()
        if catalog_id in KIT_REQUIRED_CATALOGS and not (lowered_name.endswith(".adg") and "kit" in tokens(name)):
            continue
        haystack = set(tokens(str(name) + " " + str(path)))
        tier = _identity_tier(catalog_id, haystack)
        if tier is not None:
            tiered.append((tier, -_rank_item(name, path, intent, requested_device, catalog_id),
                           str(path).casefold(), name, path, item))
    if tiered:
        tiered.sort(key=lambda value: value[:3])
        tier, _, _, name, path, item = tiered[0]
        return name, path, item, "identity" if tier == 0 else "family_fallback"

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
                return name, path, item, "device_fallback"
    return None


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
