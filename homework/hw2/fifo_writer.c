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
#define TIMEOUT         60
#define MIN(x, y) (((x) < (y)) ? (x) : (y))


void usage(char* filename);
int is_exist(char* filename);

void usage(char* filename) {
    printf("Usage: %s <%s>\n"
           "Aborting...\n",
            filename,
            "file_size");
}

int is_exist(char* filename) {
    return (access(filename,F_OK) != -1);
}

static void clean_and_exit (int sig) {
    printf("INSIDE SIGNAL HANDLING!\n");
    char    fpath[1024] = {'\0'};
    int rc;

    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    rc = unlink(fpath);
    if(rc) {
        printf("error: failed to unlink file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
    }
    exit(rc);
}

int main ( int argc, char *argv[]) {

    //validate 3 command line arguments given
    if (argc != 2) {
        usage(argv[0]);
        goto exit;
    }

    //declerations:
    char    *end_ptr;
    char    fpath[1024] = {'\0'};
    char    buf[BUFSIZE] = {'\0'};
    char    eof = EOF;
    int     fd, rc, _rc;
    double  elapsed_msec;
    size_t  b_to_write;
    ssize_t b_write;
    size_t  size, b_left;
    struct  timeval t1, t2;
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));

    //create FIFO file
    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    if(!is_exist(fpath)) {
        rc = mkfifo(fpath, PERMISSIONS);
        if (rc) {
            printf("ERROR: Failed to create fifo file [%s]\n"
                   "Cause: %s [%d]\n",
                   fpath, strerror(errno), errno);
            goto exit;
        }
    }

    //open file for writing
    fd = open(fpath, O_WRONLY);
    if (fd < 0) {
        printf("ERROR: Failed to open file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = -1;
        goto exit;
    }

    //validate file size
    size = (size_t)strtol(argv[1], &end_ptr, 10);
    if (!size) {
        printf("ERROR: Invalid num of writes: [%lu]\n", size);
        rc = -1;
        goto cleanup;
    } 

    //construct write_buffer
    memset(&buf, 'a', sizeof(char) * BUFSIZE);

    //register sigpipe handler
    sa.sa_handler = clean_and_exit;
    rc = sigaction(SIGPIPE, &sa, NULL);
    if (rc) {
        printf("error: failed to create sigaction\n"
               "cause: %s [%d]\n",
               strerror(errno), errno);
       return -1;
    }

    //start measurements
    rc = gettimeofday(&t1, NULL);
    if (rc) {
       printf("ERROR: Failed to measure time\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
        goto cleanup;
    }

    //write to file
    b_left = size;
    printf("Start!\n");
    while(b_left > 0) {
        b_to_write = MIN(b_left, BUFSIZE);
        b_write = write(fd,(char*)buf, sizeof(char) * b_to_write);
        if (b_write < 0) {
            printf("ERROR: Failed to measure time\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
            rc = -1;
            goto cleanup;
        }
        b_left -= (size_t)b_write;
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
    printf("%lu bytes were written in %f microseconds through FIFO\n",
           size, elapsed_msec);

cleanup:
    rc = unlink(fpath);
    if(rc) {
        printf("writer: failed to unlink file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
    }
exit:
    return rc;
}
