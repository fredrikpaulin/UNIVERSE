#ifndef _XCURSOR_H_
#define _XCURSOR_H_

#include <X11/Xlib.h>

/* Type definitions */
typedef int XcursorBool;
typedef unsigned int XcursorUInt;
typedef unsigned int XcursorDim;
typedef unsigned int XcursorPixel;

/* XcursorImage struct */
typedef struct {
    XcursorDim width;
    XcursorDim height;
    XcursorDim xhot;
    XcursorDim yhot;
    XcursorPixel *pixels;
    XcursorUInt delay;
} XcursorImage;

/* XcursorImages struct */
typedef struct {
    int nimage;
    XcursorImage **images;
} XcursorImages;

/* Function declarations */
XcursorImage *XcursorImageCreate(int width, int height);
void XcursorImageDestroy(XcursorImage *image);

XcursorImages *XcursorImagesCreate(int nimages);
void XcursorImagesDestroy(XcursorImages *images);

XcursorImages *XcursorImagesLoadFilename(const char *filename);
Cursor XcursorImageLoadCursor(Display *dpy, XcursorImage *image);
Cursor XcursorImagesLoadCursor(Display *dpy, XcursorImages *images);

const char *XcursorGetTheme(Display *dpy);
int XcursorGetDefaultSize(Display *dpy);

XcursorImages *XcursorLibraryLoadImages(const char *library, const char *theme, int size);
XcursorBool XcursorSupportsARGB(Display *dpy);
XcursorBool XcursorImageHash(XcursorImage *image, unsigned char *hash);

XcursorImages *XcursorShapeLoadImages(unsigned int shape, const char *theme, int size);
Cursor XcursorShapeLoadCursor(Display *dpy, unsigned int shape);
Cursor XcursorLibraryLoadCursor(Display *dpy, const char *library);

#endif /* _XCURSOR_H_ */
