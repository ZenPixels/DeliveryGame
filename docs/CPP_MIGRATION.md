# C++ migration — traffic system

Status as of 2026-08-04. Strategy: **reparent Blueprints in place** onto native bases, so assets,
designer-tuned values, and map references survive.

## What exists

`Source/DeliveryGame/` — module scaffolding (`Build.cs`, both `Target.cs`, module impl with a
`LogDeliveryGame` category) plus five native traffic classes:

| Native class | Replaces | Notes |
| --- | --- | --- |
| `UDGTrafficSubsystem` | — (new) | `UWorldSubsystem` path registry |
| `ADGPathActor` | `BP_Path` | Spline route, `NextPaths` links, `ThrottleOverride` |
| `UDGPathFollowComponent` | `BP_Path_Follow` | The steering/throttle brain |
| `ADGAIVehiclePawn` | `BP_AI_Vehicle_Base` | `AWheeledVehiclePawn` child; volumes + crash audio |
| `ADGPathDeciderActor` | `BP_Path_Decider` | Junction routing, round-robin or random |

UHT has generated reflection code for all five. **The C++ bodies have not yet been compiled** —
that needs an editor-closed build (see `CLAUDE.md`).

## Road topology in Island — read this first

Established 2026-08-04 by inspecting the level via `SceneTools.find_actors` and a top-down
`CaptureViewport`, after three PIE failures caused by assuming otherwise.

`Island` contains only **3 `BP_Path` splines, 3 deciders and 4 vehicles**
(`BP_Path_C_1/2/3`, `BP_AI_Van_C_1/2/3`, `BP_AI_School_Bus_C_3`).

- **Splines run along road *centre lines*** — the spline handles sit exactly on the dashed painted
  divider, not along a lane.
- **Roads are two-way**, one lane either side of that divider.
- Therefore **one spline carries traffic in both directions**. Three splines cover the whole network.

Two consequences that dominate the design:

1. **Vehicles must be laterally offset from the spline to sit in a lane.** `LateralOffset` on the
   path-follow template is 254.89 cm, recovered from the old `Route Collider` marker. It must flip
   with travel direction or a reversed vehicle is pushed into oncoming traffic.
2. **A path-following controller here must be direction-aware.** The Blueprint's cross-track
   controller only ever pushed the vehicle *towards* the spline, so which way it travelled fell out
   of whichever way it happened to be pointing — direction-agnostic for free. **Pure pursuit does not
   have that property**: aiming at `DistanceAlongSpline + ForwardAimDistance` always advances along
   the spline, so a vehicle needing to travel the other way U-turns and drives the road backwards.
   Hence `TravelDirection` (+1/-1), decided from the vehicle's heading whenever a route is assigned.

**Never filter candidate routes on spline alignment.** An early attempt rejected any route running
against the vehicle's heading, which rejected roughly half of all legitimate routes and made
behaviour worse. Use `IsPathUsable` (alignment *or* `bAllowReverseTravel`), never `IsPathAligned`
alone, when choosing a route.

## The original algorithm

Recovered from the live Blueprint via `BlueprintTools.read_graph_dsl`, not inferred. `BP_Path_Follow`
is a **cross-track** controller:

```
probe       = <box component tagged "routing">.WorldLocation + Owner.Forward * ForwardAimDistance
Destination = Spline.FindLocationClosestToWorldLocation(probe, World)

magnitude   = Clamp(|probe - Destination| / 100, 0..1) ^ 2
direction   = MapRangeClamped(DeltaRotator(ActorRotation, LookAt(probe -> Destination)),
                              in 120..-120 -> out -1..1)
Steering    = magnitude * direction
Throttle    = Clamp((1 - Abs(Steering) * 0.5) * MaxThrottle, 0.1, MaxThrottle)
```

`EventTick` runs `GetCurrentDistanceOnSpline` → `UpdateDestination` → `ProceedToDestination` every
frame, unthrottled. `EventBeginPlay` does `GetAllActorsOfClass(BP_Path_C)` + `FindNearestActor` once.
`GetNextTargetSpline(Target Splines)` removes the current spline from the passed array and picks a
**random** remaining one. There is no brake input anywhere.

