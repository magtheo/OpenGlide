/* uinput backend: creates a kernel virtual keyboard and injects raw EV_KEY
 * events. evdev has no representation of arbitrary Unicode codepoints — only
 * physical key codes — so only ASCII mapped to evdev KEY_* is injectable.
 * This is the lowest level of the raw-key fallback (ADR-0002).
 * Resolves injectability unconditionally; emits only when g_fire. */
#include "probe.h"
#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static int fd = -1;

static void emit(int type, int code, int val) {
    struct input_event ev; memset(&ev, 0, sizeof ev);
    ev.type = type; ev.code = code; ev.value = val;
    if (write(fd, &ev, sizeof ev) < 0) { /* best-effort; device may be torn down */ }
}
static void tap_key(int code) {
    emit(EV_KEY, code, 1); emit(EV_SYN, SYN_REPORT, 0);
    emit(EV_KEY, code, 0); emit(EV_SYN, SYN_REPORT, 0);
}
static void msleep(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int ascii_to_evdev(uint32_t cp, int *key, int *shift) {
    *shift = 0;
    static const int letters[26] = {
        KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,
        KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,
        KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z
    };
    static const int digits[10] = {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
    if (cp >= 'a' && cp <= 'z') { *key = letters[cp - 'a']; return 1; }
    if (cp >= 'A' && cp <= 'Z') { *key = letters[cp - 'A']; *shift = 1; return 1; }
    if (cp >= '0' && cp <= '9') { *key = digits[cp - '0']; return 1; }
    switch (cp) {
        case ' ':  *key = KEY_SPACE;  return 1;
        case '.':  *key = KEY_DOT;    return 1;
        case ',':  *key = KEY_COMMA;  return 1;
        case '\n': *key = KEY_ENTER;  return 1;
        case '\t': *key = KEY_TAB;    return 1;
        case '-':  *key = KEY_MINUS;  return 1;
        case '=':  *key = KEY_EQUAL;  return 1;
        default:   return 0;
    }
}

static int uinput_init(void) {
    if (!g_fire) { printf("[uinput] dry-run: skipping device creation\n"); return 0; }
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
        KEY_SPACE, KEY_DOT, KEY_COMMA, KEY_ENTER, KEY_TAB, KEY_MINUS, KEY_EQUAL,
        KEY_LEFTSHIFT
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, keys[i]);

    struct uinput_setup us; memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB;
    us.id.vendor  = 0x8888;
    us.id.product = 0x0001;
    us.id.version = 1;
    strncpy(us.name, "openglide-text-probe", UINPUT_MAX_NAME_SIZE);
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0) { perror("UI_DEV_SETUP"); close(fd); fd=-1; return -1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0)     { perror("UI_DEV_CREATE"); close(fd); fd=-1; return -1; }
    msleep(250);
    printf("[uinput] virtual keyboard 'openglide-text-probe' created on fd %d\n", fd);
    return 0;
}

static int uinput_commit(const char *utf8) {
    const char *p = utf8, *end = utf8 + strlen(utf8);
    uint32_t cp; int inj = 0, skip = 0;
    printf("[uinput] per-codepoint:\n");
    while (p < end) {
        int n = utf8_next(&p, end, &cp);
        if (!n) { skip++; continue; }
        int key, shift;
        if (!ascii_to_evdev(cp, &key, &shift)) {
            skip++;
            printf("  U+%04X  -> NO evdev keycode (uninjectable; evdev has no Unicode)\n", cp);
            continue;
        }
        if (g_fire && fd >= 0) {
            if (shift) { emit(EV_KEY, KEY_LEFTSHIFT, 1); emit(EV_SYN, SYN_REPORT, 0); }
            tap_key(key);
            if (shift) { emit(EV_KEY, KEY_LEFTSHIFT, 0); emit(EV_SYN, SYN_REPORT, 0); }
        }
        inj++;
        printf("  U+%04X  -> evdev key=%d%s -> %s\n", cp, key,
               shift ? " (shift)" : "", g_fire ? "emitted" : "emit-able (dry-run)");
    }
    printf("[uinput] RESULT: emit-able=%d  uninjectable=%d   [fire=%d]\n", inj, skip, g_fire);
    if (skip) printf("[uinput] evdev cannot represent non-ASCII; arbitrary UTF-8 impossible here — confirmed.\n");
    return 0;
}

static void uinput_fini(void) {
    if (fd >= 0) { ioctl(fd, UI_DEV_DESTROY); close(fd); fd = -1; }
}

static const Backend b = {
    "uinput", "Linux uinput virtual-keyboard raw-key injection",
    uinput_init, uinput_commit, uinput_fini
};
const Backend *uinput_backend(void) { return &b; }
