# Training script review sheet

For the owner's review (2026-11-20): every screen of the Training lessons in order, each with a marker, what it says (summarised in BlockPlanet's own words; the game's own text stays in the player's ROM), and what moves it on. Mark any screen to reword, cut or move by its marker; BlockPlanet can replace any screen's words with its own (`tetrisphere::text_rewrite`).

**How a screen moves on** (from the codes in each lesson's text, `src/platform/linux_main.cpp`):

| Mark | Meaning |
| --- | --- |
| **A** | Waits for the player to press A (the panel shows "More") |
| **Demo** | A demonstration plays by itself, then the lesson moves on (the panel shows "Wheels is showing you…") |
| **Action** | A scripted action starts as the screen opens (the arrow appears, the camera or the tutor moves); the next **Action end** stops it. No text of its own |
| **Ends** | The lesson's last screen: it moves on to the next lesson in its chain (Basics: Slide and Drop → Lives → Combos → Earning Magic; Advanced: Crystal Bricks → X Count → Fuse Combos), or ends Training after the last |

**Things that apply throughout:** the cards say "ball" where BlockPlanet says "sphere"; the tutor (Wheels) is no longer drawn, but he still introduces himself and the demo hint still names him; lesson-end lines say "next level" or "last level" where they mean the next lesson.

## The Basics

### Slide and Drop (`SD`)

| Marker | Says | Moves on |
| --- | --- | --- |
| SD-01 | (no text) | Action |
| SD-02 | Heading "Training": page through the text with Down or A | A |
| SD-03 | (no text) | Action end |
| SD-04 | The tutor introduces himself by name and says he will show the basics | A · *mentions Wheels* |
| SD-05 | (no text) | Action (the arrow at the shadow appears) |
| SD-06 | The dark shape on the ball is your shadow; it shows where you are | A · *the flashing arrow is on screen here* |
| SD-07 – SD-08 | (no text) | Action end, Action |
| SD-09 | Move the shadow with the control pad | Demo |
| SD-10 – SD-12 | (no text) | Action end, Action, Action |
| SD-13 | The shadow has the shape of your current piece | A |
| SD-14 | (no text) | Action end |
| SD-15 | Watch the shadow line up with a piece of the same shape | Demo |
| SD-16 | (no text) | Action end |
| SD-17 | When the shadow is lined up, that piece can be moved: sliding | A |
| SD-18 | (no text) | Action |
| SD-19 | How to slide, four numbered steps: line up, hold B, move, let go | Demo |
| SD-20 | (no text) | Action end |
| SD-21 | Heading "Removing pieces": to pass levels you remove pieces from the ball | A |
| SD-22 | First line up two matching pieces, for example by sliding | Demo |
| SD-23 | (no text) | Action |
| SD-24 | The shadow changes when the two pieces line up | Demo |
| SD-25 – SD-26 | (no text) | Action end, Action |
| SD-27 | With two lined up, A drops a third onto them | Demo |
| SD-28 – SD-29 | (no text) | Action end, Action |
| SD-30 | With the third added and lined up, the pieces are removed | Demo |
| SD-31 | (no text) | Action end |
| SD-32 | An upbeat line: on to the next level | Ends → Lives |

### Lives (`LV`)

| Marker | Says | Moves on |
| --- | --- | --- |
| LV-01 | (no text) | Action |
| LV-02 | Heading "Lives": the red hearts are your lives; you have three | A |
| LV-03 – LV-04 | (no text) | Action end, Action |
| LV-05 | Dropping a piece where two are not together costs a life | Demo |
| LV-06 – LV-07 | (no text) | Action end, Action |
| LV-08 | The piece was stacked on the ball where the shadow was | A |
| LV-09 – LV-10 | (no text) | Action end, Action |
| LV-11 | Heading "Game objective": uncover a picture on the ball's core | A |
| LV-12 – LV-13 | (no text) | Action end, Action |
| LV-14 | You do it by removing every piece over the picture | A |
| LV-15 | (no text) | Action end |
| LV-16 | Watch the picture being uncovered | Demo |
| LV-17 | Most levels need pieces removed several layers deep | A |
| LV-18 | An upbeat line: on to the next level | Ends → Combos |

### Combos (`CB`)

| Marker | Says | Moves on |
| --- | --- | --- |
| CB-01 | Heading "Combos": three or more pieces removed together make a combo | A |
| CB-02 | Watch a combo of six | Demo |
| CB-03 | (no text) | Action |
| CB-04 | The number six on screen is your combo counter | A |
| CB-05 | (no text) | Action end |
| CB-06 | The counter adds up the pieces breaking in all current combos | A · *spelling "tallys"* |
| CB-07 | (no text) | Action |
| CB-08 | Combos of under 20 pieces give power pieces | Demo · *"less than" (fewer)* |
| CB-09 | (no text) | Action end |
| CB-10 | Power pieces are special: a combo started with one takes longer to finish | A |
| CB-11 | Meanwhile new combos can start, for a bigger count | A |
| CB-12 | Watch a power combo | Demo |
| CB-13 | An upbeat line: now the last level | Ends → Earning Magic · *"last level": Earning Magic is now in the Magic group* |

## Magic

### Earning Magic (`EM`)

| Marker | Says | Moves on |
| --- | --- | --- |
| EM-01 | Heading "Magic": a combo of 20 or more gives a magic | Demo |
| EM-02 | (no text) | Action |
| EM-03 | The icon that appeared after the combo shows your magic | A |
| EM-04 | (no text) | Action end |
| EM-05 | Magic removes many pieces quickly | A |
| EM-06 | (no text) | Action |
| EM-07 | Use the Magic button to use it | Demo |
| EM-08 | (no text) | Action end |
| EM-09 | End of basics training, congratulations, ready to play | Ends (Basics complete) · *in BlockPlanet's list this lesson is under Magic, not The Basics* |

### The six magics (`MG`, BlockPlanet's own lessons)

These are BlockPlanet's, so their words are shown in full and can be changed freely (`magic_lesson_texts`, `src/platform/linux_main.cpp`). Each has one card, then the player fires the magic, then a results panel.

| Marker | Card | Then |
| --- | --- | --- |
| MG-1 Firecracker | "The Firecracker blasts a round patch of the top layer, about four bricks out from your shadow in every direction. Press [Magic] to use it." | Start (A); fire the magic; results "Firecracker Complete": Next, Try Again, Back to Training |
| MG-2 Dynamite | "The Dynamite throws five sticks in a ring round your shadow. Each blows a small hole in the top layer, leaving the middle standing. Press [Magic] to use it." | as above |
| MG-3 Magnet | "The Magnet pulls bricks out from under your shadow for about four seconds. Hold it still to dig deep, or move the shadow to dig where you want. Press [Magic] to use it." | as above |
| MG-4 Atom | "The Atom clears the whole top layer of the sphere: the far side first, then the part you can see, from its edge to the middle. Press [Magic] to use it." | as above |
| MG-5 Bomb | "The Bomb blasts a wide patch of the top layer, about six bricks out in every direction, then a second layer in the middle. Press [Magic] to use it." | as above |
| MG-6 Ray Gun | "The Ray Gun drills straight down under your shadow, one layer at a time, for about seven seconds. Move the shadow to drill somewhere else. Press [Magic] to use it." | as above; no Next (last) |

## Advanced

### Crystal Bricks (`CR`)

| Marker | Says | Moves on |
| --- | --- | --- |
| CR-01 | (no text) | Action |
| CR-02 | Heading "Crystal pieces": they are found all round the ball | A |
| CR-03 | (no text) | Action end |
| CR-04 | With the shadow over a group of crystals, they can become a power piece of the shadow's shape | A |
| CR-05 | (no text) | Action |
| CR-06 | You do it with B | Demo |
| CR-07 | (no text) | Action end |
| CR-08 | An upbeat line: on to more advanced techniques | Ends → X Count |

### X Count (`XC`)

| Marker | Says | Moves on |
| --- | --- | --- |
| XC-01 | (no text) | Action |
| XC-02 | Heading "X count": the number in the coloured box | A |
| XC-03 | (no text) | Action end |
| XC-04 | It multiplies your combo score | A |
| XC-05 | In VS it also raises what is sent to the opponent | A |
| XC-06 | There are several ways to raise it; gravity combos are one | A |
| XC-07 | Heading "Gravity combos": a piece falling under gravity can make a combo | A |
| XC-08 | The tutor sets up a simple gravity combo | Demo |
| XC-09 | (no text) | Action |
| XC-10 | The piece on top falls if the one beneath is moved | Demo |
| XC-11 – XC-12 | (no text) | Action end, Action |
| XC-13 | The fallen piece makes a combo, so those pieces break | Demo |
| XC-14 – XC-15 | (no text) | Action end, Action |
| XC-16 | The gravity combo raised your X count | A |
| XC-17 | (no text) | Action end |
| XC-18 | Gravity combos can give up to ten X count raises | A |
| XC-19 | An upbeat line: the last advanced level | Ends → Fuse Combos · *in the new list X Count comes after Fuse Combos; this lesson also teaches gravity combos (the list's "Gravity Combos" could point here)* |

### Fuse Combos (`FC`)

| Marker | Says | Moves on |
| --- | --- | --- |
| FC-01 | Heading "Fuse combos": a breaking piece flashes; that is a fuse | Demo |
| FC-02 | (no text) | Action |
| FC-03 | A fuse from a power combo can be slid | Demo |
| FC-04 – FC-05 | (no text) | Action end, Action |
| FC-06 | Slide a fuse into line with two or more pieces and the new combo breaks | A |
| FC-07 | That is a fuse combo | Demo |
| FC-08 – FC-09 | (no text) | Action end, Action |
| FC-10 | Fuse combos can also give up to ten X count raises | A |
| FC-11 | (no text) | Action end |
| FC-12 | Done with advanced training | Ends (Advanced complete) |

## Other Modes

### Puzzle Controls (`PZ`)

| Marker | Says | Moves on |
| --- | --- | --- |
| PZ-01 | Heading "Puzzle training": puzzles have extra controls | A |
| PZ-02 – PZ-03 | (no text) | Action, Action |
| PZ-04 | L and R zoom in and out | Demo |
| PZ-05 – PZ-07 | (no text) | Action end, Action end, Action |
| PZ-08 | C-up starts a puzzle again | A |
| PZ-09 – PZ-10 | (no text) | Action end, Action |
| PZ-11 | Your piece is always a wildcard, usable on any shape | A |
| PZ-12 – PZ-14 | (no text) | Action end, Action, Action |
| PZ-15 | To pass, remove every piece within the slides and drops allowed | Demo |
| PZ-16 – PZ-17 | (no text) | Action end, Action end |
| PZ-18 | An upbeat line: have fun with puzzles | Ends (Training) |

## Points to decide (collected)

1. **SD-04 and the "Wheels is showing you…" hint** now name a tutor who is not shown.
2. **CB-13 ("last level") and EM-09 ("end of basics")**: Earning Magic is now under Magic in the list.
3. **XC teaches gravity combos**, which the list shows as its own lesson (Not started): point "Gravity Combos" at X Count's pages, or keep it for a BlockPlanet lesson.
4. **The order Advanced plays in** (the game's chain: Crystal → X Count → Fuse) differs from the list's new order (Gravity, Crystal, Power, Fuse, X Count).
5. **Wording**: "ball" or "sphere"; CB-06's spelling; CB-08's "less than".
6. **SD-06**: the flashing arrow is still on screen here (being fixed).
