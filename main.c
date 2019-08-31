// based on https://keithp.com/blogs/Cursor_tracking/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

enum ScrollDirection
{
    SCROLL_HORIZONTAL = 1,
    SCROLL_VERTICAL = 2
};

enum ScrollDirectionLinuxButtons
{
    SCROLL_UP = 4,
    SCROLL_DOWN = 5,
    SCROLL_LEFT = 6,
    SCROLL_RIGHT = 7
};

struct Config {
    uint mouse_move_delta_to_scroll_threshold;
    int allow_horizontal_scroll;
};

struct ScreenPoint {
    int x;
    int y;
};

// tell Xlib that we want to receive pointer motion events
static void select_events(Display *dpy, Window win)
{
    XIEventMask evmasks[1];
    unsigned char mask1[(XI_LASTEVENT + 7)/8];

    memset(mask1, 0, sizeof(mask1));

    /* select for button and key events from all master devices */
    XISetMask(mask1, XI_RawMotion);

    evmasks[0].deviceid = XIAllMasterDevices;
    evmasks[0].mask_len = sizeof(mask1);
    evmasks[0].mask = mask1;

    XISelectEvents(dpy, win, evmasks, 1);
    XFlush(dpy);
}

/* Return 1 if XI2 is available, 0 otherwise */
static int has_xi2(Display *dpy)
{
    /* We support XI 2.2 */
    int major = 2;
    int minor = 2;

    int rc = XIQueryVersion(dpy, &major, &minor);
    if (rc == BadRequest) {
        printf("No XI2 support. Server supports version %d.%d only.\n", major, minor);
        return 0;
    } else if (rc != Success) {
        fprintf(stderr, "Internal Error! This is a bug in Xlib.\n");
    }
    //    printf("XI2 supported. Server provides version %d.%d.\n", major, minor);

    return 1;
}

// scroll wheel input in linux is modelled as (mouse) buttons presses
// Button 4: (scrolls up), Button 5 (scrolls down)
// Button 6 (scrolls left), Button 7 (scrolls right)
void trigger_scroll(Display* display, enum ScrollDirection scrollDirection, int amount)
{
    if (amount == 0) return;

    uint negative_scroll_amount_btn = scrollDirection == SCROLL_VERTICAL ? SCROLL_UP : SCROLL_LEFT;
    uint postitive_scroll_amount_btn = scrollDirection == SCROLL_VERTICAL ? SCROLL_DOWN : SCROLL_RIGHT;
    uint scroll_button = amount <  0 ? negative_scroll_amount_btn : postitive_scroll_amount_btn;
    //    printf("scroll_button %d\n", scroll_button);

    for (int i = 0; i < abs(amount); i++)
    {
        XTestFakeButtonEvent(display, scroll_button, 1, CurrentTime); // "button" down
        XTestFakeButtonEvent(display, scroll_button, 0, CurrentTime); // "button" up
    }
}

struct ScreenPoint get_pointer_position(Display* display, Window window)
{
    struct ScreenPoint pos;

    Window  root_ret, child_ret;
    int win_x, win_y;
    unsigned int mask;
    XQueryPointer(display, window,
                  &root_ret, &child_ret,
                  &pos.x, &pos.y,
                  &win_x, &win_y,
                  &mask);

    return pos;
}

