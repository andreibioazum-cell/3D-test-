#!/bin/sh
# Same C modules as Android. SANITIZE=1 enables ASan/UBSan/float-cast checks.
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
DIR=${BUILD_DIR:-build-tests/regression}
mkdir -p "$DIR/fixtures"
FLAGS="-std=c99 -Wall -Wextra -Werror -Itools/preview/compat -Isrc -I."
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS="$FLAGS -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
else
    FLAGS="$FLAGS -O2"
fi
CORE="src/core/log.c src/core/state.c src/core/assets.c tools/preview/host_compat.c"
RENDER3D="src/engine/render/rend3d.c src/engine/render/rend_shapes.c src/engine/render/rend_material.c"
VOXEL="src/engine/render/voxel_world.c src/engine/render/voxel_light.c src/engine/render/voxel_water.c \
    src/engine/render/voxel_terrain.c src/engine/render/voxel_mesh.c src/engine/render/voxel_edits.c"
PERF="src/engine/render/rend_perf.c"
GFX2D="src/graphics/image.c src/graphics/gfx_frame.c src/graphics/gfx_draw.c src/graphics/gfx_text.c \
    src/graphics/ttf/ttf_font.c src/graphics/ttf/ttf_outline.c"
ENGINE="src/engine/eng_api.c src/engine/eng_node.c src/engine/eng_mesh.c src/engine/eng_physics.c \
    src/engine/eng_input.c src/engine/eng_scene.c src/engine/eng_script.c \
    src/engine/eng_script_builtin.c src/engine/eng_project.c src/engine/eng_fs.c"
ACTORS="src/geometrium/geometrium_player.c src/geometrium/geometrium_input.c src/geometrium/geometrium_interact.c"
GEOM="src/geometrium/geometrium_game.c $ACTORS src/geometrium/geometrium_scene.c \
    src/geometrium/geometrium_hud.c src/geometrium/geometrium_hand.c"

# Android-only or Android-flavoured translation units never run on the host, but
# CI compiles them: syntax-check them against a minimal NDK header stub so the
# device code paths (APK asset IO, no dlopen, native activity glue) cannot rot.
$CC -std=c99 -Wall -Wextra -Werror -D__ANDROID__ \
    -Itools/tests/android_stub -Isrc -I. \
    -fsyntax-only src/main.c src/sound/sound_android.c src/core/game.c \
    src/geometrium/geometrium_game.c src/geometrium/geometrium_hand.c \
    src/geometrium/geometrium_hud.c src/geometrium/geometrium_input.c \
    src/geometrium/geometrium_interact.c src/geometrium/geometrium_player.c \
    src/geometrium/geometrium_scene.c \
    src/engine/eng_fs.c src/engine/eng_script.c src/engine/eng_scene.c \
    src/engine/eng_project.c src/engine/eng_api.c

# Intentional splitting of source/flag lists, no .c includes or generated runtime.
$CC $FLAGS $CORE src/graphics/image.c $RENDER3D tools/tests/render.c -lm -o "$DIR/render"
"$DIR/render"
$CC $FLAGS $CORE $VOXEL tools/tests/world.c -lm -o "$DIR/world"
"$DIR/world"
$CC $FLAGS $CORE $VOXEL tools/tests/water.c -lm -o "$DIR/water"
"$DIR/water" "$DIR/fixtures/water"
# The first-person hand: viewmodel topology and swing/equip animation, stubbed.
$CC $FLAGS $CORE src/geometrium/geometrium_hand.c tools/tests/hand.c -lm -o "$DIR/hand"
"$DIR/hand"
# Game layer against the real voxel world: walking, flight, multitouch, HUD.
$CC $FLAGS $CORE $VOXEL $PERF $GEOM tools/tests/controls.c -lm -o "$DIR/controls"
"$DIR/controls"
# Breaking/building, ray picking, half-block collisions and world edits saves.
$CC $FLAGS $CORE $VOXEL src/geometrium/geometrium_hand.c $ACTORS tools/tests/edits.c -lm -o "$DIR/edits"
"$DIR/edits" "$DIR/fixtures"
$CC $FLAGS -DPREVIEW_EXTERNAL_AUDIO $CORE src/sound/sound.c tools/tests/audio.c -lm -lpthread -o "$DIR/audio"
"$DIR/audio"
# Engine layer: node tree, scene files, C scripting via dlopen, generalized rendering.
$CC $FLAGS -rdynamic $CORE $VOXEL $PERF $RENDER3D src/graphics/image.c $ENGINE \
    tools/tests/engine.c -lm -ldl -o "$DIR/engine"
"$DIR/engine"
# The app itself: the Minecraft-style main menu and the Geometrium block world.
$CC $FLAGS -rdynamic $CORE $VOXEL $PERF $RENDER3D $GFX2D $GEOM src/core/game.c \
    src/core/settings.c tools/tests/game.c -lm -o "$DIR/launcher"
"$DIR/launcher"
