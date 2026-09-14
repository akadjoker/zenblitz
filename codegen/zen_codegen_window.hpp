#ifndef ZEN_CODEGEN_WINDOW_HPP
#define ZEN_CODEGEN_WINDOW_HPP

#include <SDL2/SDL.h>
#include <cstdlib>

static SDL_Window *zen_window = 0;
static SDL_GLContext zen_gl_context = 0;
static bool zen_window_alive = false;

inline void zen_graphics_open(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    zen_window = SDL_CreateWindow("zencc native", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, width, height,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!zen_window) return;
    zen_gl_context = SDL_GL_CreateContext(zen_window);
    SDL_GL_SetSwapInterval(1);
    zen_window_alive = zen_gl_context != 0;
}

inline void zen_flip()
{
    if (!zen_window_alive) return;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_QUIT) std::exit(0);
    SDL_GL_SwapWindow(zen_window);
}

inline void zen_graphics_close()
{
    if (zen_gl_context) SDL_GL_DeleteContext(zen_gl_context);
    if (zen_window) SDL_DestroyWindow(zen_window);
    zen_gl_context = 0;
    zen_window = 0;
    zen_window_alive = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

#endif