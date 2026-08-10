---
name: game-vision
description: "The game's design vision, from the author directly (2026-08-09) — GTA-lite delivery game with creeping folk horror on a Pacific Northwest island"
metadata: 
  node_type: memory
  type: project
  originSessionId: d4cdc8d0-4559-4c96-a55c-21ea2b09f286
  modified: 2026-08-10T00:18:57.527Z
---

From the author, 2026-08-09. Explicitly "a bit of a loose idea" — hold it loosely, don't treat as
locked spec.

**Shape:** GTA-lite delivery game. Low poly, arcade feel. No weapons planned (maybe reconsidered
later). Set on an **island in the Pacific Northwest** — the island is deliberate: it bounds the
world. Town areas plus mountainous/forested areas. Open-world feel, but possibly loading sections
and **GTA-style roadblocks gating areas until story beats resolve**.

**Core loop:** receive delivery requests, deliver A→B under time pressure. Player can **exit the
car**; on-foot **platforming** to reach places the car can't. ("Just delivering things in time might
be enough" — the author is unsure how much more system weight the loop needs.)

**The phone is the hub**, with multiple apps: delivery (built first), store, music, podcasts,
**insurance**. Insurance/fines: hitting cars or pedestrians costs money. They liked the idea of
**survival costs** — scraping by on a crappy gig job. *(Resolved 2026-08-10, see ratified
decisions below: fines are one simple number, food/hunger is cut, rent stays.)*

**Economy (author, 2026-08-09 — "another sub system I'm not ready for yet", do not build
unprompted):** purchasable **car upgrades** — it shouldn't all be about survival. The intended
tension: **delay paying rent (with a penalty) to upgrade the jeep and hopefully earn more.** That
makes the money loop a real decision space: income (deliveries) vs. drains (rent, fines/insurance)
vs. investment (upgrades) — borrowing against survival to invest in the job. Upgrades also give the
future car-feel work a progression home (speed/handling/cargo as purchasable tiers) and the phone's
store app a purpose.

**Car decals are EARNED, never purchased** (author, 2026-08-09): some tied to achievements, but many
that aren't — hidden decals, decals requiring side quests, etc. Keeps cosmetics outside the money
economy: cash stays pure survival-pressure, while decals become the exploration/story reward track.
The prototyped spray/decal station (`BP_VehSprayStation`, `WB_UI_VehSpray`) is where they'd be
applied. (Imagine what the ShellStop taco or a Bushed Baby decal is earned by — and whether some
late-game decals are corrupted variants.)

**Ratified design decisions (2026-08-10 — a design-suggestion pass was reviewed by the author,
who ruled on each; these are decided, not speculative):**
- **Package knowledge = recognition, not mystery.** The player usually knows what an order is and
  who it's for — seeing the request should trigger "oh, it's *that* NPC" excitement or dread (the
  arsonist's water bottle only works because you recognize it). Sometimes it's just "a box" you
  deliver unopened. Rarely, a box gets opened and something unexpected happens. Author's guiding
  principle: *"a key to a game like this is adding in the unexpected from time to time."*
- **The delivery log is the horror text.** The app keeps a scrollable history; when the evil
  surfaces, the player can scroll back and find THE package in their own paperwork — date,
  address, their own 5-star rating on it.
- **Podcasts/radio are the lore channel** (author already intended this): island lore, call-in
  shows, brand ads while driving; corruption degrades them (dead air, hosts who don't sound right).
- **Day structure + home base.** Everything timeboxed into a day, maybe stretching into early
  night; a **penalty for staying up too late**; the day ends by **returning home**. Home should
  feel like a safe relief from the craziness of each day — and is a second stage for weird stuff
  (author: *"maybe the dog comes home with you!"*).
- **Deliveries unlock the map.** A region opens because a delivery is addressed there — the job is
  the key, not abstract roadblock flags.
- **Corruption infects systems, not just art.** Late-game: the GPS arrow occasionally lies, a
  request arrives with no address, lights cycle wrong at night, one traffic car (a corrupted driver
  personality) shadows the player.
- **No food/hunger system** (for now — keep it simple and test). The pressure triangle is rent +
  fines + upgrades.
- **Insurance collapses to a single fines number** — no premiums or policy sim; "insurance"
  survives as the app's name and dark-comedy flavor.
- **Dialog is flat-with-flavor:** branches recolor responses/relationships and there may be a few
  endings — no wildly divergent storylines.
- **Platforming is traversal, not challenge — with ONE exception:** a single delivery character
  who makes the player attempt annoying platforming challenges. The exception to the rule
  (character unassigned so far).
- **The vertical slice is "Day One":** wake at The Bushed Baby, Pam gossip, three deliveries (one
  normal, one dog treats, one water bottle to a polite young man near a small fire), rent notice
  at dusk, the mail truck idling across the street as you park at home.

**Characters and dialog:** interacting with NPCs opens a **2D branching dialog, RPG-style**
(player portrait one side, NPC the other). Possible side quests. Characters should be interesting,
some **unsettling or threatening**. Canonical example — the water-bottle arsonist: an NPC who
repeatedly orders a single bottle of water; the player starts recognizing the strange order; each
delivery, he has set increasingly larger fires (never admits it — he's entranced by flames, "the
heat is making me thirsty"), eventually burning people in a car or house. **"But you need the
money!"** — economic pressure makes the player complicit. That line is arguably the game's thesis.

**Story arc:** over time, an evil is unleashed on the island — *perhaps directly from something the
player delivered* — weird happenings, monsters, possessions, strange/frightening deliveries.
Tone: "frightening (but not really, this is a low poly indie game after all)".

**Already prototyped by the author:** jeep paint/decal system (the VehSprayStation); a random
pedestrian generator (known bugs: mesh seams don't line up; ragdolls all snap to the same position
when hit).

**Existing assets that map to this:** `WB_UI_Delivery_App` + `IA_Toggle_Phone` (the phone),
`DeliveryTimer` on the character, `BP_Delivery_Start`/`BP_Delivery_End` VFX, `BP_Arrow` (guidance),
`BP_VehSprayStation`, `Modular_Character` NPC stack. Note: the **DialogueSx plugin exists but is
disabled** in the uproject — the author had dialog tooling at some point.

**Implications for engineering priorities:**
- Traffic is *ambience plus an economic hazard* (fines/insurance), not a core mechanic — its current
  state (plausible, arcade, self-sustaining) is close to sufficient. Don't gold-plate it.
- The **delivery loop is the actual game** and mostly doesn't exist yet — request → pickup → timed
  drive → deliver → payout is the highest-value next system.
- **Player car feel** matters disproportionately (author: "doesn't feel very fun to drive").
- The insurance question resolves elegantly if fines are simply the pressure that keeps the player
  delivering (to arsonists) — build the minimal money loop first, decide survival-sim depth later.
- Island geography drives the road network work: town grid + rural mountain roads, roadblocks as
  progression gates.

See [[traffic-system-goals]], [[road-network-plans]].
