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

//global variables needed to handle
//sigpipe and enable the program
//to exit "gracefully"
sigset_t old_mask;
size_t actual_wsize;
struct timeval t1, t2;
int fd;


void usage(char* filename);
int is_exist(char* filename);

void usage(char* filename) {
    printf("Usage: %s <%s>\n"
           "Aborting...\n",
            filename,
            "file_size");
}

int is_exist(char* filename) {

    int rc;
    struct stat statbuf;
    rc = stat(filename, &statbuf);
    if (rc < 0) {
        //something bad happened...
        if (errno != ENOENT) {
            return rc;
        }
        // error is ENOENT - path doesn't exist
        else {
            return 0;
        }
    }
    //file exist
    return 1;
}

static void clean_and_exit (int sig) {
    char    fpath[1024] = {'\0'};
    int _rc, rc = -1;
    double elapsed_msec;

    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);

    //finish time measurement
    _rc = gettimeofday(&t2, NULL);
    if (_rc) {
        printf("ERROR: Failed to measure time\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
    }
    elapsed_msec = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsed_msec += (t2.tv_usec - t1.tv_usec) / 1000.0;

    //print results
    printf("%lu bytes were written in %f miliseconds through FIFO\n",
           actual_wsize, elapsed_msec);

    _rc = close(fd);
    if(_rc) {
        printf("ERROR: failed to close file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
    }

    _rc = unlink(fpath);
    if (_rc) {
        printf("error: failed to unlink file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
    }

    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) < 0) {
            printf("ERROR: Failed to allow back SIGINT\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
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
    int     rc, _rc;
    double  elapsed_msec;
    size_t  b_to_write;
    ssize_t b_write;
    size_t  size, b_left;
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigset_t mask;
    fd = 0;

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

    //create FIFO file (if not exist)
    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    if((_rc = is_exist(fpath)) == 0) {
        rc = mkfifo(fpath, PERMISSIONS);
        if (rc) {
            printf("ERROR: Failed to create fifo file [%s]\n"
                   "Cause: %s [%d]\n",
                   fpath, strerror(errno), errno);
            goto exit;
        }
    }
    else {
        if (_rc < 0) {
            printf("ERROR: Failed to approach fifo file [%s]\n"
                   "Cause: %s [%d]\n",
                   fpath, strerror(errno), errno);
            rc = _rc;
            goto exit;
        }
    }

    //change permissions for file
    rc = chmod((char*)fpath, PERMISSIONS);
    if (rc) {
        printf("ERROR: Failed to change mode to file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        goto cleanup;
    }

    //open file for writing
    fd = open(fpath, O_WRONLY);
    if (fd < 0) {
        printf("ERROR: Failed to open file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = -1;
        goto cleanup;
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

    //write to file
    b_left = size;
    actual_wsize = 0;
    while(b_left > 0) {
        b_to_write = MIN(b_left, BUFSIZE);
        b_write = write(fd,(char*)buf, sizeof(char) * b_to_write);
        if (b_write < 0) {
            printf("ERROR: Failed to write to file [%s]\n"
                   "Cause: %s [%d]\n",
                   fpath, strerror(errno), errno);
            rc = -1;
            goto cleanup;
        }
        b_left -= (size_t)b_write;
        actual_wsize += (size_t)b_write;
    }

    //validate that actual write is of expected size
    if (actual_wsize != size) {
        printf("ERROR: Insufficient bytes were written to file [%s]\n"
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
    printf("%lu bytes were written in %f miliseconds through FIFO\n",
           actual_wsize, elapsed_msec);

cleanup:
    _rc = close(fd);
    if(_rc) {
        printf("ERROR: failed to close file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        if(!rc)
            rc = _rc;
    }

//    _rc = unlink(fpath);
    if(_rc) {
        printf("ERROR: failed to unlink file [%s]\n"
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
