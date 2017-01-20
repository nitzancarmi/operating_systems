#include<sys/types.h>
#include<sys/stat.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<errno.h>
#include<dirent.h>

#define TESTER_PATH "/tmp/tester"

int main (int argc, char *argv[]) {
    int fd, fd_in;
    char buf[128];
    int cmd;
    int offset;
    int b_read;
    fd = creat(TESTER_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    fd_in = open(TESTER_PATH,O_RDONLY);
    printf("Info:\n-----\n"
           "PID: %d\n"
           "FD Write: %d\n"
           "FD Read: %d\n",
           getpid(), fd, fd_in); 
    
    while(1) {
        errno = 0;
        cmd = getchar();
        switch(cmd) {
        case 'r':
            b_read = read(fd_in, buf, 128);
            printf("read(%d): %s\n",b_read, buf);
            printf("%s\n",strerror(errno));
            break;
        case 'w':
            write(fd, argv[1], 1);
            printf("write: %s\n", argv[1]);
            printf("%s\n",strerror(errno));
            break;
        case 'c':
            memset(buf, 0, sizeof(buf));
            break;
        case 'x':
            close(fd);
            close(fd_in);
            unlink(TESTER_PATH);
            return 0;
        default:
            continue;
        }
    }
    printf("ERROR - shouldn't get here\n");
} 
