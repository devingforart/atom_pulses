import unittest

from ableton.PulsoDeployRemote.neutral_audition import (
    apply_neutral_contract, audition_profile, neutral_patch, select_neutral_sound)


class NeutralAuditionTests(unittest.TestCase):
    def test_profiles_follow_musical_function_not_authored_preset_branding(self):
        self.assertEqual(audition_profile({"department": "harmony", "catalog_id": "sub_synth"}),
                         "sub")
        self.assertEqual(audition_profile({"department": "harmony", "role": "hypnotic arpeggio"}),
                         "arp")
        self.assertEqual(audition_profile({"department": "melody", "preset_intent": "rare oboe"}),
                         "lead")
        self.assertEqual(audition_profile({"department": "harmony", "role": "constant pad floor"}),
                         "pad")

    def test_contract_preserves_ai_intent_but_publishes_neutral_device(self):
        result = apply_neutral_contract({
            "department": "melody", "native_device": "Sampler",
            "preset_intent": "breathy orchestral flute", "sound_locked": True,
        })
        self.assertEqual(result["authored_native_device"], "Sampler")
        self.assertEqual(result["authored_preset_intent"], "breathy orchestral flute")
        self.assertEqual(result["native_device"], "Wavetable")
        self.assertEqual(result["device_candidates"], ["Wavetable", "Drift", "Operator"])
        self.assertEqual(result["audition_policy"], "neutral_role_v1")
        self.assertFalse(result["sound_locked"])

    def test_selection_is_stable_and_falls_back_only_inside_native_palette(self):
        items = [
            ("Breathy Flute.adg", "Sounds/Winds/Breathy Flute.adg", "flute"),
            ("Operator", "Instruments/Operator", "operator"),
            ("Drift", "Instruments/Drift", "drift"),
        ]
        spec = apply_neutral_contract({"department": "melody", "catalog_id": "flute"})
        first = select_neutral_sound(items, spec)
        second = select_neutral_sound(items, spec)
        self.assertEqual(first, second)
        self.assertEqual(first[0], "Drift")
        self.assertEqual(first[3], "neutral_audition")

    def test_release_limits_are_short_and_versioned_by_role(self):
        self.assertLessEqual(neutral_patch("arp")["release"], 0.12)
        self.assertLessEqual(neutral_patch("pad")["release"], 0.60)


if __name__ == "__main__":
    unittest.main()
