import unittest

from ableton.PulsoDeployRemote.sound_matcher import (
    _rank_item, best_inventory_match, catalog_capabilities, intent_fidelity,
    select_track_sound, spec_intent_consistency, tokens,
    select_track_sound_variants,
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
            ("Kick Open Hat Combo.wav", "drums/Drums/Drum Hits/Kick/Kick Open Hat Combo.wav", "combo-hat"),
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
        self.assertEqual(viola[3], "character_fallback")

    def test_winds_and_brass_stay_in_family(self):
        self.assertIn(self.select("flute", "soft solo flute")[2], ("flute", "flute-2"))
        self.assertEqual(self.select("french_horns", "soft french horn ensemble")[2], "horns")

    def test_timbre_ranks_only_after_identity(self):
        self.assertEqual(self.select("analog_pad", "dark warm analog pad", "Analog")[2], "analog-pad")

    def test_felt_piano_and_cymbal_intents_avoid_misleading_electronic_matches(self):
        self.assertEqual(self.select("piano", "felt intimate piano")[2], "prepared-piano")
        self.assertEqual(self.select("cymbals", "natural cymbal swell")[2], "cymbal")

    def test_character_intent_is_binding_after_instrument_identity(self):
        breathy = {"catalog_id": "alto_flute", "preset_intent": "breathy alto flute"}
        generic = select_track_sound([
            ("Basic Synth Flute.adg", "Sounds/Winds/Basic Synth Flute.adg", "generic")
        ], breathy)
        self.assertIsNotNone(generic)
        self.assertEqual(generic[3], "character_fallback")
        self.assertLess(intent_fidelity(generic[0], generic[1], breathy), 0.62)

        exact = select_track_sound([
            ("Basic Synth Flute.adg", "Sounds/Winds/Basic Synth Flute.adg", "generic"),
            ("Breathy Alto Flute.adg", "Sounds/Winds/Breathy Alto Flute.adg", "exact"),
        ], breathy)
        self.assertEqual(exact[2], "exact")
        self.assertEqual(exact[3], "identity")

    def test_glassy_synth_and_felt_muted_piano_report_character_mismatch(self):
        glass = {"catalog_id": "poly_synth", "preset_intent": "glassy poly synth"}
        brass = select_track_sound([
            ("Poly Synth Brass.adg", "Sounds/Synth/Poly Synth Brass.adg", "brass")
        ], glass)
        self.assertEqual(brass[3], "character_fallback")
        piano = {"catalog_id": "piano", "preset_intent": "felt muted piano"}
        grand = select_track_sound([
            ("Grand Piano Single Sample.adg", "Sounds/Piano/Grand Piano.adg", "grand")
        ], piano)
        self.assertEqual(grand[3], "character_fallback")

    def test_reuse_is_avoided_within_the_same_identity_tier(self):
        first = self.select("flute", "soft flute")
        second = select_track_sound(self.items, {
            "catalog_id": "flute", "preset_intent": "soft flute", "native_device": "Sampler",
            "device_candidates": [],
        }, {first[1]})
        self.assertIsNotNone(second)
        assert second is not None
        self.assertNotEqual(second[1], first[1])

    def test_duplicate_preset_name_is_treated_as_the_same_timbre(self):
        items = [
            ("ASMR Cave Texture.adg", "sounds/A/ASMR Cave Texture.adg", "cave-a"),
            ("ASMR Cave Texture.adg", "sounds/B/ASMR Cave Texture.adg", "cave-b"),
            ("Granular Air Texture.adg", "sounds/C/Granular Air Texture.adg", "air"),
        ]
        first = select_track_sound(items, {
            "catalog_id": "ambient_texture", "preset_intent": "dark cave texture",
            "native_device": "Granulator III", "device_candidates": [],
        })
        self.assertIsNotNone(first)
        second = select_track_sound(items, {
            "catalog_id": "ambient_texture", "preset_intent": "upper granular air texture",
            "native_device": "Granulator III", "device_candidates": [],
        }, {first[1].casefold(), "name:" + first[0].casefold()})
        self.assertIsNotNone(second)
        self.assertEqual(second[2], "air")

    def test_register_role_prevents_inverted_pad_selection(self):
        items = [
            ("Glass High Strings Pad.adv", "sounds/Pad/Glass High Strings Pad.adv", "high"),
            ("Glass Low Strings Pad.adv", "sounds/Pad/Glass Low Strings Pad.adv", "low"),
        ]
        low = select_track_sound(items, {
            "catalog_id": "analog_pad", "preset_intent": "dark low foundation body",
            "native_device": "Wavetable", "device_candidates": [],
        })
        high = select_track_sound(items, {
            "catalog_id": "analog_pad", "preset_intent": "luminous upper air",
            "native_device": "Wavetable", "device_candidates": [],
        })
        self.assertEqual(low[2], "low")
        self.assertEqual(high[2], "high")

    def test_split_ride_never_falls_back_to_a_clap(self):
        result = select_track_sound([
            ("Claps and Caixa.wav", "drums/Drum Hits/Clap/Claps and Caixa.wav", "clap"),
        ], {
            "catalog_id": "orchestral_percussion", "preset_intent": "ride",
            "articulation_aliases": ["ride", "cymbal"],
            "native_device": "Drum Rack", "device_candidates": [],
        })
        self.assertIsNone(result)

    def test_register_and_state_qualifiers_are_part_of_percussion_identity(self):
        items = [
            ("Bongo C78 Hi.wav", "Drums/Drum Hits/Bongo/Bongo C78 Hi.wav", "bongo-high"),
            ("Bongo C78 Low.wav", "Drums/Drum Hits/Bongo/Bongo C78 Low.wav", "bongo-low"),
            ("Conga Hi Muted.wav", "Drums/Drum Hits/Conga/Conga Hi Muted.wav", "conga-muted"),
            ("Conga Open Hard.wav", "Drums/Drum Hits/Conga/Conga Open Hard.wav", "conga-open"),
            ("Hihat Open Pedal.wav", "Drums/Drum Hits/Hihat/Hihat Open Pedal.wav", "hat-pedal"),
            ("Hihat Open Pure.wav", "Drums/Drum Hits/Hihat/Hihat Open Pure.wav", "hat-open"),
        ]
        def choose(catalog, identity, aliases):
            return select_track_sound(items, {
                "catalog_id": catalog, "preset_intent": identity,
                "articulation_aliases": aliases,
            })
        self.assertEqual(choose("latin_percussion", "high bongo", ["high bongo", "bongo"])[2],
                         "bongo-high")
        self.assertEqual(choose("latin_percussion", "low bongo", ["low bongo", "bongo"])[2],
                         "bongo-low")
        self.assertEqual(choose("latin_percussion", "mute high conga",
                                ["mute high conga", "muted conga", "conga"])[2],
                         "conga-muted")
        self.assertEqual(choose("latin_percussion", "open high conga",
                                ["open high conga", "open conga", "conga"])[2],
                         "conga-open")
        self.assertEqual(choose("hi_hats", "pedal hat", ["pedal hat", "pedal hihat"])[2],
                         "hat-pedal")
        self.assertEqual(choose("hi_hats", "open hat", ["open hat", "open hihat"])[2],
                         "hat-open")

    def test_china_splash_crash_and_ride_do_not_collapse_into_one_cymbal(self):
        items = [
            ("Crash China Dark.wav", "Drums/Drum Hits/Cymbal/Crash China Dark.wav", "china"),
            ("Crash Club.wav", "Drums/Drum Hits/Cymbal/Crash Club.wav", "crash"),
            ("Splash Short.wav", "Drums/Drum Hits/Cymbal/Splash Short.wav", "splash"),
            ("Ride Dry.wav", "Drums/Drum Hits/Cymbal/Ride Dry.wav", "ride"),
        ]
        def choose(identity, aliases):
            return select_track_sound(items, {
                "catalog_id": "cymbals", "preset_intent": identity,
                "articulation_aliases": aliases,
            })
        self.assertEqual(choose("chinese cymbal", ["chinese cymbal", "china cymbal"])[2], "china")
        self.assertEqual(choose("splash", ["splash", "cymbal"])[2], "splash")
        self.assertEqual(choose("crash", ["crash", "cymbal"])[2], "crash")
        self.assertEqual(choose("ride", ["ride", "cymbal"])[2], "ride")

    def test_split_articulation_rejects_tempo_loops_compounds_and_chromatic_presets(self):
        items = [
            ("Congas 128 bpm.aif", "drums/Drum Hits/Misc Percussion/Congas 128 bpm.aif", "loop"),
            ("Conga Dry.wav", "drums/Drum Hits/Conga/Conga Dry.wav", "conga-hit"),
            ("Conga and Tambourine 106 bpm.aif",
             "drums/Drum Hits/Misc Percussion/Conga and Tambourine 106 bpm.aif", "compound"),
            ("Tambourine Dry.wav", "drums/Drum Hits/Misc Percussion/Tambourine Dry.wav", "tamb-hit"),
            ("E-Ride Cymbal Bell.adv", "drums/Drum Hits/Misc Percussion/E-Ride Cymbal Bell.adv", "preset"),
            ("Kick Buss Ride.wav", "drums/Drum Hits/Ride/Kick Buss Ride.wav", "compound-ride"),
            ("Ride Dry.wav", "drums/Drum Hits/Ride/Ride Dry.wav", "ride-hit"),
        ]
        conga = select_track_sound(items, {
            "catalog_id": "latin_percussion", "preset_intent": "mute high conga",
            "articulation_aliases": ["mute high conga", "muted conga", "conga"],
        })
        tambourine = select_track_sound(items, {
            "catalog_id": "orchestral_percussion", "preset_intent": "tambourine",
            "articulation_aliases": ["tambourine"],
        })
        ride = select_track_sound(items, {
            "catalog_id": "orchestral_percussion", "preset_intent": "ride",
            "articulation_aliases": ["ride", "cymbal"],
        })
        self.assertEqual(conga[2], "conga-hit")
        self.assertEqual(tambourine[2], "tamb-hit")
        self.assertEqual(ride[2], "ride-hit")

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

    def test_clean_sine_sub_beats_a_complex_saw_even_when_both_are_subs(self):
        result = select_track_sound([
            ("Saw Sub Complex Bass.adv", "Sounds/Bass/Saw Sub Complex Bass.adv", "saw"),
            ("Basic Sub Sine.adv", "Sounds/Bass/Basic Sub Sine.adv", "sine"),
        ], {
            "catalog_id": "sub_synth", "preset_intent": "clean mono sine sub",
            "native_device": "Operator", "device_candidates": [],
        })
        self.assertEqual(result[2], "sine")
        self.assertEqual(result[3], "identity")

    def test_global_palette_cannot_contaminate_an_individual_track_intent(self):
        result = select_track_sound([
            ("Muted Lead.adv", "Sounds/Lead/Muted Lead.adv", "muted"),
            ("Warm Sweet Lead.adv", "Sounds/Lead/Warm Sweet Lead.adv", "warm"),
        ], {
            "catalog_id": "lead_synth", "name": "Warm Lead",
            "preset_intent": "warm expressive lead",
            "sound_world": "muted bass and glassy piano",
            "native_device": "Wavetable", "device_candidates": [],
        })
        self.assertEqual(result[2], "warm")

    def test_track_identity_and_preset_character_contradiction_is_visible(self):
        self.assertEqual(spec_intent_consistency({
            "name": "Glassy Piano Memory", "role": "luminous high figure",
            "preset_intent": "soft felt piano",
        }), 0.5)
        self.assertEqual(spec_intent_consistency({
            "name": "Warm Pad", "role": "warm harmonic body",
            "preset_intent": "warm analog pad",
        }), 1.0)
        self.assertEqual(spec_intent_consistency({
            "name": "D Minor Sub Anchor",
            "role": "Low foundation absent during bright foreground attacks",
            "preset_intent": "clean sine sub bass",
        }), 1.0)

    def test_articulation_identity_beats_a_generic_family_match(self):
        open_hat = select_track_sound(self.items, {
            "catalog_id": "shakers", "preset_intent": "open hat",
            "articulation_aliases": ["open hat", "open hihat"],
            "native_device": "Drum Rack", "device_candidates": [],
        })
        self.assertIsNotNone(open_hat)
        self.assertEqual(open_hat[2], "open-hat")
        self.assertNotEqual(open_hat[2], "combo-hat")
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

    def test_raw_909_kick_inherits_observable_dry_low_character(self):
        spec = {"catalog_id": "kick_drum", "preset_intent": "deep dry electronic kick"}
        fidelity = intent_fidelity("Kick 909 1.aif", "Drums/Drum Hits/Kick/Kick 909 1.aif", spec)
        self.assertGreaterEqual(fidelity, 0.65)

    def test_round_robin_sound_selection_returns_distinct_exact_hits(self):
        matches = select_track_sound_variants([
            ("Hihat Closed Chirp.wav", "Drums/Drum Hits/Hihat/Hihat Closed Chirp.wav", "a"),
            ("Hihat Closed Dust.wav", "Drums/Drum Hits/Hihat/Hihat Closed Dust.wav", "b"),
        ], {
            "catalog_id": "hi_hats", "preset_intent": "closed hat",
            "articulation_aliases": ["closed hat", "closed hihat", "hat"],
        }, 2)
        self.assertEqual(len(matches), 2)
        self.assertNotEqual(matches[0][1], matches[1][1])

    def test_missing_contrabass_never_masquerades_as_violin(self):
        result = self.select("contrabass", "deep natural contrabass", "Sampler", ("Drift",))
        self.assertEqual(result[2], "drift")
        self.assertEqual(result[3], "device_fallback")

    def test_generic_search_uses_complete_words(self):
        self.assertIsNone(best_inventory_match(self.items, "aire particles", "Granulator III"))


if __name__ == "__main__":
    unittest.main()
