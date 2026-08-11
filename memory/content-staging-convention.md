---
name: content-staging-convention
description: "Content/Staging is quarantine for possibly-unused assets and will be excluded from production builds — live maps must only reference Content/Game"
metadata:
  type: project
---

Author's content organization (2026-08-10): assets they are not sure they're using live in
`/Game/Staging/` (asset-pack dumps like PolygonCity, PolygonTown). When an asset is actually
adopted, it gets **moved to an appropriate folder under `/Game/Game/`**. The intent is that
Staging is **ignored/excluded on production builds** to reduce size.

**Why it matters:** anything referenced by a live map from a Staging path will vanish from a
production build.

**MIGRATION DONE 2026-08-10:** a recursive dependency audit of the Island map found **147 Staging
assets in the live closure** (not 3 as first spotted): road/grass tiles, buildings, props, the bus/
hearse/muscle-car rigs, the ENTIRE player+NPC character stack (skeletal meshes, anim BPs, anims),
their materials/textures, and `BP_ThirdPersonGameMode` (the "missing GameMode" of the known-issues
list — it lived in Staging). All moved: meshes → `/Game/Game/Meshes/Props/{Roads,Outdoor,Buildings,
StreetProps}` + `Meshes/Vehicles`; characters → `/Game/Game/Characters/<subtree>`; materials/
textures → `/Game/Game/{Materials,Textures}/<Pack>`; GameMode → `/Game/Game/GameMode/`. Verified:
the map's closure has ZERO /Game/Staging references.

**Tooling traps hit (for the next bulk move):** deleting redirectors does NOT fix up referencers
whose packages aren't dirty — `save_assets` silently skips clean packages, leaving on-disk imports
pointing at deleted redirectors. The reliable rewrite is a **move-out/move-back cycle** (move
force-saves the package with its correct in-memory imports): move X→X_fixup, delete the redirector
left AT X, move X_fixup→X, delete leftover redirector. Also: move assets in dependency order or
just run the audit→rewrite loop until clean — packages resaved before their own deps moved need a
second pass. `update_metadata_tags` fails on most asset types (can't be used to dirty packages).

**Known dead references (pre-existing, still broken):** something references the deleted
`BP_Sedan`; the jeep references missing `/Game/Vehicles/Veh_Offroad/SkeletonCar`; modular character
hair meshes reference missing `Skeleton_Modular_Char`. Worth a cleanup pass someday.

**How to apply:** when placing assets over MCP, prefer `/Game/Game/...` paths; if matching existing
map content forces a Staging path, flag it and offer the move (AssetTools.move fixes references).
Never treat Staging contents as safe long-term dependencies.
