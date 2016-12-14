#define _GNU_SOURCE
#include<sys/types.h>
#include <sys/time.h>
#include<sys/stat.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<errno.h>
#include <sys/mman.h>
#include <signal.h>

#define PIPE_PATH       "/tmp"
#define PIPE_FILENAME   "osfifo"
#define PERMISSIONS     0600
#define BUFSIZE         4096
#define MIN(x, y) (((x) < (y)) ? (x) : (y))


void usage(char* filename);

void usage(char* filename) {
    printf("Usage: %s \n"
           "Aborting...\n",
            filename);
}

int main ( int argc, char *argv[]) {

    //validate 3 command line arguments given
    if (argc != 1) {
        usage(argv[0]);
        return -1;
    }

    //declerations:
    char    fpath[1024] = {'\0'};
    char    buf[BUFSIZE] = {'\0'};
    int     fd, rc, _rc;
    int     i, a_count;
    struct  timeval t1, t2;
    double  elapsed_msec;
    ssize_t b_read;
    sigset_t mask, old_mask;

    //set mask to ignore SIGINT
    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, &old_mask) < 0) {
            printf("ERROR: Failed to block SIGINT\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
            rc = -1;
            goto exit;
    }

    //wait for 1 second to avoid sync problem
    //(in case writer hasnt created fifo file yet)
    sleep(1);

    //open file
    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    fd = open(fpath, O_RDONLY);
    if (fd < 0) {
        printf("ERROR: Failed to open file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = -1;
        goto cleanup;
    }

    //start measurements
    rc = gettimeofday(&t1, NULL);
    if (rc) {
       printf("ERROR: Failed to measure time\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
        goto cleanup;
    }

    //read from file
    a_count = 0;
    while((b_read = read(fd, buf, sizeof(char) * BUFSIZE)) > 0) {
        for(i=0; i<b_read; i++) {
            if(buf[i] == 'a')
                a_count++;
        }
    }
    if (b_read < 0) {
        printf("ERROR: Failed to read from file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = -1;
        goto cleanup;
    }

    //finish time measurement
    rc = gettimeofday(&t2, NULL);
    if (rc) {
       printf("ERROR: Failed to measure time\n"
              "Cause: %s [%d]\n",
              strerror(errno), errno);
        goto cleanup;
    }
    elapsed_msec = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsed_msec += (t2.tv_usec - t1.tv_usec) / 1000.0;

    //print results
    printf("%d bytes were read in %f miliseconds through FIFO\n",
           a_count, elapsed_msec);

cleanup:
    _rc = close(fd);
    if(_rc) {
        printf("ERROR: failed to close file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        if(!rc)
            rc = _rc;
    }
    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) < 0) {
            printf("ERROR: Failed to allow back SIGINT\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
        if(!rc)
            rc = -1;
    }

exit:
    return rc;

}
