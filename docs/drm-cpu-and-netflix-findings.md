# DRM on 10.6: findings of record (2026-09-26)

## CPU cost of DRM playback

The ~100% CPU during DRM playback on 10.6 is **CDM-internal software H.264
decode**, measured by 10-second sampling of the GMP helper during active
playback. 6053 top-of-stack samples in VerifyCdmHost_0 (the CDM's obfuscated
decoder); zero in TLS emulation; zero in YUV→RGB conversion. No Gecko-side
optimization can reduce this.

The ~40-point gap vs Mavericks (same hardware) is likely the same CDM
running more efficiently on 10.9's libSystem/scheduler, not a different
decode path. The CDM does not link VideoToolbox on any OS.

## Hardware decode path: reachable but blocked by CDM quirk

The `media.eme.video.prefer-platform-decoder` path (CDM decrypt-only →
Gecko VDA hardware decode) is **technically reachable** — proven by
single-subsample probe: the CDM successfully decrypts video when the
subsample array has exactly one entry.

**Blocker**: the CDM's standalone `Decrypt` rejects subsample arrays with
2+ entries, returning kNoKey regardless of key availability. This is a CDM
build quirk (DecryptAndDecodeFrame handles the same multi-subsample data
correctly). Key metadata was verified correct (same kid as audio, present
in key store, status=usable, subsample layouts sum correctly).

**Workaround considered and rejected**: splitting multi-subsample decrypt
into N single-subsample calls with advanced IVs is cryptographically sound
and proven feasible, but was rejected for fragility on an unmaintained
branch, correctness-critical reassembly, and validation on only 9 frames
of one stream.

## Netflix on 10.7+: closed

Netflix requires a Widevine-VMP-verified host (a `.sig` file over the exact
binary bytes, issued via Mozilla's Autograph under license). Self-builds
structurally cannot obtain this. Confirmed against Widevine's own docs.
Not a bug; a platform limit.

## The 10.6 pthread lock-word collision (the original wedge)

The CDM's obfuscated runtime spills state through `%gs:0x10` — TSD slot 0
of the modern pthread layout — which on 10.6 is the per-thread lock word.
The spill poisons the lock; the next lock-taker spins forever in the
commpage `__spin_lock`.

**Fix**: neuter `__pthread_testcancel` in the GMP child (patch first byte
to `ret` at the real libSystem offset). This prevents the lock from ever
being taken, so the spill sits harmlessly. Zero repairs across a full
playback session (6168 frames).

**Page-integrity warning**: any byte modification of CDM text triggers its
page-integrity/decrypt-on-execute machinery, resulting in an eternal
NX-fault-retry at the modified instruction. Do not patch CDM code.
