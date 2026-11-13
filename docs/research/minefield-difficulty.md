# Minefield on the sphere: how hard each arena is

Measured 2026-11-13 with `tools/minefield/difficulty.cpp` (the owner's question: "test them in the cloud to work out how their difficulty scales"). A solver plays each arena hundreds of times seeing only what a player sees: the numbers uncovered and its own flags, never the mines. It makes every certain move (a number's flags complete, or its covered cells all mines; two numbers whose cells are one inside the other) and guesses the safest-looking cell only when nothing is certain. A good player can do a little better (longer chains of reasoning, counting the mines left at the end), so the win rates are a cautious estimate of how often a careful player wins; the rest of the losses are forced guesses, the luck the owner accepts as part of the game.

    g++ -O2 -std=c++20 -Iinclude tools/minefield/difficulty.cpp src/platform/minefield.cpp -o difficulty
    ./difficulty 2000            # the arenas as they are
    ./difficulty 600 --sweep     # win rate at several mine densities
    ./difficulty 600 --sweep --hints   # with one empty cell uncovered in each separate area at the start

## The arenas as they are (300 games each)

| Arena | Cells | Mines | Density | Won | Won with no guess | Guesses a game |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Full | 1,024 | 130 | 12.7% | 95.7% | 93.7% | 0.12 |
| Half | 256 | 32 | 12.5% | 92.7% | 76.7% | 0.32 |
| Mini | 144 | 20 | 13.9% | 90.0% | 70.3% | 0.48 |
| Crossover | 540 | 69 | 12.8% | 91.7% | 84.7% | 0.25 |
| Decompartmental | 576 | 73 | 12.7% | 29.3% | 0.0% | 5.18 |
| Bow | 512 | 65 | 12.7% | 88.0% | 66.7% | 0.52 |
| Horizon | 640 | 81 | 12.7% | 94.0% | 83.3% | 0.31 |

## Win rate by mine density (600 games each)

| Arena | 8% | 10% | 12.5% | 15% | 17.5% | 20% | 22.5% |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Full | 100.0% | 99.5% | 96.8% | 94.5% | 89.0% | 77.8% | 52.0% |
| Half | 98.5% | 97.7% | 92.5% | 86.5% | 68.7% | 49.5% | 19.0% |
| Mini | 99.0% | 98.3% | 95.0% | 88.8% | 77.2% | 52.8% | 35.3% |
| Crossover | 99.3% | 97.5% | 92.8% | 86.0% | 68.2% | 47.5% | 14.2% |
| Decompartmental | 64.0% | 50.8% | 32.7% | 15.5% | 6.2% | 1.3% | 0.5% |
| Bow | 97.5% | 95.3% | 90.3% | 80.8% | 61.0% | 36.8% | 13.8% |
| Horizon | 99.5% | 98.2% | 94.8% | 88.7% | 75.7% | 53.0% | 21.3% |

With one empty cell uncovered in each compartment at the start, Decompartmental becomes 93.0%, 82.5%, 65.7%, 43.2%, 16.7%, 4.8% and 0.5% at the same densities.

## What it shows

- **Density sets the difficulty, and the curve is steep:** about 12.5% is easy (90% or more won), 17.5% medium (60 to 90%), 20% hard (35 to 80%), 22.5% very hard. For comparison, the classic game's beginner, intermediate and expert boards are about 12%, 16% and 21%.
- **The whole sphere is the easiest shape** at any density: with no edges, every cell has eight neighbours, so there are no corners to guess in. Arenas with more edge (Bow, Crossover, Half) are harder at the same density.
- **Decompartmental is in a class of its own:** its compartments are sealed from one another, so the numbers in one say nothing about the next, and each compartment starts with a blind guess (about five a game). The owner's view (2026-11-13): that luck is part of Minefield; the tactic is to make a start in every compartment before finishing any. One uncovered cell per compartment (`--hints`) would make it about as hard as the others, if wanted later.
- **For progression:** the same arena could come in levels by density (for example 12.5%, 15%, 17.5%, 20%), and the arenas ordered by how much edge they have.

## The counts chosen (2026-11-13)

The owner's call: the arenas in the menu in order of size, each a little harder than the last. Each arena's mine count was found with `--calibrate` for a win rate falling evenly from 90% (Mini) to 50% (Full), then checked over 2,000 games:

| Arena | Cells | Mines | Density | Won | Won with no guess | Guesses a game |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Mini | 144 | 21 | 14.6% | 90.0% | 65.0% | 0.58 |
| Half | 256 | 39 | 15.2% | 84.7% | 56.9% | 0.81 |
| Bow | 512 | 80 | 15.6% | 76.2% | 48.5% | 1.06 |
| Crossover | 540 | 94 | 17.4% | 73.5% | 46.5% | 1.30 |
| Decompartmental | 576 | 47 | 8.2% | 61.5% | 0.0% | 4.39 |
| Horizon | 640 | 124 | 19.4% | 58.8% | 30.6% | 2.34 |
| Full | 1,024 | 231 | 22.6% | 50.9% | 27.1% | 3.08 |

Decompartmental keeps its place in the curve with far fewer mines: its blind starts make it hard on their own.

    ./difficulty 800 --calibrate Mini=90 Half=83 Bow=77 Crossover=70 Decompartmental=63 Horizon=57 Full=50