Notes that matter:

- `Time Since Last Update` is **vestigial** — `ProceedToDestination` assigns it `Delta Time` and
  nothing ever reads it. There was no hand-rolled accumulator.
- Steering depends on a box component **tagged `"routing"`**, found via `GetComponentsByTag`. Any
  native port keeping the probe approach must reproduce that tag.
- `UpdateDebug` lives on `BP_AI_Vehicle_Base` and is called *upward* from the component.

## Divergence in the current native code

`UDGPathFollowComponent` as written is a **pure-pursuit** controller, not a cross-track one: it aims
at a point `DistanceAlongSpline + ForwardAimDistance` along the spline and steers on yaw error from
the vehicle to that point. Pure pursuit generally corners better — cross-track steering falls to
zero whenever the probe happens to sit on the spline, which under-steers through curves — but **it
will not feel identical**, and "reparent in place" was chosen to preserve tuning. Decide before
reparenting whether to match the original formula or keep this one.

Other deliberate changes:

1. **Progress is re-derived, not integrated.** `UpdateDestination` recomputes
   `DistanceAlongSpline` from the vehicle's actual position via
   `FindInputKeyClosestToWorldLocation`. Being shunted off the route self-corrects instead of
   accumulating error.
2. **Path lookup is registry-based.** Replaces `GetAllActorsOfClass` + `FindNearestActor` at
   BeginPlay with `UDGTrafficSubsystem`, measuring to the closest point *on each spline* rather than
   to the path actor's origin — so a long spline whose origin is far away still wins if the vehicle
   sits on it. (The original cost was once per vehicle at BeginPlay, not per frame.)
3. **Blocking is reference-counted and recomputed.** `TrafficCollider` and `StopZone` overlaps
   rebuild a `TSet` of blocking actors on every overlap change. One actor can occupy both volumes,
   and each reports end-overlap independently — incremental add/remove would either double-count on
   entry or release early on exit. Manual holds (`AddBlocker`/`RemoveBlocker`) are counted
   separately for non-overlap sources.
4. **Braking exists.** The original only ever lowered throttle, with a 0.1 floor, so vehicles never
   truly stopped. `StoppingBrakeForce` is applied when blocked or stopped.
5. **Cruise shaping.** Throttle tapers over the last 20% of the approach to `CruiseSpeedMPH`. The
   original had no speed target at all.
6. **Round-robin junctions.** `ADGPathDeciderActor` defaults to `RoundRobin`; the original picked
   uniformly at random via `GetNextTargetSpline`.
7. **Debug text is built in.** `GetDebugStatus()` + `DrawDebugString` replace the `BP_Debug_Text`
   actor plumbing and the upward `UpdateDebug` call, both of which can be retired.
8. **Destination updates are throttled** by `DestinationUpdateInterval`; steering and throttle still
   apply every frame. The original re-evaluated every frame.

## Captured designer values

Read from the live assets via `ObjectTools.get_properties`, 2026-08-04. **The tuned values live on
the Simple Construction Script component templates, not the Blueprint CDO** — the CDO of
`BP_Path_Follow` reports misleading defaults. Always read
`<Blueprint>_C:<ComponentName>_GEN_VARIABLE`.

`BP_Path_Follow` CDO (misleading — do not use):
`Forward Aim Distance 0`, `Max Throttle 0.3`, `Draw Debug false`, `Target Spline None`

`BP_AI_Vehicle_Base_C:BP_Path_Follow_GEN_VARIABLE` (**authoritative**):

| Variable | Value |
| --- | --- |
| Forward Aim Distance | 100 |
| Max Throttle | 0.4 |
| Auto Find Spline | true |
| Draw Debug | true |
| Stop Zone | false |

`BP_AI_Vehicle_Base_C:Route Collider_GEN_VARIABLE`:
`boxExtent (32,32,32)`, `relativeLocation (415.47, -254.89, 38.79)`, `componentTags ["routing"]`,
**`bGenerateOverlapEvents false`**

`BP_AI_Vehicle_Base_C:Traffic Collider_GEN_VARIABLE`:
`boxExtent (32,32,32)`, `relativeLocation (0, 0, 100.82)`, no tags, `bGenerateOverlapEvents true`

