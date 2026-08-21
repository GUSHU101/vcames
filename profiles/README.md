# Device Profile catalog

This directory contains public, exact-build metadata only. It must never contain vendor camera blobs, OEM libraries, private signing keys, user media, or credentials.

The catalog is intentionally empty: the repository has no Google or Xiaomi handset/OTA combination that has completed the release gates yet. A candidate must not be called `VERIFIED` until its device report exists, its compatibility ID and artifact hashes match, and the signed catalog validator accepts it.

Run:

```bash
python3 tools/compatibility-builder/validate_profiles.py profiles/catalog.json
```

For a release catalog, sign canonical JSON with an offline Ed25519 key. Keep the private key outside the repository and pass the public key to the validator. See `docs/PROFILE_SCHEMA.md` and `docs/RELEASE_GATES.md`.
