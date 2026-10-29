#ifndef CROSE_SYSTEM_TRAY_H
#define CROSE_SYSTEM_TRAY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CroseTray CroseTray;

enum {
    CROSE_TRAY_TOGGLE = 1,
    CROSE_TRAY_EXIT = 2
};

// nativeWindow is the value returned by raylib GetWindowHandle().
CroseTray *crose_tray_create(void *nativeWindow);
void crose_tray_update_icon(CroseTray *tray, const unsigned char *rgba, int width, int height);
int crose_tray_take_requests(CroseTray *tray);
void crose_tray_destroy(CroseTray *tray);

#ifdef __cplusplus
}
#endif

#endif
