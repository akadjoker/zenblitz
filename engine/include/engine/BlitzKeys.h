/*
** BlitzKeys.h — SDL_Scancode -> DIK, the codes KeyDown/KeyHit use.
*/
#ifndef ENGINE_BLITZ_KEYS_H
#define ENGINE_BLITZ_KEYS_H

#include <SDL2/SDL.h>
#include <cstdint>

namespace engine
{
    constexpr int kMaxDIK = 256;

    int scancode_to_dik(SDL_Scancode sc);
}

#endif
