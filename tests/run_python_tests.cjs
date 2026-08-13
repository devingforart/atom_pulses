// Runs the pure Ableton Remote Script tests without requiring a system Python.
// Usage: npx -p pyodide node tests/run_python_tests.cjs
const fs = require("fs");
const { loadPyodide } = require("pyodide");

(async () => {
  const py = await loadPyodide();
  py.FS.mkdirTree("/workspace/ableton/PulsoDeployRemote");
  py.FS.mkdirTree("/workspace/tests");
  py.FS.writeFile("/workspace/ableton/__init__.py", "");
  py.FS.writeFile("/workspace/ableton/PulsoDeployRemote/__init__.py", "");
  for (const file of ["sound_matcher.py", "request_guard.py"])
    py.FS.writeFile(`/workspace/ableton/PulsoDeployRemote/${file}`,
                    fs.readFileSync(`ableton/PulsoDeployRemote/${file}`, "utf8"));
  for (const file of ["test_sound_matcher.py", "test_request_guard.py"])
    py.FS.writeFile(`/workspace/tests/${file}`, fs.readFileSync(`tests/${file}`, "utf8"));
  py.runPython(`
import sys
import unittest
sys.path.insert(0, "/workspace")
suite = unittest.defaultTestLoader.discover("/workspace/tests", pattern="test_*.py")
result = unittest.TextTestRunner(verbosity=2).run(suite)
assert result.wasSuccessful()
`);
  if (process.env.PULSO_AUDIT_INVENTORY && process.env.PULSO_AUDIT_REQUEST) {
    py.FS.writeFile("/workspace/inventory.json", fs.readFileSync(process.env.PULSO_AUDIT_INVENTORY));
    py.FS.writeFile("/workspace/request.json", fs.readFileSync(process.env.PULSO_AUDIT_REQUEST));
    py.runPython(`
import json
from ableton.PulsoDeployRemote.sound_matcher import select_track_sound
with open("/workspace/inventory.json", encoding="utf-8") as source:
    inventory = json.load(source)
with open("/workspace/request.json", encoding="utf-8") as source:
    request = json.load(source)
items = [(entry["name"], entry["path"], entry["path"]) for entry in inventory["items"]]
print("\\nPREDICTED LIVE SOUND RESOLUTION")
for spec in request["tracks"]:
    match = select_track_sound(items, spec)
    print("{} | {} -> {} | {}".format(
        spec.get("catalog_id", ""), spec.get("name", ""),
        match[0] if match else "MISSING", match[3] if match else "missing"))
`);
  }
})().catch(error => {
  console.error(error);
  process.exit(1);
});
