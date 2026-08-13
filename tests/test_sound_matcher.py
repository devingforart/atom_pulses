import unittest

from ableton.PulsoDeployRemote.sound_matcher import _rank_item, best_inventory_match, select_track_sound, tokens


class SoundMatcherTests(unittest.TestCase):
    def setUp(self):
        self.items = [
            ("Drum Rack", "drums/Drums/Drum Rack", "empty-drum-rack"),
            ("Instrument Rack", "instruments/Instruments/Instrument Rack", "empty-instrument-rack"),
            ("Drum Sampler", "instruments/Instruments/Drum Sampler", "empty-drum-sampler"),
            ("Mini 2 OSC Solo Bass.adv", "sounds/Sounds/Bass/Mini 2 OSC Solo Bass.adv", "wrong-bass"),
            ("Riser Section 8.adv", "sounds/Sounds/Effects/Riser Section 8.adv", "wrong-riser"),
            ("Orchestral Sweep Pad.adv", "sounds/Sounds/Pad/Orchestral Sweep Pad.adv", "wrong-pad"),
            ("Swell Brass.adg", "sounds/Sounds/Brass/Swell Brass.adg", "wrong-brass"),
            ("Kick Felt Crack.wav", "drums/Drums/Drum Hits/Kick/Kick Felt Crack.wav", "kick"),
            ("Analog Tom.adv", "sounds/Sounds/Percussive/Analog Tom.adv", "tom"),
            ("Timpani Orchestra.wav", "drums/Drums/Drum Hits/Misc Percussion/Timpani Orchestra.wav", "timpani"),
            ("Crash 909.wav", "drums/Drums/Drum Hits/Cymbal/Crash 909.wav", "cymbal"),
            ("909 Core Kit.adg", "drums/Drums/909 Core Kit.adg", "kit-909"),
            ("Percussion Core Kit.adg", "drums/Drums/Percussion Core Kit.adg", "percussion-kit"),
            ("Conga High.wav", "drums/Drums/Drum Hits/Conga/Conga High.wav", "single-conga"),
            ("Violin Strings.adv", "sounds/Sounds/Strings/Violin Strings.adv", "violin"),
            ("Cello Strings.adv", "sounds/Sounds/Strings/Cello Strings.adv", "cello"),
            ("Flute Mellow.adv", "sounds/Sounds/Winds/Flute Mellow.adv", "flute"),
            ("Horns Mellow.adg", "sounds/Sounds/Brass/Horns Mellow.adg", "horns"),
            ("Warm Analog Pad.adg", "sounds/Sounds/Pad/Warm Analog Pad.adg", "analog-pad"),
            ("Wavetable", "instruments/Instruments/Wavetable", "wavetable"),
        ]

    def select(self, catalog, intent, device="Sampler", candidates=()):
        result = select_track_sound(self.items, {
            "catalog_id": catalog,
            "preset_intent": intent,
            "native_device": device,
            "device_candidates": list(candidates),
        })
        self.assertIsNotNone(result)
        assert result is not None
        return result

    def test_tokens_normalize_accents(self):
        self.assertIn("calido", tokens("Cello cálido"))

    def test_identity_beats_generic_solo_and_section_words(self):
        self.assertEqual(self.select("violin_1", "expressive solo violin")[2], "violin")
        self.assertEqual(self.select("cello", "solo cello")[2], "cello")
        # No viola exists: a strings-family fallback is valid; a riser is not.
        viola = self.select("viola", "warm viola section")
        self.assertIn(viola[2], ("violin", "cello"))
        self.assertEqual(viola[3], "family_fallback")

    def test_winds_and_brass_stay_in_family(self):
        self.assertEqual(self.select("flute", "soft solo flute")[2], "flute")
        self.assertEqual(self.select("french_horns", "soft french horn ensemble")[2], "horns")

    def test_timbre_ranks_only_after_identity(self):
        self.assertEqual(self.select("analog_pad", "dark warm analog pad", "Analog")[2], "analog-pad")

    def test_percussion_identity_cannot_resolve_to_pad_bass_or_brass(self):
        self.assertEqual(self.select("timpani", "orchestral timpani")[2], "timpani")
        self.assertGreater(
            _rank_item("Analog Tom.adv", "sounds/Sounds/Percussive/Analog Tom.adv",
                       "cinematic low toms", "Sampler", "orchestral_percussion"),
            _rank_item("Timpani Orchestra.wav", "drums/Drums/Drum Hits/Misc Percussion/Timpani Orchestra.wav",
                       "cinematic low toms", "Sampler", "orchestral_percussion"))
        self.assertEqual(self.select("orchestral_percussion", "cinematic low toms")[2], "tom")
        self.assertEqual(self.select("cymbals", "orchestral cymbal swell")[2], "cymbal")

    def test_multivoice_latin_percussion_requires_a_populated_kit(self):
        self.assertEqual(self.select("latin_percussion", "natural latin percussion", "Drum Rack")[2],
                         "percussion-kit")

    def test_missing_orchestral_identity_uses_its_family(self):
        result = self.select("oboe", "expressive solo oboe", "Sampler", ("Wavetable",))
        self.assertEqual(result[2], "flute")
        self.assertEqual(result[3], "family_fallback")

    def test_raw_synth_is_an_explicit_audible_last_resort(self):
        result = self.select("unknown_voice", "unknown timbre", "Sampler", ("Wavetable",))
        self.assertEqual(result[2], "wavetable")
        self.assertEqual(result[3], "device_fallback")

    def test_empty_containers_never_match(self):
        result = select_track_sound(self.items, {
            "catalog_id": "unknown", "preset_intent": "balanced natural",
            "native_device": "Instrument Rack", "device_candidates": [],
        })
        self.assertIsNone(result)

    def test_generic_search_uses_complete_words(self):
        self.assertIsNone(best_inventory_match(self.items, "aire particles", "Granulator III"))


if __name__ == "__main__":
    unittest.main()
