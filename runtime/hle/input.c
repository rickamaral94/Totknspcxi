// Controller input: SDL keyboard/gamepad -> nn::hid Npad state.
//
// Both go through the player's bindings (runtime/hle/bindings.c, defaults there: keyboard WASD left stick, arrows
// d-pad, K/J/L/I = A/B/X/Y, Q/E = L/R, 1/3 = ZL/ZR, Enter / Backspace = Plus / Minus, numpad 8/4/5/6 right stick;
// controller by position, Switch A = east).
#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../nvn/nvn.h"
#include "../nvn/renderer.h"
#include "../settings.h"
#include "../input_bind.h"

enum {
    BTN_A = 0, BTN_B, BTN_X, BTN_Y, BTN_LSTICK, BTN_RSTICK, BTN_L, BTN_R, BTN_ZL, BTN_ZR, BTN_PLUS, BTN_MINUS,
    BTN_LEFT, BTN_UP, BTN_RIGHT, BTN_DOWN
};

_Static_assert(BTN_DOWN == BIND_DOWN && BTN_ZL == BIND_ZL, "bindings follow the button bits");

static _Atomic uint64_t key_buttons, pad_buttons;
static _Atomic int32_t key_lx, key_ly, key_rx, key_ry, pad_lx, pad_ly, pad_rx, pad_ry;
static uint8_t keys_down[SDL_SCANCODE_COUNT];
// Every connected controller is open and read together (a phone may list a built-in device as a gamepad before the
// player's own: reading only the first one left the real controller unread); pad: the first, for the button prompts.
#define MAX_PADS 8
static SDL_Gamepad *pads[MAX_PADS], *pad;
static int npads;
static _Atomic int64_t sampling;

// Host keys / buttons held by a test script (LARECOMP_HOST_INPUT, input_frame): they go through the bindings
static uint8_t test_keys[SDL_SCANCODE_COUNT];
static _Atomic uint32_t test_pad;
static _Atomic int32_t test_lx, test_ly;  // (a resting stick's drift: "lstick" in LARECOMP_HOST_INPUT)
static bool host_test;  // (then a real controller is left out: the player's own pad must not spoil the test)

static int held_key(int sc) { return sc > 0 && sc < SDL_SCANCODE_COUNT && (keys_down[sc] || test_keys[sc]); }

// Through the bindings (runtime/hle/bindings.c: g_bind_key), read on every key event, so a rebind applies at once.
static void recompute_keyboard(void) {
    uint64_t b = 0;
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (held_key(g_bind_key[i])) b |= 1ull << i;
    key_buttons = b;
    key_lx = (held_key(g_bind_key[BIND_LS_RIGHT]) - held_key(g_bind_key[BIND_LS_LEFT])) * 32767;
    key_ly = (held_key(g_bind_key[BIND_LS_UP]) - held_key(g_bind_key[BIND_LS_DOWN])) * 32767;
    key_rx = (held_key(g_bind_key[BIND_RS_RIGHT]) - held_key(g_bind_key[BIND_RS_LEFT])) * 32767;
    key_ry = (held_key(g_bind_key[BIND_RS_UP]) - held_key(g_bind_key[BIND_RS_DOWN])) * 32767;
}

// Controller buttons (and the triggers as buttons) -> Switch buttons through g_bind_pad; the sticks stay the sticks.
static void recompute_pad(void) {
    uint32_t host = test_pad;
    for (int p = 0; p < npads && !host_test; p++) {
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
            if (SDL_GetGamepadButton(pads[p], (SDL_GamepadButton)i)) host |= 1u << i;
        if (SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) host |= 1u << PADB_LTRIGGER;
        if (SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) host |= 1u << PADB_RTRIGGER;
    }
    uint64_t b = 0;
    for (int i = 0; i < BIND_PAD_COUNT; i++)
        if (g_bind_pad[i] >= 0 && g_bind_pad[i] < PADB_HOST_COUNT && (host >> g_bind_pad[i] & 1)) b |= 1ull << i;
    {   // LARECOMP_LOG_PAD=1 (debugging a controller): the host buttons (SDL_GamepadButton bits) and what they become
        static int log_pad = -1;
        static uint32_t last_host;
        if (log_pad < 0) log_pad = getenv("LARECOMP_LOG_PAD") != NULL;
        if (log_pad && host != last_host) LOG("[input] pad host %#x -> switch %#llx\n", host, (unsigned long long)b);
        last_host = host;
    }
    pad_buttons = b;
    if (!npads || host_test) { pad_lx = test_lx, pad_ly = test_ly, pad_rx = pad_ry = 0; return; }
    int32_t ax[4] = {0};  // each stick axis: the controller holding it furthest
    for (int p = 0; p < npads; p++) {
        int32_t v[4] = {SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_LEFTX), -SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_LEFTY),
                        SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_RIGHTX), -SDL_GetGamepadAxis(pads[p], SDL_GAMEPAD_AXIS_RIGHTY)};
        for (int i = 0; i < 4; i++) if (abs(v[i]) > abs(ax[i])) ax[i] = v[i];
    }
    // Settings > Controls' dead zone: each stick's position inside it is centre, and past it the travel is stretched
    // back to full (round: a diagonal keeps its angle), so a worn stick's drift doesn't walk Link or turn the camera.
    // Never under 8%, as the Switch's own sticks report a centred one as exactly 0: a stick resting a step off centre
    // (GameSir X4: 128 of 0-255) read as a held right stick, and the game turned the motion aim off (#149 #175 #78)
    float dz = fmaxf(g_settings.stick_deadzone, 0.08f);
    for (int s = 0; dz > 0 && s < 2; s++) {
        float x = ax[2 * s] / 32767.0f, y = ax[2 * s + 1] / 32767.0f, m = sqrtf(x * x + y * y);
        float k = m <= dz ? 0 : fminf((m - dz) / (1 - dz), 1) / m;
        ax[2 * s] = (int32_t)(x * k * 32767), ax[2 * s + 1] = (int32_t)(y * k * 32767);
    }
    pad_lx = ax[0], pad_ly = ax[1], pad_rx = ax[2], pad_ry = ax[3];
    {   // LARECOMP_LOG_PAD=1: the left stick too, when it moves by more than ~1/8
        static int log_axes = -1;
        static int32_t llx, lly;
        if (log_axes < 0) log_axes = getenv("LARECOMP_LOG_PAD") != NULL;
        if (log_axes && (abs(pad_lx - llx) > 4096 || abs(pad_ly - lly) > 4096)) {
            LOG("[input] pad left stick %d %d\n", (int)pad_lx, (int)pad_ly);
            llx = pad_lx, lly = pad_ly;
        }
    }
}

