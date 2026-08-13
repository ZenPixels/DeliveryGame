# Asset sourcing to-do

Things the game needs sourced or made. Engineering hooks for most of these already exist —
the notes say where each asset plugs in once it lands in the project.

## Audio — vehicle impacts (hooks LIVE, using placeholder sound today)

The jeep already has a three-tier impact system plus a landing detector; every tier currently
plays the same `MetaSound_Car_Crash` at different volumes. Each tier wants its own **pool of
3-5 short variants, chosen at random per hit** (author requirement: no two hits should sound
identical). MetaSounds can do the random-pool selection internally — build one MetaSound per
tier with a random node over its pool, keep the existing `Reset Sound` / `Stop Sound` triggers.

- [ ] **Small crunch pool** — short, light contact noises (bumper taps, ramp entries, curb
      hops). Trigger: impulse 250-800 tier + `LandingThump` event on the jeep.
- [ ] **Medium/large crash pool** — real collisions; impulse tier > 800 scales volume 0.35-1.0
      up to impulse ~3000. Bigger variants can gate on higher impulse later.
- [ ] **Landing thump** — could share the small-crunch pool, or be its own softer suspension
      "whump". Volume already scales with fall speed (0.15-0.55).

## Audio — driving

- [ ] **Skid/drift loop** — looping tire squeal. Trigger already exists: start on
      `DriftGripOn`, stop on `DriftGripOff` (jeep Blueprint events).
- [ ] **Car horn pool** — 2-3 honks. Hook LIVE: `ADGAIVehiclePawn::HornSound` plays on
      near-miss dodge (null-safe, silent until assigned). Different horns per vehicle =
      personality flavor for free.
- [ ] **Surface rolling ambience** (later, pairs with surface-resistance work) — road hum vs
      grass rumble.
- [ ] Crash sounds for **AI vehicles** can share the same pools (their audio is positional
      now; volume-scaling for AI needs a small C++ pass, flagged).

## Vehicle damage (backlog feature, author 2026-08-11 — needs art before code)

Author wants **player and NPC vehicle damage** as a real system. Sketch from the conversation:
windows breaking, doors falling off, mirrors snapping, tires flying off, **a few levels of
damage**, and — for the worst crashes only — **fire and explosion with all four tires shooting
out**. Note the deliberate irony: "all four wheels come off" was a *bug* at ordinary impact
(fixed 2026-08-11 by simulating chassis only); it becomes a *reward* reserved for spectacular
crashes. Already shipped as the first step: single-wheel detach near a hard impact
(`bAllowWheelDetach` / `WheelDetachImpulse` / `WheelDetachRadius` on ADGAIVehiclePawn), which
also forces the Wrecked state.

- [ ] **Damage-state meshes or swappable parts** per vehicle (clean → dented → wrecked), or
      breakable sub-meshes for doors/mirrors/windows.
- [ ] **Glass break VFX/SFX**, debris particles.
- [ ] **Fire + explosion VFX/SFX** for catastrophic crashes.
- [ ] Player jeep needs the same treatment (its damage state is also the visible cost of the
      insurance economy — see game-vision).

## Sky / day-night (mostly programmatic — this is the whole asset list)

`SkyAtmosphere` computes the sky from the sun's angle, so sunrise/sunset/dusk/darkness all come
free from rotating the directional light. What it does **not** provide:

- [ ] **Starfield** — cubemap or panoramic texture for the night sky. The one genuine asset need.
- [ ] **A better moon** (author, 2026-08-13: "need a new moon"). The atmosphere currently draws a
      plain bright disc via the moon light's `LightSourceAngle` +
      `AtmosphereSunDiskColorScale`. A real moon wants a texture — cratered, ideally with a
      subtle halo. Knobs live on `UDGTimeOfDaySubsystem`: `MoonDiscAngleDegrees`,
      `MoonDiscBrightness`, `MoonColor`, `MoonIntensity`.
- [ ] Optional: night ambience audio bed, night grading LUT.
- [ ] **Emissive lamp/window materials** for the night-lighting work (streetlamps, shop signage,
      building windows) — see BACKLOG "Night lighting".

Notes for the build: delete/hide the legacy `SM_SkySphere` (it occludes `SkyAtmosphere` and does
not follow a moving sun); set `SkyLight` to **Real Time Capture**; the directional light must be
**Movable**, which means abandoning the baked lighting in `Island_BuiltData`.

## World / props

- [ ] **Placeholder buildings** for the five delivery destinations (author sourcing own
      assets) — replace the floating TextRenderActor signs in the `Destinations` folder.
- [ ] **SM_box-style package prop** — the carried delivery item for the interact-to-pickup
      upgrade (SM_Prop_CarboardBox_03 exists in /Game/Game/Meshes/Props/StreetProps as a stopgap).
- [ ] **Stop sign props** — needed before the 4-way-stop right-of-way rules can exist.
- [ ] **Destructible props** — meshes with break states (or Chaos geometry collections):
      mailboxes, trash cans, fences are the classics. Impact hooks exist on vehicles.
- [ ] **Skid mark decal texture(s)** — for drift marks on the ground (drift state is the
      trigger; needs a decal spawner when built).

## Notes

- The impulse debug print on the jeep (on-screen number per hit) is the calibration tool for
  choosing which sounds go with which impulse ranges.
- Content/Staging is excluded from production builds — anything adopted from a pack there must
  be **moved** into /Game/Game (see memory: content-staging-convention).
