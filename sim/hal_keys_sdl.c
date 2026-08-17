#include "../HAL/hal.h"
#include "main_sim.h"

#include <SDL2/SDL.h>

/* SDL 键盘 -> 5 键映射 (WASD + Enter, 上下左右 + Enter) */
static int sdl_to_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:    case SDLK_w: return KEY_BTN_UP;
    case SDLK_DOWN:  case SDLK_s: return KEY_BTN_DOWN;
    case SDLK_LEFT:  case SDLK_a: return KEY_BTN_LEFT;
    case SDLK_RIGHT: case SDLK_d: return KEY_BTN_RIGHT;
    case SDLK_RETURN:case SDLK_KP_ENTER: case SDLK_SPACE: return KEY_BTN_ENTER;
    case SDLK_ESCAPE: return KEY_BTN_BACK;
    default: return -1;
    }
}

void hal_delay_ms(uint32_t ms)
{
    SDL_Delay(ms);
}

uint32_t hal_tick_get(void)
{
    return SDL_GetTicks();
}

int hal_key_init(void)
{
    return 0;
}

int hal_key_read(key_event_t *ev)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            sim_set_quit(1);
            continue;
        }
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            int id = sdl_to_key(e.key.keysym.sym);
            if (id < 0)
                continue;
            ev->id       = (key_id_t)id;
            ev->is_press = (e.type == SDL_KEYDOWN) ? 1 : 0;
            return 1;
        }
    }
    return 0;
}