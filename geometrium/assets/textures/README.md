Original pixel-art tiles authored for Enjoer; no textures from other games.

Each tile is a regular 32×32 RGBA PNG. It covers one full world block (1×1),
including all eight half-size pieces. The engine builds mip levels once on load.

Optional offline authoring tool: `python3 tools/art/make_assets.py`.
The game reads the PNG files directly and does not generate textures at runtime.
