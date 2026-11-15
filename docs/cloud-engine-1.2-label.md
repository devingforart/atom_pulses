# Public engine version 1.2

The user corrected the latest engine's public name from `0.1.3` to `1.2`.
This is the same listener-approved engine source
`83a3faaee73c8044d05680adaa7aef81abaab10b` and the same deployed Linux binary
SHA256 `3427e013bfeb57fc55aabd7f76f5eacb6687444eb2dd630af59570d19d833f85`.
No composer sources, notes, runtime profile or musical decisions change.

New Cloud drafts and requests default to 1.2. Existing 0.1.3 drafts/repeat
settings/display labels map to 1.2; v1 and v1.1 selections remain unchanged.
Historical database requests keep their original identifiers. The backend
accepts 0.1.3 for compatibility and maps both names to the same stored worker
and immutable runtime profile. Migration 0019 adds the alias and updates the
database default without rewriting past compositions.

The label-only Dockerfile reuses the exact deployed image's worker and builds
only the API and frontend, not the musical engine or VST. Deploy using the
existing guarded release script with `PULSO_ENGINE_RELEASE_DOCKERFILE` set to
`web/Dockerfile.engine-label-release`, `PULSO_ENGINE_RELEASE_MIGRATION=19`
and `PULSO_ENGINE_PUBLIC_LABEL=1.2`. Existing source/binary checks, no-active-job
guard, backups and rollback remain in force.
