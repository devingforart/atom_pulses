import json
import os
import tempfile
import unittest

from ableton.PulsoDeployRemote.request_guard import RequestGuard


class RequestGuardTests(unittest.TestCase):
    def test_existing_request_is_adopted_and_never_replayed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "request.json")
            with open(path, "w", encoding="utf-8") as target:
                json.dump({"request_id": "old-deployment"}, target)
            guard = RequestGuard(path)
            self.assertEqual(guard.adopted_request_id, "old-deployment")
            self.assertFalse(guard.claim({"request_id": "old-deployment"}))
            self.assertTrue(guard.claim({"request_id": "explicit-new-click"}))
            self.assertFalse(guard.claim({"request_id": "explicit-new-click"}))

    def test_missing_or_invalid_request_never_deploys(self):
        with tempfile.TemporaryDirectory() as folder:
            guard = RequestGuard(os.path.join(folder, "missing.json"))
            self.assertFalse(guard.claim({}))
            self.assertFalse(guard.claim(None))


if __name__ == "__main__":
    unittest.main()
