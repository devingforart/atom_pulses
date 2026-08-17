import unittest

from ableton.PulsoDeployRemote.audible_contract import (
    aggregate_meter_snapshots, apply_release_contract, meter_snapshot,
    release_target_seconds,
)


class FakeParameter:
    def __init__(self, name, value):
        self.name = name
        self.min = 0.0
        self.max = 2.0
        self.value = value

    def str_for_value(self, value):
        return "{:.1f} ms".format(value * 1000.0)


class FakeDevice:
    def __init__(self, name="Instrument"):
        self.name = name
        self.parameters = [FakeParameter("Amp Release", 1.2)]
        self.chains = []


class AudibleContractTests(unittest.TestCase):
    def test_release_contract_caps_parseable_instrument_envelope(self):
        device = FakeDevice()
        report = apply_release_contract(device, {"catalog_id": "kick_drum"})
        self.assertEqual(release_target_seconds({"catalog_id": "kick_drum"}), 0.14)
        self.assertEqual(report["parameters_capped"], 1)
        self.assertLessEqual(device.parameters[0].value, 0.141)

    def test_effect_release_is_not_rewritten_as_instrument_envelope(self):
        report = apply_release_contract(FakeDevice("Reverb"), {"catalog_id": "analog_pad"})
        self.assertEqual(report["parameters_found"], 0)

    def test_meter_audit_distinguishes_presence_and_excess_tail(self):
        spec = {"catalog_id": "kick_drum", "notes": [
            {"start": 4.0, "duration": 0.25},
        ]}
        active = meter_snapshot(spec, 0.4, 4.1, 120.0)
        tail = meter_snapshot(spec, 0.2, 5.0, 120.0)
        self.assertTrue(active["expected_active"])
        self.assertFalse(active["silent_while_active"])
        self.assertTrue(tail["tail_violation"])
        aggregate = aggregate_meter_snapshots([active, tail])
        self.assertEqual(aggregate["audible_presence_ratio"], 1.0)
        self.assertEqual(aggregate["tail_violations"], 1)
        self.assertFalse(aggregate["spectral_analysis_available"])


if __name__ == "__main__":
    unittest.main()
