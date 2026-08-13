---
name: traffic-system-goals
description: "DeliveryGame traffic AI goals — rule-following randomised traffic, scaling to a large city map later"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-09T23:42:37.034Z
---

Stated by the author on 2026-08-04, while debugging the native traffic port:

> "We don't have to completely reproduce my original system. The point is to create traffic that
> follows normal traffic rules and behaves a bit randomly for now, making random choices at an
> intersection."

And on scope:

> "This is a very small test map. Eventually we need to build a large city map, but that's after we
> understand and fix the systems."

`Island` has only 3 road splines, 3 intersection deciders, 4 vehicles and 4 traffic lights — it is a
test bed, not the target scale.

**Why:** correctness of the *rules* beats fidelity to the old Blueprints, so a cleaner design that
behaves properly is preferred over a faithful port. Do not spend effort reproducing original quirks.

**Keep the player's viewpoint in mind (author, 2026-08-11):** "a player will be unlikely to sit
there and watch random intersections — they could, but they would lose the game as we've defined
it." Traffic only has to hold up **at delivery speed, from a moving jeep**. Bugs visible from the
driver's seat (vehicles in your lane, wrecks blocking a route, standoffs you get stuck behind)
matter; imperfections only visible while parked and staring at a junction do not. A useful test
protocol: judge traffic while driving a delivery, not while spectating.

**Traffic is kinematic, not physics** (author decision 2026-08-09, "hybrid: kinematic until hit").
AI vehicles move via `UDGPathFollowComponent::MoveMode = Kinematic` — integrated speed, capped yaw
rate toward the goal, transform set directly; Chaos stays for the player. Rationale: every
late-stage traffic bug was Chaos fighting the logic (failed launches from manual gearboxes, kerb
understeer, brake authority), the author wants arcade feel and had already considered dropping
Chaos, and the decision layer was proven sound by the event log. **Physics-on-player-impact is the
agreed later layer** — a hit AI vehicle should switch to physics. The player car's own feel ("not
fun to drive") is a separate future tuning/replacement effort.

**Vehicles are never teleported** (2026-08-09). A stuck vehicle parks and retries after a delay;
the aim point already sits on its lane, so anything that can move is always steering back toward its
goal. A `SnapToLane` teleport recovery was built and removed on the author's direction. The real fix
is **vehicle reverse logic**, planned but unbuilt — until then, stuck means stopped.

Speed is **data on the road, not a constant in code**: `ADGPathActor::SpeedLimitMPH` per spline, with
`UDGPathFollowComponent::SpeedLimitCompliance` as a per-vehicle multiplier. The author expects NPCs
that deliberately exceed limits eventually (speeders, pursuit), so limits must stay a scalable target
rather than a hard cap — corner slowdown still applies on top of compliance.

**Standing behaviour rules** (author, 2026-08-09):
- **No U-turns.** Vehicles must not turn around and go back the way they came — not as a junction
  choice, not as a continuation. The one exception: off-road recovery finding its way back to a
  spline, and a genuine dead end where the only continuation is back.
- **A vehicle never stops unless something explicitly stops it** (signal, traffic ahead, manual
  hold). Unexplained immobility — including driving off the road — means re-acquire the nearest
  spline, reset the goal, keep driving. No permanent parking, no teleporting.

