// converts pointer (mouse, trackpad, ...) movements into scroll wheel events
// uses XLib and XLib extension XInput2
// cursor pos tracking based on https://keithp.com/blogs/Cursor_tracking/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>
#include <sys/file.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

const char* PROGRAM_VERSION = "1.0";
int is_active = False;

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
static void request_to_receive_events(Display *dpy, Window win)
{
    XIEventMask evmasks[1];
    unsigned char mask1[(XI_LASTEVENT + 7)/8];

    memset(mask1, 0, sizeof(mask1));

    /* select for button and key events from all master devices */
    XISetMask(mask1, XI_RawMotion);
//    XISetMask(mask1, XI_ButtonPress);
//    XISetMask(mask1, XI_ButtonRelease);
    XISetMask(mask1, XI_KeyPress);
    XISetMask(mask1, XI_KeyRelease);

    evmasks[0].deviceid = XIAllDevices;
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
        // XSendEvent doesn't seem to work, so XTestFakeButtonEvent is used
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
            case 'c':
                cvalue = optarg;
                intmax_t num = strtoimax(optarg, NULL, 10);
                if (errno == ERANGE)
                {
                    fprintf(stderr, "error parsing value for -s. It must be a positive integer.");
                    exit(-1);
                }
                cfg->mouse_move_delta_to_scroll_threshold = (uint) labs(num);
//                printf("s arg: %s %d", optarg, cfg->mouse_move_delta_to_scroll_threshold);
                break;
            case 'h': // print help
                printf("Converts X pointer movement (mouse, touchpad, trackpoint, trackball) to scroll wheel events.\n\n");
                printf("Options:\n");
                printf("-c [x:int]\tconversion distance: pointer travel distance (in pixels) required to trigger a scroll. Determines how frequently scrolling occurs. A lower number means more frequent scroll events.\n");
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
                if (optopt == 'c')
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
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

// returns xi opcode
int ensure_xinput2_or_exit(Display* display)
{
    int xi_opcode, event, error;
    if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event, &error)) {
        fprintf(stderr, "Error: X Input extension not available.");
        exit(-3);
    }

    if (!has_xi2(display))
        exit(-4);

    return xi_opcode;
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

// -1 returned if device is not found
int find_input_device_id_by_name(Display* display, const char* device_name)
{
    int ret = -1;
    int numDevices = 0;
    XDeviceInfo* dd = XListInputDevices(display, &numDevices);
    for(int i = 0; i < numDevices; i++)
    {
        //        printf("%s %ld %ld\n", dd[i].name, dd[i].id);
        fflush(stdout);
        if (strcasecmp(dd[i].name, device_name) == 0)
        {
            ret = (int) dd[i].id;
            break;
        }
    }
    XFreeDeviceList(dd);
    return ret;
}

void ensure_single_instance()
{
    // /tmp is often mounted as ramdisk (tmpfs)
    int pid_file = open("/tmp/MouseToScroll.pid", O_CREAT | O_RDWR, 0666);
    int rc = flock(pid_file, LOCK_EX | LOCK_NB); // non-blocking
    if (rc)
    {
        if(EWOULDBLOCK == errno)
        {
            fprintf(stderr, "another instance is already running\n");
            exit(-88);
        }
    }
}

// TODO:
// toggle
// variable trigger code(s) from arg
int main(int argc, char **argv)
{
    ensure_single_instance();

    struct Config cfg = create_default_config();
    parse_args_into_config(argc, argv, &cfg);
    int trigger_key_code = 64; // 64 == left alt

    Display* display = open_display_or_exit();
    int xi_opcode = ensure_xinput2_or_exit(display);

    Window window = DefaultRootWindow(display);
    request_to_receive_events(display, window);

    int xtest_keyboard_device_id = find_input_device_id_by_name(display, "Virtual core XTEST keyboard");
    if (xtest_keyboard_device_id == -1)
        printf("warn: could not find 'Virtual core XTEST keyboard'. Things might not work well.");

    struct ScreenPoint start_pointer_pos = get_pointer_position(display, window);

    // vars for keeping track of accumulated pointer movement over time. Helps to decide when to scroll and how much.
    double total_movement_y_delta = 0;
    double total_movement_x_delta = 0;

    while(1) {
        XEvent ev;
        XGenericEventCookie* cookie = &ev.xcookie;

        XNextEvent(display, &ev);

        if (cookie->type != GenericEvent ||
                cookie->extension != xi_opcode ||
                !XGetEventData(display, cookie))
            continue;

        XIRawEvent* ree = (XIRawEvent*) cookie->data;

        switch (cookie->evtype) {
        case XI_KeyPress:
        {
            int key_code = ree->detail;
            Bool is_repeat = ree->flags & XIKeyRepeat;
            if (ree->deviceid != xtest_keyboard_device_id
                    && !is_active
                    && key_code == trigger_key_code
                    && !is_repeat)
            {
                printf("ACTIVATE\n");
                is_active = True;
                start_pointer_pos = get_pointer_position(display, window);
            }
            break;
        }
        case XI_KeyRelease:
        {
            int key_code = ree->detail;
            if (ree->deviceid != xtest_keyboard_device_id
                    && is_active
                    && key_code == trigger_key_code)
            {
                printf("DEACTIVATE\n");
                is_active = False;
            }
            break;
        }
        case XI_ButtonPress:
            break;
        case XI_RawMotion:
            if (!is_active)
                break;
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
        fflush(stdout);

        XFreeEventData(display, cookie);
    }

    return 0;
}

