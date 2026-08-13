---
name: island-perf-notes
description: "Known performance characteristics of the Island map — VSM non-Nanite overflow from the moving sun, and the many-small-meshes structure behind it"
metadata:
  type: project
---

**VSM non-Nanite marking overflow (first seen 2026-08-13, when the day/night cycle landed).**
The log warns:

> `[VSM] Non-Nanite Marking Job Queue overflow. Performance may be affected. This occurs when
> many non-nanite meshes cover a large area of the shadow map.`

It is a **performance warning, not an error** — nothing renders wrong, at worst a hitch or a
shadow page updating late.

**Cause:** the island is built from hundreds of individual **non-Nanite** static meshes (every
road tile, sidewalk, grass tile, prop), and a **moving directional light** invalidates shadow
pages every frame, so all of them must be re-marked continuously. A static sun cached this work;
`UDGTimeOfDaySubsystem` no longer lets it.

**Fixes, in order of value (none applied yet — judged as log noise for now):**
1. **Enable Nanite** on the environment static meshes. Nanite uses a much cheaper shadow path and
   would likely silence it entirely. Batch asset setting.
2. **Reduce shadow draw distance** — a driving game rarely needs crisp shadows a kilometre out.
3. **Merge tiles** — the road/grass grid is hundreds of separately-tracked instances.

**Related knob:** `UDGTimeOfDaySubsystem::SunDiscAngleDegrees` (2°, vs the physical 0.5°) widens
the sun for looks, which also **softens shadows and costs more VSM work**. Dial it back if this
ever becomes a real framerate problem rather than a log line.

Also relevant: night lighting (headlights, streetlights) will add dynamic lights on top of this —
see `docs/BACKLOG.md`.
