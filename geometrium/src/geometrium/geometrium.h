/* Enjoer first-person block world: public controls and lifecycle.
 * The app router (src/core/game.c) forwards the generic game_* hooks here. */
#ifndef GEOMETRIUM_H
#define GEOMETRIUM_H

#include "engine.h"

void geometrium_key(const char *name, int down);
/* Release every held control on focus loss or gesture cancel. */
void geometrium_cancel_input(void);

void geometrium_game_init(AAssetManager *assets);
void geometrium_game_update(void);
void geometrium_game_draw(Buffer *buffer);
void geometrium_game_touch(float x, float y, int action, int pointer_id);
void geometrium_game_reset(void);
void geometrium_game_save(void);

#endif
