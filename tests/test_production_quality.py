import unittest

from ableton.PulsoDeployRemote.production_quality import evaluate_creative_quality


class ProductionQualityTests(unittest.TestCase):
    def test_human_ai_authored_club_score_passes(self):
        report = evaluate_creative_quality({
            "narrative_audited": True, "creative_ready": True,
            "creative_score": 0.83, "production_domain": "club_electronic",
            "foreground_ai_authorship_ratio": 0.92,
            "movement_bass_ai_authorship_ratio": 0.88,
            "foreground_note_count": 40,
            "movement_bass_note_count": 48,
            "groove_authorship_coverage": 0.56,
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

    def test_electronic_fabric_rejects_track_count_without_music(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "electronic_fabric_audited": True,
            "creative_ready": False,
            "creative_score": 0.82,
            "production_domain": "adaptive",
            "independent_musical_lines": 36,
            "meaningful_musical_lines": 12,
            "harmonic_floor_coverage": 0.20,
            "median_harmonic_floor_layers": 1.0,
            "protagonist_phrase_windows": 1,
            "arpeggio_note_count": 4,
            "dialogue_musical_lines": 0,
        })
        self.assertFalse(report["passed"])
        self.assertIn("tracks_without_independent_musical_content", report["codes"])
        self.assertIn("harmonic_floor_incomplete", report["codes"])
        self.assertIn("primary_speaker_incomplete", report["codes"])
        self.assertIn("electronic_arpeggio_incomplete", report["codes"])
        self.assertIn("melodic_dialogue_incomplete", report["codes"])

    def test_static_electronic_score_does_not_invent_a_motion_requirement(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "electronic_fabric_audited": True,
            "electronic_motion_required": False,
            "creative_ready": True,
            "creative_score": 0.90,
            "production_domain": "adaptive",
            "independent_musical_lines": 12,
            "meaningful_musical_lines": 12,
            "harmonic_floor_coverage": 1.0,
            "median_harmonic_floor_layers": 2.0,
            "protagonist_phrase_windows": 4,
            "arpeggio_note_count": 0,
            "dialogue_musical_lines": 1,
        })
        self.assertNotIn("electronic_arpeggio_incomplete", report["codes"])

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

    def test_percussion_free_electronic_score_does_not_require_groove_lanes(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.88,
            "production_domain": "club_electronic",
            "percussion_free": True,
            "foreground_note_count": 24,
            "foreground_ai_authorship_ratio": 0.95,
            "movement_bass_note_count": 0,
            "groove_authorship_coverage": 0.0,
            "maximum_club_drum_gap_bars": 192,
            "maximum_club_low_end_gap_bars": 192,
        })
        self.assertTrue(report["passed"])
        self.assertNotIn("groove_not_ai_authored", report["codes"])

    def test_pulse_bearing_hybrid_uses_exported_drum_gap(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.90,
            "production_domain": "hybrid",
            "electronic_pulse_required": True,
            "groove_authorship_coverage": 0.92,
            "maximum_club_drum_gap_bars": 96,
            "maximum_club_low_end_gap_bars": 0,
        })
        self.assertFalse(report["passed"])
        self.assertIn("club_pulse_absent_too_long", report["codes"])

    def test_track_viability_rejects_token_tracks_and_underwritten_cast(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": False,
            "creative_score": 0.84,
            "production_domain": "adaptive",
            "track_viability_audited": True,
            "track_viability_ready": False,
            "track_viability_score": 0.62,
            "retained_viability_tracks": 19,
            "viable_instrument_tracks": 12,
            "token_instrument_tracks": 7,
        })
        self.assertFalse(report["passed"])
        self.assertIn("token_instrument_tracks_present", report["codes"])
        self.assertIn("instrument_cast_exceeds_authored_material", report["codes"])

    def test_explicit_cast_rejects_missing_published_destinations(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.95,
            "exact_instrument_cast_published": False,
        })
        self.assertFalse(report["passed"])
        self.assertIn("requested_instrument_cast_not_fully_published", report["codes"])

    def test_meaningful_terminal_compaction_is_not_a_creative_failure(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.95,
            "track_viability_audited": True,
            "track_viability_ready": True,
            "track_viability_score": 1.0,
            "declared_viability_tracks": 40,
            "retained_viability_tracks": 37,
            "viable_instrument_tracks": 37,
            "token_instrument_tracks": 0,
            "merged_instrument_tracks": 3,
            "pruned_instrument_tracks": 0,
            "exact_instrument_cast_published": False,
        })
        self.assertTrue(report["passed"])
        self.assertTrue(report["compacted_instrument_cast_valid"])
        self.assertNotIn("requested_instrument_cast_not_fully_published", report["codes"])

    def test_density_director_may_not_delete_authored_material(self):
        report = evaluate_creative_quality({
            "narrative_audited": True,
            "creative_ready": True,
            "creative_score": 0.95,
            "density_notes_removed": 3,
            "semantic_notes_removed": 0,
            "authored_notes_preserved": 997,
        })
        self.assertFalse(report["passed"])
        self.assertIn("authored_material_removed_by_density", report["codes"])


if __name__ == "__main__":
    unittest.main()
