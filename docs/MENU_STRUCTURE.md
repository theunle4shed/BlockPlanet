# Menu structure and unlocking (the owner's plan, 2026-11-14)

The owner's picture of how the menus grow as the player progresses. It replaces the menus' current layout (all modes open from the start, Solo > Rescue > Campaign / Casual / Time Attack / Score Attack). Single player is settled first; two-player and online follow.

## First launch

1. **Choose a profile or play as a guest.**
2. **"Do you want to go through basic training?"** Yes: Training (Basics) starts. No: the main menu.

## The menus as they open up

| When | What the menus show |
|---|---|
| **From the start** | Main menu: **Solo**, **Training**, **Settings**, **Profiles**. Solo holds only **Rescue**, which launches **Episode 1**. |
| **Episode 1 completed** (shown from the next time the game starts) | Solo > Rescue offers **Campaign** (picks up at the next stage not yet completed) and **Score Attack**. |
| **Episode 3 completed** | **Time Attack** joins Campaign and Score Attack in the same menu. |
| **Episode 5 completed** | **Solo > Adventure** opens. |
| **In Adventure** | Adventure introduces new game modes one at a time. Once a mode's introductory level is completed, it is available in **Solo > Bonus** and **Multi > Bonus**. |

- **Campaign** means Rescue's episodes played through in order (the owner's word for the whole run); it remembers the player's progress and goes straight to the next stage not yet completed.
- **Lines** is not locked behind finishing Rescue, as it was in the original.
- **Awards along the way:** completing parts of Rescue can also unlock themes and other rewards.

## Two players and online (to decide once single player is done)

- **Multi:** how stages are unlocked for two players is still to decide.
- **Online:** the host picks from the game modes they have unlocked.
- **Multi > Bonus** gets the same modes as Solo > Bonus as Adventure unlocks them.

## Compared with today

| Today | The plan |
|---|---|
| All modes open from the start (Solo: Rescue, Puzzle, Hide & Seek, Lines, Minefield) | Only Rescue at first; the rest arrive through Rescue and Adventure |
| Training asked once, before the first Rescue game | Asked once, at the first launch, for every new profile and guest |
| Rescue > Campaign, Casual, Time Attack, Score Attack, all open | Rescue > Campaign and Score Attack after Episode 1, Time Attack after Episode 3 |
| Campaign opens an episode list, then a stage list | Campaign goes straight to the next stage not yet completed |
| No Adventure, no Bonus | Adventure after Episode 5; Bonus filled from it |
| Training inside Rescue's menus (X) | Training on the main menu |

## The owner's answers (2026-11-14)

- **Puzzle and Hide & Seek** have no menu items of their own in the final menus: their games (the puzzles, Hide & Seek's goal types) appear in Bonus as Adventure introduces them.
- **Casual** stays, in Rescue's menu beside Campaign; its episodes open once played in Campaign.
- **Lines** is introduced by Adventure, then in Solo > Bonus and Multi > Bonus.
- **Replaying stages:** Campaign only continues; earlier episodes and stages are replayed in Time Attack and Score Attack.
- **Existing profiles** keep what they have unlocked: unlocks are worked out from the progress already saved.
- **Testing:** no separate "everything unlocked" choice. The main build shows the whole menu as if unlocked, each item with its development status in brackets: (Not started) in red, (In progress) in amber, (Viable) in green. `BLOCKPLANET_LOCKED=1` applies the unlocking, for testing it.

## Development status shown in the menus (2026-11-14)

| Menu | Viable | In progress | Not started |
|---|---|---|---|
| Main | Solo, Profiles, Options, Credits | Local, Trophies | Online, Media Player |
| Solo | Rescue, Hide & Seek, Puzzle, Lines | Minefield | Multiphasic, Adventure, Bonus |
| Rescue | Campaign, Time Attack, Score Attack, Casual | | |
| Local | | Rescue (Versus against the computer) | Lines, Custom, Cooperative, a second player |
| Online | | | Host, Join |

The owner can correct any of these; they are set in each screen's file (`assets/ui/screens/*.rml`, the `rag` notes).

## Built (2026-11-14)

- **First launch:** a new profile (nothing played yet) or a guest's first visit in a session is asked "Do you want to go through basic training?" over the main menu: "Yes, Show Me" starts Training (Basics), "No, Go to the Menu" goes to the main menu. Asked once per profile. The question before the first Rescue game is gone.
- **The menus by progress** (`include/tetrisphere/menu_unlocks.h`, with its own test): the main, Solo and Rescue menus are built by the game. Main: Solo, Training, Profiles, Options, Credits, Exit from the start. Solo: Rescue, which launches Episode 1 straight away until it is completed; then Rescue's menu offers Campaign, Score Attack and Casual; Time Attack after Episode 3; Solo > Adventure after Episode 5. Unlocks follow the progress already saved, so existing profiles keep theirs, and the menus update as soon as an episode is completed.
- **Campaign** carries on from the next stage not yet completed; once every episode is done it offers the episode list.
- **Training** is on the main menu.
- In the main build all of it shows, with each item's status; `BLOCKPLANET_LOCKED=1` shows the menus as a player would see them.
