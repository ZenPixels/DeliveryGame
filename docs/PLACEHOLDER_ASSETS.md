# Placeholder asset registry (AI-generated content)

**Author's intent (2026-08-12): production art should be made by human artists.** Any
AI-generated asset that enters the project is a **temporary placeholder** and must be findable and
replaceable later. This file is the registry; the naming rule below is the search mechanism.

## The rule

1. **Name it with the `AIGEN_` prefix.** Example: `AIGEN_T_Billboard_Ness`, `AIGEN_SM_Crate`.
   The prefix survives moves and renames of folders, and makes every placeholder findable with a
   single content-browser search for `AIGEN_`.
2. **Register it in the table below** with what it stands in for, so a future artist has a brief.
3. **Never reference an `AIGEN_` asset from something hard to change** (e.g. baked into a
   material function used everywhere) without noting it here.
4. This applies to textures, meshes, audio, icons, concept references — anything shipped-looking
   that a model produced.

Not covered by this rule (no tagging needed): programmer-art primitives, engine default
materials, the author's own concept art in `/ArtAssets`, and third-party asset packs (Polygon,
Assetsville) — those are licensed content with their own replacement decisions.

## Registry

| Asset | Path | Stands in for | Added |
| --- | --- | --- | --- |
| _(none yet)_ | | | |

**Status as of 2026-08-12: the project contains no AI-generated assets.** Everything placed so
far is from the author's asset packs, the author's own concept art, or engine content. The
`TextRenderActor` destination signs and `SM_box`-style props are engine/pack content used as
placeholders, not generated art — they are tracked in `ASSET_TODO.md` instead.

## When replacing

Search `AIGEN_` in the content browser, replace the asset, delete the row here. A final pass
before any release should confirm the registry is empty and the search returns nothing.