// Forget held keys (the menu took focus); the gamepad is re-read on its next event.
void input_reset(void) {
    memset(keys_down, 0, sizeof keys_down);
    recompute_keyboard();
}

volatile int g_debug_frame_request;  // F9: dump the next frame's passes (build/pass_*.png) + draw log

// Events a test pushes (LARECOMP_HOST_INPUT type: / sdlkey: / sdlpad:) carry this device id: they go the whole way, menu first.
#define TEST_DEVICE 0x7E57u
static bool test_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return e->key.which == TEST_DEVICE;
    case SDL_EVENT_TEXT_INPUT: return e->text.windowID == TEST_DEVICE;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: return e->gbutton.which == TEST_DEVICE;
    default: return false;
    }
}

// Scripted test runs ignore the user's own input (a key or stick touched while a test drives Link spoils it):
// LARECOMP_NO_USER_INPUT=1, on by default whenever LARECOMP_INPUT is set
// (or LARECOMP_HOST_INPUT / LARECOMP_BIND_TEST; LARECOMP_NO_USER_INPUT=0 lets it through again).
bool input_user_blocked(const SDL_Event *e) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("LARECOMP_NO_USER_INPUT");
        on = v ? atoi(v) != 0 : (getenv("LARECOMP_INPUT") || getenv("LARECOMP_HOST_INPUT") || getenv("LARECOMP_BIND_TEST"));
        if (on) LOG("[input] scripted run: keyboard, mouse and controllers ignored\n");
    }
    if (!on) return false;
    if (test_event(e)) return false;  // (LARECOMP_HOST_INPUT type: / sdlkey: / sdlpad: - the test's own presses)
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_AXIS_MOTION: case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: case SDL_EVENT_JOYSTICK_ADDED: case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP:
        return true;
    default:
        return false;
    }
}

// Controller family for the button prompts (runtime/nvn/texpatch.c): 0 Nintendo / other / none, 1 Xbox, 2 PlayStation.
// LARECOMP_PAD_TYPE=xbox|playstation|nintendo pretends one is attached (tests).
static _Atomic int pad_family;
static void note_family(void) {
    int f = 0;
    if (pad) switch (SDL_GetGamepadType(pad)) {
        case SDL_GAMEPAD_TYPE_XBOX360: case SDL_GAMEPAD_TYPE_XBOXONE: f = 1; break;
        case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: f = 2; break;
        default: break;
    }
    if (f != pad_family) LOG("[input] button prompts: %s controller\n", f == 1 ? "Xbox" : f == 2 ? "PlayStation" : "Nintendo / other");
    pad_family = f;
}
int input_pad_family(void) {
    static int env = -2;
    if (env == -2) {
        const char *e = getenv("LARECOMP_PAD_TYPE");
        env = !e ? -1 : !strcmp(e, "xbox") ? 1 : !strcmp(e, "playstation") || !strcmp(e, "ps") ? 2 : 0;
    }
    return env >= 0 ? env : pad_family;
}