### What those numbers reveal

- **`Route Collider` is not a collider.** Overlaps are off; it exists purely as a transform marker
  tagged `"routing"` for the steering probe origin.
- **Its lateral offset is ~255 cm** (`Y = -254.89`). Under cross-track steering that biases the
  probe sideways, so vehicles held the spline off to one side rather than centred on it. Pure
  pursuit aims at the spline itself, so traffic will now drive **centred**. If the offset lane
  position was wanted, that needs an explicit lateral-offset knob — it is not a bug in the port.
- **`Traffic Collider` is broken as authored** — a 32 cm box 100 cm *above* the vehicle centre with
  no forward offset. It can essentially never detect a vehicle ahead, which is why traffic never
  yielded. The native version places a real forward volume, so blocking will start working for the
  first time.
- **`Forward Aim Distance = 100` does not transfer between controllers.** In the original it was a
  probe offset in a cross-track formula. As a pure-pursuit look-ahead, 100 cm is roughly one car
  length and will oscillate; pure pursuit wants several metres. The native default stays at 600 —
  deliberately *not* the captured value. Re-tune from there.
- `BP_AI_Van` and `BP_AI_School_Bus` hold no template overrides of their own; they inherit the
  parent's values.

Not yet checked: per-instance overrides on actors placed in `Island`/`Test`. Those live in
`__ExternalActors__` and will **not** carry across for any property whose name changed.

## Reparenting: done

Completed 2026-08-04 via `BlueprintTools.set_parent`, with graphs stripped and each asset saved.
All six compile clean and are non-dirty on disk.

| Blueprint | Parent | Stripped |
| --- | --- | --- |
| `BP_Path` | `ADGPathActor` | — (construction script left; it only sets spline draw-debug) |
| `BP_Path_Follow` | `UDGPathFollowComponent` | 8 function graphs, 28 event nodes, 11 variables |
| `BP_Path_Decider` | `ADGPathDeciderActor` | 11 event nodes |
| `BP_AI_Vehicle_Base` | `ADGAIVehiclePawn` | 30 event nodes, `Update Debug`, 2 variables |
| `BP_AI_Van`, `BP_AI_School_Bus` | `BP_AI_Vehicle_Base_C` | inherited, untouched |

### Rules learned the hard way

1. **A native parent must not create components the Blueprint already owns.** `ADGPathActor`
   originally made its own spline root; reparenting then displaced `BP_Path`'s `Spline Path`, broke
   its component binding, and left the construction script reading None. All four native classes now
   **resolve** their components (`ResolveRouteSpline`, `ResolveComponents`, `DecisionBox` in
   `BeginPlay`) rather than creating them. This also avoids two path-follow components fighting over
   the throttle.
2. **Name collisions are display-name based.** `SplinePath` collided with `Spline Path`. Native
   properties were renamed to `RouteSpline` / `TargetSpline` to stay clear of the Blueprint's own
   names. Only an *exact* FName match is absorbed — `Destination` was, while `Max Throttle` vs
   `MaxThrottle` was not, so it survived as a dead duplicate. **Tuned values therefore do not
   transfer**; they must be re-entered as C++ defaults (done: `MaxThrottle 0.4`, `bDrawDebug true`).
3. **Live Coding is not a foundation to reparent onto** — see the memory note. Patches vanish on
   editor restart, leaving assets bound to stale class layouts.
4. **MCP Blueprint edits are not auto-saved.** `set_parent` persisted; `delete_node`,
   `remove_function_graph` and `remove_variable` did not, and a crash lost them. Call
   `AssetTools.save_assets` after every mutation.
5. **Strip callers before callees.** Deleting `BP_Path_Follow`'s `Get Next Target Spline` broke
   `BP_Path_Decider`, which called it — the error only surfaced when the decider was recompiled. Run
   `AssetTools.get_referencers` on anything before deleting parts of it; skipping that also broke
   `BP_Prop_Traffic_Light_Sm`, which called the removed `Start Moving` / `Stop Moving`.
