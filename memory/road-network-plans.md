---
name: road-network-plans
description: "Road network expansion plans — richer test network with 3-way junctions now, Dynamic Road System and varied terrain later"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-10T00:26:18.376Z
---

From the author, 2026-08-09:

**AGREED ROADMAP (author, 2026-08-09):** roads first, then deliveries. Extend the map with more
intersections (including 3-ways) into a **closed system**, then build the delivery-loop vertical
slice **testing deliveries between a few fixed points** on that network. The delivery loop is the
game (see [[game-vision]]); the network build-out is its substrate and also owns the junction
connector arcs that fix the remaining turn artefacts. Prerequisite before starting: one full build —
the 2026-08-09 evening fixes (sensor body-only detection, oriented bounds, decider-defers-to-plan,
handoff settling time, re-acquire direction) exist only as Live Coding patches; source is committed.

**Near term — build a richer test network over MCP** using the existing blueprints. The Island map is
"a boring square"; random intersection choice is agreed-correct but with the current sparse network
it sends cars off the roads. Wanted: more road stretches and **three-way (T) intersections**, forming
a closed series of roads where every random choice leads somewhere real. The traffic mechanics
already handle N-way junctions — this is purely geometry + spline authoring.

**Tooling hook (built 2026-08-09):** `ADGPathActor::RoutePoints` — an editable local-space FVector
array that rebuilds the route spline in `OnConstruction` when it has 2+ points (empty = leave the
hand-authored spline alone). Exists because editor tooling cannot reliably write spline curve data
into placed instances (see [[unreal-property-edits-shadowed]]); laying out a road becomes "spawn
BP_Path, set transform + array". Spawn **BP_Path**, not bare ADGPathActor — the spline component
comes from the Blueprint. Verify array writes with a read-back.

**Plugin note:** the author tested **Dynamic Road System**
(https://www.fab.com/listings/8afb963e-5179-4641-b9bc-703def0f6216) — makes roads of varied heights
and angles much easier, but clashed with their other assets, which assume flat ground everywhere.
Shelved for now; **eventually they want varied terrain and road heights**. Keep road/traffic code
plugin-agnostic so it can sit on top of whatever generates the road geometry later — the traffic
system only cares about splines.

Road meshes for manual assembly live in `Content/Game/Meshes/Props/Roads/`.
