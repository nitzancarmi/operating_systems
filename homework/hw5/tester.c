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
    printf("%s\n",strerror(errno));
    
    while(1) {
        cmd = getchar();
        switch(cmd) {
        case 'r':
//            lseek(fd_in, 0, SEEK_SET);
            b_read = read(fd_in, buf, 128);
            printf("read(%d): %s\n",b_read, buf);
            printf("%s\n",strerror(errno));
            break;
        case 'w':
            write(fd, argv[1], 1);
            printf("write: %s\n", argv[1]);
            break;
        case 'x':
            close(fd);
            unlink(TESTER_PATH);
            exit(0);
        default:
            continue;
        }
    }
    printf("ERROR - shouldn't get here\n");
} 
