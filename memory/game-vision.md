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

**CORE LOOP EVOLUTION (author, 2026-08-11/12 — the target design; the built slice is still
single-job):**
- **Passenger deliveries ("Uber app")** alongside packages. You drive a *person*. Recognised
  purpose: **lore delivery** — a passenger is a captive audience who can talk *while you drive*,
  which converts otherwise dead driving time into story time (same job the podcasts do, but
  interactive). Prime candidates: the arsonist, the Mailman, the talking dog after the collar.
- **Multiple simultaneous opportunities** — several packages plus a passenger available at once;
  the player chooses what to take. The game becomes **triage**, not just driving.
- **Offers themselves expire**: a countdown on each *opportunity*, not only on accepted jobs.
  This is what forces choosing under uncertainty instead of accepting everything and optimising.
- **Decaying payout instead of binary failure**: each job has a **full value** window; miss it and
  the driver's tip/cut ticks down toward zero rather than the job failing outright. (Replaces the
  current hard fail. Also the natural **difficulty/accessibility dial** — see the story-mode
  question below: slower decay or a floor = gentler game.)
- **Dawdling blocks the queue**: while a job is overdue/decaying, **no new opportunities arrive**
  until it is delivered. Suggested refinement (agent, for discussion): a **job capacity** (how
  many you can hold at once) that is **upgradeable via the car economy**, with the offer queue
  pausing whenever any held job goes into decay — so overcommitting is self-punishing.
- **VIP / special deliveries carry the story.** Key moments become special by *removing* pressure,
  not adding it: often **no timer at all**, so dialog can breathe and the player knows this one
  matters. They can gate the end of the day or unlock freeform play. Natural home for the Mailman
  sabotage beats.
- Pairs with the impact systems already built: `OnStruck` means **driving quality can affect a
  passenger's tip** (hits, near misses, kerb-hopping), and the insurance "woop" fires in the same
  moment.
- **VIP/special opportunities are exempt from the queue rules** and **can only be taken alone**
  (author) — they never compete with routine work, which is part of what marks them as story.
- **The day is the outer timer**: the real question each day is *how much can you pack in*. Job
  capacity (a jeep upgrade — roof rack/storage) multiplies what a day can hold.
- **Harder/farther deliveries pay more** — distance and difficulty scale the full value.
- **The opportunity board**: a phone screen where multiple offers sit with their timers visibly
  decaying, forcing the player to triage from short descriptions in the time available.
- **The phone should be invaluable, annoying and distracting all at once** (author's words):
  notification sounds, pop-up toasts, alerts competing for attention while you drive. The
  corruption layer eventually turns this against the player.

**The phone is the hub**, with multiple apps: delivery (built first), store, music, podcasts,
**insurance**.

**Phone as a 3D object (author, 2026-08-11):** the interface should be an actual **3D cellphone
model**, probably **lower left** of the screen, not a flat full-screen UI. Apps envisioned:
**navigation/mini-map** (possibly merged with the delivery app), **phone** (lore), **podcasts**
(lore), **music** (lore), **delivery**, **insurance**, **banking** (or maybe just a balance
indicator always on screen), **current time**, room for more. Navigate apps with the **d-pad**.
**Held vs mounted (author, 2026-08-12):** until the player buys the **phone mount** upgrade the
phone is *in their hand* — large, off to one side, **swaying into view on turns and bumps**,
genuinely distracting; the mount locks it small and static in a corner. A boring-sounding upgrade
made necessary by experience, and thematically perfect (the app is what makes you crash). Full
notes in `docs/DESIGN_LISTS.md`, including the accessibility requirement that phone stabilisation
also exist as a **settings toggle**, never only as a purchase.
**The phone IS the mini-map** (author, 2026-08-12): the map is visible *only while the phone is
out* — there is no always-on mini-map. Makes navigation a real cost and the mount upgrade
valuable; de-risked by the world-space objective marker handling turn-by-turn. Author flagged it
as **revisit-after-the-island-grows**. Full UX proposal and open questions: `docs/PHONE_UX.md`.
Corruption layer targets this directly (apps glitching = jump scares). **Insurance "woop"**
(author's idea, same day): hitting a car or damaging your own plays an annoying alert chirp and
shows the insurance charge as your balance ticks down — the hook exists already:
`ADGAIVehiclePawn::OnStruck` broadcasts who hit whom and how hard. Insurance/fines: hitting cars or pedestrians costs money. They liked the idea of
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
- **Day/night cycle (author, 2026-08-11):** a real time-of-day cycle, with a **limited window to
  do as many runs as possible** per day — the pressure that makes the day structure above bite.
- **Weather (author, 2026-08-11):** at minimum **variable rain levels, affecting traction**
  (pairs with the surface-resistance item); and eventually **horrible "monster weather"** for the
  corrupted/late-game island.
- **The vertical slice is "Day One":** wake at The Bushed Baby, Pam gossip, three deliveries (one
  normal, one dog treats, one water bottle to a polite young man near a small fire), rent notice
  at dusk, the mail truck idling across the street as you park at home.

**Delivery interaction upgrade (author, 2026-08-10 — queued, not yet built):** pickup and
drop-off should be **more than entering/exiting collision boxes**. Pickup = interacting (the
author believes **E is already mapped to interact**) with an **actual item** — use `SM_box` as
the placeholder package for everything at first. Drop-off = to a **person or a specific drop-off
point**, also requiring the interact press. The green arrow keeps marking objectives for now;
the author intends to shift to **something more subtle** eventually. Implementation home:
`ADGDeliveryPointActor` gains an interact path (overlap = "in range", E = commit) and the
subsystem gains a carried-package state; a person recipient ties into the NPC roster later.

**OPEN QUESTION — is the game losable? (author, 2026-08-11, explicitly deferred "for a long
time"):** does losing too much money mean starting over, or is the penalty something else? And
should there be a **"story mode"** for players who struggle with games (disability or otherwise)
and want the characters and story without the difficulty? Notes toward the answer:
- A hard restart deletes the story, which is the reward — poor fit for a narrative game. The
  author's own instinct on rent penalties was already **"pressure, not spiral"**.
- A **narrative failure state** fits the tone better than a game over: miss rent enough and you
  are evicted, sleep in the jeep, dialog changes, some content closes off — the island keeps
  going and you keep playing, poorer.
- **Story mode is cheap if planned early** and expensive if retrofitted: make the difficulty
  surface a small set of multipliers (delivery timer generosity, fine scale, whether timers can
  fail at all, traffic aggression) rather than hard-coded numbers. **Timers are the main
  accessibility barrier** in a delivery game, so "timers never fail" is the single
  highest-value toggle. The one platforming-challenge character ([[game-vision]] rule 10) is
  exactly the kind of content such a mode should let players skip.

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
