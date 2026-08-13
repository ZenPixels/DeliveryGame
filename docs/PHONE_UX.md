# Phone UX — proposal for discussion

**Status: proposal, not decided.** Author's fixed points so far are marked ✅; everything else is
a suggestion to argue with. Nothing here is built.

## Fixed points (author)

- ✅ The phone is a **3D object**, lower-left, not a flat full-screen UI.
- ✅ Held in hand and **swaying** until the phone-mount upgrade locks it to a corner.
- ✅ Apps: navigation/mini-map, delivery, phone (lore), podcasts, music, insurance, banking,
  clock. D-pad to select.
- ✅ Delivery app needs **two views**: a **list of offers you approve or decline line by line**,
  and a **view of currently active jobs**.
- ✅ Should be **invaluable, annoying and distracting**: notification sounds, pop-up toasts.
- ✅ Corruption layer eventually attacks this UI (glitches, wrong info, jump scares).

## Proposed structure

### Three layers of information

1. **Always-on HUD** (no phone required): money, the active objective name, its timer. Small and
   glanceable — enough to drive by, not enough to plan by.
2. **Phone visible** (its default state while driving): whatever app is open, readable at a
   glance if you're willing to look away from the road.
3. **Phone focused** (hold a trigger, or press the toggle): you can navigate lists and accept or
   decline. This is the deliberate, expensive action.

### Time does not stop ✅ DECIDED

✅ **The phone never pauses the world or any timer** (author, 2026-08-12) — "the pressure is
always on". Offers keep lapsing, tips keep decaying, traffic keeps coming while you read.

✅ A **traditional pause / game menu** exists separately and *does* stop the game; it should
**blur or obscure the world** while open, so it reads as clearly out-of-game in a way the phone
never does.

(The "slow time while the phone is open" idea survives only as a possible accessibility option.)

### Two speeds of interaction

- **Fast — toast quick-accept.** A new offer arrives as a toast: `Ness Mart → Bakery · $9 · 38s`.
  Accept or dismiss it with a single button, eyes barely leaving the road. Most offers are taken
  this way.
- **Slow — open the board.** Opening the delivery app to *compare* several offers is the
  attention-expensive action, and the one where the swaying unmounted phone genuinely costs you.

That split is what makes the mount upgrade meaningful without making the pre-mount game
unplayable.

### Controls (controller-first)

| Input | Action |
| --- | --- |
| Toggle Phone (existing `IA_Toggle_Phone`) | Raise/lower the phone |
| D-pad ◀ ▶ | Switch app |
| D-pad ▲ ▼ | Move the selection within the current list |
| A | Accept / confirm / open item |
| B | Decline / back |
| Shoulder | Switch tab within an app (Offers ↔ Active) |
| Toast prompt | Single-button accept, separate button dismiss |

### The delivery app

- **Offers tab** — one line per opportunity: pickup → dropoff, fare, distance, and its countdown
  visibly draining. Sorted by urgency. Approve/decline per line (author's requirement). Greyed
  out and unselectable when at capacity or blocked, with the reason shown ("at capacity",
  "finish your late delivery").
- **Active tab** — held jobs with stage (fetching / carrying), the objective, time left in the
  full-value window, and — once decaying — the **live falling payout**, which is the number that
  should make the player wince.

### Navigation app ✅ DECIDED

✅ **The phone is the mini-map, and the map is only visible while the phone is out** (author,
2026-08-12). There is no always-on mini-map. Navigation should therefore be the default app.

Consequences, in the good sense: checking the map becomes a real action with a cost, the
unmounted swaying phone is a genuine navigational handicap (so the mount upgrade earns its
price), and players learn the island by memory rather than by following a permanent map — which
suits a small, named, walkable island.

What de-risks it: the **world-space objective marker** (currently the green arrow) already
handles moment-to-moment "where am I going", so the map is for **planning and orientation** —
which offer to take, roughly which way it is — not turn-by-turn guidance. If the map were the
only guidance, this decision would be much harsher.

⚠️ **Revisit after the island grows** (author's own caveat): a big map may make phone-only
navigation frustrating. Re-test once more systems are in and the island is larger. Escape
hatches if it goes badly: a better-phone/GPS upgrade that adds a small always-on strip, or making
the objective marker smarter rather than reinstating a permanent mini-map.

### Notifications

Toasts stack in a corner near the phone with distinct sounds: new offer (inviting), offer about
to lapse (urgent), job now decaying (bad), insurance charge (the "woop"), podcast/music/lore
chatter (ignorable, and the ones that make the phone *annoying*). Corruption later inserts toasts
that shouldn't exist.

## Open questions for the author

1. **Does the world keep moving while the phone is up?** (Proposal: yes, with a slow-time
   accessibility option.)
2. ~~Is the phone the mini-map?~~ ✅ **Yes — phone only, no always-on mini-map.** Revisit when the
   island is larger.
3. ~~Toast quick-accept?~~ 🟡 **Liked in principle, must be tested** (author). Open sub-question
   that decides it: **what information does a toast need for the player to judge an offer at a
   glance?** Candidates: pickup → dropoff, fare, distance/ETA, expiry countdown, and possibly a
   "fits your current route?" hint. Too little and it is a coin flip; too much and it is not a
   toast.
4. **What is always visible without the phone?** (Proposal: money, objective, timer.)
5. ~~Phone occlusion — hazard or attention cost?~~ 🟡 **Both, by design — test before
   committing** (author, 2026-08-12): raising the phone plays a *character pulls out phone*
   animation, and the phone itself comes up **first-person, right side of the screen**, even
   though the game is third-person. You can still drive and walk with it up, but it is
   deliberately annoying — and **the more the character moves, the more the arm bobs and sways**,
   compounding the annoyance. The mount upgrade is what tames this.