// A gamepad with no axis at all isn't a controller to play with: a phone's fingerprint sensor can register one (the
// OnePlus 7 Pro's "gf_input": navigation keys and a BTN_EAST, so Android and SDL call it a gamepad, and the touch
// controls never showed, #477). Such a device is left out wherever the app asks whether a controller is connected.
// LARECOMP_PAD_ANY=1: every gamepad counts, as before. The answer is kept per id (SDL gives a reconnected device a
// new one). (from BOTWRecomp 0ea51bb)
bool input_pad_usable(uint32_t id) {
    static int any = -1;
    if (any < 0) any = getenv("LARECOMP_PAD_ANY") && atoi(getenv("LARECOMP_PAD_ANY"));
    if (any) return true;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static struct { uint32_t id; bool usable; } seen[16];
    static unsigned nseen;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < nseen && i < 16; i++)
        if (seen[i].id == id) { bool u = seen[i].usable; pthread_mutex_unlock(&lock); return u; }
    pthread_mutex_unlock(&lock);
    SDL_Gamepad *g = SDL_OpenGamepad((SDL_JoystickID)id);  // (counted: closing it leaves another opener's open)
    int axes = g ? SDL_GetNumJoystickAxes(SDL_GetGamepadJoystick(g)) : -1;
    bool usable = axes != 0;  // (one that can't be opened: as before)
    if (!usable) LOG("[input] gamepad %u: %s has no axes - not a controller, left out (LARECOMP_PAD_ANY=1: counted)\n",
                     (unsigned)id, SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "(unnamed)");
    if (g) SDL_CloseGamepad(g);
    pthread_mutex_lock(&lock);
    seen[nseen++ % 16].id = id, seen[(nseen - 1) % 16].usable = usable;
    pthread_mutex_unlock(&lock);
    return usable;
}
uint32_t input_pad_first(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    uint32_t first = 0;
    for (int i = 0; ids && i < n && !first; i++) if (input_pad_usable(ids[i])) first = ids[i];
    SDL_free(ids);
    return first;
}
bool input_pad_present(void) { return input_pad_first() != 0; }

static void pad_open(SDL_JoystickID id) {
    for (int p = 0; p < npads; p++) if (SDL_GetGamepadID(pads[p]) == id) return;
    if (npads == MAX_PADS || !input_pad_usable(id)) return;
    SDL_Gamepad *g = SDL_OpenGamepad(id);
    if (!g) { LOG("[input] gamepad %u: %s\n", (unsigned)id, SDL_GetError()); return; }
    pads[npads++] = g;
    pad = pads[0];
    LOG("[input] gamepad %u: %s (type %d, %d open)\n", (unsigned)id, SDL_GetGamepadName(g), (int)SDL_GetGamepadType(g), npads);
    note_family();
    extern void motion_pad_opened(SDL_Gamepad *g);  // (runtime/hle/motion.c: its gyroscope, if it has one)
    motion_pad_opened(g);
}
static void pad_close(SDL_JoystickID id) {
    for (int p = 0; p < npads; p++)
        if (SDL_GetGamepadID(pads[p]) == id) { SDL_CloseGamepad(pads[p]); pads[p] = pads[--npads]; break; }
    pad = npads ? pads[0] : NULL;
    note_family();
}

void input_handle_event(const SDL_Event *e) {
    static bool scanned;  // pads present at startup: their ADDED event may have gone to the start screen's loop
    if (!scanned) {
        scanned = true;
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        for (int i = 0; i < n; i++) pad_open(ids[i]);
        SDL_free(ids);
    }
    extern void motion_event(const SDL_Event *e);  // runtime/hle/motion.c
    switch (e->type) {
    case SDL_EVENT_SENSOR_UPDATE: case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        motion_event(e);
        break;
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F9 && !e->key.repeat) g_debug_frame_request = 1;
        if (e->key.scancode < SDL_SCANCODE_COUNT) keys_down[e->key.scancode] = e->type == SDL_EVENT_KEY_DOWN;
        recompute_keyboard();
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        pad_open(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        pad_close(e->gdevice.which);
        recompute_pad();
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
        recompute_pad();
        break;
    }
}

// Scripted input for testing: LARECOMP_INPUT="frame:BUTTON:frames,..." (BUTTON = A,B,X,Y,L,R,ZL,ZR,PLUS,
// MINUS,LEFT,UP,RIGHT,DOWN, or STICK_UP/DOWN/LEFT/RIGHT, RSTICK_UP/DOWN/LEFT/RIGHT, STICK_<degrees>: full tilt at
// that angle, 0 = right, 90 = up).
static _Atomic uint64_t script_buttons;
static _Atomic int32_t script_lx, script_ly, script_rx, script_ry;

// Tests of the bindings, through them like a real player:
// LARECOMP_HOST_INPUT="frame:key:<SDL key name>:frames,frame:pad:<button>:frames,..." holds a host key / controller
//   button (names as in larecomp.ini's bind_* lines: "Z", "Keypad 0", "south", "ltrigger"...);
//   "frame:lstick:<x>/<y>:frames" holds the controller's left stick there (raw SDL units, y up: a drifting stick).
// "frame:type:<text>" types <text> as real key events (down, text, up per character), through the overlay menu / name
//   entry first like a player's typing; "{Escape}" (any SDL key name) presses that key. No ',' or ':' in <text>.
// "frame:sdlpad:<button>:frames" / "frame:sdlkey:<key>:frames" hold a controller button / key as real events (down
//   at frame, up `frames` later): they reach menus and the on-screen keyboard first, as a player's would.
// "frame:shot:<name>" saves the window (game + menus) to build/<name>.png.
// LARECOMP_BIND_TEST="frame:pad|key:<input>:<host>,..." starts a rebind capture of <input> (bind_* name: a, zl,
//   ls_up...) at that frame (the Controls tab shows it waiting) and presses <host> 90 frames later ("esc" cancels,
//   "wait" lets it time out); "frame:reset:pad|key" resets to the defaults.
static void push_key(SDL_Scancode sc, SDL_Keymod mod, bool down) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.which = TEST_DEVICE, e.key.scancode = sc, e.key.mod = mod, e.key.down = down;
    e.key.key = SDL_GetKeyFromScancode(sc, mod, false);
    SDL_PushEvent(&e);
}
char g_window_shot_request[256];  // a test's "shot": the renderer saves its next window image there, then clears it

