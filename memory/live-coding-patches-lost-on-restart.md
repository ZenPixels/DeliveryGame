---
name: live-coding-patches-lost-on-restart
description: "Live Coding patches vanish when the Unreal editor restarts, so Blueprints reparented onto patched native classes break"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-05T02:15:31.952Z
---

Live Coding writes `UnrealEditor-DeliveryGame.patch_N.dll` files and patches the **running**
process only. On editor restart, only the base `UnrealEditor-DeliveryGame.dll` from the last full
UBT build is loaded and every patch is discarded.

This bit hard on 2026-08-04: six patches (8:53–9:08 PM) refined `ADGPathActor` and
`ADGAIVehiclePawn`, Blueprints were reparented onto those refined classes, then the editor crashed
and reloaded the 8:24 PM base DLL. The assets were left parented to stale class definitions — the
exact broken state the patches had fixed.

**Why:** reparenting is persisted into the `.uasset`, but the native class layout it was validated
against lives only in memory. The two fall out of sync silently.

**How to apply:** before reparenting Blueprints onto native classes, get the change into a **full
editor-closed UBT build**, not just a Live Coding patch. Live Coding is fine for iterating on
function bodies; it is not a foundation to reparent assets onto. Also note Live Coding refuses
outright when a change adds a virtual (vtable change) — it returns `CompileNotStarted`.

Second lesson from the same incident: MCP Blueprint edits are **not auto-saved**. `set_parent`
persisted, but `delete_node`, `remove_function_graph`, and `remove_variable` did not, and that work
was lost in the crash. Save each asset explicitly after modifying it. See
[[deliverygame-mcp-toolsets]] and [[ue58-ubt-needs-bundled-dotnet]].

**Reading Live Coding failures remotely:** the MCP tool only says "please see Live console", and the
console is a separate window CaptureEditorImage cannot see. But Live Coding compiles through UBT, so
the real compiler errors are always in `C:\Users\Todd\AppData\Local\UnrealBuildTool\Log.txt` —
grep that instead of guessing.
