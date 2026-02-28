/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LVGL_DISPLAY_LVGL_DISPLAY_H_
#define LVGL_DISPLAY_LVGL_DISPLAY_H_

/**
 * @brief Drive the LVGL timer loop from the calling thread.
 *
 * Must be called from the Zephyr main thread so that SDL rendering
 * operations run on the POSIX thread that initialised the video system.
 * This function never returns.
 */
void lvgl_display_run(void);

#endif /* LVGL_DISPLAY_LVGL_DISPLAY_H_ */
