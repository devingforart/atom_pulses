{
  "patcher": {
    "fileversion": 1,
    "appversion": { "major": 9, "minor": 0, "revision": 0, "architecture": "x64" },
    "classnamespace": "box",
    "rect": [100.0, 100.0, 420.0, 180.0],
    "boxes": [
      { "box": { "id": "device", "maxclass": "newobj", "text": "live.thisdevice", "patching_rect": [20.0, 20.0, 92.0, 22.0] } },
      { "box": { "id": "button", "maxclass": "live.text", "text": "NEW VARIATION", "texton": "NEW VARIATION", "parameter_enable": 1, "patching_rect": [20.0, 62.0, 130.0, 30.0] } },
      { "box": { "id": "js", "maxclass": "newobj", "text": "js pulso_bridge.js", "patching_rect": [170.0, 66.0, 124.0, 22.0] } },
      { "box": { "id": "midiout", "maxclass": "newobj", "text": "midiout", "patching_rect": [320.0, 66.0, 52.0, 22.0] } },
      { "box": { "id": "status", "maxclass": "message", "text": "ready", "patching_rect": [170.0, 112.0, 202.0, 22.0] } }
    ],
    "lines": [
      { "patchline": { "source": ["button", 0], "destination": ["js", 0] } },
      { "patchline": { "source": ["js", 0], "destination": ["midiout", 0] } },
      { "patchline": { "source": ["js", 1], "destination": ["status", 1] } }
    ]
  }
}

