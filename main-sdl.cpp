#include "src/cerberus.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

// stuff from cat.cpp
extern void cat_setup();
extern volatile bool fast;
extern volatile bool mode;
extern void runCode();
extern char* autoloadBinaryFilename;

static std::deque<uint8_t> keyQueue;
static std::mutex keyQueueMutex;

const uint8_t cerb_color[8][3] = {
    { 0, 255, 0 },
    { 255, 0, 0 },
    { 0, 0, 255 },
    { 255, 255, 0 },
    { 0, 255, 255 },
    { 255, 0, 255 },
    { 0, 0, 0 },
    { 255, 255, 255 }
};
uint8_t buf[320 * 240 * 3];

SDL_Window* window = NULL;

SDL_Rect calc_4_3_output_rect()
{
    int wx, wy;
    SDL_GetWindowSize(window, &wx, &wy);
    if (wx > 4 * wy / 3) {
        return SDL_Rect { (wx - 4 * wy / 3) >> 1, 0, 4 * wy / 3, wy };
    } else {
        return SDL_Rect { 0, (wy - 3 * wx / 4) >> 1, wx, 3 * wx / 4 };
    }
}

void draw_screen(SDL_Renderer* renderer, SDL_Texture* tex)
{
    memset(buf, 0, sizeof buf);
    buf[0] = 0xff;

    const uint8_t* bgcolor = cerb_color[6];

    for (int scanLine = 0; scanLine < 240; scanLine++) {
        for (int col = 0; col < 40; col++) {
            // what tile?
            uint8_t tile_num = cerb_ram[0xf800 + (scanLine / 8) * 40 + col];
            // what line in the tile (0-7)
            int tile_line = (scanLine & 0x7);
            uint8_t tile_dat = cerb_ram[0xf000 + tile_num * 8 + tile_line];
            const uint8_t* fgcolor = cerb_color[7];
            if (tile_num >= 8 && tile_num < 32) {
                fgcolor = cerb_color[(tile_num - 8) % 6];
            }

            for (int p = 0; p < 8; p++) {
                const uint8_t* color = tile_dat & (0x80 >> p) ? fgcolor : bgcolor;
                const int x = col * 8 + p;
                memcpy((void*)&buf[scanLine * 320 * 3 + x * 3], (void*)color, 3);
            }
        }
    }

    SDL_Rect dest_rect = calc_4_3_output_rect();

    SDL_UpdateTexture(tex, NULL, buf, 320 * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, tex, NULL, &dest_rect);
    SDL_RenderPresent(renderer);
}

int readKey()
{
    auto lock = std::unique_lock<std::mutex>(keyQueueMutex);
    if (keyQueue.empty()) {
        return 0;
    } else {
        uint8_t key = keyQueue.front();
        keyQueue.pop_front();
        return key;
    }
    return 0;
}

static void loop()
{
    using namespace std::chrono_literals;
    auto t = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    for (;;) {
        cpuInterrupt();
        cat_loop();
        cpu_clockcycles(fast ? 160000 : 80000); // 8 mhz cycles in 0.02 seconds
        // 50Hz timer (every 0.02 seconds)
        typeof(t) now;
        for (;;) {
            now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            if (now - t >= 20000)
                break;
            std::this_thread::sleep_for(1ms);
        }

        // debug_log("CPU clock %.2f MHz\r\n", (fast ? 8.0 : 4.0) * 20000.0/ (now - t));
        t = now;
    }
}

int main(int argc, char* argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow(
        "One-Headed-Dog Cerberus 2100 Emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "could not create window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_StartTextInput();

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 320, 240);

    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "-z80") == 0) {
            mode = true;
        } else if (strcmp(argv[arg], "-6502") == 0) {
            mode = false;
        } else {
            fprintf(stderr, "Loading binary to $205: %s\n", argv[arg]);
            autoloadBinaryFilename = argv[arg];
        }
    }

    cat_setup();
    std::thread cpu_thread(loop);

    for (;;) {
        draw_screen(renderer, tex);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_TEXTINPUT) {
                auto lock = std::unique_lock<std::mutex>(keyQueueMutex);
                for (size_t i = 0; i < strlen(event.text.text); i++) {
                    keyQueue.push_back(event.text.text[i]);
                }
                break;
            }
            if (event.type == SDL_KEYDOWN) {
                auto lock = std::unique_lock<std::mutex>(keyQueueMutex);
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    keyQueue.push_back(27);
                    break;
                case SDLK_BACKSPACE:
                case SDLK_LEFT:
                    keyQueue.push_back(8);
                    break;
                case SDLK_RIGHT:
                    keyQueue.push_back(21);
                    break;
                case SDLK_UP:
                    keyQueue.push_back(11);
                    break;
                case SDLK_DOWN:
                    keyQueue.push_back(10);
                    break;
                case SDLK_F12:
                    keyQueue.push_back(1);
                    break;
                case SDLK_RETURN:
                    keyQueue.push_back('\r');
                    break;
                }
                // ...
            } else if (event.type == SDL_QUIT) {
                goto exit;
            }
        }
    }

exit:
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

void debug_log(const char* format, ...)
{
#ifdef DEBUG
    va_list ap;
    va_start(ap, format);
    auto size = vsnprintf(nullptr, 0, format, ap) + 1;
    if (size > 0) {
        va_end(ap);
        va_start(ap, format);
        char buf[size + 1];
        vsnprintf(buf, size, format, ap);
        fputs(buf, stderr);
    }
    va_end(ap);
#endif /* DEBUG */
}

void platform_delay(int ms)
{
    SDL_Delay(ms);
}
