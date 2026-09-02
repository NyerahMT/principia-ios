#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ios_restore_gpu_after_resume(void);
void ios_bind_window_for_color_pass(void);
void ios_bind_window_framebuffer(void);
void ios_bind_window_drawable(void);

#ifdef __cplusplus
}
#endif
