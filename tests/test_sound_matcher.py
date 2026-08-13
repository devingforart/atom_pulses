import unittest

from ableton.PulsoDeployRemote.sound_matcher import best_inventory_match, tokens


class SoundMatcherTests(unittest.TestCase):
    def setUp(self):
        self.items = [
            ("Drum Rack", "drums/Drums/Drum Rack", "empty-drum-rack"),
            ("Instrument Rack", "instruments/Instruments/Instrument Rack", "empty-instrument-rack"),
            ("Drum Sampler", "instruments/Instruments/Drum Sampler", "empty-drum-sampler"),
            ("Kick Felt Crack.wav", "drums/Drums/Drum Hits/Kick/Kick Felt Crack.wav", "single-kick"),
            ("909 Core Kit.adg", "drums/Drums/909 Core Kit.adg", "kit-909"),
            ("Solo Cello Warm.adg", "sounds/Orchestral/Solo Cello Warm.adg", "cello"),
            ("Corvaire Kit.adg", "drums/Drums/Corvaire Kit.adg", "unrelated"),
        ]

    def test_tokens_normalize_accents(self):
        self.assertIn("calido", tokens("Cello cálido"))

    def test_device_affinity_cannot_select_empty_or_unrelated_sampler(self):
        match = best_inventory_match(self.items, "cello solista cálido", "Sampler")
        self.assertEqual(match[2], "cello")

    def test_drum_rack_requires_a_loaded_kit_not_a_sample_or_container(self):
        match = best_inventory_match(self.items, "909 Core Kit.adg", "Drum Rack")
        self.assertEqual(match[2], "kit-909")

    def test_tokens_do_not_match_inside_unrelated_words(self):
        self.assertIsNone(best_inventory_match(self.items, "aire particles", "Granulator III"))


if __name__ == "__main__":
    unittest.main()
