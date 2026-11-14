# Musical moments versus existing macro sections

Job 28bd5b79-4910-4bc8-a40c-69459899ba68 received a completed, syntactically
valid short score. Its macro contains one 32-bar section, while four journey
entries name section indices 0,1,2,3. The schema allowed any nonnegative integer;
the runtime correctly rejected nonexistent sections but reported only a generic
invalid-structure message. Completion of transport did not imply valid references.

The short-score schema now enumerates actual macro section IDs. Several musical
moments may name the same existing section; journey order is not a section ID.
No validator maps, renumbers, deletes, transposes or rewrites those moments or
their notes. Existing references in historical data remain subject to the same
validation predicates. Validation now optionally returns a precise field path and
reason for invalid section/owner/intent/reference-note fields, and the worker
surfaces that diagnosis rather than reporting a completed response as generically
invalid. The recovery reuses the accepted macro/cast and asks the AI to author
the argument with the corrected schema; it does not regenerate the macro.

Tests reproduce four fictitious indices over one real section, verify no source
mutation, permit four moments sharing the valid section, verify multi-section
enumeration, and check diagnostics are cleared after a successful validation.
