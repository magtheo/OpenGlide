#ifndef OPENGLIDE_PROBE_H
#define OPENGLIDE_PROBE_H
#include <stddef.h>
#include <stdint.h>

/* Global: actually emit events into the focused surface/app. OFF by default so
 * the probe never types into the user's windows unprompted. Set with --fire. */
extern int g_fire;

typedef struct {
    const char *name;
    const char *description;
    int  (*init)(void);
    int  (*commit)(const char *utf8);  /* returns 0 ok; prints diagnostics to stdout */
    void (*fini)(void);
} Backend;

extern const Backend *xtest_backend(void);
extern const Backend *uinput_backend(void);
extern const Backend *im2_backend(void);   /* Wayland input-method-v2 */

/* Decode one UTF-8 sequence from *p into *cp, advance *p.
   Returns bytes consumed, or 0 on invalid sequence (caller skips 1 byte). */
int  utf8_next(const char **p, const char *end, uint32_t *cp);

/* Map a Unicode codepoint to an X11 keysym (Latin-1 identity, or
   0x01000000 + cp in the Unicode-keysym range). 0 if not representable. */
uint32_t cp_to_xkeysym(uint32_t cp);

#endif
