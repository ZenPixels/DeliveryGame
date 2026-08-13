# Design lists

Running lists of *content* the game needs, as opposed to systems (those live in `BACKLOG.md`).
Nothing here is committed design — these are catalogues to grow and prune.

## Jeep upgrades (purchased with money; the rent-vs-upgrade tension)

Bought from the phone's store app. The economy's core decision is delaying rent (with a penalty)
to buy one of these and earn more. **Not built — do not implement unprompted.**

| Upgrade | Effect | Notes |
| --- | --- | --- |
| Roof rack / cargo box | **+ job capacity** (hold more simultaneous deliveries) | Author's example; the direct link between upgrades and the multi-job loop |
| Insulated bag / cooler | Slows payout decay on certain jobs (food?) | Speculative |
| Engine / turbo | Higher top speed, faster acceleration | Maps to existing movement tuning |
| Suspension | Better landings, less bounce, off-road stability | Existing wheel/suspension knobs |
| Tires | Grip; less traction loss in rain (see weather) | Pairs with surface-resistance work |
| Brakes | Shorter stopping distance | |
| Reinforced bumper | Less insurance damage per collision | Ties collisions to the economy |
| **Phone mount** | Locks the phone to a screen corner: small, static, readable | **See below — the best-designed upgrade so far** |
| Better phone | More offers visible at once, earlier warnings | A UI upgrade as a gameplay upgrade |
| Fuel tank | Fewer Ness Mart stops | Only if fuel becomes a system |

### The phone mount (author, 2026-08-12)

**Before you own it:** the phone is *held in your hand* — rendered large, off to one side, and it
**sways into view when you turn or hit bumps**. Distracting, obstructive, and hard to read on the
move. **After you buy it:** locked into a corner, small, static, no jiggle.

Why this is the strongest upgrade on the list:
- The player **experiences the problem before being sold the solution**, so a mundane-sounding
  purchase becomes an obvious relief. Ideal first purchase — it teaches the whole economy.
- It is thematically exact: you are crashing *because* you are checking the delivery app. The
  phone actively making you a worse driver wires straight into the insurance/complicity engine
  (glance at an offer → clip a van → the insurance "woop" chirps at you about the fine).
- It gives offer triage a **real cost in attention**, so the multi-job juggling has teeth beyond
  arithmetic.
- Cheap to build: an attach socket + spring/lag on the phone actor, swapped for a fixed corner
  transform once owned.

**Accessibility caveat (agent):** phone sway will be unreadable or nauseating for some players.
There must be a **settings toggle to stabilise the phone independent of the upgrade** — never
gate an accessibility need behind an in-game purchase. Same family as the story-mode question.

**Optional variant worth considering:** before the mount, require *holding a button to raise the
phone*, so checking offers physically takes a hand off the wheel. Stronger tension, but risks
being annoying rather than characterful — prototype before committing.

## Decals (EARNED, never purchased)

Applied at the spray station (`BP_VehSprayStation`). Cash stays survival pressure; decals are the
exploration/story reward track. Some tied to achievements, many **hidden** or gated behind side
quests.

- Business decals: **ShellStop** skateboarding taco, **Ness Mart** Loch Ness monster, **Bushed
  Baby** galago, **Nucleo** corporate mark, the bakery, the Schiavone butchers
- Achievement-style: X deliveries, a perfect (no-damage) day, a full VIP chain
- Hidden: found by exploring the island, no announcement
- Side-quest: the dog's paw print (after the translation collar), the arsonist's… something
- **Corrupted variants** of any of the above, appearing only as the town degrades

## Paint / colors

- Base palette for the jeep (spray station already prototyped)
- Rarer colors as rewards rather than purchases? (open question — decals are earned; colors could
  go either way)

## Characters and businesses

Maintained in memory, not here: see `memory/island-world-bible.md` for the full roster (MC, the
arsonist, the Mailman, Pam at The Bushed Baby, the cat man, the dog, the Schiavone butchers, the
baker girls, the Christian girl, the hipster) and the companies (Ness Mart, ShellStop, Nucleo,
The Bushed Baby).

**Passenger candidates** (for the Uber-style app — a captive audience is the best lore delivery
we have): the arsonist, the Mailman, the dog (post-collar), Pam off-shift, a Nucleo employee.

## VIP / story deliveries

Special opportunities that carry the plot. Rules: usually **no timer**, **taken alone**, exempt
from the offer queue. Candidates:

- The package that unleashes the evil (the player delivers it unknowingly; later found in their
  own delivery log)
- The dog's translation collar
- Mailman confrontation beats
- The arsonist's escalating water-bottle orders (arguably a recurring VIP chain)
