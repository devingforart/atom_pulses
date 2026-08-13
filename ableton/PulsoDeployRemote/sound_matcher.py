"""Deterministic, testable matching for playable Ableton browser items."""

import re
import unicodedata


EMPTY_CONTAINERS = {
    "drum rack",
    "instrument rack",
    "sampler",
    "simpler",
    "drum sampler",
    "impulse",
    "external instrument",
}

GENERIC_TOKENS = {
    "acoustic", "aire", "air", "amplio", "balanced", "calido", "clean",
    "cola", "corta", "corto", "dark", "deep", "dry", "evolving", "expressive",
    "granulado", "grave", "graves", "intimo", "largo", "long", "mate", "movil",
    "natural", "noble", "oscura", "oscuro", "processed", "profundo", "redondo",
    "resonancia", "seco", "secos", "short", "soft", "warm", "wide",
}


def tokens(value):
    normalized = unicodedata.normalize("NFKD", str(value).casefold())
    ascii_value = "".join(character for character in normalized if not unicodedata.combining(character))
    return [token for token in re.sub(r"[^a-z0-9]+", " ", ascii_value).split() if len(token) > 2]


def is_playable_item(name):
    return str(name).strip().casefold() not in EMPTY_CONTAINERS


def best_inventory_match(items, query, preferred_device=""):
    query_tokens = set(tokens(query))
    meaningful = query_tokens.difference(GENERIC_TOKENS)
    if not meaningful:
        meaningful = query_tokens
    if not meaningful:
        return None
    preferred_tokens = set(tokens(preferred_device))
    require_kit = str(preferred_device).strip().casefold() == "drum rack"
    best = None
    best_score = 0
    for name, path, item in items:
        if not is_playable_item(name):
            continue
        lowered_name = str(name).casefold()
        if require_kit and not lowered_name.endswith(".adg"):
            continue
        haystack = set(tokens(str(name) + " " + str(path)))
        overlap = meaningful.intersection(haystack)
        exact = str(query).strip().casefold() == lowered_name
        if not overlap and not exact:
            continue
        score = len(overlap) * 8
        score += len(query_tokens.intersection(haystack)) * 2
        if exact:
            score += 40
        if overlap and preferred_tokens and preferred_tokens.issubset(haystack):
            score += 3
        if score > best_score:
            best_score = score
            best = (name, path, item)
    return best