6. **Setting a component-template value does not reach placed instances, and verifying on the
   template will not reveal it.** Child vehicle Blueprints and placed actors carry archetype
   overrides. Saving the level after changing a template serialises whatever the instances still hold
   in memory as deltas, silently pinning the old value — `LateralOffset` stayed 0 through two test
   rounds while the template read 254.89, so traffic drove down the centre line and *correctly* held a
   target of zero.

   **Always verify on the instance that actually runs**, e.g.
   `Island.Island:PersistentLevel.BP_AI_Van_C_1.BP_Path_Follow`, not
   `BP_AI_Vehicle_Base_C:BP_Path_Follow_GEN_VARIABLE`. For fleet-wide tuning prefer a **C++ default**,
   which no archetype override can shadow; use `ObjectTools.reset_properties` to clear existing deltas
   so instances track it again.
7. **`ObjectTools.set_properties` writes struct fields only partially on a placed instance.** Writing
   `relativeLocation {x,y,z}` to an instance component applied `x` and silently left `y`/`z` at their
   old values; the same write against the class template applied in full. `reset_properties` did not
   clear the deltas either. **Do not trust a struct write to an instance — read it back.**

   Because of 6 and 7 together, the traffic detection volume could not be fixed reliably in the asset
   at all, and **was still the broken 32 cm cube above the roof for every test up to 2026-08-05** —
   meaning the follow logic had never once been exercised. Functional geometry is now applied from
   `ADGAIVehiclePawn::ResolveComponents` under `bOverrideTrafficColliderShape`. Prefer code for
   anything gameplay depends on; leave the asset for artistic placement.

### Behaviour recovered from the graphs during stripping

Details that were only discoverable by reading the real DSL, now reproduced natively:

- **`StopMoving` applies the handbrake**, not just zero throttle. Without it a stopped vehicle rolls
  away on any slope. `StartMoving` releases it.
- **Crash MetaSound inputs are `"Reset Sound"` and `"Stop Sound"`** — not the `"Crash"`/`"Intensity"`
  originally guessed, which would have left every crash silent. Threshold is
  `VectorLength(NormalImpulse) / 1000 > 100`, i.e. **100000**; a sub-threshold hit fires
  `"Stop Sound"`.
- **`BP_Path_Decider` discovered its routes dynamically**, via
  `GetOverlappingActors(DecisionBox, BP_Path_C)`, and excluded the vehicle's current route. An
  authored-array-only design would have made every existing decider a silent no-op, so
  `GatherValidTargets` falls back to overlap discovery when `TargetPaths` is empty, and
  `ChoosePathFor` removes the current path.

## Intended traffic design (from the author, 2026-08-04)

The system the Blueprints were reaching for, stated directly rather than inferred. **The goal is
traffic that obeys road rules and picks randomly at junctions — not a faithful port.**

1. Every road has a spline down its middle; vehicles drive **offset to the right** to sit in a lane.
2. A collision box at every intersection makes the vehicle pick a different spline to follow.
3. A stoplight volume holds a vehicle from moving onto its chosen next spline until the light greens.
4. A **front bumper volume** detects vehicles/objects ahead so the vehicle can **adjust speed** to
   avoid collision — the author noted this was never fully implemented, and indeed the authored
   volume was a 32 cm cube 100 cm *above* the vehicle centre, unable to overlap anything.

Native status: (1) `LateralOffset` 254.89 along the **vehicle's** right. (2) `ADGPathDeciderActor`,
random choice, current route excluded. (3) `ADGTrafficLightActor`. (4) `TrafficClearance` +
`SafeFollowDistance`/`MinFollowDistance` proportional throttle; volume repositioned to
`relativeLocation (600, 0, 30)`, `boxExtent (350, 90, 60)`.

### Three holds, deliberately distinct

Conflating these caused real bugs, so they are separate flags:

| Flag | Source | Subject to `BlockedTimeout`? |
| --- | --- | --- |
| `bBlockedAhead` | `AddBlocker`/`RemoveBlocker` manual holds only | **Yes** — breaks mutual deadlocks |
| `bHeldBySignal` | `ADGTrafficLightActor` | **No** — a timeout here would run red lights |
| `TrafficClearance` | front volume overlaps, re-measured per tick | n/a — scales throttle, never a hard stop |

