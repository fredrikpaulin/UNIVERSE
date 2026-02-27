/*
 * Minimal Xrender.h stub — provides type declarations so Raylib's GLFW/RGFW
 * compiles without libxrender-dev installed. Actual Xrender functions are
 * loaded dynamically at runtime via dlopen/dlsym.
 */
#ifndef _XRENDER_H_STUB
#define _XRENDER_H_STUB

#include <X11/Xlib.h>

/* Forward declaration of opaque type */
typedef struct _XRenderPictFormat XRenderPictFormat;

/* Direct color component structure */
typedef struct {
    short red;
    short redMask;
    short green;
    short greenMask;
    short blue;
    short blueMask;
    short alpha;
    short alphaMask;
} XRenderDirectFormat;

/* Picture format structure */
typedef struct {
    XRenderDirectFormat direct;
    /* Additional fields can be added here as needed */
} XRenderPictFormat;

/* Function declarations */
XRenderPictFormat *XRenderFindVisualFormat(Display *dpy, Visual *visual);

#endif /* _XRENDER_H_STUB */