void parse_args_to_cfg(int argc, char** argv, struct Config* cfg) {
    char *cvalue = NULL;
    int c;

    if (argc > 1) {
        while ((c = getopt (argc, argv, "Hhs:")) != -1)
            switch (c)
            {
            case 's':
                cvalue = optarg;
                uintmax_t num = strtoumax(optarg, NULL, 10);
                if (num == UINTMAX_MAX && errno == ERANGE)
                    abort();
                cfg->mouse_move_delta_to_scroll_threshold = (uint) num;
                break;
            case 'h': // print help
                printf("Converts X pointer movement (mouse, touchpad, trackpoint, trackball) to scroll wheel events.\n\n");
                printf("Options:\n");
                printf("-s [SCROLL_SPEED_VALUE]\n");
                printf("-H\tallow horizontal scrolling\n");
                printf("-h\tshow this help");
                exit(0);
            case 'H':
                cfg->allow_horizontal_scroll = 1;
                break;
            case '?':
                if (optopt == 's')
                    fprintf (stderr, "Option -%s requires an argument.\n", optopt);
                else if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character `\\x%x'.\n",
                             optopt);
                exit(1);
            default:
                abort ();
            }
    }
}

void check_requirements(Display* dpy)
{
    int xi_opcode, event, error;
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) {
        fprintf(stderr, "Error: X Input extension not available.");
        exit(-3);
    }

    if (!has_xi2(dpy))
    {
        exit(-4);
    }
}

struct Config create_default_config()
{
    struct Config cfg = {
        .allow_horizontal_scroll = 0,
                .mouse_move_delta_to_scroll_threshold = 10
    };
    return cfg;
}

void check_for_scroll(double* total_movement_y_delta, double deltaY, struct Config cfg, enum ScrollDirection scroll_direction, Display* display)
{
    *total_movement_y_delta += deltaY;
    if (fabs(*total_movement_y_delta) > cfg.mouse_move_delta_to_scroll_threshold)
    {
        int scroll_amount = (int) (*total_movement_y_delta / cfg.mouse_move_delta_to_scroll_threshold);
        trigger_scroll(display, scroll_direction, scroll_amount);

        // adjust accumulator: reduce for distance traveled that is 'used up' by scrolling
        // example: y mouse delta is 22, scroll threshold is 10, then scrollamount is 2 (2*10) and the (2*10) is subtracted from accumulator
        // the (abs) new value of total_movement_y_delta is smaller mouse_move_delta_to_scroll_threshold
        int scroll_amount_as_movement_amount = scroll_amount * (int) cfg.mouse_move_delta_to_scroll_threshold;
        *total_movement_y_delta -= scroll_amount_as_movement_amount;
    }
}

// TODO delcaration/definition order?
int main(int argc, char **argv)
{
    struct Config cfg = create_default_config();
    parse_args_to_cfg(argc, argv, &cfg);

    char* display_name = NULL;
    Display *dpy;
    if ((dpy = XOpenDisplay(display_name)) == NULL) {
        fprintf(stderr, "Failed to open display.\n");
        return -1;
    }

    check_requirements(dpy);

    Window window =  DefaultRootWindow(dpy);
    select_events(dpy, window);

    struct ScreenPoint start_pointer_pos = get_pointer_position(dpy, window);

    // vars for keeping track of accumulated pointer movement over time
    double total_movement_y_delta = 0;
    double total_movement_x_delta = 0;

    while(1) {
        XEvent ev;
        XGenericEventCookie* cookie = &ev.xcookie;

        XNextEvent(dpy, &ev);

        if (cookie->type != GenericEvent ||
                !XGetEventData(dpy, cookie))
            continue;

        switch (cookie->evtype) {
        case XI_RawMotion:
            /// fixate pointer (set pointer to start pos)
            XWarpPointer(dpy, None, window, 0, 0, 0, 0, start_pointer_pos.x, start_pointer_pos.y);

            /// update total movement deltas and check if we need to scroll
            XIRawEvent* raw_event = (XIRawEvent*) cookie->data;
            double deltaX = raw_event->raw_values[0];
            double deltaY = raw_event->raw_values[1];

            check_for_scroll(&total_movement_y_delta, deltaY, cfg, SCROLL_VERTICAL, dpy);
            if (cfg.allow_horizontal_scroll)
                check_for_scroll(&total_movement_x_delta, deltaX, cfg, SCROLL_HORIZONTAL, dpy);

            XFlush(dpy); // may not be necessary
            break;
        }
        XFreeEventData(dpy, cookie);
    }

    return 0;
}
