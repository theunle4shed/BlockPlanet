# Matriz de contenido — M0

Fecha: 2026-09-27. Inventario de áreas de contenido para la referencia M0. Incluye rutas documentadas y escenas ejecutadas; la última columna conserva la cobertura exhaustiva de regresión M3. `ROM` significa etiqueta localizada en el dump validado, no función ejecutada. No se afirma haber completado todas las etapas o escuchado todas las pistas.

## Modos y rutas

| Elemento | Evidencia | Cobertura pendiente |
|---|---|---|
| Introducción / título / demo | Introducción y menú observados en ares | Ciclo completo, salto, música |
| Training | ROM `0xc7d44`; BASICS, ADVANCED, PUZZLE, BATTLE, RULES, LINES cerca de `0xc7f40` | Menú real: BASICS, ADVANCED, PUZZLE; las tres rutas abiertas. Advanced: Crystal Pieces, X-value, Gravity/Fuse Combo con demo y contador observados. BATTLE/RULES pertenecen al submenú VS; no son seis rutas iniciales de Training |
| Rescue | ROM `0xc7d50`; manual | Inicio/giro/pausa observados; progresión/final pendientes; manual describe 100 etapas |
| Hide & Seek | ROM `0xc7d58`; manual; sesión M0 | MULTI1:1 y DRILL1:2, instrucciones de descubrir imagen/buscar cinco taladros, tablero y pausa GOAL observados. Otras variantes y final: regresión M3 |
| Puzzle | ROM `0xc7d64`; manual | Puzzle1 resuelto: deslizar pieza inferior izquierda a la derecha y eliminar; transición a Puzzle2 observada. Reinicio/cámara pendientes; manual describe 100 puzzles |
| Time Trial | ROM `0xc7d6c`; manual | Inicio/reloj/pérdida de vidas/totales observados; ronda completa pendiente |
| Vs CPU | ROM `0xc7d78`; tablero VS y rival observados | Esferas/HUD, eliminación del rival y pausa observados; dificultades/finales pendientes |
| Vs Player | Ruta observada VS → BATTLE/RULES → 2 PLAYERS → nombres P1/P2 | Nombres/bots/core area y configuración pieces/layers/rounds accesibles; tablero de dos esferas y entrada por dos puertos observados. Mandos físicos/reconexión pendientes |
| Practice | ROM `0xc7d30`; manual | Menú observado: start/default/pieces (seis tipos), layers (4 inicial), core area (15 inicial); se modificó selección de piezas. Escena jugable, cambios de piezas, bad drops y pausa/salida observados; rangos completos pendientes |
| Lines | ROM `0xc7d80`; NEW NAME LINES ejecutado | Apareció nueva fila LINES en Single; reglas y tablero1:1 abiertos. No es un minijuego 2D inferido del nombre: usa esfera/núcleo. Finales: regresión M3 |

