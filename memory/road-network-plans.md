---
name: road-network-plans
description: "Road network: closed 8-junction network BUILT 2026-08-09 over MCP (topology + wiring recorded here); Dynamic Road System and varied terrain later"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-10T00:26:18.376Z
---

**BUILT 2026-08-09 over MCP** (saved into `/Game/Game/Maps/Island`; the map is binary, so the
wiring is recorded here). Coordinate frame: top-down with **+X up, +Y right** in captures.

- **Topology:** the original loop (4 junctions) plus a second ring east/south of it — **zero dead
  ends**. Junctions: `W` corner (~930,-2260), `NW` corner (~930,4690), `N-mid` T (8900,4694),
  `E` 4-way signalized (8900,-2278, the only lights), `F` T (16334,-2278, was the orphaned pad),
  `NE` corner (16334,4694), `S` corner (8899,-8254), `SE` corner (16334,-8254).
- **Path segments** (all straight centerline BP_Path with `RoutePoints`, SpeedLimitMPH 25):
  existing `BP_Path_C_1` (road A-west, extended west to x=1600 via RoutePoints), `C_2` (D-north),
  `C_3` (road C); new `Path_B`, `Path_AEast`, `Path_G`, `Path_H`, `Path_DSouth`, `Path_I`,
  `Path_J` (outliner folder `TrafficNetwork/Paths`). Road B previously had **no path at all**.
- **Deciders:** all 8 junctions have one, all with **authored `TargetPaths`** (overlap discovery no
  longer relied on). New ones in `TrafficNetwork/Deciders`: `Dec_W/F/NE/S/SE`.
- **Destination placeholders** (TextRenderActors, folder `Destinations`): NESS MART (11600,-6100;
  real gas station + shop already there), THE BUSHED BABY (10050,-650, beside the two corner-shop
  buildings), SHELLSTOP (12400,3600), BAKERY (14300,-4500), HOME (15000,2800 — deliberately apart
  from the commercial cluster, per the day-structure "return home" decision in [[game-vision]]).
- ~365 new actors total: ~220 road/sidewalk/pad tiles, 145 grass tiles, paths/deciders/markers.
- **Tile conventions** (for future MCP road building): 500-unit tiles; EW roads = south row yaw 90
  at (x,y0) covering [x,x+500] + north row yaw −90 at (x,y0+999) covering [x−500,x]; NS roads =
  west row yaw 0 covering [y−500,y] + east row yaw 180 at x0+999 covering [y,y+500]; pads = 4×
  Road_Bare + 4 sidewalk corners + 8 crossing stubs extending outward; grass tile covers
  [x,x+500]×[y−500,y]. Decider boxes need `RelativeScale3D` (30,30,3) — BP default is a 32-unit box.
  **New-path trap: `SpeedLimitMPH` spawns at 0** (C++ default); existing paths use 25 per-instance.
- **Not done:** junction connector arcs (turn quality), lights at new junctions, extra AI vehicles
  on the new ring, buildings/props for the placeholder destinations (author will place own assets).

Original agreement, from the author, 2026-08-09:

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
