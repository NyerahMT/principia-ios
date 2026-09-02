#include "main.hh"
#ifdef SDL_PLATFORM_IOS
#include "ios_gpu_restore.hh"
#endif
#include "pipe.hh"
#include "settings.hh"
#include "version.hh"
#include <SDL3/SDL.h>
#include <clocale>
#include <glad/gl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <tms/cpp.hh>
#include <unistd.h>

// Include for SDL's main function wrapper
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

FILE *_f_out = stdout;

int keys[235];
int mouse_down;

static int T_intercept_input(SDL_Event ev);

static void _catch_signal(int signal) {
    tms_errorf("Segmentation fault!");

    if (_f_out != stdout) {
        fflush(_f_out);
        fclose(_f_out);
    }

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Principia",
R"(An unrecoverable error has occurred and Principia will now close.

Please report this crash to the issue tracker with the relevant steps
to reproduce it, if possible.
)", 0);

    exit(1);
}

#ifdef SDL_PLATFORM_IOS
static void SDLCALL ios_log_output(
        void *userdata,
        int category,
        SDL_LogPriority priority,
        const char *message)
{
    FILE *log = static_cast<FILE *>(userdata);

    if (log) {
        const char *prefix = "I";

        switch (priority) {
            case SDL_LOG_PRIORITY_TRACE:
                prefix = "T";
                break;

            case SDL_LOG_PRIORITY_VERBOSE:
                prefix = "V";
                break;

            case SDL_LOG_PRIORITY_DEBUG:
                prefix = "D";
                break;

            case SDL_LOG_PRIORITY_INFO:
                prefix = "I";
                break;

            case SDL_LOG_PRIORITY_WARN:
                prefix = "W";
                break;

            case SDL_LOG_PRIORITY_ERROR:
                prefix = "E";
                break;

            case SDL_LOG_PRIORITY_CRITICAL:
                prefix = "F";
                break;

            default:
                prefix = "?";
                break;
        }

        fprintf(log, "%s: %s\n", prefix, message);
        fflush(log);
    }

    SDL_LogOutputFunction default_output =
        SDL_GetDefaultLogOutputFunction();

    if (default_output) {
        default_output(nullptr, category, priority, message);
    }
}
#endif

void redirect_log_output() {
#ifdef SDL_PLATFORM_IOS
    char logfile[1024];
    const char *documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);

    if (documents) {
        snprintf(logfile, sizeof(logfile), "%srun.log", documents);
    } else {
        snprintf(logfile, sizeof(logfile), "%s/run.log", tms_storage_path());
    }

    FILE *log = fopen(logfile, "w");

    if (log) {
        _f_out = log;
        setvbuf(log, nullptr, _IONBF, 0);
        SDL_SetLogOutputFunction(ios_log_output, log);
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "Persistent iOS logger opened: %s",
            logfile);
    } else {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not open persistent log file: %s",
            logfile);
    }
#else
#if defined(SDL_PLATFORM_SWITCH) || (!defined(DEBUG) && !defined(SDL_PLATFORM_EMSCRIPTEN))
    char logfile[1024];
    snprintf(logfile, 1023, "%s/run.log", tms_storage_path());

    tms_infof("Redirecting log output to %s", logfile);
    FILE *log = fopen(logfile, "w+");
    if (log) {
        _f_out = log;
    } else {
        tms_errorf("Could not open log file for writing! Nevermind.");
    }
#endif
#endif
}

void print_log_header() {
    tms_printf( \
        "            _            _       _       \n"
        " _ __  _ __(_)_ __   ___(_)_ __ (_) __ _ \n"
        "| '_ \\| '__| | '_ \\ / __| | '_ \\| |/ _` |\n"
        "| |_) | |  | | | | | (__| | |_) | | (_| |\n"
        "| .__/|_|  |_|_| |_|\\___|_| .__/|_|\\__,_|\n"
        "|_|                       |_|            \n"
        "Version %s, commit %s\n", principia_version_string(), principia_version_hash());
}

static void find_data_dir() {
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_SWITCH)
    // Check if we're in the right place
    struct stat st{};
    if (stat("data", &st) != 0) {
        // We're in the build dir, go up
        tms_infof("chdirring to ../");
        chdir("../");

        // How about now?
        if (stat("data", &st) != 0) {
            // If that doesn't work we're assuming a system install.
            tms_infof("chdirring to ./share/principia/");
            chdir("./share/principia/");

            if (stat("data", &st) != 0) {
                // We're doomed, better just fail.
                tms_fatalf("Could not find data directories.");
            }
        }
    }
#endif
}

static int do_step = 1;

