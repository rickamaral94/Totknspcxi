// Software keyboard (TOTK: naming a horse at a stable). On the Switch nn::swkbd::ShowKeyboard launches the keyboard
// library applet; our am service has no applets, so the game crashed. ShowKeyboard is replaced here: the overlay
// (runtime/ui/menu.mm menu_keyboard_run) asks for the text - typed directly with a keyboard, Android's keyboard on a
// touch screen, an on-screen keyboard with a controller - while this (game) thread keeps re-presenting the last frame.
//
// The argument layout, the UTF-16 / UTF-8 answer, the disabled keys and the cancel result follow totk-android's
// games/totk/runtime/swkbd_host.c (MIT, Copyright (c) 2026 MacNativeGames contributors; NOTICE.md), which read them from
// TOTK's SDK: ShowKeyboardArg = KeyboardConfig (0x4d0 bytes: type +0, OK text +4, disabled keys +0x1c, header +0x24,
// sub text +0xa6, guide +0x1a8 - UTF-16; max length +0x3ac, min +0x3b0, password +0x3b4, UTF-8 answer +0x3bd, initial
// text offset / length +0x3c0 / +0x3c4), then the work buffer {pointer, size} at +0x4d0 / +0x4d8 (the initial text
// lives there). String = {pointer, byte size}. The horse's name is a user-name preset: 9 characters at most, '@' '%'
// '\' disabled, answered in UTF-16 - this used to write UTF-8 there, and a name with accents came out '?' both ways.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../platform.h"

int menu_keyboard_run(const char *prompt, const char *initial, int max_len, char *out, int cap);  // runtime/ui/menu.mm

enum {
    KB_TYPE = 0x00, KB_DISABLED = 0x1c, KB_HEADER = 0x24, KB_SUB = 0xa6, KB_GUIDE = 0x1a8,
    KB_MAX_LEN = 0x3ac, KB_MIN_LEN = 0x3b0, KB_PASSWORD = 0x3b4, KB_UTF8 = 0x3bd, KB_INIT_OFF = 0x3c0, KB_INIT_LEN = 0x3c4,
    ARG_WORK = 0x4d0, ARG_WORK_SIZE = 0x4d8,
    KB_TYPE_NUMPAD = 1, KB_LIMIT = 64, SWKBD_CANCELED = 0x29f,
};

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static size_t u16_len(const uint8_t *s, size_t max) {
    size_t n = 0;
    while (n < max && (s[2 * n] | s[2 * n + 1])) n++;
    return n;
}

// UTF-16 (n units, little-endian bytes) -> NUL-terminated UTF-8 in cap bytes, cut at a character boundary
static size_t utf16_to_utf8(const uint8_t *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    if (!cap) return 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = s[2 * i] | s[2 * i + 1] << 8, d = i + 1 < n ? (uint32_t)(s[2 * i + 2] | s[2 * i + 3] << 8) : 0;
        if (c >= 0xd800 && c < 0xdc00 && d >= 0xdc00 && d < 0xe000) c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00), i++;
        else if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
        char b[4];
        size_t k;
        if (c < 0x80) b[0] = (char)c, k = 1;
        else if (c < 0x800) b[0] = (char)(0xc0 | c >> 6), b[1] = (char)(0x80 | (c & 0x3f)), k = 2;
        else if (c < 0x10000)
            b[0] = (char)(0xe0 | c >> 12), b[1] = (char)(0x80 | ((c >> 6) & 0x3f)), b[2] = (char)(0x80 | (c & 0x3f)), k = 3;
        else
            b[0] = (char)(0xf0 | c >> 18), b[1] = (char)(0x80 | ((c >> 12) & 0x3f)), b[2] = (char)(0x80 | ((c >> 6) & 0x3f)),
            b[3] = (char)(0x80 | (c & 0x3f)), k = 4;
        if (o + k + 1 > cap) break;
        memcpy(out + o, b, k);
        o += k;
    }
    out[o] = 0;
    return o;
}

// UTF-8 -> UTF-16 units, at most cap (a character that doesn't fit whole is dropped); bad bytes become U+FFFD
static size_t utf8_to_utf16(const char *s, uint16_t *out, size_t cap) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = 0;
    while (*p) {
        uint32_t c;
        int k;
        if (*p < 0x80) c = *p, k = 0;
        else if ((*p & 0xe0) == 0xc0) c = *p & 0x1f, k = 1;
        else if ((*p & 0xf0) == 0xe0) c = *p & 0x0f, k = 2;
        else if ((*p & 0xf8) == 0xf0) c = *p & 0x07, k = 3;
        else c = 0xfffd, k = 0;
        p++;
        for (int i = 0; i < k; i++, p++) {
            if ((*p & 0xc0) != 0x80) { c = 0xfffd; break; }
            c = c << 6 | (*p & 0x3f);
        }
        if (c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) c = 0xfffd;
        if (c >= 0x10000) {
            if (n + 2 > cap) break;
            out[n++] = (uint16_t)(0xd800 + ((c - 0x10000) >> 10));
            out[n++] = (uint16_t)(0xdc00 + ((c - 0x10000) & 0x3ff));
        } else {
            if (n + 1 > cap) break;
            out[n++] = (uint16_t)c;
        }
    }
    return n;
}

