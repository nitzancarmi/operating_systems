#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "kci.h" /*our header file*/ 

#define PR_ERR(msg)     printf("ERROR [%s, %d] %s : [%d] %s\n", __func__, __LINE__, msg, errno, strerror(errno)) 


/*call wrappers for syscalls */
/* CREDIT: http://stackoverflow.com/a/29185035 */
static inline int finit_module(int fd, const char *uargs, int flags) {
    return syscall(__NR_finit_module, fd, uargs, flags);
}
static inline int delete_module(const char *name, int flags) {
    return syscall(__NR_delete_module, name, flags);
}
/* END OF CREDIT */

void usage(char* filename);
void kmod_init(char *mod);
void kmod_exec_ioctl_cmd(enum ioctl_command cmd, void* args);
int cp(char *from, char *to);
void kmod_remove();


void usage(char* filename) {
    printf("Usage: %s [opts] [args]\n", filename);
    printf("\nOptions:\n"

            "-init [KO]\t\t"
            "inserts the relevant kernel module into the kernel space, as well as creating a device file for it\n"

            "-pid [PID]\t\t"
            "set the PID of the process the cipher works for\n"

            "-fd [FD]\t\t"
            "set the file descriptor of the file the cipher works for\n"

            "-start \t\t"
            "start actual encryption/decryption\n"

            "-stop\t\t"
            "stop actual encryption/decryption\n"

            "-rm\t\t"
            "remove the kernel module from the kernel space\n"
    );
}

void kmod_init(char *mod) {
    int rc;
    int fd;
    dev_t dev;

    //install module inside kernel
    fd = open(mod,O_RDONLY);
    if(!fd) {
        PR_ERR("failed to open module");
        exit(-1);
    }
    rc = finit_module(fd, "", 0);
    if(rc) {
        PR_ERR("failed to install module into kernel");
        exit(rc);
    }

    //create character device
    dev = makedev(MAJOR_NUM, MINOR_NUM);
    if (!dev) {
        PR_ERR("failed to create device in makedev");
        exit(-1);
    }
    rc = mknod(DEV_PATH, S_IFCHR|S_IRWXU|S_IRWXG|S_IRWXO, dev);
    if (rc) {
        PR_ERR("failed to create device in mknod");
        exit(rc);
    }

    //close file before finishes
    rc = close(fd);
    if(rc) {
        PR_ERR("failed to close module");
        exit(rc);
    }

    exit(rc);
}

void kmod_exec_ioctl_cmd(enum ioctl_command cmd, void* args) {
    int dev, rc = 0;
    int arg = -1;

    dev = open(DEV_PATH,O_RDWR);
    if(!dev) {
        PR_ERR("failed to open module");
        exit(-1);
    }

    switch(cmd) {
    case KMOD_SET_PID:
        arg = *((int *)args);
        rc = ioctl(dev, IOCTL_SET_PID, arg);
        break;
    case KMOD_SET_FD:
        arg = *((int *)args);
        rc = ioctl(dev, IOCTL_SET_FD, arg);
        break;
    case KMOD_START:
        rc = ioctl(dev, IOCTL_CIPHER, 1);
        break;
    case KMOD_STOP:
        rc = ioctl(dev, IOCTL_CIPHER, 0);
        break;
    default:
        rc = -1;
    }
    if(rc) {
        PR_ERR("failed to send ioctl command");
        exit(rc);
    }

    rc = close(dev);
    if(rc) {
        PR_ERR("failed to close module");
        exit(rc);
    }

    exit(rc);
}

void kmod_remove() {
    int rc = 0;

    /*copy log file into current directory*/
    char tgt_path[1024] = {0};
    char cwd[1024] = {0};
    getcwd(cwd, 1024);
    sprintf(tgt_path, "%s/%s", cwd, LOG_FILE); 
//    rc = cp(LOG_PATH, tgt_path); //TODO needs to be fixed!!!!!!!!!!
    if(rc) {
        PR_ERR("failed to copy log file into folder");
        exit(rc);
    }

    rc = delete_module(MODULE_NAME, 0);
    if(rc) {
        PR_ERR("failed to remove module");
        exit(rc);
    }

    rc = unlink(DEV_PATH);
    if(rc) {
        PR_ERR("failed to remove device");
        exit(rc);
    }
    
    exit(rc);
}

int cp(char *from, char *to) {
    int fd_to, fd_from;
    char buf[4096];
    ssize_t b_read, b_write;
    int rc, _rc = 0;
    char *off_to = 0;

    fd_from = open(from, O_RDONLY, 0666);
    if (fd_from < 0) {
        PR_ERR("failed to open source file");
        return -1;
    }
    printf("path: %s, fd = %d\n", from,fd_from);
    b_read = read(fd_from, buf, sizeof(buf));
    if (b_read) {
        printf("PROBLEM\n");
        return 0;
    }

    fd_to = creat(to, O_WRONLY | O_EXCL);
    if (fd_to < 0) {
        PR_ERR("failed to open source file");
        close(fd_from);
        return -1;
    }

    while ((b_read = read(fd_from, buf, sizeof(buf))) > 0) {
        do {
            b_write = write(fd_to, off_to, (size_t)b_read);
            if (b_write < 0) {
                PR_ERR("failed to write to target file");
                rc = -1;
                goto cp_exit;
            }
            b_read -= b_write;
            off_to += b_write;
        } while (b_read > 0);
    }
    if (b_read < 0) {
        PR_ERR("failed to read from source file");
        rc = -1;
    }


cp_exit:
    _rc |= close(fd_from);
    _rc |= close(fd_to);
    if (_rc) {
        PR_ERR("failed to close files in copy");
        rc = rc ? rc:_rc;
    }
    return rc;
}

int main(int argc, char *argv[]) {
    pid_t pid;
    int fd;
    enum ioctl_command cmd;
    void* args = NULL;
    bool valid = false;
    

    /* init option */
    if (!strcmp(argv[1], "-init")) {
        if (argc != 3)
            goto exit;
        kmod_init(argv[2]); //exiting function
    }

    /* remove option */
    if (!strcmp(argv[1], "-rm")) {
        if (argc != 2)
            goto exit;
        kmod_remove(); //exiting function
    }

    /* ioctl options */
    if (!strcmp(argv[1], "-pid")) {
        if (argc != 3)
            goto exit;
        cmd = KMOD_SET_PID;
        pid = (int)strtol(argv[2], NULL, 10);
        args = (void*)&pid;
        valid = true;
    }
    if (!strcmp(argv[1], "-fd")) {
        if (argc != 3)
            goto exit;
        cmd = KMOD_SET_FD;
        fd = (int)strtol(argv[2], NULL, 10);
        args = (void*)&fd;
        valid = true;
    }
    if (!strcmp(argv[1], "-start")) {
        if (argc != 2)
            goto exit;
        cmd = KMOD_START;
        valid = true;
    }
    if (!strcmp(argv[1], "-stop")) {
        if (argc != 2)
            goto exit;
        cmd = KMOD_STOP;
        valid = true;
    }
    if (valid)
        kmod_exec_ioctl_cmd(cmd, args); //exiting function

exit:
    usage(argv[0]);
    return -1;
}