#ifdef SDL_PLATFORM_IOS
static bool ios_soft_paused = false;
static bool ios_rebind_context = false;
static SDL_GLContext ios_gl_context = NULL;
static void ios_reset_finger_slots();
static void ios_handle_interrupt(void);
static void ios_handle_resume(void);
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {

#ifndef SDL_PLATFORM_ANDROID
    signal(SIGSEGV, _catch_signal);

#ifdef SDL_PLATFORM_WINDOWS
    setlocale(LC_ALL, "C");
#endif

    setup_pipe(argc, argv);

    const char* exedir = SDL_GetBasePath();
    tms_infof("chdirring to %s", exedir);
    chdir(exedir);
#endif

    // Switch to portable if ./portable.txt exists next to binary
    if (access("portable.txt", F_OK) == 0) {
        tms_infof("We're becoming portable!");
        tms_storage_set_portable(true);
    }

    tms_storage_create_dirs();

    // The Android app ID is com.bithack.principia because it has always been like that, but for e.g.
    // Linux we want to use se.principia_web.principia as it's a domain we have better access to for
    // e.g. Flatpak domain verification and such. SDL does not actually use the app ID currently, but
    // if they do we want to report something that's consistent with the APK itself.
#if SDL_PLATFORM_ANDROID
    #define PRINCIPIA_ID "com.bithack.principia"
#else
    #define PRINCIPIA_ID "se.principia_web.principia"
#endif
    SDL_SetAppMetadata("Principia", principia_version_string(), PRINCIPIA_ID);

    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "1");

    redirect_log_output();

    print_log_header();

    find_data_dir();

    const int compiled = SDL_VERSION;
    const int linked = SDL_GetVersion();

    tms_infof("Compiled against SDL v%d.%d.%d",
            SDL_VERSIONNUM_MAJOR(compiled),
            SDL_VERSIONNUM_MINOR(compiled),
            SDL_VERSIONNUM_MICRO(compiled));

    tms_infof("Linked against SDL v%d.%d.%d",
            SDL_VERSIONNUM_MAJOR(linked),
            SDL_VERSIONNUM_MINOR(linked),
            SDL_VERSIONNUM_MICRO(linked));

    tms_infof("Initializing SDL...");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);

#ifdef SDL_PLATFORM_EMSCRIPTEN
    _tms.window_width = 1280;
    _tms.window_height = 720;

#elif defined(SDL_PLATFORM_IOS)
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());

    if (mode) {
        _tms.window_width = mode->w;
        _tms.window_height = mode->h;
    } else {
        tms_errorf("Couldn't get display mode: %s", SDL_GetError());
        _tms.window_width = 1280;
        _tms.window_height = 720;
    }

    tms_infof("set initial res to %dx%d", _tms.window_width, _tms.window_height);

#elif !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_SWITCH)
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    SDL_Point screen;
    if (mode) {
        screen.x = mode->w;
        screen.y = mode->h;
    } else {
        tms_errorf("Couldn't get display mode: %s", SDL_GetError());
        screen.x = 1280;
        screen.y = 720;
    }

    _tms.window_width = 1280;

    if (mode->w <= 1280)
        _tms.window_width = (int)((double)mode->w * .9);
    else if (mode->w >= 2100 && mode->h > 1100)
        _tms.window_width = 1920;

    _tms.window_height = (int)((double)_tms.window_width * .5625);

    tms_infof("set initial res to %dx%d", _tms.window_width, _tms.window_height);
#endif

    tproject_set_args(argc, argv);

    tms_preinit();

    uint32_t flags = SDL_WINDOW_OPENGL | 0;

#ifdef SDL_PLATFORM_IOS
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_SWITCH) || defined(SDL_PLATFORM_IOS)
    flags |= SDL_WINDOW_FULLSCREEN;
#else
    _tms.window_width = settings["window_width"]->v.i;
    _tms.window_height = settings["window_height"]->v.i;

    if (settings["window_maximized"]->v.b)
        flags |= SDL_WINDOW_MAXIMIZED;

    if (settings["window_fullscreen"]->v.b)
        flags |= SDL_WINDOW_FULLSCREEN;

    if (settings["window_resizable"]->v.b)
        flags |= SDL_WINDOW_RESIZABLE;
