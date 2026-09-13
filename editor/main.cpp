 
//
// Build: cmake -B build -DZEN_BUILD_EDITOR=ON
// Run:   ./build/bin/zenblitz-editor [file.bb]   (or ./bin/zenblitz-editor)

#include <SDL.h>

#include <igui/Gui.hpp>
#include <igui_sdl2/SdlBackend.hpp>

#include "Editor.hpp"
#include "ClassicTheme.hpp"
#include "ToolbarIcons.h"

int main(int argc, char **argv)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;

 
    zed::EditorSettings savedSettings;
    savedSettings.load();

 
    const zed::EditorSettings::WindowGeometry &savedGeometry = savedSettings.windowGeometry();
    int windowX = SDL_WINDOWPOS_CENTERED;
    int windowY = SDL_WINDOWPOS_CENTERED;
    int windowWidth = 1280;
    int windowHeight = 800;
    if (savedGeometry.valid && savedGeometry.monitorIndex >= 0 &&
        savedGeometry.monitorIndex < SDL_GetNumVideoDisplays())
    {
        SDL_Rect displayBounds;
        if (SDL_GetDisplayBounds(savedGeometry.monitorIndex, &displayBounds) == 0)
        {
            windowX = displayBounds.x + savedGeometry.x;
            windowY = displayBounds.y + savedGeometry.y;
            windowWidth = savedGeometry.width;
            windowHeight = savedGeometry.height;
        }
    }

    SDL_Window *window = SDL_CreateWindow("zenblitz editor", windowX, windowY,
                                          windowWidth, windowHeight,
                                          SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Quit();
        return 1;
    }
 
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    { // Backend textures must be released before their SDL renderer.
        ig::sdl2::Backend backend(renderer, ig::defaultFontRanges(), 2048, 1024, 16.0f);
        if (!backend.prepareFontAtlas())
        {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        ig::Context ui(backend, &backend.fontAtlas());
        ui.setTheme(zed::classicTheme());
 
        zed::Editor editor;
        // Baked into the binary (see ToolbarIcons.h) rather than read from
        // disk: the path this used to load from was CMake's absolute
        // source-tree path, so a copied or relocated exe failed here and
        // quit. SDL_LoadBMP_RW's trailing 1 tells SDL to close the RWops
        // it was handed, including on the failure path.
        SDL_Surface *iconSurface =
            SDL_LoadBMP_RW(SDL_RWFromConstMem(zed::kToolbarBmp, (int)zed::kToolbarBmp_len), 1);
        if (!iconSurface)
        {
            SDL_Log("Toolbar: %s", SDL_GetError());
            return 1;
        }
        SDL_SetColorKey(iconSurface, SDL_TRUE, SDL_MapRGB(iconSurface->format, 192, 192, 192));
        SDL_Texture *icons = SDL_CreateTextureFromSurface(renderer, iconSurface);
        SDL_FreeSurface(iconSurface);
        if (!icons)
        {
            SDL_Log("Toolbar: %s", SDL_GetError());
            return 1;
        }
        editor.setToolbarIcons(ig::sdl2::textureId(icons));
        editor.openInitial(argc > 1 ? argv[1] : nullptr);

        bool running = true;
        bool textInputActive = false;
        ig::String windowTitle;
        uint64_t previousCounter = SDL_GetPerformanceCounter();
        const uint64_t frequency = SDL_GetPerformanceFrequency();
        // Exponentially-smoothed FPS, shown in the status bar by Editor.
        // Seeded at a sane value and clamped per-frame so a near-zero delta
        // on the first frames (before vsync settles) can't spike the readout
        // into the hundreds of thousands.
        float smoothedFps = 60.0f;

        while (running)
        {
            const uint64_t currentCounter = SDL_GetPerformanceCounter();
            const float deltaSeconds = static_cast<float>(currentCounter - previousCounter) /
                                       static_cast<float>(frequency);
            previousCounter = currentCounter;
            const float rawInstantFps = deltaSeconds > 0.0f ? 1.0f / deltaSeconds : 0.0f;
            const float instantFps = rawInstantFps > 240.0f ? 240.0f : rawInstantFps;
            smoothedFps = smoothedFps * 0.95f + instantFps * 0.05f;
            editor.setFps(smoothedFps);

            SDL_Event nativeEvent;
            while (SDL_PollEvent(&nativeEvent))
            {
                if (nativeEvent.type == SDL_QUIT)
                    running = false;
                ig::Event event;
                if (ig::sdl2::translateEvent(nativeEvent, event))
                    ui.pushEvent(event);
            }

            ui.beginFrame(ig::sdl2::frameInfo(window, deltaSeconds));
            editor.update(ui);
            const ig::DrawData &drawData = ui.endFrame();

            ig::String title = editor.windowTitle();
            if (title != windowTitle)
            {
                windowTitle = title;
                SDL_SetWindowTitle(window, windowTitle.c_str());
            }

            const bool wantsTextInput = ui.wantsTextInput();
            if (wantsTextInput != textInputActive)
            {
                if (wantsTextInput)
                    SDL_StartTextInput();
                else
                    SDL_StopTextInput();
                textInputActive = wantsTextInput;
            }

            SDL_SetRenderDrawColor(renderer, 24u, 27u, 35u, 255u);
            SDL_RenderClear(renderer);
            if (!backend.render(drawData))
                running = false;
            SDL_RenderPresent(renderer);

   
        }

  
        {
            zed::EditorSettings::WindowGeometry geometry;
            geometry.monitorIndex = SDL_GetWindowDisplayIndex(window);
            if (geometry.monitorIndex < 0)
                geometry.monitorIndex = 0; // SDL couldn't tell - falls back to display 0 on restore too
            SDL_Rect displayBounds;
            int windowPosX = 0, windowPosY = 0;
            SDL_GetWindowPosition(window, &windowPosX, &windowPosY);
            SDL_GetWindowSize(window, &geometry.width, &geometry.height);
            if (SDL_GetDisplayBounds(geometry.monitorIndex, &displayBounds) == 0)
            {
                geometry.x = windowPosX - displayBounds.x;
                geometry.y = windowPosY - displayBounds.y;
            }
            else
            {
                geometry.x = windowPosX;
                geometry.y = windowPosY;
            }
            geometry.valid = true;
            editor.setWindowGeometry(geometry);
        }

        SDL_DestroyTexture(icons);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
