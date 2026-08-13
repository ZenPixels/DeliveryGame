# Backlog (prioritized)

Consolidated 2026-08-12 from `memory/game-vision.md`, `memory/traffic-system-goals.md`,
`memory/road-network-plans.md`, `ASSET_TODO.md` and `CPP_MIGRATION.md`. Those files remain the
detailed design record; this file is the running order.

Ordering principle: **the delivery loop is the game.** Traffic, the jeep and the map are now good
enough to serve it — further polish there is optional, while the loop itself is still a
single-job prototype. Build the loop's data model before the UI that displays it, because the UI
is expensive to redo.

---

## DONE (for reference)

Traffic: kinematic movement, path following, signals, following distance, junction planning,
generated turn arcs, right-of-way rules, impact reactions + wreck states, near-miss dodge,
interim wreck cleanup, per-vehicle personalities (as instance values), 13 vehicles incl. the
city bus. Map: closed 8-junction road network, 5 named destination points, Staging dependencies
migrated. Jeep: chassis/gearing/steering tuning, scripted drift, jump feel, camera lag + pitch
clamp, tiered positional crash audio. Delivery: single-job subsystem (offer→pickup→timed
drive→payout), phone app showing live job state, green arrow objective marker.

---

## TIER 1 — Finish the core loop (highest value, mostly C++)

1. ~~**Multi-job / offer state model**~~ — **BUILT 2026-08-12, awaiting PIE validation.**
   `UDGDeliverySubsystem` now holds a single `Jobs` array where `EDGJobStage` distinguishes
   offers from held work: offer generation + **expiry**, **capacity** (`JobCapacity`, the
   roof-rack upgrade hook), **decaying payout** instead of failure (`ComputePayout` slides full
   value → `FloorDollars` over `DecaySeconds`), **queue blocking while any held job is overdue**
   (`IsOfferQueueBlocked`), and **VIP jobs** (`OfferVipJob`) that are untimed by default, never
   lapse, and are taken alone in both directions. Multiple objective markers light at once.
   Events: `OnOffersChanged/JobAccepted/JobPickedUp/JobDelivered/JobLost/JobDecayStarted`.
   A primary-job compatibility view (`GetState`/`GetActiveJob`/`GetTimeRemaining`/
   `StartRandomJob`) is retained so the existing phone widget keeps working untouched.
   **Still to do here:** passenger jobs are a data-model slot only (`EDGJobKind::Passenger`) —
   no passenger pawn or ride behaviour yet.
2. ~~**Distance/difficulty pricing**~~ — **DONE with the above**: fare is now
   `BaseFareDollars + FarePer100m × (route/100 m)`, replacing the per-km rate that rounded to
   zero on this island. Difficulty (beyond distance) is still unmodelled.
