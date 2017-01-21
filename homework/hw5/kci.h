#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>



/* The major device number. We can't rely on dynamic 
 * registration any more, because ioctls need to know 
 * it. */
#define MAJOR_NUM 245
#define MINOR_NUM 0


/* Set the message of the device driver */
#define IOCTL_SET_PID   _IOW(MAJOR_NUM, 0, int)
#define IOCTL_SET_FD    _IOW(MAJOR_NUM, 1, int)
#define IOCTL_CIPHER    _IOW(MAJOR_NUM, 2, int)

#define MODULE_NAME "kci_kmod"
#define DEV_PATH    "/dev/kci_dev"
#define LOG_PATH    "/sys/kernel/debug/kcikmod/calls"
#define LOG_DIR     "kcikmod"
#define LOG_FILE    "calls"


enum ioctl_command {
    KMOD_SET_PID,
    KMOD_SET_FD,
    KMOD_START,
    KMOD_STOP,
};

#endif
