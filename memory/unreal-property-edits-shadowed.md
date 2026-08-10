---
name: unreal-property-edits-shadowed
description: "MCP property edits on Unreal class templates are shadowed by archetype overrides, and struct writes to instances apply only partially"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-05T05:31:13.586Z
---

Two failure modes cost several test cycles on DeliveryGame's traffic work (2026-08-04/05):

1. **A value set on a Blueprint component template does not reach placed instances.** Child
   Blueprints and placed actors carry archetype overrides. Saving a level after changing a template
   serialises whatever the instances still hold in memory as deltas, silently pinning the old value.
   `LateralOffset` read 254.89 on the template and 0 on every running vehicle for two rounds, so
   traffic drove down the centre line while *correctly* holding a target of zero.
2. **`ObjectTools.set_properties` writes struct fields only partially to an instance.** Writing
   `relativeLocation {x,y,z}` applied `x` and silently left `y`/`z` stale. The same write against the
   class template applied in full. `reset_properties` did not clear the deltas either. This left the
   vehicles' traffic-detection box as a 32 cm cube above the roof through *every* test, so the
   collision-avoidance code had never actually run.

**Why:** both fail silently, and both defeat verification done on the wrong object. Reading the
template back showed the intended value while the running instances used something else.

**How to apply:**
- **Verify on the object that actually runs** — `Map.Map:PersistentLevel.Actor_C_1.Component`, never
  `Actor_C:Component_GEN_VARIABLE`.
- Always **read struct writes back** and expect only partial application on instances.
- For anything gameplay depends on, prefer a **C++ default** or set it from code at `BeginPlay`
  (as `ADGAIVehiclePawn::bOverrideTrafficColliderShape` does) — neither can be shadowed.
- Scalars and bools do write correctly to instances; the problem is specific to structs.

See [[deliverygame-mcp-toolsets]] and `docs/CPP_MIGRATION.md`.
