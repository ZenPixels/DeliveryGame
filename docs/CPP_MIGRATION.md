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

## Reparenting steps (editor work)

Do these one class at a time, testing in PIE between each.

1. **Full build with the editor closed**, then reopen.
2. **`BP_Path` → `ADGPathActor`.** Class Settings → Parent Class. The BP's own `Spline Path`
   component will collide with the native one; delete the BP's copy and re-point spline data.
   Set `NextPaths` per instance to wire junctions.
3. **`BP_Path_Follow` → `UDGPathFollowComponent`.**
4. **`BP_AI_Vehicle_Base` → `ADGAIVehiclePawn`.** Its `BP_AI_Van` and `BP_AI_School_Bus` children
   follow automatically.
5. **`BP_Path_Decider` → `ADGPathDeciderActor`.**

### The name-collision trap

On reparent, a Blueprint variable or component whose name matches an inherited native property is
**renamed** by the editor (e.g. `MaxThrottle` → `MaxThrottle_0`) rather than merged. The native
property takes over with its **C++ default**, and the designer-tuned value stays on the renamed
variable.

So for each collision: read the value off the renamed variable, type it into the native property in
Class Defaults, *then* delete the renamed one. Do not delete first — the tuned value is lost.

Values worth capturing before reparenting: `Forward Aim Distance`, `Max Throttle`, `Auto Find
Spline`, and the extents/offsets of `Route Collider`, `Traffic Collider`, and `Stop Zone`.

## Not yet done

- No native `AGameModeBase` — `GlobalDefaultGameMode` still points at the nonexistent
  `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode`.
- Player vehicle (`BP_Vehicle_Jeep`) and Enhanced Input untouched; still a standalone
  `AWheeledVehiclePawn`, not sharing the AI base.
- `BP_Stop_Sign` and `BP_Prop_Traffic_Light_Sm` have no native counterpart; they should drive
  `AddBlocker`/`RemoveBlocker` on `ADGAIVehiclePawn`.
- NPC stack (`BP_Modular_Char`, `BP_NPC_AI_Controller`, `BTree_NPC`) untouched.
- Stale `Content/Game/Vehicles/` duplicates not yet deleted.