static void test_push(uint64_t frame, unsigned long start, unsigned long len, const char *dev, const char *arg) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    if (!strcmp(dev, "shot")) {
        if (frame == start) snprintf(g_window_shot_request, sizeof g_window_shot_request, "build/%s.png", arg);
        return;
    }
    if (!strcmp(dev, "sdlkey")) {
        if (frame == start || frame == start + len) push_key(SDL_GetScancodeFromName(arg), SDL_KMOD_NONE, frame == start);
        return;
    }
    if (!strcmp(dev, "sdlpad")) {
        int b = bind_pad_from_name(arg);
        if (b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT || (frame != start && frame != start + len)) return;
        e.type = frame == start ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.which = TEST_DEVICE, e.gbutton.button = (Uint8)b, e.gbutton.down = frame == start;
        SDL_PushEvent(&e);
        return;
    }
    if (frame != start) return;
    LOG("[input] test: typing \"%s\"\n", arg);
    static char ascii[128][2];  // (a text event keeps a pointer to its text: these live on)
    for (const char *p = arg; *p; p++) {
        if (*p == '{' && strchr(p, '}')) {  // a named key
            char nm[32];
            size_t n = (size_t)(strchr(p, '}') - p - 1);
            snprintf(nm, sizeof nm, "%.*s", (int)(n < sizeof nm ? n : sizeof nm - 1), p + 1);
            SDL_Scancode sc = SDL_GetScancodeFromName(nm);
            push_key(sc, SDL_KMOD_NONE, true), push_key(sc, SDL_KMOD_NONE, false);
            p += n + 1;
            continue;
        }
        SDL_Keymod mod = SDL_KMOD_NONE;
        SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)tolower((unsigned char)*p), &mod);
        if (isupper((unsigned char)*p)) mod |= SDL_KMOD_LSHIFT;
        push_key(sc, mod, true);
        int c = *p & 0x7f;
        ascii[c][0] = (char)c;
        memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_TEXT_INPUT, e.text.windowID = TEST_DEVICE, e.text.text = ascii[c];
        SDL_PushEvent(&e);
        push_key(sc, mod, false);
    }
}

static void host_test_frame(uint64_t frame) {
    static const char *hspec, *bspec;
    static int init;
    if (!init) { init = 1; hspec = getenv("LARECOMP_HOST_INPUT"); bspec = getenv("LARECOMP_BIND_TEST"); host_test = hspec; }
    char buf[2048];
    if (hspec) {
        uint8_t keys[SDL_SCANCODE_COUNT] = {0};
        uint32_t padm = 0;
        int32_t sx = 0, sy = 0;
        snprintf(buf, sizeof buf, "%s", hspec);
        char *save = NULL;
        for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            unsigned long start = 0, len = 10;
            char dev[8] = "", name[400] = "";
            if (sscanf(tok, "%lu:%7[a-z]:%399[^:]:%lu", &start, dev, name, &len) < 3) continue;
            if (!strcmp(dev, "type") || !strcmp(dev, "sdlpad") || !strcmp(dev, "sdlkey") || !strcmp(dev, "shot")) {
                test_push(frame, start, len, dev, name);
                continue;
            }
            if (frame < start || frame >= start + len) continue;
            if (!strcmp(dev, "key")) { SDL_Scancode sc = SDL_GetScancodeFromName(name); if (sc) keys[sc] = 1; }
            else if (!strcmp(dev, "lstick")) sscanf(name, "%d/%d", &sx, &sy);
            else { int h = bind_pad_from_name(name); if (h >= 0) padm |= 1u << h; }
        }
        bool kch = memcmp(keys, test_keys, sizeof keys) != 0, pch = padm != test_pad || sx != test_lx || sy != test_ly;
        memcpy(test_keys, keys, sizeof keys);
        test_pad = padm;
        test_lx = sx, test_ly = sy;
        if (kch) recompute_keyboard();
        if (pch) recompute_pad();
    }
    if (bspec) {
        snprintf(buf, sizeof buf, "%s", bspec);
        char *save = NULL;
        for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            unsigned long start = 0;
            char dev[8] = "", in[16] = "", host[40] = "";
            if (sscanf(tok, "%lu:%7[a-z]:%15[a-z_]:%39[^:]", &start, dev, in, host) < 3) continue;
            if (!strcmp(dev, "reset")) { if (frame == start) bind_reset(!strcmp(in, "key") ? BIND_DEV_KEY : BIND_DEV_PAD); continue; }
            int d = !strcmp(dev, "key") ? BIND_DEV_KEY : BIND_DEV_PAD;
            if (frame == start) {
                LOG("[input] test: capture %s %s\n", dev, in);
                bind_capture_begin(d, bind_slot_from_name(in));
            }
            if (frame == start + 90 && strcmp(host, "wait")) {
                SDL_Event e;
                memset(&e, 0, sizeof e);
                if (!strcmp(host, "esc") || d == BIND_DEV_KEY) {
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.scancode = !strcmp(host, "esc") ? SDL_SCANCODE_ESCAPE : SDL_GetScancodeFromName(host);
                } else {
                    int h = bind_pad_from_name(host);
                    if (h >= PADB_LTRIGGER) {
                        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                        e.gaxis.axis = h == PADB_LTRIGGER ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                        e.gaxis.value = 32767;
                    } else {
                        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                        e.gbutton.button = (Uint8)h;
                    }
                }
                bind_capture_event(&e);
            }
        }
    }
}