3. **Interact-based pickup/dropoff** — ✅ **WORKING 2026-08-12** (C++ + character wiring done).
   `IA_Action` (E) calls `TryInteract`; package interaction wins over entering the jeep, so
   **deliveries happen on foot** (author's rule). A parcel prop (`ADGDeliveryPointActor::ParcelMesh`)
   appears at pickups only, resting on the floor beneath the point, using **per-job meshes**
   (`FDGDeliveryJob::ParcelMesh` → point's `DefaultParcelMesh` → cardboard box), settable via
   `OfferVipJob`, `SetJobParcelMesh`, or the `GeneratedParcelMeshes` random pool.
   **Debugging history worth remembering** (all in memory: unreal-property-edits-shadowed):
   the legacy Blueprint delivery system in the character was completing deliveries on proximity
   AND destroying delivery points; MCP-added Blueprint components are half-real; and the actual
   invisible-box cause was **`bHidden` (Actor Hidden In Game) saved on 4 of 5 points**, which
   hides every component regardless of component visibility.
   **Remaining, content-side:** interact prompt widget (`OnPlayerInRangeChanged` +
   `GetInteractPrompt`), carried-parcel visual (`GetCarriedCount`), person recipients.
   *(Old note: C++ BUILT 2026-08-12; needs Blueprint wiring.)*
   Arriving no longer completes anything: points track `bPlayerInRange`, and
   `UDGDeliverySubsystem::TryInteract()` performs whatever the nearest in-range point offers
   (deliveries outrank collections). `GetInteractPrompt()` supplies prompt text,
   `GetCarriedCount()` supports showing parcels on the player/jeep, and
   `OnPlayerInRangeChanged(bool)` fires on the point for prompt visuals.
   `bRequireInteractToComplete` (default true) can be flipped to restore arrival-completes for
   testing.
   **Remaining, all content-side:** call `TryInteract` from `IA_Action` (already mapped to **E**)
   in the character and jeep Blueprints; a prompt widget driven by `OnPlayerInRangeChanged` +
   `GetInteractPrompt`; a carried-parcel mesh (`SM_box`/`SM_Prop_CarboardBox_03`) shown from
   `GetCarriedCount`. Person recipients come with the passenger work.
4. **Day structure** — 🟡 **clock + sky BUILT 2026-08-13; the gameplay day still needs hooking up.**
   `UDGTimeOfDaySubsystem` is the single authority for time: `DayLengthMinutes` (12), `StartHour`
   (8), `LateNightHour` (22), `CurrentHour`, `DayNumber`, `GetTimeText`, `IsNight`,
   `IsLateNight`, `SetHour`, `EndDay`, and events `OnDayStarted/OnDayEnded/OnSunrise/OnNightfall/
   OnLateNight`. It adopts the level's existing atmosphere sun (keeping its authored intensity as
   full daylight), forces it Movable, sweeps its pitch a full turn per day, fades it across the
   horizon, and spawns a transient moon light registered as the atmosphere's *second* sun.
   Console: **`dg.SetHour 20`** jumps straight to any hour.
   The legacy `SM_SkySphere` (`M_SimpleSkyDome`) is **hidden** — it occluded SkyAtmosphere and
   ignored the sun; it now sits in the `Lighting/Legacy` folder if the old look is ever wanted.
   **Remaining:** return-home-to-end-the-day (call `EndDay`), the late-night penalty, wiring the
   phone's clock to `GetTimeText`, and deciding how the delivery day interacts with offers
   (do they stop at night?). Stars/moon disc are the only art needed — see `ASSET_TODO.md`.
5. **Passenger deliveries (Uber app)** — the lore-delivery vehicle. Needs at minimum a passenger
   pawn that rides along; full value comes with dialog (Tier 4), so a silent version is a valid
   first step. Driving quality affects the tip (hook: `OnStruck`).

## TIER 2 — The phone (after the data model is stable)

6. **3D phone object**, lower-left, d-pad app switching. Apps: navigation/mini-map (likely merged
   with delivery), **opportunity board** (offers with visibly decaying timers), delivery log,
   insurance, banking/balance, time, music/podcasts.
7. **Notifications with personality** — toasts, alert sounds, the insurance "woop" on collisions.
   Deliberately *invaluable, annoying and distracting* (author). Corruption layer later turns
   this against the player.
8. **Delivery log** — scrollable history; later the horror payoff when the player finds THE
   package in their own paperwork.

## TIER 3 — World feel (parallel, several asset-blocked)

9. **Drivers in AI vehicles** — seated driver mesh + socket; eventually they get out after a
   crash (hooks exist: Shaken/Wrecked states).
10. **Wheel rotation + steering wheel animation** for kinematic vehicles (wheel spin from
    `KinematicSpeed`, steer angle from `GetSteeringInput`); body lean in the same anim layer.
11. **Audio pools** — small-crunch / big-crash / landing / skid loop / horn, each a random pool so
    hits don't repeat. *Blocked on sourcing — see `ASSET_TODO.md`.*
12. **Skid marks** (decals during drift state) and **surface resistance** (road vs grass; pairs
    with rain).
13. **Traffic spawn/despawn population system** — GTA-3 style, out-of-view + proximity, with
    hysteresis; subsumes wreck cleanup and formalises personalities as presets. Needed before any
    city-scale map. Also the eventual home for **pedestrians**.
14. **Destructible props and vehicle damage** — dents/parts/glass, fire and explosion reserved
    for the worst crashes. *Art-blocked.*
15. **Stop signs** — the sign props plus the 4-way-stop right-of-way rules already specced.
16. ~~**Day/night visuals**~~ — ✅ **WORKING 2026-08-13.** Sun arc, horizon fade, moonlight and
    visible sun/moon discs, all driven by `UDGTimeOfDaySubsystem`. Still wanted: a **better moon**
    (author — the disc works but needs a proper look; see `ASSET_TODO.md`) and **weather**
    (rain levels affecting traction; "monster weather" later).
17. **Night lighting** (author, 2026-08-13 — "if I want things to happen at night I'll need
    headlights, streetlights and other lights that switch on when night comes"). The switching
    hooks already exist: `OnNightfall` / `OnSunrise` events and `IsNight()` on the time
    subsystem. Needed:
    - **Vehicle headlights** — player jeep and AI traffic (spot lights + emissive lamp material).
      AI vehicles already have a natural owner in `ADGAIVehiclePawn`.
    - **Streetlights** — the `SM_Prop_Streetlamp_02` props are placed but unlit; they need light
      components and an emissive pass.
    - **Lit windows** on buildings — usually an emissive material swap rather than real lights.
    - **Shop/business signage** lighting up, which the corruption layer later abuses.
    Perf note: every added dynamic light interacts with the VSM cost below.

## TIER 4 — Story systems (need writing + dialog tech)

17. **Dialog system** — the `DialogueSx` plugin is present but disabled; evaluate vs. building a
    small 2D portrait dialog system. Blocks everything below.
18. **Passenger and NPC conversations** — the actual writing. Every character is a content task.
19. **VIP delivery chain** — the story spine: the package that unleashes the evil, the collar,
    the Mailman escalation.
20. **Corruption layer** — billboard/app/asset variants swapped by a global corruption level;
    later the *systems* corruption (GPS lies, a car that follows you, wrong signal timing).
21. **Economy** — rent, fines/insurance as real costs, the store, jeep upgrades (see
    `DESIGN_LISTS.md`), earned decals.

## DEFERRED / OPEN QUESTIONS

- **Is the game losable?** Hard restart vs. narrative failure (eviction, poorer, story
  continues). Leaning narrative. See `memory/game-vision.md`.
- **Story mode / accessibility** — cheap if difficulty stays a set of multipliers (timer
  generosity, decay rate, fine scale, "timers never fail"); expensive to retrofit. Decide the
  *shape* early even if built late.
- **Point-to-point AI routing** and **one-way lane splines** — only needed at city scale.
- **Junction connector arc polish** for long vehicles (the bus tail cuts corners).
- **Tow truck** as flavour once despawn handles cleanup mechanically.
- **Player vehicle damage model** and its link to insurance costs.
