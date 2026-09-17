# Enjoer — a first-person block world in C99

Enjoer is a first-person voxel block world written in **C99** with no
runtime dependencies: you dig, build and fly around a streamed terrain of
grass, stone, sand, water, logs and leaves, with a 3D hand, diffuse skylight,
flowing water and caves that break the surface. The same code runs on Android
(APK) and in the PC/browser preview.

The app opens on a **Minecraft-style main menu** — flat, non-rounded buttons
for **Play**, **Servers**, **Options** and **Quit** — with the block world
frozen behind a dark overlay. **Play** drops you into the world; the round
button in the top-left corner (or `Esc` / `M`, or the Android back key) opens
the menu again. **Options** sets the sound volume and the render quality;
**Servers** is a placeholder for an offline build. Everything in the app is
English: the bundled font carries Latin only.

## Repository layout

```
assets/        runtime assets: textures/*.png, fonts/*.ttf, sounds/*.wav
game/          Android packaging: AndroidManifest.xml and the Java activity
src/           all C sources
  main.c       Android entry point (native activity)
  engine.h     compact platform/asset/immediate-HUD/lifecycle API
  core/        app state, error handling, logging, asset reads, settings,
               and game.c — the menu/game router every platform calls
  graphics/    2D renderer: primitives, textures, text (+ ttf/)
  sound/       audio: PCM16 mixer and AudioTrack output
  geometrium/  the first-person block world: player, picking, touch layout,
               HUD and the 3D hand (on the voxel renderer)
  engine/      the software 3D/voxel render backend (render/) plus the
               scene-graph engine (eng_*, kept for its own test suite but
               not shipped in the app)
third_party/   stb_image / stb_image_write
tools/         preview server, regression tests, offline art tool
stage_assets.py  stages assets/ into the APK asset tree
```

## Quick start (PC / browser)

```sh
tools/preview/build.sh
./preview --port 8090
# open http://localhost:8090 — the main menu appears; tap or Enter on Play
```

Default frame is **960×540**; `--w`, `--h`, `--port`, `--assets`, `--storage`
are available. The server listens on `0.0.0.0` and the page uses relative
URLs, so it works through an external HTTPS proxy.

## Controls

Main menu: `↑`/`↓` (or `W`/`S`) move the selection, `Enter` (or a tap/click)
activates the highlighted button. In **Options**, `←`/`→` change the value of
the highlighted row.

In the **Geometrium** world (mouse + keyboard):

| Input | Effect |
| --- | --- |
| `W A S D` / arrows | walk / fly |
| mouse move | look around |
| `Space` | jump (swim / rise in flight) |
| `C` (hold) | crouch / crawl — flatten to a one-block-tall box to slip through the low gaps in the terrain |
| `1`–`6` | hotbar block |
| `E` (Break) / `R` (Place) | break / place the aimed block |
| `F` | toggle flight |
| `Esc` / `M` / round button | open the main menu |

On a phone: left thumb is the joystick, the right side looks; the round
**Break** and **Place** buttons, the hotbar, **Jump** and the **Flight** and
**Crouch** pills are on-screen. **Crouch** is a hold button: while pressed the
hitbox flattens to roughly one block tall (and crawling is slower, with no
jumping), so you can crawl through the one-block openings the terrain carves;
release it and the player stands back up wherever there is room. The Android
back key steps out one level — world → menu,
menu → quit. World edits autosave to `world.edits` (atomic, debounced) and
the menu's settings persist to `enjoer.settings`.

## Options

- **Sound** — the master volume in percent (0–100, step 5), applied to the
  PCM mixer.
- **Render quality** — `Auto`, `High`, `Medium` or `Low`. `Auto` adapts the
  internal resolution from the measured frame time (it drops when a frame
  runs long and recovers once three consecutive windows are fast); the other
  three pin the resolution to a fixed scale factor (1×, 2×, 3×) for a
  predictable FPS.

The in-game status line shows only the measured **FPS** (bottom-left); the
rendering runs at the highest resolution the quality setting allows.

## The render backend

The app draws through a software 3D rasterizer with a streamed voxel backend
(`src/engine/render/`):

- `assets/textures/` holds nine **original 32×32 RGBA PNGs**; a quarter of a
  face receives the matching 16×16 pixels, UVs are bound to the world grid
  (including negative coordinates), merged faces repeat the PNG once per
  source block. `python3 tools/art/make_assets.py` regenerates the PNGs and
  the PCM16 effects offline.
