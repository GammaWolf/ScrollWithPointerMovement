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

void set_default_config_values(struct Config* cfg) {
    cfg->allow_horizontal_scroll = 0;
    cfg->mouse_move_delta_to_scroll_threshold = 10;
}

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

// TODO delcaration/definition order?
int main(int argc, char **argv)
{
    struct Config cfg;
    set_default_config_values(&cfg);
    parse_args_to_cfg(argc, argv, &cfg);

    /// main logic

    char* display_name = NULL;
    Display *dpy;
    if ((dpy = XOpenDisplay(display_name)) == NULL) {
        // TODO
        return -1;
    }

    check_requirements(dpy);

    Window window =  DefaultRootWindow(dpy);
    select_events(dpy, window);

    Window  root_ret, child_ret;
    int root_x, root_y;
    int win_x, win_y;
    unsigned int mask;
    XQueryPointer(dpy, window,
                  &root_ret, &child_ret,
                  &root_x, &root_y,
                  &win_x, &win_y,
                  &mask);

    int start_x = root_x;
    int start_y = root_y;

    // vars for keeping track of accumulated pointer movement over time
    double total_movement_y_delta = 0;
    double total_movement_x_delta = 0;

    while(1) {
        XEvent ev;
        XGenericEventCookie *cookie = &ev.xcookie;
        XIRawEvent *re;

        XNextEvent(dpy, &ev);

        if (cookie->type != GenericEvent ||
                !XGetEventData(dpy, cookie))
            continue;

        switch (cookie->evtype) {

        case XI_RawMotion:
            re = (XIRawEvent *) cookie->data;
            XQueryPointer(dpy, window,
                          &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);
            //            printf ("raw %g,%g root %d,%d\n",
            //                    re->raw_values[0], re->raw_values[1],
            //                    root_x, root_y);

            /// fixate pointer (set pointer to start pos)
            XWarpPointer(dpy, None, window, 0, 0, 0, 0, start_x, start_y);

            /// translate movement to scrol

            double deltaX = re->raw_values[0];
            double deltaY = re->raw_values[1];

            total_movement_y_delta += deltaY;
            if (fabs(total_movement_y_delta) > cfg.mouse_move_delta_to_scroll_threshold)
            {
                int scroll_amount = (int) (total_movement_y_delta / cfg.mouse_move_delta_to_scroll_threshold);
                trigger_scroll(dpy, SCROLL_VERTICAL, scroll_amount);

                // adjust accumulator: reduce for distance traveled that is 'used up' by scrolling
                // example: y mouse delta is 22, scroll threshold is 10, then scrollamount is 2 (2*10) and the (2*10) is subtracted from accumulator
                // the (abs) new value of total_movement_y_delta is smaller mouse_move_delta_to_scroll_threshold
                int scroll_amount_as_movement_amount = scroll_amount * (int) cfg.mouse_move_delta_to_scroll_threshold;
                total_movement_y_delta -= scroll_amount_as_movement_amount;
            }

            if (cfg.allow_horizontal_scroll)
            {
                total_movement_x_delta += deltaX;
                if (fabs(total_movement_x_delta) > cfg.mouse_move_delta_to_scroll_threshold)
                {
                    int scroll_amount = (int) (total_movement_x_delta / cfg.mouse_move_delta_to_scroll_threshold);
                    trigger_scroll(dpy, SCROLL_HORIZONTAL, scroll_amount);

                    int scroll_amount_as_movement_amount = scroll_amount * (int) cfg.mouse_move_delta_to_scroll_threshold;
                    total_movement_x_delta -= scroll_amount_as_movement_amount;
                }
            }

            XFlush(dpy); // may not be necessary
            break;
        }
        XFreeEventData(dpy, cookie);
    }

    return 0;
}
