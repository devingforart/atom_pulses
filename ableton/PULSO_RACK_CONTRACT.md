# PULSO Live-native sound contract

Every `FULL SONG` export writes `name.mid` and `name.pulso.json`. Each populated part
contains `catalog_id`, `live_device`, `live_preset_intent`, orchestral function,
articulation and divisi. No VST identifier is part of this contract.

`CREATE IN LIVE` publishes schema 3 to the Remote Script. The bridge indexes only native
Live browser roots and resolves candidates in this order:

1. Preset/sample whose complete-word identity matches `catalog_id` (for example cello).
2. Declared family fallback (for example Strings when no viola preset is installed).
3. Explicit audible native instrument such as Drift or Wavetable.

Colour and articulation words only rank candidates after identity passes. `solo`, `section`,
`orchestral`, `low` and similar adjectives cannot cross instrument families. Empty Rack,
Sampler and Simpler containers are forbidden. For Drum Racks, every MIDI pitch used by the
track must address a populated pad. The resolver reports identity/family/device quality,
loaded, fallback and missing counts; failure is preferred to a musically false assignment.
It never visits the Plug-Ins root or reports success for a device it did not verify.