#endif

    tms_infof("Creating window...");
    _tms._window = SDL_CreateWindow("Principia",
        _tms.window_width, _tms.window_height, flags);

    if (_tms._window == NULL) {
        tms_infof("ERROR: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

#ifdef SDL_PLATFORM_EMSCRIPTEN
    SDL_SetWindowFillDocument(_tms._window, true);
#endif

#ifdef SDL_PLATFORM_IOS
    SDL_GetWindowSizeInPixels(
            _tms._window,
            &_tms.opengl_width,
            &_tms.opengl_height);

    _tms.window_width = _tms.opengl_width;
    _tms.window_height = _tms.opengl_height;

#elif defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_SWITCH)
    SDL_GetWindowSizeInPixels(_tms._window, &_tms.window_width, &_tms.window_height);
#endif

    float content_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

#ifdef SDL_PLATFORM_IOS
    float pixel_density = SDL_GetWindowPixelDensity(_tms._window);

    if (pixel_density > 0.f)
        content_scale = pixel_density;
#endif

    _tms.xppcm = 108.f / 2.54f * 1.5f * content_scale;
    _tms.yppcm = 107.f / 2.54f * 1.5f * content_scale;

    tms_infof("Device dimensions: %d %d", _tms.window_width, _tms.window_height);
    tms_infof("Device PPCM: %f %f", _tms.xppcm, _tms.yppcm);
    tms_infof("Device content scale: %f", content_scale);

    if (_tms.use_gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    } else {
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(_tms._window);

    if (gl_context == NULL)
        tms_fatalf("Error creating GL Context: %s", SDL_GetError());
#ifdef SDL_PLATFORM_IOS
    ios_gl_context = gl_context;
#endif

    int version;
    if (_tms.use_gles)
        version = gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress);
    else
        version = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    tms_infof("Loaded GL version %d.%d", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    tms_infof("GL Info: %s/%s/%s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    tms_infof("GLSL Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
    tms_infof("GL Extensions: %s", glGetString(GL_EXTENSIONS));

#ifdef SDL_PLATFORM_WINDOWS

    if (!GLAD_GL_VERSION_1_2) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Principia",
R"(Your graphics driver does not support OpenGL >1.1 and as such Principia will not start.
Most likely this is because you do not have any graphics drivers installed and are using
Windows' software rendering driver. Please install the necessary driver for your
graphics card.

If you are on a VM for testing purposes, then you can use Mesa's software renderer to
get Principia running. (place the Mesa opengl32.dll library next to principia.exe))", 0);

        return SDL_APP_FAILURE;
    }

#endif

    tms_init();

    if (_tms.screen == 0) {
        tms_fatalf("Context has no initial screen!");
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    tproject_quit();
    SDL_Quit();
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *ev) {
    switch (ev->type) {
        #ifdef __ANDROID__
            case SDL_EVENT_WINDOW_MINIMIZED:
                tproject_soft_pause();
                do_step = 0;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
                tproject_soft_resume();
                do_step = 1;
                break;
        #else
            case SDL_EVENT_WINDOW_RESIZED: {
#ifdef SDL_PLATFORM_IOS
                break;
#else
                tms_infof("Window %d resized to %dx%d",
                        ev->window.windowID, ev->window.data1,
                        ev->window.data2);
                int w = ev->window.data1;
                int h = ev->window.data2;

                _tms.window_width  = _tms.opengl_width  = w;
                _tms.window_height = _tms.opengl_height = h;
#endif

                tproject_window_size_changed();
            } break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
                settings["window_maximized"]->v.b = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                settings["window_maximized"]->v.b = false;
                break;
        #endif

#ifdef SDL_PLATFORM_IOS
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_OCCLUDED:
            ios_handle_interrupt();
            break;

        case SDL_EVENT_WILL_ENTER_FOREGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_EXPOSED:
            ios_handle_resume();
            break;

        case SDL_EVENT_DROP_FILE:
            if (ev->drop.data && strncmp(ev->drop.data, "principia://", 12) == 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Received Principia iOS link: %s", ev->drop.data);

                char app_name[] = "principia";
                char *args[] = {
                    app_name,
                    const_cast<char *>(ev->drop.data),
                };
                tproject_set_args(2, args);
            }
            break;
#endif

        case SDL_EVENT_QUIT:
            _tms.state = TMS_STATE_QUITTING;
            break;

        case SDL_EVENT_KEY_DOWN:
            T_intercept_input(*ev);
            keys[ev->key.scancode] = 1;
            break;

        case SDL_EVENT_KEY_UP:
            T_intercept_input(*ev);
            keys[ev->key.scancode] = 0;
            break;

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_TEXT_INPUT:
            T_intercept_input(*ev);
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {

    if (do_step) {
        for (int i = 0; i < 235; ++i) {
            if (keys[i] == 1) {
                struct tms_event spec;
                spec.type = TMS_EV_KEY_DOWN;
                spec.data.key.keycode = i;

                tms_event_push(spec);
            }
        }
    }

    if (_tms.state == TMS_STATE_QUITTING) {
        return SDL_APP_SUCCESS;
    }

    if (_tms.is_paused) {
        SDL_Delay(100);
        return SDL_APP_CONTINUE;
    }

    tms_step();
#ifdef SDL_PLATFORM_IOS
    if (ios_rebind_context || _tms.ios_reload_buffers) {
        if (ios_gl_context && _tms._window)
            SDL_GL_MakeCurrent(_tms._window, ios_gl_context);
        ios_rebind_context = false;
        ios_restore_gpu_after_resume();
    }
#endif
    tms_begin_frame();
    tms_render();
    SDL_GL_SwapWindow(_tms._window);
    tms_end_frame();
#ifdef SDL_PLATFORM_IOS
    if (_tms.ios_reload_buffers > 0)
        _tms.ios_reload_buffers--;
#endif

    return SDL_APP_CONTINUE;
}

int mouse_button_to_pointer_id(int button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return 4;
    }
}

#ifdef MAX_P
#undef MAX_P
#endif

#define MAX_P 10

static uint64_t finger_ids[MAX_P];

#ifdef SDL_PLATFORM_IOS
static bool finger_slots_used[MAX_P];
static void ios_reset_finger_slots()
{
    for (int x = 0; x < MAX_P; ++x) {
        finger_ids[x] = 0;
        finger_slots_used[x] = false;
    }

    mouse_down = 0;
}

static void ios_handle_interrupt(void)
{
    ios_reset_finger_slots();
    ios_rebind_context = true;
    if (!ios_soft_paused) {
        tproject_soft_pause();
        ios_soft_paused = true;
    }
    do_step = 0;
}

static void ios_handle_resume(void)
{
    ios_reset_finger_slots();
    ios_rebind_context = true;
    if (ios_gl_context && _tms._window)
        SDL_GL_MakeCurrent(_tms._window, ios_gl_context);
    _tms.ios_reload_buffers = 2;
    if (ios_soft_paused) {
        tproject_soft_resume();
        ios_soft_paused = false;
    }
    do_step = 1;
}
#endif

static int finger_to_pointer(uint64_t finger, bool create) {
#ifdef SDL_PLATFORM_IOS
    for (int x = 0; x < MAX_P; x++) {
        if (finger_slots_used[x] && finger_ids[x] == finger)
            return x;
    }

    if (!create)
        return -1;

    for (int x = 0; x < MAX_P; x++) {
        if (!finger_slots_used[x]) {
            finger_ids[x] = finger;
            finger_slots_used[x] = true;
            return x;
        }
    }

    finger_ids[MAX_P-1] = finger;
    finger_slots_used[MAX_P-1] = true;
    return MAX_P-1;
#elif defined(SDL_PLATFORM_WINDOWS)
    // Windows gives each finger tap session an unique incrementing ID that starts on each boot, so
    // we need to keep track of them and allocate in slots that fit TMS' pointer ID system.
    for (int x = 0; x < MAX_P; x++) {
        // If create=true, find first empty slot
        // else, find the slot that matches the finger ID returned from Windows
        if ((finger_ids[x] == 0 && create) || finger_ids[x] == finger) {
            tms_infof("found %" PRIu64 " at %d", finger, x);
            finger_ids[x] = finger;
            return x;
        }
    }

    // No slot found... Who has more than ten fingers?

    // Just replace the last one with this new finger ID.
    finger_ids[MAX_P-1] = finger;
    return MAX_P-1;
#else
    // Linux, Android - Easy, they handle finger IDs basically the way we want them to.
    return finger - 1;
#endif
}

int T_intercept_input(SDL_Event ev) {
    struct tms_event spec;
    spec.type = -1;

    int motion_y = _tms.window_height-ev.motion.y;
    int button_y = _tms.window_height-ev.button.y;

    int f;

    switch (ev.type) {
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.repeat)
                spec.type = TMS_EV_KEY_REPEAT;
            else
                spec.type = TMS_EV_KEY_PRESS;

            spec.data.key.keycode = ev.key.scancode;

            spec.data.key.mod = ev.key.mod;
            switch (spec.data.key.keycode) {
                case TMS_KEY_LEFT_CTRL: spec.data.key.mod |= TMS_MOD_LCTRL; break;
                case TMS_KEY_RIGHT_CTRL: spec.data.key.mod |= TMS_MOD_RCTRL; break;
                case TMS_KEY_LEFT_SHIFT: spec.data.key.mod |= TMS_MOD_LSHIFT; break;
                case TMS_KEY_RIGHT_SHIFT: spec.data.key.mod |= TMS_MOD_RSHIFT; break;
            }
            break;

        case SDL_EVENT_KEY_UP:
            spec.type = TMS_EV_KEY_UP;
            spec.data.key.keycode = ev.key.scancode;

            spec.data.key.mod = ev.key.mod;
            break;

        case SDL_EVENT_FINGER_DOWN:
            spec.type = TMS_EV_POINTER_DOWN;
            spec.data.button.pointer_id = finger_to_pointer(ev.tfinger.fingerID, true);
            spec.data.button.x = (int)(ev.tfinger.x*(float)_tms.window_width);
            spec.data.button.y = _tms.window_height-(int)(ev.tfinger.y*(float)_tms.window_height);
#ifdef SDL_PLATFORM_IOS
            spec.data.button.button = SDL_BUTTON_LEFT;
#endif
            break;

        case SDL_EVENT_FINGER_UP:
            spec.type = TMS_EV_POINTER_UP;
            f = finger_to_pointer(ev.tfinger.fingerID, false);

#ifdef SDL_PLATFORM_IOS
            if (f < 0)
                return T_OK;
#endif

            spec.data.button.pointer_id = f;
            spec.data.button.x = (int)(ev.tfinger.x*(float)_tms.window_width);
            spec.data.button.y = _tms.window_height-(int)(ev.tfinger.y*(float)_tms.window_height);

#ifdef SDL_PLATFORM_IOS
            spec.data.button.button = SDL_BUTTON_LEFT;
#endif

            // Free up the slot for this finger ID
#ifdef SDL_PLATFORM_IOS
            finger_slots_used[SDL_min(f, MAX_P - 1)] = false;
#else
            finger_ids[SDL_min(f, MAX_P - 1)] = 0;
#endif
            break;

        case SDL_EVENT_FINGER_MOTION:
            spec.type = TMS_EV_POINTER_DRAG;
#ifdef SDL_PLATFORM_IOS
            f = finger_to_pointer(ev.tfinger.fingerID, false);

            if (f < 0)
                return T_OK;

            spec.data.button.pointer_id = f;
#else
            spec.data.button.pointer_id = finger_to_pointer(ev.tfinger.fingerID, false);
#endif
            spec.data.button.x = (int)(ev.tfinger.x*(float)_tms.window_width);
            spec.data.button.y = _tms.window_height-(int)(ev.tfinger.y*(float)_tms.window_height);
#ifdef SDL_PLATFORM_IOS
            spec.data.button.button = SDL_BUTTON_LEFT;
#endif
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.which == SDL_TOUCH_MOUSEID)
                return T_OK;

            spec.type = TMS_EV_POINTER_DOWN;
            spec.data.button.pointer_id = mouse_button_to_pointer_id(ev.button.button);
            spec.data.button.x = ev.button.x;
            spec.data.button.y = button_y;
            spec.data.button.button = ev.button.button;

            if (mouse_down == 0)
                mouse_down = ev.button.button;

            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.which == SDL_TOUCH_MOUSEID)
                return T_OK;

            spec.type = TMS_EV_POINTER_UP;
            spec.data.button.pointer_id = mouse_button_to_pointer_id(ev.button.button);
            spec.data.button.x = ev.button.x;
            spec.data.button.y = button_y;
            spec.data.button.button = ev.button.button;

            if (mouse_down == ev.button.button)
                mouse_down = 0;

            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (ev.button.which == SDL_TOUCH_MOUSEID)
                return T_OK;

            spec.data.button.pointer_id = mouse_button_to_pointer_id(ev.button.button);

            if (mouse_down) {
                spec.type = TMS_EV_POINTER_DRAG;
                spec.data.button.x = ev.motion.x;
                spec.data.button.y = button_y;
                spec.data.button.button = mouse_down;
            } else {
                spec.type = TMS_EV_POINTER_MOVE;
                spec.data.button.x = ev.motion.x;
                spec.data.button.y = motion_y;
            }

            break;

        case SDL_EVENT_MOUSE_WHEEL:
            spec.type = TMS_EV_POINTER_SCROLL;
            spec.data.scroll.x = ev.wheel.x;
            spec.data.scroll.y = ev.wheel.y;
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            spec.data.scroll.mouse_x = (int)mx;
            spec.data.scroll.mouse_y = (int)my;
            break;

        case SDL_EVENT_TEXT_INPUT:
            spec.type = TMS_EV_TEXT_INPUT;
            spec.data.text.text = ev.text.text;
            break;
    }

    tms_event_push(spec);

    return T_OK;
}
