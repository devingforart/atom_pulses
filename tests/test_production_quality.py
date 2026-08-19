import unittest

from ableton.PulsoDeployRemote.production_quality import evaluate_creative_quality


class ProductionQualityTests(unittest.TestCase):
    def test_human_ai_authored_club_score_passes(self):
        report = evaluate_creative_quality({
            "narrative_audited": True, "creative_ready": True,
            "creative_score": 0.83, "production_domain": "club_electronic",
            "foreground_ai_authorship_ratio": 0.68,
            "movement_bass_ai_authorship_ratio": 0.74,
            "foreground_note_count": 40,
            "movement_bass_note_count": 48,
            "groove_authorship_coverage": 0.42,
            "bass_phrase_continuity": 0.72,
            "maximum_melodic_step_run": 4,
            "maximum_club_drum_gap_bars": 12,
            "maximum_club_low_end_gap_bars": 10,
        })
        self.assertTrue(report["passed"])
        self.assertEqual(report["codes"], [])

    def test_procedural_notey_score_is_degraded_with_exact_causes(self):
        report = evaluate_creative_quality({
            "narrative_audited": True, "creative_ready": False,
            "creative_score": 0.61, "production_domain": "club_electronic",
            "foreground_ai_authorship_ratio": 0.14,
            "movement_bass_ai_authorship_ratio": 0.35,
            "foreground_note_count": 40,
            "movement_bass_note_count": 48,
            "groove_authorship_coverage": 0.08,
            "bass_phrase_continuity": 0.33,
            "maximum_melodic_step_run": 9,
            "maximum_club_drum_gap_bars": 32,
            "maximum_club_low_end_gap_bars": 42,
        })
        self.assertFalse(report["passed"])
        self.assertIn("procedural_foreground", report["codes"])
        self.assertIn("procedural_movement_bass", report["codes"])
        self.assertIn("groove_not_ai_authored", report["codes"])
        self.assertIn("scalar_melody_without_speech", report["codes"])
        self.assertIn("club_pulse_absent_too_long", report["codes"])
        self.assertIn("low_end_absent_too_long", report["codes"])

    def test_legacy_request_is_not_falsely_rejected(self):
        self.assertTrue(evaluate_creative_quality({"schema_version": 9})["passed"])

    def test_deliberately_instrumental_score_does_not_require_lead_or_bass(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.88,
            "production_domain": "adaptive",
            "foreground_note_count": 0,
            "movement_bass_note_count": 0,
        })
        self.assertTrue(report["passed"])


if __name__ == "__main__":
    unittest.main()
