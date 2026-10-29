#include "system_tray.h"

struct CroseTray { int unused; };

CroseTray *crose_tray_create(void *nativeWindow) { (void)nativeWindow; return 0; }
void crose_tray_update_icon(CroseTray *tray, const unsigned char *rgba, int width, int height)
{ (void)tray; (void)rgba; (void)width; (void)height; }
int crose_tray_take_requests(CroseTray *tray) { (void)tray; return 0; }
void crose_tray_destroy(CroseTray *tray) { (void)tray; }
