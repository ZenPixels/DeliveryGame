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

**Future traffic work the author has named** (do not build unprompted; design so they stay easy):
- **Point-to-point routing**: vehicles planning a route between two locations over the spline graph
  and following it, replacing pure random choice at junctions.
- **Driver personalities**: slow/over-safe drivers, reckless/fast drivers, etc. The tuning surface
  already exists per vehicle — `SpeedLimitCompliance`, `FollowHeadwaySeconds`,
  `ComfortableDeceleration`, `KinematicAcceleration/Braking/YawRate` — a personality is a named
  preset over those knobs.

**How to apply:** favour designs that survive a city-sized map — the `UDGTrafficSubsystem` path
registry over per-vehicle actor searches, throttled per-vehicle updates
(`DestinationUpdateInterval`), and opt-in debug drawing. Before scaling up, flag anything O(vehicles
× paths) per frame. Also worth revisiting at that point: the author is open to replacing centreline
splines with **one-way lane splines**, which removes both the lateral-offset hack and travel-direction
inference — see [[deliverygame-mcp-toolsets]] and `docs/CPP_MIGRATION.md`.