static void input_latch_frame(void);  // (below fill_state's TOUCH_* definitions)
static uint64_t g_pad_obj;
volatile int g_inject_plus;  // frames to hold Plus for the game (movie.c: A/B skip a skippable movie)
RT_ON_RESET(input_reboot_reset) { g_pad_obj = 0; g_inject_plus = 0; }  // (reboot: the controller object was guest memory)
void input_frame(uint64_t frame) {
    if (g_pad_obj && getenv("LARECOMP_DUMP_PAD") && frame % 600 == 0) {
        const uint8_t *m = (const uint8_t *)(uintptr_t)(g_pad_obj - 0x98);
        LOG("[pad] mgr %llx 4e0=%u 4e4=%u 4e8=%u 548=%#x 54c..=%02x %02x %02x 56c=%02x\n", g_pad_obj - 0x98,
            *(uint32_t *)(m + 0x4e0), *(uint32_t *)(m + 0x4e4), *(uint32_t *)(m + 0x4e8), *(uint32_t *)(m + 0x548),
            m[0x54c], m[0x54d], m[0x54e], m[0x56c]);
        for (int i = 0; i < 9; i++) {
            const uint8_t *o = m + 0x98 + 0x48 * i;
            uint64_t player = *(uint64_t *)(m + 0x10 * i);
            LOG("[pad]  ctl%d id %#x style %#x btn %#x conn %d %d | player %llx [8]=%u\n", i, *(uint32_t *)(o + 8),
                *(uint32_t *)(o + 0xc), *(uint32_t *)(o + 0x14), o[0x44], o[0x45], player,
                player ? *(uint32_t *)(uintptr_t)(player + 8) : 0);
        }
    }
    extern void watch_arm(void);
    watch_arm();
    host_test_frame(frame);
    input_latch_frame();
    const char *spec = getenv("LARECOMP_INPUT");
    if (!spec) return;
    static const char *const names[] = {"A", "B", "X", "Y", "LSTICK", "RSTICK", "L", "R", "ZL", "ZR", "PLUS",
                                        "MINUS", "LEFT", "UP", "RIGHT", "DOWN"};
    uint64_t b = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
    static char buf[16384];  // (long scripted sessions: the perf walk is ~4 KB)
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        unsigned long start = 0, len = 10;
        char name[32] = "";
        if (sscanf(tok, "%lu:%31[A-Z_0-9]:%lu", &start, name, &len) < 2) continue;
        if (frame < start || frame >= start + len) continue;
        if (!strcmp(name, "STICK_UP")) ly = 32767;
        else if (!strcmp(name, "STICK_DOWN")) ly = -32767;
        else if (!strcmp(name, "STICK_LEFT")) lx = -32767;
        else if (!strcmp(name, "STICK_RIGHT")) lx = 32767;
        else if (!strcmp(name, "RSTICK_UP")) ry = 32767;
        else if (!strcmp(name, "RSTICK_DOWN")) ry = -32767;
        else if (!strcmp(name, "RSTICK_LEFT")) rx = -32767;
        else if (!strcmp(name, "RSTICK_RIGHT")) rx = 32767;
        else if (!strncmp(name, "STICK_", 6) && name[6] >= '0' && name[6] <= '9') {
            double r = atoi(name + 6) * 3.14159265358979 / 180;
            lx = (int32_t)lround(cos(r) * 32767), ly = (int32_t)lround(sin(r) * 32767);
        }
        for (int i = 0; i < 16; i++) if (!strcmp(name, names[i])) b |= 1ull << i;
    }
    script_buttons = b;
    script_lx = lx;
    script_ly = ly;
    script_rx = rx;
    script_ry = ry;
}

extern bool menu_input_captured(void);  // runtime/ui/menu.mm: overlay menu or name entry up (it has the keys / pad)

// Buttons held right now (keyboard, gamepad, test script), independent of whether the game polls (movies).
#ifdef __ANDROID__
extern _Atomic uint64_t g_touch_buttons;  // runtime/ui/touch_controls.mm: the on-screen controls
extern _Atomic int32_t g_touch_lx, g_touch_ly, g_touch_rx, g_touch_ry;
#define TOUCH_BUTTONS g_touch_buttons
#define TOUCH_LX g_touch_lx
#define TOUCH_LY g_touch_ly
#define TOUCH_RX g_touch_rx
#define TOUCH_RY g_touch_ry
#else
#define TOUCH_BUTTONS 0
#define TOUCH_LX 0
#define TOUCH_LY 0
#define TOUCH_RX 0
#define TOUCH_RY 0
#endif
uint64_t input_buttons_now(void) { return (menu_input_captured() ? 0 : key_buttons | pad_buttons | TOUCH_BUTTONS) | script_buttons; }

