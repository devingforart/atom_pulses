import unittest

from ableton.PulsoDeployRemote.playback_adapter import (
    adapt_notes, build_drum_pitch_map, expand_percussion_specs,
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


if __name__ == "__main__":
    unittest.main()
