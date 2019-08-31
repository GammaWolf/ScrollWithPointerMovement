// converts pointer (mouse, trackpad, ...) movements into scroll wheel events
// uses XLib and XLib extension XInput2
// cursor pos tracking based on https://keithp.com/blogs/Cursor_tracking/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

const char* PROGRAM_VERSION = "0.1";

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
    Bool allow_horizontal_scroll;
    Bool allow_triggering_of_repeated_scroll_event;
    Bool show_debug_output;
};

struct ScreenPoint {
    int x;
    int y;
};

// tell Xlib that we want to receive pointer motion events
static void request_to_receive_pointer_move_events(Display *dpy, Window win)
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
        fprintf(stderr, "No XI2 support. Server supports version %d.%d only.\n", major, minor);
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
void trigger_scroll(Display* display, struct Config* cfg, enum ScrollDirection scrollDirection, int amount)
{
    if (amount == 0) return;

    if (cfg->show_debug_output)
        printf("scroll %s, %dx %s\n",
               scrollDirection == SCROLL_VERTICAL ? "v" : "h",
               cfg->allow_triggering_of_repeated_scroll_event ? abs(amount) : 1,
               amount < 0 ? "up" : "down");

    uint negative_scroll_amount_btn = scrollDirection == SCROLL_VERTICAL ? SCROLL_UP : SCROLL_LEFT;
    uint postitive_scroll_amount_btn = scrollDirection == SCROLL_VERTICAL ? SCROLL_DOWN : SCROLL_RIGHT;
    uint scroll_button = amount <  0 ? negative_scroll_amount_btn : postitive_scroll_amount_btn;

    for (int i = 0; i < abs(amount); i++)
    {
        XTestFakeButtonEvent(display, scroll_button, 1, CurrentTime); // "button" down
        XTestFakeButtonEvent(display, scroll_button, 0, CurrentTime); // "button" up
        if (!cfg->allow_triggering_of_repeated_scroll_event)
            break;
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

void parse_args_into_config(int argc, char** argv, struct Config* cfg) {
    char *cvalue = NULL;
    int c;

    if (argc > 1) {
        while ((c = getopt (argc, argv, "Hdrhvc:")) != -1)
            switch (c)
            {
            case 's':
                cvalue = optarg;
                intmax_t num = strtoimax(optarg, NULL, 10);
                if (errno == ERANGE)
                {
                    fprintf(stderr, "error parsing value for -s. It must be a positive integer.");
                    exit(-1);
                }
                cfg->mouse_move_delta_to_scroll_threshold = (uint) labs(num);
                printf("s arg: %s %d", optarg, cfg->mouse_move_delta_to_scroll_threshold);
                break;
            case 'h': // print help
                printf("Converts X pointer movement (mouse, touchpad, trackpoint, trackball) to scroll wheel events.\n\n");
                printf("Options:\n");
                printf("-c [x:int]\tconversion distance: pointer travel distance (in pixels) required to trigger a scroll. Determines how frequently scrolling occurs. A lower number mean more frequent scroll events.\n");
                printf("-r\t\tallow multiple scroll events to be generated from a fast wide pointer move\n");
                printf("-H\t\tallow horizontal scrolling\n");
                printf("-d\t\tenable debug logging\n");
                printf("-v\t\tshow version\n");
                printf("-h\t\tshow this help\n");
                exit(0);
            case 'H':
                cfg->allow_horizontal_scroll = True;
                break;
            case 'r':
                cfg->allow_triggering_of_repeated_scroll_event = True;
                break;
            case 'd':
                cfg->show_debug_output = True;
                break;
            case 'v':
                printf("%s\n", PROGRAM_VERSION);
                exit(0);
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

void ensure_requirements_or_exit(Display* display)
{
    int xi_opcode, event, error;
    if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event, &error)) {
        fprintf(stderr, "Error: X Input extension not available.");
        exit(-3);
    }

    if (!has_xi2(display))
        exit(-4);
}

Display* open_display_or_exit()
{
    char* display_name = NULL;
    Display* display;
    if ((display = XOpenDisplay(display_name)) == NULL) {
        fprintf(stderr, "Failed to open display.\n");
        exit(-1);
    }
    return display;
}

struct Config create_default_config()
{
    struct Config cfg =
    {
        .mouse_move_delta_to_scroll_threshold = 50,
        .allow_horizontal_scroll = False,
        .allow_triggering_of_repeated_scroll_event = False,
        .show_debug_output = False,
    };
    return cfg;
}

void check_for_scroll_trigger(enum ScrollDirection scroll_direction, double* total_movement_delta, double delta, struct Config* cfg, Display* display)
{
    if (cfg->show_debug_output)
        printf ("check: dir: %s, total_movement_delta: %g, delta: %g, thres: %d\n",
                scroll_direction == SCROLL_VERTICAL ? "v" : "h",
                *total_movement_delta,
                delta,
                cfg->mouse_move_delta_to_scroll_threshold);

    *total_movement_delta += delta;
    if (fabs(*total_movement_delta) > cfg->mouse_move_delta_to_scroll_threshold)
    {
        int scroll_amount = (int) (*total_movement_delta / cfg->mouse_move_delta_to_scroll_threshold);
        trigger_scroll(display, cfg, scroll_direction, scroll_amount);

        // adjust accumulator: reduce for distance traveled that is 'used up' by scrolling
        // example: y mouse delta is 22, scroll threshold is 10, then scrollamount is 2 (2*10) and the (2*10) is subtracted from accumulator
        // the (abs) new value of total_movement_y_delta is smaller mouse_move_delta_to_scroll_threshold
        int scroll_amount_as_movement_amount = scroll_amount * (int) cfg->mouse_move_delta_to_scroll_threshold;
        *total_movement_delta -= scroll_amount_as_movement_amount;
    }
}

int main(int argc, char **argv)
{
    struct Config cfg = create_default_config();
    parse_args_into_config(argc, argv, &cfg);

    Display* display = open_display_or_exit();
    ensure_requirements_or_exit(display);

    Window window =  DefaultRootWindow(display);
    request_to_receive_pointer_move_events(display, window);

    struct ScreenPoint start_pointer_pos = get_pointer_position(display, window);

    // vars for keeping track of accumulated pointer movement over time. Helps to decide when to scroll and how much.
    double total_movement_y_delta = 0;
    double total_movement_x_delta = 0;

    while(1) {
        XEvent ev;
        XGenericEventCookie* cookie = &ev.xcookie;

        XNextEvent(display, &ev);

        if (cookie->type != GenericEvent ||
                !XGetEventData(display, cookie))
            continue;

        switch (cookie->evtype) {
        case XI_RawMotion:
            /// fixate pointer (set pointer to start pos): not the best solution (is wiggles a bit) but I've not found a bette one (maybe hide the cursor while scrolling to hide the wiggling)
            XWarpPointer(display, None, window, 0, 0, 0, 0,
                         start_pointer_pos.x, start_pointer_pos.y);

            /// update total movement deltas and check if we need to scroll
            XIRawEvent* raw_event = (XIRawEvent*) cookie->data;
            double deltaX = raw_event->raw_values[0];
            double deltaY = raw_event->raw_values[1];

            check_for_scroll_trigger(SCROLL_VERTICAL, &total_movement_y_delta, deltaY, &cfg, display);
            if (cfg.allow_horizontal_scroll)
                check_for_scroll_trigger(SCROLL_HORIZONTAL, &total_movement_x_delta, deltaX, &cfg, display);

            XFlush(display); // may not be necessary, but it's here to ensure immediacy
            break;
        }

        XFreeEventData(display, cookie);
    }

    return 0;
}
