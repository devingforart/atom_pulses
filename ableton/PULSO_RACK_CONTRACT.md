# PULSO Live-native sound contract

Every `FULL SONG` export writes `name.mid` and `name.pulso.json`. Each populated part
contains `catalog_id`, `live_device`, `live_preset_intent`, orchestral function,
articulation and divisi. No VST identifier is part of this contract.

`CREATE IN LIVE` publishes schema 3 to the Remote Script. The bridge indexes only native
Live browser roots and resolves candidates in this order:

1. Installed preset or Rack matching `preset_intent` and the preferred device.
2. The exact native device chosen by AI or the user.
3. A generic installed Instrument Rack.

The resolver reports loaded, fallback and missing counts. It never visits the Plug-Ins
root and never reports success for a device it did not load. User Racks remain portable:
giving them descriptive names and tags improves matching without requiring proprietary
sample content in the PULSO repository.
