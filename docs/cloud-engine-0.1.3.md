# Cloud engine 0.1.3

Approved engine source: `83a3faaee73c8044d05680adaa7aef81abaab10b`, tag
`engine-0.1.3`. Listening anchor and exact Windows worker hash are recorded in
`docs/anchors/engine-0.1.3-dorian-afterglow.md`. Linux source checksums are frozen
in `web/shared/engine-0.1.3-sources.sha256`; the Linux binary hash is registered
at deployment. No API key is embedded in source, manifests or executables.

The Cloud selector offers `0.1.3`, `v1` and `v1.1`. New drafts and requests
default to 0.1.3; explicit saved choices remain respected. Repeating a historical
work preserves its stored engine, using v1 only for legacy records without a
version. Migration 0018 expands the allowed versions without relabeling past
jobs or modifying works, credits, invitations or licenses.

The new engine has a separate immutable executable at
`engines/0.1.3/pulso_cloud_worker` and a frozen runtime profile matching the local
approved generation: first-pass audition, phrase architecture and Club ensemble
scenes enabled. Earlier engines retain their existing binaries and profiles;
checkpoint recovery keeps the original request's engine.

`scripts/deploy-cloud-engine-0.1.3.sh` accepts an exact git-archive release under
`/www/Pulso-release-<revision-prefix>`. It verifies sources, builds/tests the
Linux worker and Cloud API, checks that earlier engine hashes remain unchanged,
backs up PostgreSQL and refuses to interrupt active compositions. Health and
migration checks precede public static activation. API/static rollback is
automatic on activation failure; expanded database enum values are compatible
with the previous API. Private production configuration is reused, not replaced.

The Cloud dependency files brought into this development branch are exact
copies of the already deployed `fef9d1d8d227d239814dcc2290e0b8126715a7e8`
baseline. They preserve existing billing, invitations and email functionality;
this release changes engine selection, not their business rules.
