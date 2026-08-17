import unittest

from ableton.PulsoDeployRemote.playback_adapter import (
    adapt_notes, articulation_substitution_specs, audible_deployment_score,
    build_drum_pitch_map, deployment_outcome, expand_percussion_specs,
    note_specification_arguments, project_expression,
    requested_audible_variants, split_audible_variants, timbre_contract,
)


class PlaybackAdapterTests(unittest.TestCase):
    def test_one_shot_percussion_is_remapped_to_root_without_losing_timing(self):
        notes, report = adapt_notes({
            "catalog_id": "kick_drum",
            "notes": [{"pitch": 36, "start": 4.0, "duration": 0.5, "velocity": 110}],
        }, "one_shot", root_note=60)
        self.assertEqual(notes[0]["pitch"], 60)
        self.assertEqual(notes[0]["start"], 4.0)
        self.assertEqual(notes[0]["duration"], 0.25)
        self.assertEqual(report["pitch_remap"], {"36": 60})

    def test_latin_notes_are_mapped_by_pad_identity(self):
        spec = {"catalog_id": "latin_percussion", "notes": [
            {"pitch": 62}, {"pitch": 75},
        ]}
        mapping = build_drum_pitch_map(spec, [
            (48, "Muted High Conga"), (51, "Claves Dry"), (60, "Unrelated Bell"),
        ])
        self.assertEqual(mapping, {62: 48, 75: 51})

    def test_missing_semantic_pad_fails_instead_of_using_an_arbitrary_pad(self):
        spec = {"catalog_id": "latin_percussion", "notes": [{"pitch": 75}]}
        self.assertIsNone(build_drum_pitch_map(spec, [(48, "Muted High Conga")]))

    def test_articulation_caps_and_same_pitch_overlap_are_repaired(self):
        notes, report = adapt_notes({"catalog_id": "marimba", "notes": [
            {"pitch": 60, "start": 0.0, "duration": 6.75, "velocity": 80},
            {"pitch": 60, "start": 0.75, "duration": 6.75, "velocity": 82},
        ]})
        self.assertEqual(notes[0]["duration"], 0.75)
        self.assertEqual(notes[1]["duration"], 1.0)
        self.assertEqual(report["overlap_repairs"], 1)

    def test_percussion_duration_floor_prevents_inaudible_triggers(self):
        notes, report = adapt_notes({"catalog_id": "latin_percussion", "notes": [
            {"pitch": 62, "start": 1.0, "duration": 0.01, "velocity": 70},
        ]}, "one_shot", root_note=60)
        self.assertEqual(notes[0]["duration"], 0.125)
        self.assertEqual(report["duration_floor"], 0.125)

    def test_chromatic_duration_floor_repairs_instrumental_fragments(self):
        notes, report = adapt_notes({"catalog_id": "cello", "notes": [
            {"pitch": 65, "start": 1.0, "duration": 0.0625, "velocity": 70},
            {"pitch": 67, "start": 2.0, "duration": 0.1, "velocity": 72},
        ]})
        self.assertEqual([note["duration"] for note in notes], [0.25, 0.25])
        self.assertEqual(report["duration_floor"], 0.25)
        self.assertEqual(report["duration_repairs"], 2)

    def test_unrepresentable_same_pitch_micro_fragment_is_removed(self):
        notes, report = adapt_notes({"catalog_id": "cello", "notes": [
            {"pitch": 65, "start": 1.0, "duration": 0.01, "velocity": 66},
            {"pitch": 65, "start": 1.125, "duration": 0.5, "velocity": 78},
        ]})
        self.assertEqual(len(notes), 1)
        self.assertEqual(notes[0]["start"], 1.125)
        self.assertEqual(report["inaudible_notes_removed"], 1)

    def test_distinct_hat_articulations_receive_independent_sound_tracks(self):
        expanded = expand_percussion_specs([{
            "name": "Hats", "track_key": "part:3", "catalog_id": "hi_hats",
            "preset_intent": "dark hats", "notes": [
                {"pitch": 42, "start": 0}, {"pitch": 44, "start": 1},
            ],
        }])
        self.assertEqual(len(expanded), 2)
        self.assertEqual([item["preset_intent"] for item in expanded], ["closed hat", "pedal hat"])
        self.assertEqual([item["notes"][0]["pitch"] for item in expanded], [42, 44])

    def test_repeated_hat_attacks_receive_velocity_phrase_round_robin_lanes(self):
        spec = {"name": "Hats | Closed Hat", "track_key": "part:3:pitch:42",
                "catalog_id": "hi_hats", "articulation_identity": "closed hat",
                "notes": [{"pitch": 42, "start": index * 0.25,
                           "velocity": 50 + index % 40} for index in range(32)]}
        self.assertEqual(requested_audible_variants(spec), 2)
        variants = split_audible_variants(spec, 2)
        self.assertEqual(len(variants), 2)
        self.assertEqual(sum(len(item["notes"]) for item in variants), 32)
        self.assertEqual({item["audible_variant_mode"] for item in variants},
                         {"velocity_phrase_round_robin"})
        self.assertNotEqual({note["start"] for note in variants[0]["notes"]},
                            {note["start"] for note in variants[1]["notes"]})

    def test_critical_timbre_contract_blocks_below_its_floor(self):
        failed = timbre_contract({"catalog_id": "kick_drum",
                                  "minimum_intent_fidelity": 0.65}, 0.35,
                                 "character_fallback")
        self.assertTrue(failed["blocking"])
        passed = timbre_contract({"catalog_id": "kick_drum",
                                  "minimum_intent_fidelity": 0.65}, 0.8)
        self.assertTrue(passed["passed"])
        self.assertFalse(passed["blocking"])

    def test_latin_articulations_are_split_before_one_shot_resolution(self):
        expanded = expand_percussion_specs([{
            "name": "Latin", "track_key": "part:6", "catalog_id": "latin_percussion",
            "preset_intent": "low skin conversation", "notes": [
                {"pitch": 60, "start": 0}, {"pitch": 62, "start": 1},
            ],
        }])
        self.assertEqual(len(expanded), 2)
        self.assertEqual([item["preset_intent"] for item in expanded], ["high bongo", "mute high conga"])

    def test_toms_and_rides_keep_exact_gm_meaning(self):
        expanded = expand_percussion_specs([{
            "name": "Low conversation", "track_key": "part:7",
            "catalog_id": "latin_percussion",
            "notes": [{"pitch": 45, "start": 0}, {"pitch": 64, "start": 1}],
        }, {
            "name": "Metal", "track_key": "part:8",
            "catalog_id": "orchestral_percussion",
            "notes": [{"pitch": 51, "start": 0}, {"pitch": 54, "start": 1}],
        }])
        self.assertEqual([item["articulation_identity"] for item in expanded],
                         ["low tom", "low conga", "ride", "tambourine"])

    def test_gm_65_and_66_are_timbales_not_generic_percussion(self):
        expanded = expand_percussion_specs([{
            "name": "Latin", "track_key": "part:6", "catalog_id": "latin_percussion",
            "notes": [{"pitch": 65, "start": 2.0}, {"pitch": 66, "start": 3.0}],
        }])
        self.assertEqual([item["articulation_identity"] for item in expanded],
                         ["high timbale", "low timbale"])

    def test_declared_substitution_preserves_attacks_and_authored_identity(self):
        source = {
            "name": "Air | Vibraslap", "track_key": "part:4:pitch:58",
            "catalog_id": "shakers", "articulation_identity": "vibraslap",
            "notes": [{"pitch": 58, "start": 9.5, "duration": 0.125, "velocity": 83}],
        }
        candidates = articulation_substitution_specs(source)
        self.assertEqual(candidates[0]["articulation_identity"], "maracas")
        self.assertEqual(candidates[0]["authored_articulation_identity"], "vibraslap")
        self.assertEqual(candidates[0]["notes"], source["notes"])
        self.assertIsNot(candidates[0]["notes"], source["notes"])
        self.assertIn("for Vibraslap", candidates[0]["name"])

    def test_combined_open_hat_shaker_and_high_percussion_keep_real_articulations(self):
        expanded = expand_percussion_specs([{
            "name": "Open Hats and Shaker", "track_key": "part:4", "catalog_id": "shakers",
            "notes": [{"pitch": 46, "start": 0.5}, {"pitch": 58, "start": 1.5}],
        }, {
            "name": "High Percussion", "track_key": "part:6",
            "catalog_id": "orchestral_percussion", "notes": [
                {"pitch": 50, "start": 0}, {"pitch": 62, "start": 1},
                {"pitch": 75, "start": 2},
            ],
        }])
        self.assertEqual([item["articulation_identity"] for item in expanded],
                         ["open hat", "vibraslap", "high tom", "mute high conga", "claves"])
        self.assertTrue(all(item["articulation_aliases"] for item in expanded))

    def test_shaker_gm_identity_distinguishes_maracas_from_guiro(self):
        expanded = expand_percussion_specs([{
            "name": "Tops", "track_key": "part:4", "catalog_id": "shakers",
            "notes": [{"pitch": 70, "start": 0.5}, {"pitch": 74, "start": 1.5}],
        }])
        self.assertEqual([item["articulation_identity"] for item in expanded],
                         ["maracas", "long guiro"])

    def test_live_note_specification_arguments_preserve_extended_expression(self):
        arguments = note_specification_arguments({
            "pitch": 140, "start": 2.5, "duration": 0.5, "velocity": 91,
            "probability": 0.72, "velocity_deviation": -11, "release_velocity": 78,
        })
        self.assertEqual(arguments, (127, 2.5, 0.5, 91.0, False, 0.72, -11.0, 78.0))

    def test_chord_stab_duration_is_bounded_by_musical_role(self):
        notes, report = adapt_notes({
            "catalog_id": "poly_synth", "name": "PULSO Chord Stab",
            "role": "Rhythmic harmonic punctuation",
            "notes": [{"pitch": 60, "start": 0, "duration": 6.75, "velocity": 80}],
        })
        self.assertEqual(notes[0]["duration"], 1.0)
        self.assertEqual(report["duration_cap"], 1.0)

    def test_expression_is_projected_into_portable_live_note_properties(self):
        notes, report = project_expression({
            "controls": [
                {"beat": 0, "controller": 11, "value": 42},
                {"beat": 2, "controller": 11, "value": 118},
                {"beat": 0, "controller": 1, "value": 54},
                {"beat": 0, "controller": 64, "value": 96},
                {"beat": 1.5, "controller": 64, "value": 0},
            ],
            "expressions": [{"beat": 0, "type": "channel_pressure", "value": 48}],
        }, [
            {"pitch": 60, "start": 0, "duration": 0.5, "velocity": 90},
            {"pitch": 64, "start": 2, "duration": 0.5, "velocity": 90},
        ])
        self.assertLess(notes[0]["velocity"], notes[1]["velocity"])
        self.assertEqual(notes[0]["duration"], 1.5)
        self.assertIn("velocity_deviation", notes[0])
        self.assertIn("release_velocity", notes[0])
        self.assertEqual(report["controls_received"], 5)
        self.assertEqual(report["expressions_received"], 1)
        self.assertEqual(report["sustain_extensions"], 1)

    def test_one_bad_sound_degrades_without_discarding_verified_tracks(self):
        state, message = deployment_outcome(21, 22, missing=1, fallbacks=2)
        self.assertEqual(state, "degraded")
        self.assertIn("21/22", message)
        self.assertIn("1 TRACK SKIPPED", message)
        rejected, _ = deployment_outcome(0, 22, missing=22)
        self.assertEqual(rejected, "rejected")

    def test_audible_score_measures_the_deployed_result(self):
        self.assertEqual(audible_deployment_score(21, 21), 1.0)
        degraded = audible_deployment_score(
            20, 21, missing=1, fallbacks=2,
            chromatic_duration_repairs=3, inaudible_notes_removed=1)
        self.assertLess(degraded, 20.0 / 21.0)
        self.assertGreaterEqual(degraded, 0.0)
        character_degraded = audible_deployment_score(21, 21, mean_intent_fidelity=0.5)
        self.assertLess(character_degraded, 1.0)
        contradictory = audible_deployment_score(21, 21, mean_intent_consistency=0.5)
        self.assertLess(contradictory, 1.0)


if __name__ == "__main__":
    unittest.main()
