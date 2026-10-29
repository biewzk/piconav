#ifndef DISPLAY_GUARD_H
#define DISPLAY_GUARD_H

/*
 * display_guard - single-front-owner policy for the framebuffer.
 *
 * There is exactly one /dev/fb0 and no compositor, so any process that
 * opens it draws over everyone else. The launcher normally hands the
 * screen over by fork+exec, but a program started from a serial console
 * (or a leftover child) knows nothing about that. This guard notices
 * such a process and makes the GUI yield: it stops rendering, closes
 * the key device and releases the GPS UART until the intruder exits.
 *
 * Detection is cooperative-free: scan /proc/<pid>/fd for an open
 * /dev/fb0. The GUI is the only other holder, so anything found is a
 * foreign owner.
 */

/* pid of the process currently owning /dev/fb0, 0 when the screen is
 * free (the GUI itself is never reported, nor is the webctl screenshot
 * reader, which opens the framebuffer only for a few milliseconds). */
int display_guard_owner_pid(void);

#endif /* DISPLAY_GUARD_H */
