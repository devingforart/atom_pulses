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
  for (const file of ["sound_matcher.py", "playback_adapter.py", "audible_contract.py", "deployment_planner.py", "request_guard.py", "pulso_deploy.py"])
    py.FS.writeFile(`/workspace/ableton/PulsoDeployRemote/${file}`,
                    fs.readFileSync(`ableton/PulsoDeployRemote/${file}`, "utf8"));
  for (const file of ["test_sound_matcher.py", "test_playback_adapter.py", "test_audible_contract.py", "test_deployment_planner.py", "test_request_guard.py"])
    py.FS.writeFile(`/workspace/tests/${file}`, fs.readFileSync(`tests/${file}`, "utf8"));
  py.runPython(`
import sys
import unittest
sys.path.insert(0, "/workspace")
for module in ("sound_matcher.py", "playback_adapter.py", "audible_contract.py", "deployment_planner.py", "request_guard.py", "pulso_deploy.py"):
    path = "/workspace/ableton/PulsoDeployRemote/" + module
    with open(path, encoding="utf-8") as source:
        compile(source.read(), path, "exec")
suite = unittest.defaultTestLoader.discover("/workspace/tests", pattern="test_*.py")
result = unittest.TextTestRunner(verbosity=2).run(suite)
assert result.wasSuccessful()
`);
  if (process.env.PULSO_AUDIT_INVENTORY && process.env.PULSO_AUDIT_REQUEST) {
    py.FS.writeFile("/workspace/inventory.json", fs.readFileSync(process.env.PULSO_AUDIT_INVENTORY));
    py.FS.writeFile("/workspace/request.json", fs.readFileSync(process.env.PULSO_AUDIT_REQUEST));
    py.runPython(`
import json
from ableton.PulsoDeployRemote.playback_adapter import (
    expand_percussion_specs, is_one_shot_path)
from ableton.PulsoDeployRemote.deployment_planner import resolve_deployment
from ableton.PulsoDeployRemote.sound_matcher import intent_fidelity
with open("/workspace/inventory.json", encoding="utf-8") as source:
    inventory = json.load(source)
with open("/workspace/request.json", encoding="utf-8") as source:
    request = json.load(source)
items = [(entry["name"], entry["path"], entry["path"]) for entry in inventory["items"]]
print("\\nPREDICTED LIVE PLAYBACK CONTRACTS")
plan = resolve_deployment(items, expand_percussion_specs(request["tracks"]))
print("resolved={} missing={} blocking={}".format(
    len(plan["resolved"]), len(plan["unresolved"]), plan["blocking_timbres"]))
for spec, match in plan["resolved"]:
    source = "one_shot@60" if match and is_one_shot_path(match[1]) else "instrument"
    fidelity = intent_fidelity(match[0], match[1], spec) if match else 0.0
    print("{} | {} -> {} | {} | fidelity {:.2f} | {}{}".format(
        spec.get("catalog_id", ""), spec.get("name", ""),
        match[0] if match else "MISSING", match[3] if match else "missing", fidelity, source,
        " | shared" if match and match[4] else ""))
`);
  }
})().catch(error => {
  console.error(error);
  process.exit(1);
});
