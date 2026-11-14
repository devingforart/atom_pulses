# One energy summary per macro section

The failed candidate a4e5efc1-4824-44d6-8fe4-9306cde433e8 had one 32-bar
macro section. The AI returned three energy_trajectory entries, all section 0,
describing reveal, tension and return inside that section. Array items were
bound to valid IDs but schema cardinality/uniqueness did not match runtime's
one-summary-per-section contract. This is a serialization disagreement, not a
reason to invent additional sections or rewrite the groove's notes.

New Club requests with an actual cast/form use energy_trajectory keyed by
section_0, section_1 etc. Each existing key is required exactly once; no extra
keys or competing section_index field are admitted. The AI authors its values
and may describe multiple phases within the same summary. Decoding restores
the existing persisted indexed-array format without merging entries, averaging
energy/density, summarizing text, or touching musical reference notes. Historical
arrays remain unchanged and are still validated rather than silently repaired.

The focused-plan dispatcher was also audited: hierarchy errors now enter its
focused-repair path directly, not only when encountered after another repair.
This closes the entry-gate omission in the previous hierarchy handler.

Tests cover one/multiple sections, missing/unknown/conflicting identities,
phase-text/value preservation, source-note nonmutation, idempotence and legacy
compatibility. This mapping constrains metadata identity, not musical form.
