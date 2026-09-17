#!/bin/sh
# Same C engine as the APK; the /preview binary itself is Git-ignored.
set -eu
cd "$(dirname "$0")/../.."
${CC:-gcc} -O2 -std=c99 -Itools/preview/compat -Isrc -I. \
    src/core/log.c src/core/state.c src/core/assets.c \
    src/graphics/gfx_frame.c src/graphics/gfx_draw.c src/graphics/image.c \
    src/graphics/gfx_text.c src/graphics/ttf/ttf_outline.c src/graphics/ttf/ttf_font.c \
    src/engine/render/rend3d.c src/engine/render/rend_shapes.c src/engine/render/rend_material.c \
    src/engine/render/rend_perf.c src/engine/render/voxel_terrain.c src/engine/render/voxel_world.c \
    src/engine/render/voxel_light.c src/engine/render/voxel_water.c src/engine/render/voxel_mesh.c \
    src/engine/render/voxel_edits.c \
    src/geometrium/geometrium_game.c src/geometrium/geometrium_hand.c \
    src/geometrium/geometrium_hud.c src/geometrium/geometrium_input.c \
    src/geometrium/geometrium_interact.c src/geometrium/geometrium_player.c \
    src/geometrium/geometrium_scene.c \
    src/core/game.c src/core/settings.c \
    tools/preview/host_compat.c tools/preview/host_main.c tools/preview/frame_jpeg.c \
    -rdynamic -lm -lpthread -ldl -o preview
