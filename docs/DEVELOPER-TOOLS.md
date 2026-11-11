# Developer tools

## RT64's inspector (the owner's request, 2026-11-11)

For finding out what the game draws and where: behind the sphere, the tunnel, the backdrop.

**Start it:** choose **7 Play with developer tools** in `BlockPlanet.bat`, or start the game with `--developer`. Normal play never shows it.

**In the game:**

| Key or mouse | What it does |
| --- | --- |
| **F1** | Opens or closes the inspector (a "Game editor" window over the game). While it is open, the mouse and keyboard work the window. |
| **Debugger** tab | The tools below. |
| **F4** (or Pause in the Debugger tab) | Freezes the picture on one frame, to look around it; again to carry on. |
| **Right click** on the game | Picks the shape drawn there: the Debugger tab then shows that draw call (its texture, matrices and settings); hovering over a draw call in the list lights it up in pink on the picture. |
| **Free Camera Enabled** (Debugger tab), then a scene in **Free Camera Scene** | Lets you move the camera of that 3D scene (the sphere, the tunnel, the intro). |
| **Middle mouse drag** | Moves the free camera sideways and up and down. |
| **Ctrl + middle mouse drag** | Moves it forwards and back. |
| **Alt + middle mouse drag** | Turns it, to look behind things. |

The **Configuration** tab changes the picture's filtering for this session only.

Notes:
- F2 and F3 are RT64's own shortcuts while developer tools are on (ray tracing, a memory view); BlockPlanet's F2 memory dump for research is not available then.
- There is no wireframe view; the free camera and the draw-call list answer "what is back there?". A wireframe mode would be a new drawing mode in RT64.