El [manual transcrito](https://www.world-of-nintendo.com/manuals/nintendo_64/tetrisphere.shtml) corresponde a la edición australiana `NUS-NTTP-AUS`. Sirve de pista documental; cada diferencia debe contrastarse con NTPE. Resumen documental: Rescue libera robots; Hide & Seek alterna objetivos; Puzzle limita acciones; Time Trial puntúa durante cinco minutos; VS enfrenta jugadores o CPU. El manual describe Training, Practice, opciones y progreso en cartucho.

## Opciones, contenido y desbloqueos

| Área | Evidencia en ROM | Verificación pendiente |
|---|---|---|
| AUDIO / DATA EDIT / SCORES / CONTROLS | `0xc7d88`–`0xc7da4`; cuatro pantallas abiertas | Inventario funcional debajo; límites, todas las pistas y borrado sobre copia: regresión M3 |
| Nombre, nivel, continuar y cargar | Etiquetas cerca de `0xc7dbc`, `0xc7f0c` | Seis slots; seleccionar slot abre LOAD GAME/DELETE; F persistió tras reinicio y se confirmó LOAD GAME. Límite de nombre/cartucho lleno pendientes |
| Robots | Rocket, Gear, Gyro, Stomp, Wheels, Turbine; Jak en `0xc744c` | Selección, estadísticas, disponibilidad y desbloqueos |
| Contenido especial | VORTEX y G'MEBOY cerca de `0xc7f8c` | Acceso y comportamiento, sin asumir que sean modos normales |
| Dificultad | EASY / NORMAL / HARD cerca de `0xc7b18` | Dónde se aplica y efecto real |
| Música y efectos | Etiquetas de música cerca de `0xc7b30` | Inventario audible completo, loops y transiciones |
| Cadenas, multiplicadores, magia y piezas especiales | Documentación, ROM y demos Advanced | Gravity/Fuse Combo, X2 y power ups observados en demo original; reglas/umbrales de magia y combinaciones exhaustivas: regresión M3 |
| Finales/créditos | CREDITS cerca de `0xc7f84` | Rutas que los activan, contenido tardío |

El inventario cataloga las familias y rutas conocidas; la ejecución exhaustiva de desbloqueos, niveles y contenido tardío requiere regresión M3. No se publican dumps de strings ni recursos extraídos.

### Opciones originales observadas

| Pantalla | Elementos disponibles y comportamiento observado |
|---|---|
| Audio | Balance music/fx, selección AUTO/RANDOM/CHOOSE y selector de pista; entrada inicial PROPHETIC - TITLE, cambio a AZULE LUX observado. Todos los loops y extremos del balance requieren regresión |
| Data Edit | Seis slots; F en slot1. A abre LOAD GAME/DELETE. Acción adicional «clear cartridge data». No se ejecutó el borrado sobre el fixture de progreso |
| Scores | Selector de categoría y cinco entradas de ranking visibles. Rescue muestra puntuación; Puzzle muestra tiempo. Los rankings completos por modo se conservan como regresión |
| Controls | Diagrama de MOVE/DROP/SLIDE/PAUSE/MAGIC; pantalla informativa del juego. Las asignaciones de teclado usadas en M0 pertenecen a ares |

La selección VS permite nombres y robots por jugador, core area, pieces, layers y rounds; el inicio observado usa 15 de core area y 4 layers. Practice expone start/default/pieces/layers/core area. Esas configuraciones forman parte del contenido original y deberán conservarse en el port aunque sus combinaciones completas se prueben en M3.

### Reglas y variantes catalogadas

El manual cataloga piezas normales, power pieces, wild card y piezas oscuras de VS; combos de 3–19, magia desde 20 y X-value hasta 20. Magia: Firecracker, Bundle O' Dynamite, Electro Magnet, Atom, Bomb, Ray Gun. Son umbrales/objetos documentados, no efectos ejecutados completos.

Robots: Rocket, Gear, Gyro, Stomp, Wheels y Turbine observados en selección; Jak también está catalogado en ROM/manual. Su disponibilidad concreta queda por contrastar; no se supone que esté bloqueado. El manual distingue speed y power; no se deduce el acceso a Jak de sus estadísticas.

## Guardado

El [catálogo técnico de Mupen64Plus](https://github.com/mupen64plus/mupen64plus-core/blob/b20b27ebf9e5b099a978e86dba609111dc98c837/data/mupen64plus.ini) identifica `SaveType=Eeprom 4KB`. Se conserva la nomenclatura del catálogo; capacidad efectiva, tamaño del archivo y protocolo se comprobarán en emulación y llamadas del juego. No se confunde una etiqueta de catálogo con una medición.

Rutas de la referencia prevista: `.local/m0/saves/`; capturas en `.local/m0/captures/`. La sesión controlada creó un archivo EEPROM de 512 bytes al registrar el nombre F. Capacidad medida: 4 Kbit. Comprobado: crear F → resolver Puzzle1 → llegar a Puzzle2 → salir normalmente → EEPROM512 bytes/hash → reiniciar proceso → slot1 F → LOAD GAME → continuar Puzzle nivel2. Pendiente para regresiones posteriores: segundo nombre y borrado sobre copia. Se observaron seis slots; falta comprobar seis progresos independientes, Controller Pak y expansiones. Los savestates privados pueden facilitar estados tardíos, pero no sustituyen la validación de progreso normal.

## Accesos especiales documentados para regresión

La [guía contemporánea de M. Stoler (1997)](https://gamefaqs.gamespot.com/n64/198946-tetrisphere/faqs/3332) registra nombres especiales en NEW NAME: CREDITS (créditos), LINES (minijuego), G + icono alienígena + MEBOY (música especial), y una secuencia de cinco iconos (planeta, nave, cohete, corazón, calavera) para selección de niveles. Menciona también VORTEX. Las etiquetas CREDITS/LINES/VORTEX y G'MEBOY están localizadas en la ROM objetivo; esto permite incluirlas en el inventario sin afirmar que se hayan ejecutado. La guía distingue el progreso obtenido con selección de nivel del progreso normal, por lo que esa ruta no servirá como prueba de guardado de campaña.

La misma fuente describe la combinación L + C-right + C-down para revelar iconos en el editor; su descripción inicial usa R, una inconsistencia que debe resolverse con la ROM. No se codifica una combinación supuesta en el port. El inventario conserva el acceso como pendiente de contraste funcional.

El aporte identificado de Baseman en [GameFAQs](https://gamefaqs.gamespot.com/n64/198946-tetrisphere/cheats) describe VORTEX como acceso a una animación en bucle mientras se mantiene Reset. Se cataloga como comportamiento de reset, no como modo normal ni desbloqueo de Jak. Falta contrastarlo en NTPE; no se aplica un reset para sustituir la prueba de guardado normal.

## Gameplay variables per mode (BlockPlanet, English, started 2026-11-09)

The catalogue for the roadmap entry "One game, many rulesets": every gameplay variable, where the game keeps it, its value in each mode, and whether it changes per stage. Descriptions and addresses only (NTPE rev0); values that come from the game's level records are described, never copied (AGENTS.md). Filled in as the direct-launch work finds what each mode sets before play ([blockplanet-game-state.md](blockplanet-game-state.md#starting-modes-directly-2026-11-09)). Sources: L = [level data](blockplanet-level-data.md), G = [game state](blockplanet-game-state.md), D = the mode page's confirm handler (`func_8004127C` at `0x800462D0`) read on 2026-11-09.

Modes by their number at `0x800E44A8` (s16): 1 Rescue (Story), 2 Time Trial (BlockPlanet's Score Attack), 3 Training, 4 Puzzle, 5 Hide & Seek, 6 VS CPU, 7 (set by another original page, not yet identified), 9 Lines, 10 the attract demo (started after the main menu sits idle; the demo also runs as mode 3 with `0x80113323` = 1), 12 (returns to a menu; not identified).

**Coverage so far:** of the owner's 16 named variables, 6 have an address and per-mode values (layers, objective in part, single stage or episode, lives, playfield size, opponent settings in part), 4 are partly known (block types, core to uncover, hidden pictures, default music), and 6 are still to find (background effect, drop timer speed, shot time limit, score and speed goals, friend inside, opponent intelligence per stage). Rescue (Story) is the best covered; Lines, Puzzle and Hide & Seek the least.

### Per stage or per mode

Each stage has a level record (L): 20 bytes, with flags, the playfield layers, block colours and a size byte, and goal text in goal levels. So everything held in that record is **per stage** already in the original; what the mode page or the game code sets is **per mode**. Record groups (L): Rescue 15 chapters × 10 levels; probably Score Attack's spheres; probably Hide & Seek (5 stages of 30); further groups not yet identified.

### The owner's variables

| Variable | Where the game keeps it | Per mode | Per stage? |
| --- | --- | --- | --- |
| Playfield layers | Level record byte 3 (L), copied to the level structure (`0x8013D170` + 0xC, `0x8013D17C`); the board has 9 layers of 32 × 32 cells (G, "Layers, core size and stacking") | Rescue: rises with the chapter (chapter 1 one layer above the core, chapter 10 up to six). Score Attack: 3 in the sphere records seen. Others: to read from their records | Yes (record) |
| Block types | Block values in the board tables (`0x800E44D0`, 2 bytes per cell); colour count in the level record (L) | Rescue: colours rise with the chapter; special blocks (value 7, probably buried bonuses) from 1:2 on; the core is value 10 (L). Crystal pieces and power pieces exist (Training Advanced, manual). Per-mode use to catalogue | Yes (record) |
| Objective | Mode code, plus the record's flags (`0x10`/`0x20` add goal text) and group byte (L) | Rescue: uncover an S × S square of the core (context `+0x23E4`). Time Trial: score in a clock. Puzzle: clear with limited moves. Hide & Seek: per-stage goals (MULTI, DRILL, core, SHIFT, CONNECT, TOWER, …). VS CPU: win the rounds. Lines: to describe | Yes (record) |
| Single stage or episode | Mode; levels per chapter from `func_80089D04` (returns 5 for Rescue, 3 for Hide & Seek and VS CPU); chapter `0x800E44A0`, level context `+0x23BA` | Rescue: episodes of 10 levels (chapter-end menu after the tenth). Hide & Seek: its own chapter counter, not yet found. Puzzle: one puzzle after another (number in `+0x23BA`). Time Trial: spheres one after another until the clock ends | Per mode |
| Background effect | Not found yet | — | — |
| Default music | The song playing is the music player `0x80107290` + `0x157C` (G, 2026-11-09); level songs change from `func_800906E8` | Story 1:1 plays song 1. Others to record | Probably (changes between levels) |
| Hidden pictures | Hide & Seek's goal pictures (G, "Hide & Seek goals and their pictures") | Hide & Seek only | Yes |
| Core breaks / how much to uncover | Context `+0x23E4` = 2 × record byte 12, kept between 2 and 10: the side of the core square to uncover (G, "The core opening a level needs"); VS CPU adds a chapter-based amount first; 4, 6 or 8 also set `0x8015F4F0` to 0, 1 or 2 | Rescue, VS CPU (core area). Hide & Seek: "multiple cores" goals suggest separate core objects | Yes (record) |
| Lives | Hearts, context `+0x2D7E` (G) | 3 at the start in Rescue, Time Trial, Hide & Seek; refilled at each retry; VS CPU refills each round | Per mode |
| Drop timer speed | Candidates around context `+0x2DFC`–`+0x2E08` (values for 20, 10 and about 8 s), unconfirmed (L) | — | Probably |
| Shot time limit | Not found yet | — | — |
| Score and speed goals | Time Trial clock context `+0x2B0C`: starts at 18,059 frames (about 5:01) in mode 2 and counts down; modes 4, 5 and 7 count up (G, "The original Time Trial's clock") | Time Trial: clock and sphere bonus (`func_800C7C90`). Others: to find | Per mode so far |
| Friend inside | The rescued robot (Rescue's chapter-end scene); not yet located in memory | Rescue | Per chapter |
| Playfield size and shape | Fixed: 9 layers × 32 × 32 cells round a sphere (G) | All modes | No (engine limit) |
| Opponent intelligence | Not found yet. The computer is player 2: its robot is the level number (player 2's context `+0x3102`) and its name comes from the table at `0x800E08B4` by level; its skill is still to find, probably per level | VS CPU | Owner wants per stage |

### What the mode page sets per mode (D)

All modes: `0x800DFD4C`, `0x8015D980`, `0x800DFD50`, `0x800DFD54` set to 1, the level counters, stage and chapter cleared, then the mode. Player 1's port, character, name and `0x80113323` come from the player page.

| Mode | Extra settings at the start |
| --- | --- |
| Rescue (1) | None. With saved progress the game opens another page first (`0x800E1232`). |
| Hide & Seek (5) | None at the confirm. After the hand-over the original shows an intro page (`0x800E1234`, `func_8006224C`: 3D scenes, presentation only). Play opens with its own introduction: a flight round the sphere, the stage's goal word in 3D letters ("MULTI" for 1) and the goal card ("Any Picture": dig and uncover any picture to win). Song 1 at the start. |
| Lines (9) | `0x800DFD4C` = 2 (meaning unknown). Opens on its rules pages, which are part of play (HUD up). Song 1 at the start. |
| Puzzle (4) | None at the confirm. The puzzle number is the level (`0x800E449C` before the set-up, context `+0x23BA`); the chapter stays 0. Every mode's row is also saved (`0x80102B07`, from the mode page's row memory `0x800E0756`). |
| Time Trial (2) | None at the confirm; skips the saved-progress check. |
| VS CPU (6) | Then the players page (`0x800E122A`, `func_80050BFC`): both players' ports, characters and names from the per-player tables at `0x800E07CC`, `0x800E07D4`, `0x800E07E4` and `0x800E0784` (9 bytes each), and context `+0x23E2` (the core opening's setting) from `0x800E07DC` (5 for every player by default). At the confirm: `0x800E44A4` = 1, `0x8015D980` = 2, player 1 context `+0x23D6` = 1, player 2 context `+0x23D6` = 0, `0x800E08A8` = 1, `0x800E08AC` = 0, `0x800E08B0` = 0, `0x800E088C` = 0; level counters cleared again at the hand-over. Practice and VS player menus also offer pieces, layers, core area and rounds (above). |
| Training (3) | From the Training page: training type `0x800E44AC` (1 Basics, 2 Advanced, 3 Puzzle), player 1's port from the first connected controller, `0x80113323` = 0 (1 marks the attract demo), the name at `0x800ECC4C`, character 4, and bit 0 of the save's first byte set and written. The game's own jump (`0x800DFE8C`, from a game over) sets `0x800DFDFC` = 1 and another name instead. |

### What only one mode has

- **Rescue:** robots to rescue, a message scene after each chapter, endings after chapters 5 and 8, bonus episodes 9 and 10 (G).
- **Time Trial:** the countdown clock and the doubling sphere bonus (G).
- **Training:** the instructor's text pages and demos; a SKIP item in the pause menu (G).
- **Puzzle:** the puzzle number in the HUD's level badge and a purple border on the board (G).
- **Hide & Seek:** goal pictures and goal text per stage; its own chapter counter.
- **VS CPU:** two players' contexts, rounds, and the second player's settings above.
- **Lines:** a rules screen in play before the first piece ("Lines Rules": line up three bricks across or down to remove them; clear enough of the core for the friend inside to escape).
