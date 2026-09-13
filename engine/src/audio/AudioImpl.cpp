#include "stb_vorbis.c"

#undef PLAYBACK_MONO
#undef PLAYBACK_LEFT
#undef PLAYBACK_RIGHT
#undef L
#undef C
#undef R

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
