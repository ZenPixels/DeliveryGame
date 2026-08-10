---
name: ue58-ubt-needs-bundled-dotnet
description: "UE 5.8 UnrealBuildTool on this machine must be run via the engine's bundled .NET 10 runtime, not the exe directly"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-05T01:21:00.627Z
---

On this machine (as of 2026-08-04), running `UnrealBuildTool.exe` directly fails with "You must
install or update .NET" — UBT for UE 5.8 targets .NET 10 and the highest system-wide runtime
installed is 9.0.10. Invoke the bundled runtime instead:

```
C:\Games\UE_5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe
C:\Games\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll
```

**Why:** the failure message points at installing .NET 10 system-wide, which is unnecessary and
misleading — the engine ships the runtime it needs.

**How to apply:** use the bundled-dotnet form for all UBT invocations on this project. Also note
UBT refuses to build while Live Coding is active in a running editor ("Unable to build while Live
Coding is active"), but UHT runs *before* that check — so a failed run still validates reflection
macros. See [[deliverygame-mcp-toolsets]].
