/*
 * Minimal Xrandr.h stub — provides type declarations so Raylib's GLFW/RGFW
 * compiles without libxrandr-dev installed. Actual Xrandr functions are
 * loaded dynamically at runtime via dlopen/dlsym (GLFW does this).
 */
#ifndef _XRANDR_H_STUB
#define _XRANDR_H_STUB

#include <X11/Xlib.h>

typedef unsigned long XRandrModeFlags;
typedef unsigned long RROutput;
typedef unsigned long RRCrtc;
typedef unsigned long RRMode;
typedef unsigned short Rotation;
typedef unsigned short SizeID;
typedef unsigned short SubpixelOrder;
typedef unsigned short Connection;

#define RROutputChangeNotifyMask (1L << 2)
#define RRNotify 1
#define RRNotify_CrtcChange 0
#define RRNotify_OutputChange 1

#define RR_Rotate_0   1
#define RR_Rotate_90  2
#define RR_Rotate_180 4
#define RR_Rotate_270 8
#define RR_Connected  0
#define RR_Interlace  0x00000020

typedef struct {
    unsigned long   id;
    unsigned int    width;
    unsigned int    height;
    unsigned long   dotClock;
    unsigned int    hSyncStart;
    unsigned int    hSyncEnd;
    unsigned int    hTotal;
    unsigned int    hSkew;
    unsigned int    vSyncStart;
    unsigned int    vSyncEnd;
    unsigned int    vTotal;
    char            *name;
    int             nameLength;
    XRandrModeFlags modeFlags;
} XRRModeInfo;

typedef struct {
    Time        timestamp;
    Time        configTimestamp;
    int         ncrtc;
    RRCrtc      *crtcs;
    int         noutput;
    RROutput    *outputs;
    int         nmode;
    XRRModeInfo *modes;
} XRRScreenResources;

typedef struct {
    Time            timestamp;
    int             x, y;
    unsigned int    width, height;
    RRMode          mode;
    Rotation        rotation;
    int             noutput;
    RROutput        *outputs;
    Rotation        rotations;
    int             npossible;
    RROutput        *possible;
} XRRCrtcInfo;

typedef struct {
    Time            timestamp;
    RRCrtc          crtc;
    char            *name;
    int             nameLen;
    unsigned long   mm_width;
    unsigned long   mm_height;
    Connection      connection;
    SubpixelOrder   subpixel_order;
    int             ncrtc;
    RRCrtc          *crtcs;
    int             nclone;
    RROutput        *clones;
    int             nmode;
    int             npreferred;
    RRMode          *modes;
} XRROutputInfo;

typedef struct {
    int     size;
    unsigned short  *red;
    unsigned short  *green;
    unsigned short  *blue;
} XRRCrtcGamma;

/* Function prototypes — GLFW loads these dynamically, but we need declarations */
XRRScreenResources *XRRGetScreenResourcesCurrent(Display *dpy, Window window);
void                XRRFreeScreenResources(XRRScreenResources *resources);
XRRCrtcInfo        *XRRGetCrtcInfo(Display *dpy, XRRScreenResources *resources, RRCrtc crtc);
void                XRRFreeCrtcInfo(XRRCrtcInfo *crtcInfo);
XRROutputInfo      *XRRGetOutputInfo(Display *dpy, XRRScreenResources *resources, RROutput output);
void                XRRFreeOutputInfo(XRROutputInfo *outputInfo);
RROutput            XRRGetOutputPrimary(Display *dpy, Window window);
XRRCrtcGamma       *XRRGetCrtcGamma(Display *dpy, RRCrtc crtc);
XRRCrtcGamma       *XRRAllocGamma(int size);
void                XRRFreeGamma(XRRCrtcGamma *gamma);
int                 XRRGetCrtcGammaSize(Display *dpy, RRCrtc crtc);
void                XRRSetCrtcGamma(Display *dpy, RRCrtc crtc, XRRCrtcGamma *gamma);
Status              XRRSetCrtcConfig(Display *dpy, XRRScreenResources *resources, RRCrtc crtc,
                                     Time timestamp, int x, int y, RRMode mode,
                                     Rotation rotation, RROutput *outputs, int noutputs);
Bool                XRRQueryExtension(Display *dpy, int *event_base_return, int *error_base_return);
Status              XRRQueryVersion(Display *dpy, int *major_version_return, int *minor_version_return);
void                XRRSelectInput(Display *dpy, Window window, int mask);
int                 XRRUpdateConfiguration(XEvent *event);

/* XRenderPictFormat — needed by GLFW but not included via any header.
 * GLFW uses it as a pointer type and accesses pf->direct.alphaMask. */
typedef struct {
    struct {
        short red, redMask, green, greenMask;
        short blue, blueMask, alpha, alphaMask;
    } direct;
    unsigned long id;
    int type;
    int depth;
    unsigned long colormap;
} XRenderPictFormat;

XRenderPictFormat *XRenderFindVisualFormat(Display *dpy, const Visual *visual);

#endif /* _XRANDR_H_STUB */
