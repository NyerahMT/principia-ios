#include "ios_gpu_restore.hh"

#include <SDL3/SDL.h>

#ifdef SDL_PLATFORM_IOS

#include "gui.hh"
#include "main.hh"

#include <tms/core/tms.h>
#include <tms/core/texture.h>

static int ios_size_locked = 0;
static int ios_lock_w = 0;
static int ios_lock_h = 0;

static GLuint
ios_current_window_fb(void)
{
    if (!_tms._window)
        return 0;

    SDL_PropertiesID props = SDL_GetWindowProperties(_tms._window);
    return (GLuint)SDL_GetNumberProperty(
        props,
        SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER,
        0);
}

static void
ios_lock_drawable_size(void)
{
    if (!P.loaded)
        return;

    if (!ios_size_locked) {
        ios_lock_w = _tms.window_width;
        ios_lock_h = _tms.window_height;
        ios_size_locked = 1;
    }

    _tms.window_width = ios_lock_w;
    _tms.window_height = ios_lock_h;
    _tms.opengl_width = ios_lock_w;
    _tms.opengl_height = ios_lock_h;
}

void
ios_bind_window_framebuffer(void)
{
    if (!_tms._window)
        return;

    GLuint framebuffer = ios_current_window_fb();
    if (glad_glBindFramebuffer)
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    else
        glBindFramebufferEXT(GL_FRAMEBUFFER, framebuffer);

    ios_lock_drawable_size();
}

void
ios_bind_window_drawable(void)
{
    if (!_tms._window)
        return;

    ios_bind_window_framebuffer();

    SDL_PropertiesID props = SDL_GetWindowProperties(_tms._window);
    GLuint renderbuffer = (GLuint)SDL_GetNumberProperty(
        props,
        SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER,
        0);

    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glViewport(0, 0, _tms.opengl_width, _tms.opengl_height);
}

static void
ios_reupload_gui_atlases(void)
{
    /*
     * The text atlas is generated at runtime and its GL texture remains valid
     * across a normal iOS suspend/resume. Re-uploading it from the generic
     * texture path here can replace the live glyph atlas with stale backing
     * data, which shows up as scrambled/corrupted button text after opening
     * Control Center or returning from the app switcher.
     *
     * Keep restoring the ordinary sprite atlases, but leave atlas_text alone.
     */
    if (gui_spritesheet::atlas)
        tms_texture_upload(&gui_spritesheet::atlas->texture);
    if (gui_spritesheet::tmp_atlas)
        tms_texture_upload(&gui_spritesheet::tmp_atlas->texture);
}

static void
ios_bind_window_fb(int clear_depth)
{
    ios_bind_window_framebuffer();
    glViewport(0, 0, _tms.opengl_width, _tms.opengl_height);

    if (clear_depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearDepthf(1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
}

void
ios_bind_window_for_color_pass(void)
{
    ios_lock_drawable_size();

    if (!_tms.ios_reload_buffers)
        return;

    ios_bind_window_fb(1);
}

void
ios_restore_gpu_after_resume(void)
{
    static int busy = 0;
    static int logged = 0;
    static int prev_reload = 0;
    static int need_gui = 0;

    int reload = _tms.ios_reload_buffers;

    if (reload && !prev_reload) {
        logged = 0;
        need_gui = 1;
    }

    prev_reload = reload;

    ios_lock_drawable_size();

    if (!reload || busy || !P.loaded)
        return;

    busy = 1;
    ios_bind_window_fb(1);

    if (need_gui) {
        ios_reupload_gui_atlases();
        need_gui = 0;
    }

    if (!logged) {
        logged = 1;
        tms_infof("iOS resume: window FBO %u rebound",
                  (unsigned)ios_current_window_fb());
    }

    busy = 0;
}

#else

void
ios_restore_gpu_after_resume(void)
{
}

void
ios_bind_window_for_color_pass(void)
{
}

void
ios_bind_window_framebuffer(void)
{
}

void
ios_bind_window_drawable(void)
{
}

#endif
