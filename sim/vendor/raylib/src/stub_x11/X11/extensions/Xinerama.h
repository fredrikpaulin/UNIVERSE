#ifndef _XINERAMA_H_
#define _XINERAMA_H_

#include <X11/Xlib.h>

/* XineramaScreenInfo struct */
typedef struct {
    int screen_number;
    short x_org;
    short y_org;
    short width;
    short height;
} XineramaScreenInfo;

/* Function declarations */
Status XineramaIsActive(Display *dpy);
XineramaScreenInfo *XineramaQueryScreens(Display *dpy, int *number);
Bool XineramaQueryExtension(Display *dpy, int *event_base, int *error_base);

#endif /* _XINERAMA_H_ */
