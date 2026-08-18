import unittest

from ableton.PulsoDeployRemote.deployment_planner import resolve_deployment


class DeploymentPlannerTests(unittest.TestCase):
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

    def test_preflight_blocks_critical_character_mismatch_transactionally(self):
        plan = resolve_deployment([
            ("Basic Lead.adg", "Sounds/Synth Lead/Basic Lead.adg", "lead"),
        ], [{
            "name": "Glassy Lead", "track_key": "lead", "catalog_id": "lead_synth",
            "preset_intent": "glassy crystalline lead", "timbre_priority": "critical",
            "minimum_intent_fidelity": 0.65,
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


if __name__ == "__main__":
    unittest.main()
