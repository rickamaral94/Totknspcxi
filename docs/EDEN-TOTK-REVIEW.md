# Eden/TOTK review — 2026-11-02

Primary sources inspected:
- https://git.eden-emu.dev/eden-ci/nightly/releases
- https://git.eden-emu.dev/eden-emu/eden/pulls/4565 and /files
- https://git.eden-emu.dev/eden-emu/eden/pulls/4551 and /files
- https://git.eden-emu.dev/eden-emu/eden/pulls/4566

## Local experiment
Amaral.6 enabled no-snapshot-reuse as intended (reused=0). User reports continued fire/smoke flicker. Measured summary27.03 real FPS, p9547.13ms, GPU22.38ms vs amaral.5 summary29.59FPS,p9539.89ms,GPU15.10ms. Routes and temperatures differ; do not attribute all differences to one change. Automatic no-reuse experiment removed from source. Existing explicit environment switch preserved. No new APK built by this review.

## Relevant Eden changes
4565 enables an existing texture-gather workaround for Qualcomm proprietary and MesaTurnip, to address subpixel rounding mismatches with Maxwell and black squares in TOTK. Our glsl_gather already snaps 2D coordinates to1/256texel and adds0.25 of that step, and rounds array shadow references. Same problem class, not a verified identical implementation. Do not add a second rounding correction blindly. The full affected flag and underlying Eden emitter still need comparison; some source/commit pages returned403, while PR diff was accessible.

4551 resolves conditional Phi inputs when tracing bindless texture handles. Demonstrated for Ender Magnolia bullets, not TOTK fire. Our handle_cbuf is a straight-line backward scan that does not model control-flow/predicated definitions. This is a concrete limitation to investigate with the affected shaders, not proof of the current defect. Porting the SSA pass directly is inappropriate for this translator.

4566 handles sprite textures obtained from vertex attributes, splitting draws into runs that share a handle. Demonstrated for FIFA markers/shadows/text, not a confirmed TOTK fix. Our generic TEX.B path explicitly fails bindless tex; need affected shader capture/translation failures before extending it.

Do not change Turnip yet: these examples fix shader translation/renderer behavior in Eden. Next diagnostic should identify hashes and inputs of the fire/smoke shaders, and trace actual predicates/handles and gather sampling. Preserve same signing identity for future APKs.
