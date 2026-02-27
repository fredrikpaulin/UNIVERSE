#ifndef _XINPUT2_H_
#define _XINPUT2_H_

#include <X11/Xlib.h>

/* Constants */
#define XIAllDevices               0
#define XIAllMasterDevices         1

#define XI_RawMotion               17
#define XI_Motion                  6
#define XI_Enter                   7
#define XI_Leave                   8
#define XI_FocusIn                 9
#define XI_FocusOut                10
#define XI_ButtonPress             4
#define XI_ButtonRelease           5
#define XI_KeyPress                2
#define XI_KeyRelease              3
#define XI_LASTEVENT               27

#define XISlaveKeyboard            2

/* Macros */
#define XISetMask(ptr, event)      (((unsigned char*)(ptr))[(event)>>3] |= (1 << ((event) & 7)))
#define XIClearMask(ptr, event)    (((unsigned char*)(ptr))[(event)>>3] &= ~(1 << ((event) & 7)))
#define XIMaskIsSet(ptr, event)    (((unsigned char*)(ptr))[(event)>>3] & (1 << ((event) & 7)))
#define XIMaskLen(event)           (((event) >> 3) + 1)

/* Struct definitions */
typedef struct {
    int deviceid;
    int mask_len;
    unsigned char *mask;
} XIEventMask;

typedef struct {
    int mask_len;
    unsigned char *mask;
    double *values;
} XIValuatorState;

typedef struct {
    int base_mods;
    int latched_mods;
    int locked_mods;
    int effective_mods;
} XIModifierState;

typedef struct {
    int base_group;
    int latched_group;
    int locked_group;
    int effective_group;
} XIGroupState;

typedef struct {
    int mask_len;
    unsigned char *mask;
} XIButtonState;

typedef struct {
    int extension;
    int evtype;
    XID serial;
    Time time;
    Display *display;
    int deviceid;
    int sourceid;
    int detail;
    Window root;
    Window event;
    Window child;
    double root_x;
    double root_y;
    double event_x;
    double event_y;
    int flags;
    XIButtonState buttons;
    XIValuatorState valuators;
    XIModifierState mods;
    XIGroupState group;
} XIDeviceEvent;

typedef struct {
    int extension;
    int evtype;
    XID serial;
    Time time;
    Display *display;
    int deviceid;
    int sourceid;
    int detail;
    int flags;
    double *raw_values;
    XIValuatorState valuators;
} XIRawEvent;

/* Function declarations */
Status XISelectEvents(Display *dpy, Window win, XIEventMask *masks, int num_masks);
Status XIQueryVersion(Display *dpy, int *major_version_inout, int *minor_version_inout);

#endif /* _XINPUT2_H_ */
