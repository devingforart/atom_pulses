import unittest

from ableton.PulsoDeployRemote.deployment_planner import resolve_deployment


class DeploymentPlannerTests(unittest.TestCase):
    def test_neutral_audition_bypasses_poetic_preset_search_deterministically(self):
        items = [
            ("Breathy Flute.adg", "Sounds/Winds/Breathy Flute.adg", "flute"),
            ("Wavetable", "Instruments/Wavetable", "wavetable"),
            ("Drift", "Instruments/Drift", "drift"),
        ]
        track = {
            "name": "Impossible Rare Flute", "department": "melody", "catalog_id": "flute",
            "preset_intent": "breathy orchestral flute", "audition_policy": "neutral_role_v1",
            "notes": [{"pitch": 72, "start": 0.0}],
        }
        plan = resolve_deployment(items, [track])
        self.assertEqual(len(plan["resolved"]), 1)
        spec, match = plan["resolved"][0]
        self.assertEqual(match[0], "Wavetable")
        self.assertEqual(spec["authored_preset_intent"], "breathy orchestral flute")
        self.assertEqual(plan["timbre_contracts"][0]["deployment_policy"],
                         "neutral_role_audition")
        self.assertFalse(plan["blocking_timbres"])

    def test_preflight_expands_distinct_hat_variants_and_keeps_critical_kick(self):
        items = [
            ("Kick 909 Tight.wav", "Drums/Drum Hits/Kick/Kick 909 Tight.wav", "kick"),
            ("Hihat Closed Dust.wav", "Drums/Drum Hits/Hihat/Hihat Closed Dust.wav", "hat-a"),
            ("Hihat Closed Chirp.wav", "Drums/Drum Hits/Hihat/Hihat Closed Chirp.wav", "hat-b"),
        ]
        tracks = [{
            "name": "Kick", "track_key": "kick", "catalog_id": "kick_drum",
            "preset_intent": "deep dry electronic kick", "timbre_priority": "critical",
            "minimum_intent_fidelity": 0.65,
            "notes": [{"pitch": 36, "start": 0.0}],
        }, {
            "name": "Hats | Closed Hat", "track_key": "hats:42", "catalog_id": "hi_hats",
            "preset_intent": "closed hat", "articulation_identity": "closed hat",
            "articulation_aliases": ["closed hat", "closed hihat", "hat"],
            "notes": [{"pitch": 42, "start": index * 0.25, "velocity": 60 + index % 20}
                      for index in range(32)],
        }]
        plan = resolve_deployment(items, tracks)
        self.assertEqual(len(plan["resolved"]), 3)
        self.assertFalse(plan["blocking_timbres"])
        hat_specs = [spec for spec, _ in plan["resolved"] if spec["catalog_id"] == "hi_hats"]
        self.assertEqual(len(hat_specs), 2)
        self.assertNotEqual(plan["resolved"][1][1][1], plan["resolved"][2][1][1])

    def test_preflight_degrades_playable_critical_character_mismatch(self):
        plan = resolve_deployment([
            ("Basic Lead.adg", "Sounds/Synth Lead/Basic Lead.adg", "lead"),
        ], [{
            "name": "Glassy Lead", "track_key": "lead", "catalog_id": "lead_synth",
            "preset_intent": "glassy crystalline lead", "timbre_priority": "critical",
            "minimum_intent_fidelity": 0.65,
            "notes": [{"pitch": 72, "start": 0.0}],
        }])
        self.assertEqual(len(plan["resolved"]), 1)
        self.assertEqual(plan["blocking_timbres"], [])
        self.assertEqual(plan["timbre_warnings"], ["Glassy Lead"])
        self.assertEqual(plan["timbre_contracts"][0]["deployment_policy"],
                         "audible_degraded_fallback")

    def test_explicit_strict_timbre_gate_remains_transactional(self):
        plan = resolve_deployment([
            ("Basic Lead.adg", "Sounds/Synth Lead/Basic Lead.adg", "lead"),
        ], [{
            "name": "Glassy Lead", "track_key": "lead", "catalog_id": "lead_synth",
            "preset_intent": "glassy crystalline lead", "timbre_priority": "critical",
            "minimum_intent_fidelity": 0.65, "strict_timbre_gate": True,
            "notes": [{"pitch": 72, "start": 0.0}],
        }])
        self.assertEqual(plan["resolved"], [])
        self.assertEqual(plan["blocking_timbres"], ["Glassy Lead"])

    def test_preflight_accepts_negated_reverb_and_technical_gate_language(self):
        plan = resolve_deployment([
            ("Kick 909 1.aif", "Drums/Drum Hits/Kick/Kick 909 1.aif", "kick"),
        ], [{
            "name": "Deep Dry Anchor", "track_key": "kick", "catalog_id": "kick_drum",
            "preset_intent": "deep dry techno kick; hard 180 ms gate; no reverb",
            "timbre_priority": "critical", "minimum_intent_fidelity": 0.65,
            "notes": [{"pitch": 36, "start": 0.0, "duration": 0.25}],
        }])
        self.assertEqual(len(plan["resolved"]), 1)
        self.assertFalse(plan["blocking_timbres"])
        self.assertEqual(plan["timbre_contracts"][0]["fidelity"], 1.0)

    def test_critical_character_contract_records_bounded_second_pass(self):
        plan = resolve_deployment([
            ("Basic Lead.adg", "Sounds/Synth Lead/Basic Lead.adg", "basic"),
            ("Glassy Crystal Lead.adg", "Sounds/Synth Lead/Glassy Crystal Lead.adg", "glass"),
        ], [{
            "name": "Upper Speaker", "track_key": "lead", "catalog_id": "lead_synth",
            "preset_intent": "glassy crystalline lead", "timbre_priority": "critical",
            "minimum_intent_fidelity": 0.65, "sound_selection_seed": 4,
            "notes": [{"pitch": 72, "start": 0.0}],
        }])
        self.assertEqual(len(plan["resolved"]), 1)
        self.assertIn("character_retry_improved", plan["timbre_contracts"][0])
        self.assertGreaterEqual(plan["timbre_contracts"][0]["fidelity"], 0.65)


if __name__ == "__main__":
    unittest.main()