**BUILT 2026-08-10, PIE-VALIDATED same day** (author: "arcs and turns looking good... no
collisions or weird behavior"): generated **junction turn arcs** (bezier from lane
through corner apex into the new road's lane; `bInTurnArc` commits the vehicle — signals/yields
can't stop it mid-junction; `TurnArcSpeedMPH` 9; magenta debug draw) and the **right-of-way
system** below (`UpdateYieldAwareness` on the pawn, pairwise over the subsystem's new vehicle
registry, `YieldStopDistance` as its own braking channel — the hold-refcount fix became
unnecessary; `YIELD`/`ARC` log lines for diagnosis; deadlock escape with 4s re-latch cooldown).

**Author's next focus list (2026-08-10, in their priority order, after traffic dial-in):**
1. ~~**Player jeep feel**~~ — DONE 2026-08-10 (chassis tuning + scripted drift + camera; values on the
   movement component, wheel BPs, and the jeep BP's drift/gravity events).
2. ~~**Collision/near-miss reactions**~~ — BUILT 2026-08-11 (awaiting PIE validation). The
   physics-on-impact layer: hit impulse ≥ `PhysicsImpactThreshold` (250k, tune from the STRUCK log
   line) → `EDGImpactState` machine on ADGAIVehiclePawn: Simulating (full physics + arcade shove
   from striker velocity) → settle → upright? → **Shaken** (`PostCrashPauseSeconds` — the
   personality knob, set per instance; default 3s everywhere until instance values are set) →
   Driving (UpdateDestination re-derives everything); not upright → **Wrecked** (permanent until a
   tow/despawn system). Component gains `bSuspendedForPhysics` (kinematic must not fight the sim)
   and `DodgeOffset` (self-relaxing lateral channel). **Near-miss dodge**: fast-closing player
   within 14 m → swerve ~1.9 m away from the player's side + `HornSound` hook (null until an asset
   is sourced). `OnStruck` delegate = the future fines/insurance hook. Deferred by design:
   pull-over-to-curb and get-out-and-talk (they belong to the NPC/dialog era — that's the mailman
   confrontation scene); AI-vs-AI impacts (kinematic pairs generate no hit events); corrupted
   reactions (corruption era: a van that doesn't dodge, or pulls over and just sits there).
3. **Randomized driver behavior** — the personality presets over existing knobs (below).
4. **Audio tuning** — crash sounds and general car noise (CrashAudio/MetaSound plumbing exists).
5. **Wheel rotation on kinematic vehicles** (author, 2026-08-10): Chaos used to animate the wheel
   bones; kinematic movement leaves them frozen. We must spin them ourselves — likely an anim
   instance fed from `KinematicSpeed` (angle rate = speed / wheel radius) plus steering angle on
   the front wheels from `GetSteeringInput()`. Body lean can ride along in the same layer.

**Additional flags (author, 2026-08-10, during jeep-feel tuning):**
6. **Skid/drifting sounds** — the drift state (DriftGripOn/Off events on the jeep BP) is the
   natural trigger; pairs with the audio-tuning item above.
7. **Crash sounds fire too easily and too loudly** for small jumps/impacts — threshold and volume
   curve live in `ADGAIVehiclePawn` crash audio config and the jeep's OnComponentHit (BP);
   `CrashImpulseThreshold` 100000 is evidently too low for landings.
8. **Destructible props** — some props should break/explode on impact.
9. **Stop signs** — govern traffic at some junctions; the right-of-way spec above already defines
   4-way-stop behavior (everyone stops, FIFO, right-hand tiebreak) gated on sign props existing.
10. **Surface-dependent resistance** — road vs grass acceleration/grip (physical materials per
    surface; also affects AI if they ever route off-road).
11. **Drift/skid marks** — decals left by the player during drift state (same trigger as #6).
12. **Drivers in AI vehicles** (author, 2026-08-11): the vans/bus are currently empty — they need a
    **socket on each vehicle mesh for a seated driver character**, and eventually drivers who
    **get out after an accident** (pairs with the impact-reaction states: Shaken/Wrecked are the
    natural triggers). The jeep already does this — `BP_Vehicle_Jeep` has a `Driver` mesh
    component + `DriverMesh` variable, set and shown on EventPossessed — so mirror that pattern.
13. **Steering wheel animation** (author, 2026-08-11): attach the driver character's hands/model
    to the steering wheel and **turn the wheel as the vehicle turns**. Same era as the kinematic
    wheel-spin work (#5) — both are "vehicles look alive" anim-layer tasks.
14. **Camera pitch clamp** (author, 2026-08-10): the player can rotate the camera under the world —
    a direct consequence of disabling the spring arm's collision probe for jump feel. Fix: custom
    PlayerCameraManager with ViewPitchMin ~ -25° / ViewPitchMax ~ +45° (author is even open to a
    fully height-locked cam with limited yaw), assigned via a BP PlayerController on the GameMode.
    Natural home for future camera-feel work (speed FOV kick, smoothed probe return).

**Right-of-way rules (author, 2026-08-10 — the spec for the yield system when built):**
- Turning traffic yields to straight-through traffic.
- A left-turner yields to oncoming traffic (straight or right-turning).
- At an uncontrolled 3-way T, the **terminating road yields to the through road** (through
  traffic shouldn't even slow).
- Uncontrolled 4-ways barely exist in reality; ours is signalized. 4-way STOPS exist only with
  sign props (everyone stops, first-in-first-out, right-hand tiebreak) — future, with signs.
- Agreed implementation shape: a per-junction **reservation/grant loop on the decider actor**
  (it already knows its connected roads and has the volume); intent (straight/left/right)
  classified from `PlannedNextPath`, which is known 2500 cm early; holds via the signal-hold
  channel — which first needs the **hold refcount fix** (single bool today); and a mandatory
  deadlock escape so four polite vans can't wave each other through forever.

**Traffic population: spawn/despawn (author, 2026-08-11 — "another thing we'll need to add"):**
The island currently runs **4 hand-placed vehicles**; a real city needs vehicles created and
destroyed around the player. Author's reference: **GTA 3, which swapped traffic when you moved the
camera** — so the rule is *never spawn or despawn in view*, camera-facing **plus proximity**.
Design notes for when it's built:
- `UDGTrafficSubsystem` is the natural home — it already owns the **path registry** (spawn points:
  pick a path, a distance along it, the lane offset, the travel direction) and the **vehicle
  registry** (population count, spacing checks).
- Spawn candidates: on-path, outside the view frustum or beyond a radius, with clearance from
  registered vehicles; despawn: behind the player, out of view, past a radius. Hysteresis between
  the two radii or vehicles will pop in and out at the boundary.
- **Personalities become presets at this point** (see below) — a spawner needs to roll a
  personality, not copy instance values.
- **This is also the wreck cleanup**: a wrecked vehicle nobody can see can simply be removed,
  which is cheaper than the tow-truck idea (keep the tow truck as flavour, not as the mechanism).
- Same system eventually spawns **pedestrians** (author has a prototype random pedestrian
  generator with known bugs — see [[game-vision]]).
- Watch the O(N²) right-of-way check when the population grows: it was fine for 4 vehicles, and
  wants a spatial bucket before a city's worth.

**Future traffic work the author has named** (do not build unprompted; design so they stay easy):
- **Point-to-point routing**: vehicles planning a route between two locations over the spline graph
  and following it, replacing pure random choice at junctions.
- **Driver personalities**: slow/over-safe drivers, reckless/fast drivers, etc. The tuning surface
  already exists per vehicle — `SpeedLimitCompliance`, `FollowHeadwaySeconds`,
  `ComfortableDeceleration`, `KinematicAcceleration/Braking/YawRate` — a personality is a named
  preset over those knobs. **Prototyped 2026-08-10 as per-instance values** on the 4 placed
  vehicles (commuter/cautious/speeder/bus — see instance PathFollow components). When traffic
  spawning arrives, formalize as named presets (data assets or C++ structs) instead of instance
  edits. Observed gap: no overtaking — a speeder queues politely behind the bus forever.

**How to apply:** favour designs that survive a city-sized map — the `UDGTrafficSubsystem` path
registry over per-vehicle actor searches, throttled per-vehicle updates
(`DestinationUpdateInterval`), and opt-in debug drawing. Before scaling up, flag anything O(vehicles
× paths) per frame. Also worth revisiting at that point: the author is open to replacing centreline
splines with **one-way lane splines**, which removes both the lateral-offset hack and travel-direction
inference — see [[deliverygame-mcp-toolsets]] and `docs/CPP_MIGRATION.md`.
