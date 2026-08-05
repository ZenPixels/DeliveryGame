# DeliveryGame

Unreal Engine **5.8.1** driving/delivery game. Engine install: `C:\Games\UE_5.8`.

Historically Blueprint-only; a C++ module (`Source/DeliveryGame`) was added in Aug 2026 and
gameplay systems are being migrated into it one at a time. Blueprints are **reparented onto
native classes in place** rather than rebuilt, so existing assets, designer tuning, and map
references survive the migration.

## Building

UBT for 5.8 needs the .NET 10 runtime, which is **not** installed system-wide (highest system
runtime is 9.x). Use the engine's bundled runtime — invoking `UnrealBuildTool.exe` directly
fails with "You must install or update .NET".

```powershell
$dn  = "C:\Games\UE_5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe"
$ubt = "C:\Games\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"

# Regenerate .sln / project files
& $dn $ubt -projectfiles -project="F:\Projects\DeliveryGame\DeliveryGame.uproject" -game -rocket -progress

# Build the editor target
& $dn $ubt DeliveryGameEditor Win64 Development -project="F:\Projects\DeliveryGame\DeliveryGame.uproject" -progress
```

**The editor must be closed to build from the command line.** Live Coding is enabled, and UBT
refuses to run while it is active ("Unable to build while Live Coding is active"). Once the
module exists and the editor is running, prefer Live Coding (`Ctrl+Alt+F11`) or the
`CompileLiveCoding` MCP tool for iteration — but note Live Coding can only patch already-compiled
modules, so any **newly added module** still needs a full editor-closed build.

UHT runs before compilation, so a run that fails at the Live Coding check has still validated all
reflection macros. Generated headers land in
`Intermediate/Build/Win64/UnrealEditor/Inc/DeliveryGame/UHT/`.

## MCP / agent tooling

`.mcp.json` points at `http://127.0.0.1:8000/mcp`, served by the **running editor** via the
`ModelContextProtocol` plugin. The server only exists while the editor is open, and toolsets are
contributed by separate plugins. Enabled in `DeliveryGame.uproject`:

| Plugin | Why it's on |
| --- | --- |
| `LiveCodingToolset` | `CompileLiveCoding()` — compile from the running editor, returns compiler diagnostics |
| `EditorToolset` | `GetLogEntries`, `CaptureViewport`, `SelectActors`, `Get/SetCameraTransform`, `OpenEditorForAsset` |
| `ConfigSettingsToolset` | Read/write ini sections |
| `PhysicsToolsets` | Physics asset bodies and constraints |
| `AIModuleToolset` | Behaviour tree / blackboard work |
| `UMGToolSet` | Widget and HUD work |
| `SemanticSearchToolset` | Asset discovery |
| `AutomationTestToolset` | Discover and run automation tests |

The engine ships ~28 such plugins under
`C:\Games\UE_5.8\Engine\Plugins\Experimental\Toolsets\`. The `AllToolsets` aggregator is
deliberately **not** enabled — it pulls in 21 plugins (PCG, Niagara, GAS, GameFeatures,
DataflowAgent) that this project does not need. Note `LiveCodingToolset` is not part of that
aggregator anyway.

`EditorToolset` also registers a set of **Python** toolsets that do not appear when grepping the
engine for C++ `*Toolset*.h` files — these are the most useful ones in the whole set:

- `editor_toolset.toolsets.blueprint.BlueprintTools` — full Blueprint access: `read_graph_dsl` /
  `write_graph_dsl` (graph logic as an editable S-expression DSL), `set_parent` (reparenting),
  `list_variables`, `remove_variable`, `get_default_object`, `compile_blueprint`, node/pin editing.
- `editor_toolset.toolsets.object.ObjectTools` — property get/set on any object or CDO. Always call
  `list_properties` first; property names cannot be guessed.
- `editor_toolset.toolsets.actor.ActorTools`, `.scene.SceneTools`, `.asset.AssetTools`,
  `.material.MaterialTools`, `.static_mesh.StaticMeshTools`, `.skeletal_mesh.SkeletalMeshTools`,
  `.data_table.DataTableTools`, `.texture.TextureTools`
- `editor_toolset.toolsets.programmatic.ProgrammaticToolset` — batch several tool calls through one
  sandboxed Python script
- `aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools` — inspect Behaviour Tree assets

So Blueprint graphs **can** be read and rewritten over MCP, and reparenting can be scripted rather
than done by hand. `describe_toolset` on `BlueprintTools` returns ~72k characters, over the tool
output limit — it gets spilled to a file, so extract the schemas you need with Python rather than
reading it whole.

To inspect a `.uasset` with the editor closed (no MCP server), parse its FName table: printable
ASCII runs reveal variable names, called functions, parent classes, and asset references. A Blueprint
parent shows up as a `BlueprintGeneratedClass'/Game/...'` string; native parents as
`/Script/Module.ClassName`.

## Layout

```
Source/DeliveryGame/
  Public/Traffic/, Private/Traffic/    Native traffic system (see docs/CPP_MIGRATION.md)
Content/Game/
  Blueprints/       Current Blueprints — Vehicle_Base is the live vehicle tree
  Vehicles/         STALE parallel copy of the above; scheduled for deletion
  GameMode/         Player character + Enhanced Input (InputMap_Main, 12 IA_* actions)
  UI/               HUD + widgets, with a UI/Core base-widget layer
  Maps/             Island (startup), Test, LandscapeTest
```

## Conventions

- Native gameplay classes use the `DG` prefix (`ADGPathActor`, `UDGPathFollowComponent`) to avoid
  ambiguity with engine types — notably `UDGPathFollowComponent` vs the engine's own
  `UPathFollowingComponent` in AIModule.
- Native properties mirror the old Blueprint variable names so the mapping stays obvious during
  reparenting.
- Tick is opt-in: debug-only actors call `SetActorTickEnabled(bDrawDebug)` in `BeginPlay` rather
  than ticking unconditionally.

## Known issues

- `Config/DefaultEngine.ini` sets `GlobalDefaultGameMode` to
  `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode`, but `Content/ThirdPerson/` **does not
  exist**. Maps rely on per-map World Settings overrides. Needs a real native GameMode.
- `Content/Game/Vehicles/` duplicates much of `Content/Game/Blueprints/` (van assets, the three
  `BP_Path*` actors, an orphaned `BP_AI_Car_Base`). Audio is triplicated: `Car_Thump` exists in
  `SFX/`, `Vehicle_Jeep/Sounds/`, and `Vehicle_Base/Collisions/`.
- **`Content/Game/Vehicles/Sedan/BP_Sedan` does not compile** — its parent class
  `/Script/ArcadeVehicleSystem.StaticVehicleExample` comes from a plugin that is not installed, so
  the asset has a NULL parent. It is the only failing Blueprint in the project (352 of 353 compile
  clean) and it sits in the stale `Vehicles/` tree, so deleting that tree resolves it.
  Assets also reference a missing `/Script/Narrative` package.

  Verify with:
  ```powershell
  & "C:\Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "F:\Projects\DeliveryGame\DeliveryGame.uproject" -run=CompileAllBlueprints -unattended -nopause
  ```
  Note this commandlet exits **1** if any Blueprint fails, so a non-zero exit is expected until
  `BP_Sedan` is gone. Do **not** use `-nullrhi` with plain editor mode for headless checks — the
  editor world hits a `TNotNull` fatal error on startup under a null RHI. Use a commandlet instead.
- `*_BuiltData.uasset` is gitignored yet `Island_BuiltData` and `Test_BuiltData` are tracked.
- No `.gitattributes` or LFS despite a repo full of binary `.uasset` files.
