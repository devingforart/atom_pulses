"""Pure preflight planner for transactional Live deployment."""

from .playback_adapter import (articulation_substitution_specs,
                               requested_audible_variants, split_audible_variants,
                               timbre_contract)
from .sound_matcher import intent_fidelity, select_track_sound_variants


def _retry_critical_character(items, spec, match, reserved_paths):
    """Run one bounded second pass when identity matched but audible character did not.

    The first result remains the fallback.  The retry excludes every already-reserved
    preset and changes the deterministic variation, then wins only on a strictly better
    measured fidelity.  Deployment therefore never becomes worse or unbounded.
    """
    priority = str(spec.get("timbre_priority", "")).casefold()
    floor = float(spec.get("minimum_intent_fidelity", 0.0))
    original_fidelity = intent_fidelity(match[0], match[1], spec)
    if priority not in ("featured", "critical") or original_fidelity >= floor:
        return match, False
    retry_spec = dict(spec)
    retry_spec["sound_selection_seed"] = spec.get("sound_selection_seed", 0)
    retry_spec["sound_variation"] = int(spec.get("sound_variation", 0)) + 1
    recent = list(spec.get("recent_sound_paths", ()))
    recent.append(str(match[1]))
    retry_spec["recent_sound_paths"] = recent
    retry_used = set(str(value).casefold() for value in reserved_paths)
    retry_used.add(str(match[1]).casefold())
    retry_used.add("name:" + str(match[0]).casefold())
    alternatives = select_track_sound_variants(items, retry_spec, 1, retry_used)
    if not alternatives:
        return match, False
    candidate = alternatives[0]
    candidate_fidelity = intent_fidelity(candidate[0], candidate[1], spec)
    if candidate_fidelity <= original_fidelity + 0.001:
        return match, False
    return candidate, True


def resolve_deployment(items, tracks, used_paths=None):
    resolved = []
    used = set(str(value).casefold() for value in (used_paths or ()))
    unresolved = []
    substitutions = []
    contracts = []
    blocking = []
    warnings = []
    for source_spec in tracks:
        spec = source_spec
        matches = select_track_sound_variants(
            items, spec, requested_audible_variants(spec), used)
        if not matches:
            substituted = None
            for candidate in articulation_substitution_specs(spec):
                candidate_matches = select_track_sound_variants(
                    items, candidate, requested_audible_variants(candidate), used)
                if not candidate_matches:
                    continue
                substituted = candidate
                matches = [(match[0], match[1], match[2],
                            "declared_substitution", match[4])
                           for match in candidate_matches]
                substitutions.append({
                    "track": str(spec.get("name", "PULSO Part")),
                    "authored": str(spec.get("articulation_identity", "")),
                    "deployed": str(candidate.get("articulation_identity", "")),
                    "reason": str(candidate.get("substitution_reason", "")),
                })
                spec = candidate
                break
            if substituted is None:
                unresolved.append(str(spec.get("name", "PULSO Part")))
                continue
        variant_specs = split_audible_variants(spec, len(matches))
        reserved_matches = set(used)
        for reserved in matches:
            reserved_matches.add(str(reserved[1]).casefold())
            reserved_matches.add("name:" + str(reserved[0]).casefold())
        for variant_spec, match in zip(variant_specs, matches):
            match, retried = _retry_critical_character(
                items, variant_spec, match, reserved_matches)
            fidelity = intent_fidelity(match[0], match[1], variant_spec)
            contract = timbre_contract(variant_spec, fidelity, match[3])
            contract["track"] = str(variant_spec.get("name", "PULSO Part"))
            contract["matched"] = str(match[0])
            contract["audible_variant_group"] = str(
                variant_spec.get("audible_variant_group", ""))
            contract["audible_variant_index"] = int(
                variant_spec.get("audible_variant_index", 0))
            contract["audible_variant_count"] = int(
                variant_spec.get("audible_variant_count", 1))
            contract["character_retry_improved"] = retried
            contracts.append(contract)
            if contract["blocking"]:
                warnings.append(contract["track"])
                strict = bool(variant_spec.get("strict_timbre_gate", False))
                contract["deployment_blocking"] = strict
                contract["deployment_policy"] = "strict_reject" if strict else "audible_degraded_fallback"
                if strict:
                    blocking.append(contract["track"])
                    continue
            else:
                contract["deployment_blocking"] = False
                contract["deployment_policy"] = "accepted"
            used.add(str(match[1]).casefold())
            used.add("name:" + str(match[0]).casefold())
            resolved.append((variant_spec, match))
    return {
        "resolved": resolved,
        "unresolved": unresolved,
        "substitutions": substitutions,
        "timbre_contracts": contracts,
        "blocking_timbres": blocking,
        "timbre_warnings": warnings,
        "used_paths": used,
    }
