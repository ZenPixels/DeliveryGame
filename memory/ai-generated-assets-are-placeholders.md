---
name: ai-generated-assets-are-placeholders
description: "Author wants human artists for production art — any AI-generated asset must be prefixed AIGEN_ and registered so it can be found and replaced"
metadata:
  type: feedback
---

The author (2026-08-12) intends **production art to be made by human artists**, even though
release is uncertain. Therefore **any AI-generated asset added to the project is a placeholder**
and must be trivially findable later.

**How to apply:**
- Name every AI-generated asset with the **`AIGEN_` prefix** (`AIGEN_T_Sign_Ness`,
  `AIGEN_SM_Crate`). The prefix survives folder moves and renames, unlike metadata tags — and
  `AssetTools.update_metadata_tags` fails on most asset types anyway.
- Add a row to **`docs/PLACEHOLDER_ASSETS.md`** describing what it stands in for, so a future
  artist has a brief.
- Do not bury an `AIGEN_` reference somewhere hard to swap without noting it.
- Ask before generating art at all when a pack asset or programmer-art primitive would do.

**Why:** the author wants the shipped look to be human-authored; untagged generated assets would
be indistinguishable from keepers once the project is large, and quietly become permanent.

Not covered: engine defaults, primitives, the author's own concept art in `/ArtAssets`,
third-party packs (Polygon/Assetsville) — see [[content-staging-convention]] for how pack content
is organised.