// What the applet lets through. The disabled-key bits: 1 space, 2 '@', 3 '%', 4 '/', 5 '\', 6 digits.
static int allowed(uint16_t c, uint32_t disabled, int digits_only) {
    int digit = c >= '0' && c <= '9';
    if (digits_only && !digit) return 0;
    if (c < 0x20 || c == 0x7f) return 0;
    if ((disabled & 1u << 1 && c == ' ') || (disabled & 1u << 2 && c == '@') || (disabled & 1u << 3 && c == '%')) return 0;
    if ((disabled & 1u << 4 && c == '/') || (disabled & 1u << 5 && c == '\\') || (disabled & 1u << 6 && digit)) return 0;
    return 1;
}

// Drop what isn't allowed, keep at most max units without splitting a surrogate pair
static size_t clean(uint16_t *t, size_t n, size_t max, uint32_t disabled, int digits_only) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        int pair = t[i] >= 0xd800 && t[i] < 0xdc00 && i + 1 < n;
        size_t k = pair ? 2 : 1;
        if (!pair && !allowed(t[i], disabled, digits_only)) continue;
        if (o + k > max) break;
        t[o++] = t[i];
        if (pair) t[o++] = t[++i];
    }
    return o;
}

// nn::swkbd::ShowKeyboard(nn::swkbd::String *out, const nn::swkbd::ShowKeyboardArg &arg) -> nn::Result
HLE_FUNC("_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE", swkbd_show) {
    uint64_t out = c->x[0];
    const uint8_t *arg = (const uint8_t *)(uintptr_t)c->x[1];
    if (!out || !arg) { c->x[0] = SWKBD_CANCELED; return; }
    uint32_t type = rd32(arg + KB_TYPE), disabled = rd32(arg + KB_DISABLED), password = rd32(arg + KB_PASSWORD);
    int32_t max_len = (int32_t)rd32(arg + KB_MAX_LEN), min_len = (int32_t)rd32(arg + KB_MIN_LEN);
    int utf8 = arg[KB_UTF8] != 0;
    if (max_len <= 0 || max_len > KB_LIMIT) max_len = KB_LIMIT;
    if (min_len < 0) min_len = 0;
    if (min_len > max_len) min_len = max_len;
    char header[0x41 * 4], guide[0x101 * 4], initial[KB_LIMIT * 4 + 1] = "";
    utf16_to_utf8(arg + KB_HEADER, u16_len(arg + KB_HEADER, 0x40), header, sizeof header);
    utf16_to_utf8(arg + KB_GUIDE, u16_len(arg + KB_GUIDE, 0x100), guide, sizeof guide);
    if (!*header) utf16_to_utf8(arg + KB_SUB, u16_len(arg + KB_SUB, 0x80), header, sizeof header);
    uint64_t work = rd64(arg + ARG_WORK), work_size = rd64(arg + ARG_WORK_SIZE);
    int32_t init_off = (int32_t)rd32(arg + KB_INIT_OFF), init_len = (int32_t)rd32(arg + KB_INIT_LEN);
    if (init_len > 0 && work && init_off >= 0 && (uint64_t)init_off + (uint64_t)init_len * 2 <= work_size)
        utf16_to_utf8((const uint8_t *)(uintptr_t)(work + (uint64_t)init_off), (size_t)(init_len > KB_LIMIT ? KB_LIMIT : init_len),
                      initial, sizeof initial);
    char text[KB_LIMIT * 4 + 1] = "";
    int confirmed = menu_keyboard_run(*header ? header : guide, initial, max_len, text, sizeof text);
    uint16_t u[KB_LIMIT * 2];
    size_t n = confirmed ? clean(u, utf8_to_utf16(text, u, KB_LIMIT * 2), (size_t)max_len, disabled, type == KB_TYPE_NUMPAD) : 0;
    if (confirmed && (!n || (int32_t)n < min_len)) confirmed = 0, n = 0;  // (an empty name counts as cancelled)
    uint64_t buf = LD64(out), size = LD64(out + 8);
    if (buf && size) {
        plat_write_prepare(buf, size);
        memset((void *)(uintptr_t)buf, 0, size);
        if (utf8) {
            uint8_t le[KB_LIMIT * 4];
            for (size_t i = 0; i < n; i++) le[2 * i] = (uint8_t)u[i], le[2 * i + 1] = (uint8_t)(u[i] >> 8);
            utf16_to_utf8(le, n, (char *)(uintptr_t)buf, size);
        } else if (size >= 2) {
            size_t cap = size / 2 - 1;  // (units; one kept for the terminator)
            if (n > cap) {
                n = cap;
                if (n && u[n - 1] >= 0xd800 && u[n - 1] < 0xdc00) n--;
            }
            memcpy((void *)(uintptr_t)buf, u, n * 2);
        }
    }
    LOG("[swkbd] \"%s\" (type %u, %d-%d characters, disabled keys %#x, %s answer, initial \"%s\"): %s%s%s\n", header, type,
        min_len, max_len, disabled, utf8 ? "UTF-8" : "UTF-16", initial, confirmed ? "\"" : "cancelled",
        confirmed ? (password ? "(password)" : text) : "", confirmed ? "\"" : "");
    c->x[0] = confirmed ? 0 : SWKBD_CANCELED;
}