Overlaps deliberately do **not** set `bBlockedAhead`; that made following binary. Clearance is
measured as a dot product along the vehicle's own forward axis, so a vehicle in the next lane does
not register as directly ahead.

## State at end of 2026-08-05

Working in PIE: vehicles hold their lanes, choose randomly at junctions, obey the phased traffic
lights, and no longer drive in reverse. Speed limits are set to 25 mph on all three Island splines.

Behaviour added after the reparent, in the order the failures surfaced:

| Symptom | Cause | Fix |
| --- | --- | --- |
| U-turns, reverse driving | pure pursuit has no direction; centreline splines are two-way | `TravelDirection`, re-scored but only below `DirectionFlipMaxSpeed` |
| Drove in oncoming lane | offset applied along the *vehicle's* right, diluting as it yawed | route-relative offset + PD lane correction |
| Settled 15 cm from centre | `LateralOffset` was 0 on instances — archetype shadowing | C++ default, verified on instances |
| Wide turns, left the road | fixed 600 cm look-ahead; no slowing before corners | speed-scaled look-ahead; `GetCornerSpeedScale` |
| Rear-ended stopped traffic | detection volume never worked; distance-only following | volume set from code; closing-speed braking |
| Stopped and never restarted | `StopMoving` was terminal | `bAutoResume`; wider continuation radius |
| Beached off-road | no recovery from an unsteerable position | `UpdateStuckRecovery` → `SnapToLane` |

Tuning knobs, all on `UDGPathFollowComponent` unless noted: `LateralOffset` 254.89,
`LaneCorrectionGain` 2.0, `LaneDampingGain` 0.7, `MinAimDistance` 220, `AimTimeAhead` 0.55,
`FollowHeadwaySeconds` 1.6, `ComfortableDeceleration` 350, `MinCornerSpeedScale` 0.35,
`SpeedLimitCompliance` 1.0, and `ADGPathActor::SpeedLimitMPH` per road.

## Next session

1. **Detection range does not scale with speed.** The traffic volume reaches 26 m
   (`TrafficColliderExtent.X` 1150 at offset 1450), enough to brake from ~25 mph and no more. Raising
   any speed limit much past 25 needs that derived from speed rather than left as a constant.
2. **Cross-traffic false stops.** The volume is 23 m long and 1.8 m wide, so on a curve or through a
   junction it can sweep into another lane and stop for a vehicle that is not really in the way.
   `ShouldBlockFor` currently accepts any `ADGAIVehiclePawn`; filtering by heading alignment would fix
   it if observed.
3. **Signal holds are a single bool**, not reference counted. Two overlapping light volumes could have
   one release a hold the other still wants.
4. **Lane splines.** Agreed direction for the city map: replace centreline splines with one-way lane
   splines, generated from the existing centrelines rather than hand-authored — the author's blocker
   was tedium, so build the generator first. Removes both the lateral-offset and travel-direction
   machinery entirely.

## Not yet done

- **Untested in PIE.** Nothing below has been observed running; the migration is verified only by
  compilation. Expect `ForwardAimDistance` (600) to need tuning, traffic to yield for the first time
  now that `TrafficCollider` is functional, and vehicles to drive centred on splines.
- **The decider overlap fallback exists only in a Live Coding patch.** Run a full editor-closed build
  to persist it, or it is lost on the next restart.
- No native `AGameModeBase` — `GlobalDefaultGameMode` still points at the nonexistent
  `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode`.
- Player vehicle (`BP_Vehicle_Jeep`) and Enhanced Input untouched; still a standalone
  `AWheeledVehiclePawn`, not sharing the AI base.
- `BP_Stop_Sign` and `BP_Prop_Traffic_Light_Sm` have no native counterpart; they should drive
  `AddBlocker`/`RemoveBlocker` on `ADGAIVehiclePawn`.
- NPC stack (`BP_Modular_Char`, `BP_NPC_AI_Controller`, `BTree_NPC`) untouched.
- Stale `Content/Game/Vehicles/` duplicates not yet deleted.