static int32_t clamp_axis(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

// NpadFullKeyState / NpadHandheldState / NpadJoy*State share this layout (0x28 bytes):
// {int64 sampling, u64 buttons, AnalogStick L {x,y}, AnalogStick R {x,y}, u32 attributes}
// The host's input (keys, controllers, the on-screen controls) as the game's polls see it: read once a presented frame
// (input_frame), so every poll of a frame - TOTK polls several times a frame - sees the same buttons, as the scripted
// input always was. Read live, a touch landing mid-frame was "no menu" to the part of the frame that draws the world and
// "menu" to the part that captures it for the menu's blurred background: the weapon wheel opened over black (#76, only
// with a player's touch, never with LARECOMP_INPUT) - or so it seemed: latched, 3 of 11 openings were still black
// (2026-11-23), so it is off by default. LARECOMP_INPUT_LATCH=1: latched.
static _Atomic uint64_t latch_b;
static _Atomic int32_t latch_lx, latch_ly, latch_rx, latch_ry;
static void input_latch_frame(void) {  // (input_frame: once a presented frame, on the game's thread)
    latch_b = key_buttons | pad_buttons | TOUCH_BUTTONS;
    latch_lx = key_lx + pad_lx + TOUCH_LX, latch_ly = key_ly + pad_ly + TOUCH_LY;
    latch_rx = key_rx + pad_rx + TOUCH_RX, latch_ry = key_ry + pad_ry + TOUCH_RY;
}
static bool input_latch_on(void) {
    static int on = -1;
    if (on < 0) on = getenv("LARECOMP_INPUT_LATCH") && atoi(getenv("LARECOMP_INPUT_LATCH")) != 0;
    return on;
}
static void fill_state(Cpu *c) {
    uint8_t *s = (uint8_t *)(uintptr_t)c->x[0];
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[1];
    memset(s, 0, 0x28);
    *(int64_t *)s = ++sampling;
    static int nlog;
    if (nlog < 6 && (sampling % 600) == 1) { nlog++; LOG("[input] GetNpadState id %#x lr %llx\n", id, c->x[30]); }
    if (id != 0) return;  // docked: only player 1 (FullKey); Handheld stays disconnected
    extern uint64_t g_watch_lo, g_watch_hi;
    if (id == 0 && !g_pad_obj) g_pad_obj = c->x[1] - 8;
    if (id == 0 && !g_watch_lo && getenv("LARECOMP_WATCH_PAD")) {
        unsigned lo = 0x98 + 0x10, hi = 0x98 + 0x48;  // offsets from the controller manager
        sscanf(getenv("LARECOMP_WATCH_PAD"), "%x,%x", &lo, &hi);
        g_watch_lo = c->x[1] - 8 - 0x98 + lo;
        g_watch_hi = c->x[1] - 8 - 0x98 + hi;
        LOG("[input] watching controller object %llx\n", c->x[1] - 8);
    }
    bool menu = menu_input_captured();  // the overlay menu / name entry has the controls (typing there isn't for the game)
    bool latch = input_latch_on();
    uint64_t b = menu ? script_buttons : (latch ? latch_b : key_buttons | pad_buttons | TOUCH_BUTTONS) | script_buttons;
    {   // injected Plus: held for ~10 presented frames (the game polls several times per frame)
        extern uint64_t renderer_present_count(void);
        static uint64_t until;
        if (g_inject_plus > 0) { until = renderer_present_count() + 10; g_inject_plus = 0; }
        if (renderer_present_count() < until) b |= 1ull << BTN_PLUS;
    }
    int32_t lx = clamp_axis((menu ? 0 : latch ? latch_lx : key_lx + pad_lx + TOUCH_LX) + script_lx);
    int32_t ly = clamp_axis((menu ? 0 : latch ? latch_ly : key_ly + pad_ly + TOUCH_LY) + script_ly);
    int32_t rx = clamp_axis((menu ? 0 : latch ? latch_rx : key_rx + pad_rx + TOUCH_RX) + script_rx);
    int32_t ry = clamp_axis((menu ? 0 : latch ? latch_ry : key_ry + pad_ry + TOUCH_RY) + script_ry);
    // stick directions also appear as buttons (bits 16-23)
    if (lx < -16000) b |= 1ull << 16;
    if (ly > 16000) b |= 1ull << 17;
    if (lx > 16000) b |= 1ull << 18;
    if (ly < -16000) b |= 1ull << 19;
    if (rx < -16000) b |= 1ull << 20;
    if (ry > 16000) b |= 1ull << 21;
    if (rx > 16000) b |= 1ull << 22;
    if (ry < -16000) b |= 1ull << 23;
    *(uint64_t *)(s + 8) = b;
    static uint64_t last_b;
    if (b != last_b && getenv("LARECOMP_INPUT")) LOG("[input] id %#x buttons %#llx (sampling %lld, lr %llx)\n", id, (unsigned long long)b, (long long)sampling, c->x[30]);
    last_b = b;
    *(int32_t *)(s + 16) = lx;
    *(int32_t *)(s + 20) = ly;
    *(int32_t *)(s + 24) = rx;
    *(int32_t *)(s + 28) = ry;
    *(uint32_t *)(s + 32) = 1;  // IsConnected
}

HLE_FUNC("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", hid_state_fullkey) { fill_state(c); }
HLE_FUNC("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", hid_state_dual) { fill_state(c); }
HLE_FUNC("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", hid_state_left) { fill_state(c); }
HLE_FUNC("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", hid_state_handheld) { fill_state(c); }
HLE_FUNC("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", hid_state_right) { fill_state(c); }

// v1.6.0 (SDK 7.3.2) reads the newest `count` states instead: int GetNpadStates(State *out, int count, const NpadIdType &).
// The current state first, then copies with older sampling numbers (the game skips those it has seen by that number).
static void fill_states(Cpu *c) {
    uint8_t *out = (uint8_t *)(uintptr_t)c->x[0];
    int n = (int)(int32_t)c->x[1];
    if (n < 1) { c->x[0] = 0; return; }
    if (n > 16) n = 16;  // (the hid ring's depth)
    c->x[1] = c->x[2];   // (fill_state's arguments: state, id)
    fill_state(c);
    for (int i = 1; i < n; i++) {
        memcpy(out + i * 0x28, out, 0x28);
        *(int64_t *)(out + i * 0x28) -= i;
    }
    c->x[0] = (uint64_t)n;
}
HLE_FUNC("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj", hid_states_fullkey) { fill_states(c); }
HLE_FUNC("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj", hid_states_dual) { fill_states(c); }
HLE_FUNC("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyLeftStateEiRKj", hid_states_left) { fill_states(c); }
HLE_FUNC("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj", hid_states_handheld) { fill_states(c); }
HLE_FUNC("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadJoyRightStateEiRKj", hid_states_right) { fill_states(c); }

// FullKey for player 1 only (we report docked mode, where Handheld is unavailable)
HLE_FUNC("_ZN2nn3hid15GetNpadStyleSetERKj", hid_style_set) {
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[0];
    static int nlog;
    if (nlog++ < 4) LOG("[input] GetNpadStyleSet id %#x\n", id);
    c->x[0] = id == 0 ? 1 : 0;  // docked: no handheld controller
}

// BindNpadStyleSetUpdateEvent(id, SystemEventType*, clear mode): signaled once so the game sees a
// controller connected.
#include "../kernel.h"
HLE_FUNC("_ZN2nn3hid27BindNpadStyleSetUpdateEventERKjPNS_2os15SystemEventTypeENS3_14EventClearModeE", hid_bind_style_event) {
    uint32_t w, r;
    KEvent *ev = kernel_create_event_pair(&w, &r);
    pthread_mutex_lock(&g_klock);
    kernel_signal(ev);
    pthread_mutex_unlock(&g_klock);
    uint64_t clear_mode = c->x[2];
    c->x[0] = c->x[1];
    c->x[1] = r;
    c->x[2] = 0;
    c->x[3] = clear_mode;
    guest_call(c, guest_sym("_ZN2nn2os33AttachReadableHandleToSystemEventEPNS0_15SystemEventTypeEjbNS0_14EventClearModeE"));
    LOG("[input] style set update event bound\n");
}

// Vibration devices: accepted but inert (the SDK's vibration node graph stays recompiled).
HLE_FUNC("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", hid_init_vibration) { (void)c; }
HLE_FUNC("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", hid_vibration_handles) {
    uint32_t *out = (uint32_t *)(uintptr_t)c->x[0];
    int n = (int)c->x[1] < 2 ? (int)c->x[1] : 2;
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[2];
    for (int i = 0; i < n; i++) out[i] = (id & 0xFF) << 8 | (uint32_t)i | 0x03000000u;  // type, player, position
    c->x[0] = (uint64_t)n;
}
HLE_FUNC("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", hid_vibration_info) {
    uint32_t *info = (uint32_t *)(uintptr_t)c->x[0];
    uint32_t h = *(uint32_t *)(uintptr_t)c->x[1];
    info[0] = 1;        // VibrationDeviceType_LinearResonantActuator
    info[1] = h & 1;    // position: left / right
}

// ---- HD rumble: the game's vibration values (services/hid_set.c, IPC SendVibrationValue / SendVibrationValues) on
// the controllers' motors, or the phone's vibrator when none is connected. Inert until 2026-11-14: the game sent its
// rumble and nothing moved. As in totk-android (AurelioB, MIT: runtime/hid_feedback.c, rumble_mix.h,
// feedback_android.c): a value {low band amplitude, frequency, high band amplitude, frequency} becomes one strength,
// sqrt(low^2 + high^2) x the setting, silence under 2 % and for a side the game hasn't repeated in 250 ms (a paused or
// frozen game never leaves a motor on); a change of 8 / 255 or more, or every 200 ms while it lasts, goes to the
// motors. Left Joy-Con -> the controller's low-frequency (left) motor, right -> the high-frequency one. Driven from the
// main thread's loop (input_rumble_tick), which owns the open controllers. Settings > Controls > Vibration
// (g_settings.rumble, %); LARECOMP_RUMBLE_LOG=1: each strength sent. Scripted test runs (LARECOMP_INPUT) don't move
// the motors unless LARECOMP_RUMBLE=1.
#include <pthread.h>
static pthread_mutex_t rum_mu = PTHREAD_MUTEX_INITIALIZER;
static float rum_v[2][4];
static uint64_t rum_at[2];
void input_rumble_value(unsigned side, const float v[4]) {
    if (side > 1) return;
    pthread_mutex_lock(&rum_mu);
    memcpy(rum_v[side], v, sizeof rum_v[side]);
    rum_at[side] = SDL_GetTicksNS();
    pthread_mutex_unlock(&rum_mu);
    static _Atomic int first;
    if (!atomic_exchange(&first, 1))
        LOG("[rumble] the game's first vibration: %s %.2f@%.0f Hz %.2f@%.0f Hz\n", side ? "right" : "left", v[0], v[1], v[2], v[3]);
}
static int rum_amp(const float v[4], uint64_t at, uint64_t now, float gain) {
    if (!at || now - at >= 250000000ull) return 0;
    float lo = v[0], hi = v[2];
    if (!(lo == lo) || !(hi == hi)) return 0;
    float a = sqrtf(lo * lo + hi * hi) * gain;
    if (!(a >= 0.02f)) return 0;
    if (a > 1.0f) a = 1.0f;
    int r = (int)lroundf(a * 255.0f);
    return r < 1 ? 1 : r;
}
#ifdef __ANDROID__
// The phone's own vibrator through the activity (TOTKRecompAndroidActivity.rumble): amplitude 1..255 for ms, 0 stops.
// SDL's haptic path vibrates with no attributes, and the POCO's HyperOS let none of them through - SDL said "sent" for a
// full-strength second and nothing moved (2026-11-14); ours marks them as a game's (media) vibration.
#include <jni.h>
static void phone_rumble(int amplitude, int ms) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    if (!env || !act) return;
    jclass c = (*env)->GetObjectClass(env, act);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, "rumble", "(II)V") : NULL;
    if (m) (*env)->CallStaticVoidMethod(env, c, m, (jint)amplitude, (jint)ms);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (c) (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, act);
}
#endif
void input_rumble_tick(void) {
    static int sent[2], on = -1, log = -1;
    static uint64_t sent_at;
    if (on < 0) on = !getenv("LARECOMP_INPUT") || (getenv("LARECOMP_RUMBLE") && atoi(getenv("LARECOMP_RUMBLE")));
    if (log < 0) log = getenv("LARECOMP_RUMBLE_LOG") != NULL;
    uint64_t now = SDL_GetTicksNS();
#ifdef __ANDROID__
    static int test = -1;  // LARECOMP_RUMBLE_TEST=1: one second at full strength on the phone at the start (is the motor reached?)
    if (test < 0) test = getenv("LARECOMP_RUMBLE_TEST") != NULL;
    if (test == 1) {
        test = 2;
        phone_rumble(255, 1000);
        LOG("[rumble] test: one second at full strength on the phone's vibrator\n");
    }
#endif
    float gain = (g_settings.rumble < 0 ? 0 : g_settings.rumble > 300 ? 300 : g_settings.rumble) / 100.0f;  // (up to 3x: the game's rumble is soft, 4-10 % on the POCO, weak on a phone's motor)
    int amp[2];
    pthread_mutex_lock(&rum_mu);
    for (int s = 0; s < 2; s++) amp[s] = rum_amp(rum_v[s], rum_at[s], now, gain);
    pthread_mutex_unlock(&rum_mu);
    bool changed = false;
    for (int s = 0; s < 2; s++) changed |= (!amp[s] != !sent[s]) || abs(amp[s] - sent[s]) >= 8;
    bool renew = (amp[0] || amp[1]) && now - sent_at >= 200000000ull;
    if (!changed && !renew) return;
    sent[0] = amp[0], sent[1] = amp[1], sent_at = now;
    if (log) LOG("[rumble] left %d right %d of 255 (%s)\n", amp[0], amp[1], npads ? "controller" : "phone");
    if (!on) return;
    uint32_t ms = amp[0] || amp[1] ? 300 : 0;
    if (npads) {
        for (int p = 0; p < npads; p++) SDL_RumbleGamepad(pads[p], (uint16_t)(amp[0] * 257), (uint16_t)(amp[1] * 257), ms);
        return;
    }
    int m = amp[0] > amp[1] ? amp[0] : amp[1];
#ifdef __ANDROID__
    phone_rumble(m, (int)ms);
#else
    static SDL_Haptic *haptic;  // (a computer's own haptic device, if SDL lists one)
    static bool tried;
    if (!tried) {
        tried = true;
        int n = 0;
        SDL_HapticID *ids = SDL_GetHaptics(&n);
        for (int i = 0; ids && i < n && !haptic; i++) {
            SDL_Haptic *h = SDL_OpenHaptic(ids[i]);
            if (h && SDL_InitHapticRumble(h)) haptic = h;
            else if (h) SDL_CloseHaptic(h);
        }
        SDL_free(ids);
    }
    if (!haptic) return;
    if (m) SDL_PlayHapticRumble(haptic, m / 255.0f, ms);
    else SDL_StopHapticRumble(haptic);
#endif
}
