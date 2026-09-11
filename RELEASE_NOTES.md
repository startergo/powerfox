Nightly — Firefox ESR 153 cross-compiled for Mac OS X 10.6.8 (x86_64), from the 10.6-backporting branch (894a3cc9d2f4). The app is branded Nightly (bundle id org.mozilla.nightlyunofficial) so it sits alongside — not on top of — any other Firefox, PowerFox or PowerFox-Browser install.

Requirements:
- A 64-bit-capable Mac running Mac OS X 10.6.8 (Snow Leopard). The app is a 64-bit (x86_64) binary, which runs on both the 32-bit and 64-bit kernel — the 32-bit kernel most Snow Leopard Macs boot by default should be fine. (We test on the 64-bit kernel.)
- The bundled libc++ runtime ships inside the app; no extra installs needed

What's new since macos10.6-pre2:
- WebGL 1 works, rendered on the graphics card. The WebGL implementation runs in-process (this OS vintage has no GPU process), creates a real hardware GL context, and presents through the same graphics-card compositing path video uses — no readback, no intermediate copies. WebGL 1 games and demos run at display rate on 2010-class hardware with headroom (verified on a Core i5 MacBook Pro).
- On two-core machines, frame delivery is paced to what the GPU actually sustains: an even, steady frame rate instead of a nominal higher number with visible stutter. Multisampling is off by default there for the same reason (it costs about a third of the achievable rate); pages that explicitly request antialiasing still get it.
- WebGL 2 remains unavailable — it requires a GL 3.2 core context, which no 10.6 driver offers (verified: the driver rejects the request outright). Sites that try WebGL 2 and fall back to WebGL 1 now work like they do on the sibling UXP build; forcing via webgl.force-enabled keeps its usual behavior.
- WebGL contexts keep a clean GL error state; a driver quirk that surfaced one spurious error per frame to pages that call getError() themselves is worked around internally.

What works (verified on 2010 hardware — MacBookPro6,1 and a Macmini3,1):
- Builds from a modern arm64 host against the 10.6 SDK (see README)
- Launches from Finder and terminal; default profile just works
- HTTP and HTTPS browsing
- Video: H.264 in hardware where the GPU supports it (see below), otherwise software; HEVC, VP9 software decode with audio (the vendored ffvpx carries H.264/HEVC/AAC decoders, bit-exact vs system ffmpeg — unlike stock Firefox, which has no software HEVC at all); AV1 decode is off (too slow on this class of CPU)
- WebGL 1 (see above); software WebRender compositing in-process (no GPU process) with hosted CA presentation — video and WebGL canvases composite on the graphics card

Video quality guidance:
- Prefer H.264 where offered. On VDA-capable GPUs (9400-class verified) hardware decode makes 720p/1080p H.264 by far the cheapest option. An extension such as h264ify, or media.webm.enabled = false in about:config, steers YouTube to H.264.
- 720p is the sweet spot on 2-core machines; 60fps content decodes in software at any resolution. Fullscreen playback is smooth on 2010 MacBook-class CPUs; on slower machines with large displays fullscreen may drop frames, and CPU now returns to normal when you leave fullscreen.
- On machines without working VDA, VP9 at the same resolution gives better image quality if its decode keeps up; pick by which constraint bites you.

Tunable prefs (set in about:config or user.js):
- media.mediasource.vp9.enabled = false (plus media.av1.enabled = false, already the default) — steers YouTube to H.264 (see quality guidance above)
- media.rdd-process.enabled — keep enabled (default); disabling it currently breaks H.264 playback (falls through to the GMP plugin, which crashes on 10.6)
- webgl.default-antialias = true — restores multisampled WebGL on two-core machines if you prefer image quality over frame rate
- Note: gfx.webrender.software = false does NOT enable full GPU page rendering on this build — it silently falls back to software WebRender (verified via about:support). Page raster stays on the CPU; video and WebGL canvases composite on the GPU. Leave it at the default

Known limitations:
- DNS-over-HTTPS and HTTP/3 are disabled; both hang on this OS vintage. Regular DNS and HTTP/1.1 + HTTP/2 are unaffected.
- Page rasterization is software; decoding is hardware for supported H.264 and software otherwise. High-bitrate or high-resolution software decode can still saturate a 2-core CPU — cap quality accordingly on machines without working VDA.
- WebGL 2 and WebGL2-dependent sites cannot work on this hardware generation: 10.6's GL driver tops out at GL 2.1 and rejects 3.2 core-context requests outright (verified on both test machines).
- Hardware decode availability is driver-dependent: High profile works where Main profile is refused on our 9400M; the GT 330M in the 2010 MacBook Pro refuses all sessions — the OS loads the older VP3 decode driver for that GPU's newer (VP4) decode block (verified: AVA_HD_VP3.dylib maps while AVA_HD_P4.dylib sits unused in the same bundle). Fallback is automatic.

Notes:
- Developer tools live under Tools → Browser Tools

Full build instructions are in the repository README (mozconfig-macos106 + tools/legacy-macos).
