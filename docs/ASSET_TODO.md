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
- [ ] **Surface rolling ambience** (later, pairs with surface-resistance work) — road hum vs
      grass rumble.
- [ ] Crash sounds for **AI vehicles** can share the same pools (their audio is positional
      now; volume-scaling for AI needs a small C++ pass, flagged).

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
