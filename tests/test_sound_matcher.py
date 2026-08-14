import unittest

from ableton.PulsoDeployRemote.sound_matcher import (
    _rank_item, best_inventory_match, catalog_capabilities, select_track_sound, tokens,
)


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
            ("Kick Synth Bass.aif", "drums/Drums/Drum Hits/Kick/Kick Synth Bass.aif", "bad-long-kick"),
            ("Kick 909 Tight.wav", "drums/Drums/Drum Hits/Kick/Kick 909 Tight.wav", "tight-kick"),
            ("Kick 909 Click Layer.wav", "drums/Drums/Drum Hits/Kick/Kick 909 Click Layer.wav", "click-kick"),
            ("Hihat Open 909.wav", "drums/Drums/Drum Hits/Hihat/Hihat Open 909.wav", "open-hat"),
            ("Hihat Closed Chirp.wav", "drums/Drums/Drum Hits/Hihat/Hihat Closed Chirp.wav", "closed-hat"),
            ("Clap Dry.wav", "drums/Drums/Drum Hits/Clap/Clap Dry.wav", "dry-clap"),
            ("Claves Dry.wav", "drums/Drums/Drum Hits/Misc Percussion/Claves Dry.wav", "claves"),
            ("Analog Tom.adv", "sounds/Sounds/Percussive/Analog Tom.adv", "tom"),
            ("Timpani Orchestra.wav", "drums/Drums/Drum Hits/Misc Percussion/Timpani Orchestra.wav", "timpani"),
            ("Crash 909.wav", "drums/Drums/Drum Hits/Cymbal/Crash 909.wav", "cymbal"),
            ("Crash Kick Reverse.aif", "drums/Drums/Drum Hits/Cymbal/Crash Kick Reverse.aif", "bad-cymbal"),
            ("909 Core Kit.adg", "drums/Drums/909 Core Kit.adg", "kit-909"),
            ("Percussion Core Kit.adg", "drums/Drums/Percussion Core Kit.adg", "percussion-kit"),
            ("Conga High.wav", "drums/Drums/Drum Hits/Conga/Conga High.wav", "single-conga"),
            ("FM Piano Filtered.adv", "sounds/Sounds/Piano & Keys/FM Piano Filtered.adv", "fm-piano"),
            ("Prepared Piano Mute.adv", "sounds/Sounds/Piano & Keys/Prepared Piano Mute.adv", "prepared-piano"),
            ("Violin Strings.adv", "sounds/Sounds/Strings/Violin Strings.adv", "violin"),
            ("Cello Strings.adv", "sounds/Sounds/Strings/Cello Strings.adv", "cello"),
            ("Flute Mellow.adv", "sounds/Sounds/Winds/Flute Mellow.adv", "flute"),
            ("Basic Synth Flute.adg", "sounds/Sounds/Winds/Basic Synth Flute.adg", "flute-2"),
            ("Horns Mellow.adg", "sounds/Sounds/Brass/Horns Mellow.adg", "horns"),
            ("Warm Analog Pad.adg", "sounds/Sounds/Pad/Warm Analog Pad.adg", "analog-pad"),
            ("Sub Sine Bass.adv", "sounds/Sounds/Bass/Sub Sine Bass.adv", "sub-sine"),
            ("Electric Bass Finger.adv", "sounds/Sounds/Bass/Electric Bass Finger.adv", "electric-finger"),
            ("Drift", "instruments/Instruments/Drift", "drift"),
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
        self.assertIn(self.select("flute", "soft solo flute")[2], ("flute", "flute-2"))
        self.assertEqual(self.select("french_horns", "soft french horn ensemble")[2], "horns")

    def test_timbre_ranks_only_after_identity(self):
        self.assertEqual(self.select("analog_pad", "dark warm analog pad", "Analog")[2], "analog-pad")

    def test_felt_piano_and_cymbal_intents_avoid_misleading_electronic_matches(self):
        self.assertEqual(self.select("piano", "felt intimate piano")[2], "prepared-piano")
        self.assertEqual(self.select("cymbals", "natural cymbal swell")[2], "cymbal")

    def test_reuse_is_avoided_within_the_same_identity_tier(self):
        first = self.select("flute", "soft flute")
        second = select_track_sound(self.items, {
            "catalog_id": "flute", "preset_intent": "soft flute", "native_device": "Sampler",
            "device_candidates": [],
        }, {first[1]})
        self.assertIsNotNone(second)
        assert second is not None
        self.assertNotEqual(second[1], first[1])

    def test_inventory_capabilities_distinguish_exact_and_family_only(self):
        capabilities = catalog_capabilities(self.items)
        self.assertIn("violin_1", capabilities["exact"])
        self.assertIn("viola", capabilities["family_fallback"])

    def test_percussion_identity_cannot_resolve_to_pad_bass_or_brass(self):
        self.assertEqual(self.select("timpani", "orchestral timpani")[2], "timpani")
        self.assertGreater(
            _rank_item("Analog Tom.adv", "sounds/Sounds/Percussive/Analog Tom.adv",
                       "cinematic low toms", "Sampler", "orchestral_percussion"),
            _rank_item("Timpani Orchestra.wav", "drums/Drums/Drum Hits/Misc Percussion/Timpani Orchestra.wav",
                       "cinematic low toms", "Sampler", "orchestral_percussion"))
        self.assertEqual(self.select("orchestral_percussion", "cinematic low toms")[2], "tom")
        self.assertEqual(self.select("cymbals", "orchestral cymbal swell")[2], "cymbal")

    def test_latin_percussion_prefers_a_semantic_one_shot_over_an_opaque_kit(self):
        result = self.select("latin_percussion", "natural latin percussion", "Drum Rack")
        self.assertEqual(result[2], "single-conga")
        self.assertEqual(result[3], "identity")

    def test_missing_solo_wind_uses_neutral_synth_not_wrong_named_instrument(self):
        result = self.select("oboe", "expressive solo oboe", "Sampler", ("Wavetable",))
        self.assertEqual(result[2], "wavetable")
        self.assertEqual(result[3], "device_fallback")

    def test_raw_synth_is_an_explicit_audible_last_resort(self):
        result = self.select("unknown_voice", "unknown timbre", "Sampler", ("Wavetable",))
        self.assertEqual(result[2], "wavetable")
        self.assertEqual(result[3], "device_fallback")

    def test_empty_container_request_resolves_to_audible_neutral_instrument(self):
        result = select_track_sound(self.items, {
            "catalog_id": "unknown", "preset_intent": "balanced natural",
            "native_device": "Instrument Rack", "device_candidates": [],
        })
        self.assertIsNotNone(result)
        self.assertEqual(result[2], "drift")
        self.assertEqual(result[3], "emergency_instrument")

    def test_low_end_roles_are_distinct_and_kick_has_a_short_identity(self):
        kick = self.select("kick_drum", "tight club kick with controlled sub tail")
        self.assertEqual(kick[2], "tight-kick")
        sub = self.select("sub_synth", "clean mono sine sub")
        movement = select_track_sound(self.items, {
            "catalog_id": "electric_bass", "preset_intent": "warm electronic bass groove",
            "native_device": "Wavetable", "device_candidates": ["Wavetable"],
        }, {sub[1]})
        self.assertIsNotNone(movement)
        self.assertEqual(movement[2], "electric-finger")
        self.assertNotEqual(movement[1], sub[1])

    def test_articulation_identity_beats_a_generic_family_match(self):
        open_hat = select_track_sound(self.items, {
            "catalog_id": "shakers", "preset_intent": "open hat",
            "articulation_aliases": ["open hat", "open hihat"],
            "native_device": "Drum Rack", "device_candidates": [],
        })
        self.assertIsNotNone(open_hat)
        self.assertEqual(open_hat[2], "open-hat")
        clap = select_track_sound(self.items, {
            "catalog_id": "snare_clap", "preset_intent": "clap",
            "articulation_aliases": ["clap"], "native_device": "Drum Rack",
            "device_candidates": [],
        })
        self.assertEqual(clap[2], "dry-clap")

    def test_kick_click_layer_cannot_become_the_complete_kick(self):
        kick = self.select("kick_drum", "tight full body club kick with controlled sub tail")
        self.assertEqual(kick[2], "tight-kick")
        self.assertNotEqual(kick[2], "click-kick")

    def test_missing_contrabass_never_masquerades_as_violin(self):
        result = self.select("contrabass", "deep natural contrabass", "Sampler", ("Drift",))
        self.assertEqual(result[2], "drift")
        self.assertEqual(result[3], "device_fallback")

    def test_generic_search_uses_complete_words(self):
        self.assertIsNone(best_inventory_match(self.items, "aire particles", "Granulator III"))


if __name__ == "__main__":
    unittest.main()
