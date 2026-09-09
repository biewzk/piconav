#include "../HAL/hal.h"
#include "main_sim.h"

#include <SDL2/SDL.h>

/* SDL keyboard/mouse -> 6-key mapping:
 *   Keyboard: WASD/arrows = up/down/left/right, Enter/Space/+ = ENTER, Esc/- = BACK
 *   Mouse:    wheel = ENTER/BACK (zoom), left-drag = arrow keys (pan)
 * All input goes through the same hal_key_read path as on-device. */

#define SIM_SCALE    2   /* same as main_sim.c: window coords / SIM_SCALE = logical px */
#define DRAG_STEP_PX 12  /* emit one arrow key per this many logical px dragged */

/* Synthesized key event queue (mouse events can produce many keys) */
#define Q_CAP 24
static key_event_t s_q[Q_CAP];
static int s_qh, s_qt;

static void q_push(key_id_t id, int press)
{
    int next = (s_qt + 1) % Q_CAP;
    if (next == s_qh)
        return; /* full: drop */
    s_q[s_qt].id = id;
    s_q[s_qt].is_press = (uint8_t)press;
    s_qt = next;
}

static int q_pop(key_event_t *ev)
{
    if (s_qh == s_qt)
        return 0;
    *ev = s_q[s_qh];
    s_qh = (s_qh + 1) % Q_CAP;
    return 1;
}

/* Single tap: push press + release */
static void q_click(key_id_t id)
{
    q_push(id, 1);
    q_push(id, 0);
}

static int sdl_to_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:    case SDLK_w: return KEY_BTN_UP;
    case SDLK_DOWN:  case SDLK_s: return KEY_BTN_DOWN;
    case SDLK_LEFT:  case SDLK_a: return KEY_BTN_LEFT;
    case SDLK_RIGHT: case SDLK_d: return KEY_BTN_RIGHT;
    case SDLK_RETURN:case SDLK_KP_ENTER: case SDLK_SPACE:
    case SDLK_EQUALS:case SDLK_KP_PLUS:  return KEY_BTN_ENTER;
    case SDLK_ESCAPE:case SDLK_MINUS: case SDLK_KP_MINUS: return KEY_BTN_BACK;
    default: return -1;
    }
}

/* Left-drag state: unconsumed drag remainder (logical px) */
static int s_drag;
static int s_drag_rx, s_drag_ry;

static void mouse_motion(const SDL_Event *e)
{
    if (!s_drag)
        return;
    s_drag_rx += e->motion.xrel / SIM_SCALE;
    s_drag_ry += e->motion.yrel / SIM_SCALE;
    /* Map follows the mouse: drag right -> viewport center moves left */
    while (s_drag_rx >= DRAG_STEP_PX)  { q_click(KEY_BTN_LEFT);  s_drag_rx -= DRAG_STEP_PX; }
    while (s_drag_rx <= -DRAG_STEP_PX) { q_click(KEY_BTN_RIGHT); s_drag_rx += DRAG_STEP_PX; }
    while (s_drag_ry >= DRAG_STEP_PX)  { q_click(KEY_BTN_UP);    s_drag_ry -= DRAG_STEP_PX; }
    while (s_drag_ry <= -DRAG_STEP_PX) { q_click(KEY_BTN_DOWN);  s_drag_ry += DRAG_STEP_PX; }
}

/* Sleep for the given number of milliseconds (SDL_Delay). */
void hal_delay_ms(uint32_t ms)
{
    SDL_Delay(ms);
}

/* Return the tick in ms (SDL_GetTicks). */
uint32_t hal_tick_get(void)
{
    return SDL_GetTicks();
}

int hal_key_init(void)
{
    return 0;
}

/*
 * Read one key event (non-blocking): drain the synthesized queue
 * first, then poll SDL events (quit, keys, wheel, left-drag).
 * Returns 1 if a mapped event was read into *ev, otherwise 0.
 */
int hal_key_read(key_event_t *ev)
{
    /* Drain the synthesized queue first */
    if (q_pop(ev))
        return 1;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            sim_set_quit(1);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int id = sdl_to_key(e.key.keysym.sym);
            if (id < 0)
                break;
            ev->id       = (key_id_t)id;
            ev->is_press = (e.type == SDL_KEYDOWN) ? 1 : 0;
            return 1;
        }
        case SDL_MOUSEWHEEL:
            q_click(e.wheel.y > 0 ? KEY_BTN_ENTER : KEY_BTN_BACK);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                s_drag = 1;
                s_drag_rx = s_drag_ry = 0;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT)
                s_drag = 0;
            break;
        case SDL_MOUSEMOTION:
            mouse_motion(&e);
            break;
        default:
            break;
        }
    }
    /* Mouse events may have queued synthesized keys */
    return q_pop(ev);
}
