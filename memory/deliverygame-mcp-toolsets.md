---
name: deliverygame-mcp-toolsets
description: "The Unreal MCP server is editor-hosted and its tools come from separately-enabled toolset plugins, including a full Python Blueprint API"
metadata: 
  node_type: memory
  type: reference
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-05T02:15:55.010Z
---

DeliveryGame's `.mcp.json` points at `http://127.0.0.1:8000/mcp`, served by the **running
UnrealEditor process** via the `ModelContextProtocol` plugin — the server disappears when the editor
closes. Out of the box it exposes only a 3-tool gateway (`list_toolsets`, `describe_toolset`,
`call_tool`) fronting whatever toolsets other plugins have registered.

Real tools come from ~28 plugins under `C:\Games\UE_5.8\Engine\Plugins\Experimental\Toolsets\`, all
disabled by default. Enabled for this project: `LiveCodingToolset`, `EditorToolset`,
`ConfigSettingsToolset`, `PhysicsToolsets`, `AIModuleToolset`, `UMGToolSet`,
`SemanticSearchToolset`, `AutomationTestToolset`. The `AllToolsets` aggregator pulls in 21 plugins
and notably does *not* include `LiveCodingToolset`.

**Do not conclude a capability is missing by grepping the engine for C++ `*Toolset*.h` files.** That
search misses the **Python** toolsets `EditorToolset` registers, which are the most capable ones:

- `editor_toolset.toolsets.blueprint.BlueprintTools` — `read_graph_dsl` / `write_graph_dsl` (graph
  logic as an editable S-expression DSL), `set_parent`, `list_variables`, `remove_variable`,
  `remove_function_graph`, `find_nodes`, `delete_node`, `compile_blueprint`
- `editor_toolset.toolsets.object.ObjectTools` — property get/set on any object or CDO
- `editor_toolset.toolsets.programmatic.ProgrammaticToolset` — batch many tool calls in one
  sandboxed Python script; call `get_execution_environment` first
- plus `ActorTools`, `SceneTools`, `AssetTools`, `MaterialTools`, `StaticMeshTools`,
  `SkeletalMeshTools`, `DataTableTools`, `TextureTools`, `BehaviorTreeTools`

So Blueprint graphs **can** be read and rewritten over MCP, and reparenting can be scripted.

Practical notes: `describe_toolset` on `BlueprintTools` returns ~72k chars and spills to a file —
extract schemas with Python. Tool calls sometimes return *accumulated* editor script errors, so a
returned error does not always mean the call failed; verify with a read-back. Blueprint edits are
**not auto-saved** — see [[live-coding-patches-lost-on-restart]].

With the editor closed there is no MCP server; parse the `.uasset` FName table instead (printable
ASCII runs expose variable names, called functions, parent classes).

**PIE observation protocol (author, 2026-08-09):** when live PIE state is needed for diagnosis, ask
the author to start PIE and leave it running rather than starting it remotely — remote sessions run
unfocused and throttle to a crawl, and the author may be mid-observation. They offered this
explicitly.
