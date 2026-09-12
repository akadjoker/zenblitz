/*
** backend_stdio.h — the console backend: stdio for text and files, the
** platform's directory and clock calls behind #ifdef. This is the only
** place in the runtime executables that talks to the OS; libzen itself
** carries no platform code (see zen/backend.h).
*/
#ifndef ZENBLITZ_BACKEND_STDIO_H
#define ZENBLITZ_BACKEND_STDIO_H

#include "backend.h"

namespace zen
{
    Backend stdio_backend();
}

#endif
