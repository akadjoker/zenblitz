/*
** BlitzKeys.h — SDL_Scancode -> DIK (DirectInput) translation.
**
** Blitz3D's KeyDown(n)/KeyHit(n) pass n straight to DirectInput
** (bbruntime/bbinput.cpp in the original): the numbers a .bb program uses
** ARE the DIK_* codes, not a Blitz-specific table. So the bridge here is
** SDL_Scancode -> DIK, not SDL -> some Blitz enum — see PLANO_ENGINE_KEYS.md
** for the full table and where the values come from (dinput.h).
**
** Kept separate from engine::Input (which has its own raylib-style KeyCode
** enum, e.g. KEY_A = 65): that class stays available for engine-native
** code; Blitz programs go through this table instead, unmodified.
*/
#ifndef ENGINE_BLITZ_KEYS_H
#define ENGINE_BLITZ_KEYS_H

#include <SDL2/SDL.h>
#include <cstdint>

namespace engine
{
    /* DIK codes go up to 0xED; 256 covers every one used in practice
       (the NEC PC98/Japanese-only codes above that are not mapped — no
       SDL scancode reaches them and no real Blitz3D game used them). */
    constexpr int kMaxDIK = 256;

    int scancode_to_dik(SDL_Scancode sc);
}

#endif
