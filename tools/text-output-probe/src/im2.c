/* im2 backend: Wayland zwp_input_method_v2. This is the primary UTF-8 text-commit
 * path (ADR-0002). commit_string() hands arbitrary UTF-8 to the compositor with
 * no keycode/layout involvement, so Unicode is supported by construction.
 *
 * The probe binds the input-method for the seat and waits for `activate`. On
 * GNOME/mutter the seat's input-method slot is normally held by IBus, and/or
 * `activate` only fires when an app with text-input-v3 is focused — so the
 * observed behaviour here is itself a matrix data point. */
#include "probe.h"
#include <wayland-client.h>
#include "input-method-unstable-v2-client.h"
#include <stdio.h>
#include <string.h>
#include <poll.h>

static struct wl_display *disp;
static struct wl_seat *seat;
static struct zwp_input_method_manager_v2 *im_mgr;
static struct zwp_input_method_v2 *im;

static int im_activated = 0;
static int done_count = 0;
static int got_surrounding = 0;
static int got_content_type = 0;
static int protocol_error = 0;

static void im_activate(void *d, struct zwp_input_method_v2 *x){ (void)d;(void)x; im_activated=1; fprintf(stderr,"[im2] EVENT activate\n"); }
static void im_deactivate(void *d, struct zwp_input_method_v2 *x){ (void)d;(void)x; im_activated=0; fprintf(stderr,"[im2] EVENT deactivate\n"); }
static void im_surrounding(void *d, struct zwp_input_method_v2 *x, const char *t, uint32_t c, uint32_t a){
    (void)d;(void)x;(void)a; got_surrounding=1;
    fprintf(stderr,"[im2] EVENT surrounding_text len=%zu cursor_byte=%u\n", t?strlen(t):0, c);
}
static void im_tcc(void *d, struct zwp_input_method_v2 *x, uint32_t c){ (void)d;(void)x; fprintf(stderr,"[im2] EVENT text_change_cause=%u\n", c); }
static void im_ct(void *d, struct zwp_input_method_v2 *x, uint32_t h, uint32_t p){ (void)d;(void)x; got_content_type=1; fprintf(stderr,"[im2] EVENT content_type hint=%u purpose=%u\n", h, p); }
static void im_done(void *d, struct zwp_input_method_v2 *x){ (void)d;(void)x; done_count++; fprintf(stderr,"[im2] EVENT done (serial now %d)\n", done_count); }
static void im_unavailable(void *d, struct zwp_input_method_v2 *x){ (void)d;(void)x; fprintf(stderr,"[im2] EVENT unavailable (compositor rejected this IM)\n"); }

static const struct zwp_input_method_v2_listener im_listener = {
    im_activate, im_deactivate, im_surrounding, im_tcc, im_ct, im_done, im_unavailable
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver){
    (void)d;
    int interesting = (!strcmp(iface,"wl_seat") || strstr(iface,"input_method"));
    fprintf(stderr,"[im2] registry: %-40s v%-3u %s\n", iface, ver, interesting?"<-- bound":"");
    if(!strcmp(iface,"wl_seat"))
        seat = wl_registry_bind(r, name, &wl_seat_interface, ver<7?ver:7);
    else if(!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name){ (void)d;(void)r;(void)name; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

/* Dispatch for up to total_ms (50ms slices); stop early if activated. */
static void wait_for_activate(int total_ms){
    for (int ms = 0; ms < total_ms && !im_activated && !protocol_error; ms += 50) {
        while (wl_display_prepare_read(disp) != 0)
            wl_display_dispatch_pending(disp);
        wl_display_flush(disp);
        struct pollfd pfd = { wl_display_get_fd(disp), POLLIN, 0 };
        int pr = poll(&pfd, 1, 50);
        if (pr > 0) { wl_display_read_events(disp); wl_display_dispatch_pending(disp); }
        else        { wl_display_cancel_read(disp); }
    }
}
static void drain(int ms){ for(int i=0;i<ms/50;i++){ wait_for_activate(50); } }

static int im2_init(void){
    disp = wl_display_connect(NULL);
    if(!disp){ fprintf(stderr,"[im2] wl_display_connect failed (no Wayland?)\n"); return -1; }
    fprintf(stderr,"[im2] connected to Wayland display\n");
    struct wl_registry *reg = wl_display_get_registry(disp);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(disp);

    if(!seat){ fprintf(stderr,"[im2] FAIL: no wl_seat advertised\n"); return -1; }
    if(!im_mgr){
        fprintf(stderr,"[im2] FINDING: compositor does NOT advertise zwp_input_method_manager_v2.\n");
        fprintf(stderr,"[im2]   (GNOME/mutter exposes input-method-v2 only to registered IMs; a generic client cannot bind it directly.)\n");
        printf("[im2] RESULT: input-method-v2 manager not available to this client.\n");
        return -1;
    }
    im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat);
    zwp_input_method_v2_add_listener(im, &im_listener, NULL);
    wl_display_roundtrip(disp);
    fprintf(stderr,"[im2] bound zwp_input_method_v2; waiting up to 1500ms for activate...\n");
    return 0;
}

static int im2_commit(const char *utf8){
    wait_for_activate(1500);
    drain(200);
    fprintf(stderr,"[im2] state: activated=%d done_count=%d surrounding=%d content_type=%d\n",
            im_activated, done_count, got_surrounding, got_content_type);

    if(!im_activated){
        fprintf(stderr,"[im2] NOT activated. Likely causes: (a) the seat's input-method slot is\n");
        fprintf(stderr,"[im2] already held by another IM (IBus on GNOME), or (b) no application with\n");
        fprintf(stderr,"[im2] text-input-v3 is currently focused. commit_string would be a no-op here.\n");
        printf("[im2] RESULT: not activated; UTF-8 path exists but no active consumer — see stderr.\n");
        return 1;
    }

    zwp_input_method_v2_commit_string(im, utf8);
    zwp_input_method_v2_commit(im, (uint32_t)done_count);
    wl_display_flush(disp);
    drain(300);
    printf("[im2] RESULT: commit_string(\"%s\") sent; serial=%d; still activated=%d\n",
           utf8, done_count, im_activated);
    printf("[im2] UTF-8 handed to compositor verbatim (no keycode/layout) — arbitrary Unicode OK.\n");
    return 0;
}

static void im2_fini(void){
    if(im) zwp_input_method_v2_destroy(im);
    if(disp) wl_display_disconnect(disp);
}

static const Backend b = {
    "im2", "Wayland zwp_input_method_v2 commit_string (UTF-8)",
    im2_init, im2_commit, im2_fini
};
const Backend *im2_backend(void){ return &b; }