- **Diffuse skylight** instead of binary sun/shadow: open columns get
  daylight, sideways spread decays, sealed spaces stay dark, leaves transmit
  partially, water casts no black shadow. Four light values per quad
  interpolate perspectively with `1/z`; greedy meshing only merges faces with
  constant light, so gradients never stretch.
- Terrain: two soft value-noise octaves, bedrock bottom layers, and **caves
  and holes that break the surface** — scarred columns keep a one-block crust
  instead of a sealed one, so cave air and open pits are visible from above
  (the spawn landing stays clean, and lake beds are never carved).
- **Transparent water**: opaque geometry first, then ~55 % water blending; a
  blocky source/stream simulation (down first, then lateral with support, no
  uphill or diagonal flow) steps at 0.1 s with a bounded active-cell queue.
- Chunk cache of **11×11** chunks (visible 9×9 plus a prefetch
  ring), nearest-first meshing with a soft per-frame budget, fog retreating
  as chunks become ready. Fog on dark surfaces stays dark.
- **Camera-space viewmodel**: `rend3d_viewmodel` switches the rasterizer to
  screen-locked, fog-free projection with its own overlay depth, so the
  first-person hand (arm cuboid + held block) sways and swings without
  inheriting world rotation.
- Perspective `1/z` depth/UV, signed colour arithmetic, backface culling
  before frustum tests; a bilinear upscale with an exact 2× fast path. The
  internal resolution is governed by the render-quality option above; the
  measured FPS is shown in the in-game status line.

The world is built warm at startup (a few hundred streaming steps around the
spawn), so the menu background is already a full field and **Play** drops you
into ready terrain.

## A scene-graph engine, kept in the tree

`src/engine/eng_*.c` is a small scene-graph engine (text `.escn` scenes,
typed nodes, sphere physics and C scripts) that the voxel backend grew out
of. The app no longer ships it — there are no bundled projects — but the code
and its own regression suite stay in the repository: `tools/tests/engine.c`
builds its own fixture projects under `build-tests/fixtures/` and exercises
node-tree math, scene parsing, `dlopen` scripting, physics and the
move-and-slide solver. It is deliberately left out of `CMakeLists.txt` and
the preview build so it is never linked into the APK or the preview binary.

## Tests

```sh
tools/tests/run.sh
SANITIZE=1 tools/tests/run.sh
```

Suites: the rasterizer/material regressions (`render`), voxel generation,
streaming and mesh coverage (`world`), the water simulation and saves
(`water`), the PCM mixer (`audio`), the first-person hand viewmodel
topology/animation (`hand`), the world controls — walking, toggleable
flight, crouch/crawl through one-block gaps, multitouch, HUD geometry
(`controls`) — and breaking/building with
ray picking, half-block collisions and edit saves (`edits`). The **app**
suite (`launcher`) drives the real entry points: the main menu at two
resolutions (flat buttons in frame, selection wrap, `Escape` behaviour), the
options screen (sound and quality cycling, back), the servers placeholder,
and the block world (streaming, walking, hotbar, flight, the round-button
pause that freezes the world, and re-entry via **Play**). A separate
`engine` suite covers the scene-graph engine described above. The
Android-flavoured code paths (APK asset IO, no `dlopen`, native-activity
glue) are compiled with `-D__ANDROID__` against a minimal NDK header stub so
they cannot rot. The sanitizer build enables ASan, UBSan and
float-cast-overflow.

The host app suite drives the same `game_*` entry points the platforms call,
so the menu, input routing, options and pausing are covered without a
browser.

## Android

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_NDK=$ANDROID_NDK_ROOT
cmake --build build -j
python3 stage_assets.py assets staging/assets
```

The CI workflow (`.github/workflows/main.yml`) builds both ABIs, stages the
assets and packages `EnjoerMessenger.apk` (the legacy library name
`libds_game.so` and the manifest are kept for the existing pipeline). Targets
are `arm64-v8a` and `armeabi-v7a`, Android 10+. The **Quit** menu button ends
the app through a JNI `Activity.finish()` call (falling back to process
exit), so it behaves like any other Android close.

Every `.c` is its own translation unit; `.c` files are never pulled in via
`#include`. External libraries live in `third_party/`.
