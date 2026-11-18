# Developer tools

## RT64's inspector (the owner's request, 2026-11-11)

For finding out what the game draws and where: behind the sphere, the tunnel, the backdrop.

**Start it:** choose **7 Play with developer tools** in `BlockPlanet.bat`, or start the game with `--developer`. Normal play never shows it.

**Frozen at start:** choose **9 Developer tools, frozen at start** (or `--developer-frozen`): the game starts held before its first frame, with the inspector already open, to step through the intro.

**Stepping the game (BlockPlanet's, with developer tools on):** RT64's own Pause (F4) only freezes the picture; the game runs on behind it. These hold the game itself:

| Key | What it does |
| --- | --- |
| **F6** | Freezes the game, or lets it carry on. |
| **F7** | One frame on (hold it for frame after frame). Freezes first if it was running. |

While frozen, RT64's picture is paused on the last frame, so the free camera and the draw-call list work on it. The first frames after start are black (the console starting up): hold F7 to reach the N64 logo. If the inspector window has the keyboard (its text box or list was clicked), click the game picture first so F6 and F7 reach the game.

**In the game:**

| Key or mouse | What it does |
| --- | --- |
| **F1** | Opens or closes the inspector (a "Game editor" window over the game). While it is open, the mouse and keyboard work the window. |
| **Debugger** tab | The tools below. |
| **F4** (or Pause in the Debugger tab) | Freezes the picture on one frame, to look around it; again to carry on. |
| **Right click** on the game | Picks the shape drawn there: the Debugger tab then shows that draw call (its texture, matrices and settings); hovering over a draw call in the list lights it up in pink on the picture. |
| **Free Camera Enabled** (Debugger tab) | Lets you move the camera of the frame's first 3D scene (this RT64 version has no scene choice). |
| **Middle mouse drag** | Moves the free camera sideways and up and down. |
| **Ctrl + middle mouse drag** | Moves it forwards and back. |
| **Alt + middle mouse drag** | Turns it, to look behind things. |

The **Configuration** tab changes the picture's filtering for this session only.

Notes:
- F2 and F3 are RT64's own shortcuts while developer tools are on (ray tracing, a memory view); BlockPlanet's F2 memory dump for research is not available then.
- There is no wireframe view; the free camera and the draw-call list answer "what is back there?". A wireframe mode would be a new drawing mode in RT64.
