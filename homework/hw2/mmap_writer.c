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
#define PIPE_FILENAME   "mmapped.bin"
#define PERMISSIONS      0600

void usage(char* filename);


void usage(char* filename) {
    printf("Usage: %s <%s> <%s>\n"
           "Aborting...\n",
            filename,
            "file_size",
            "reader_pid");
}

int main ( int argc, char *argv[]) {

    //validate 3 command line arguments given
    if (argc != 3) {
        usage(argv[0]);
        goto exit;
    }

    //declerations:
    char    *end_ptr, *fmap = NULL;
    char    fpath[1024] = {'\0'};
    int     fd, rc, _rc;
    size_t  size;
    pid_t   rpid = (pid_t)strtol(argv[2], &end_ptr, 10);
    struct  timeval t1, t2;
    double  elapsed_msec;

    //ignore SIGTERM signal
    sigset_t mask, old_mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, &old_mask) < 0) {
            printf("ERROR: Failed to block SIGTERM\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
            rc = -1;
            goto exit;
    }

    //open file
    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    fd = open(fpath, O_RDWR | O_CREAT);
    if (fd < 0) {
        printf("ERROR: Failed to create file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = -1;
        goto cleanup;
    }

    //change permissions fo file to enable mmapping
    rc = chmod((char*)fpath, PERMISSIONS);
    if (rc) {
        printf("ERROR: Failed to change mode to file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        goto cleanup;
    }

    //validate file size
    size = (size_t)strtol(argv[1], &end_ptr, 10);
    if (!size) {
        printf("ERROR: Invalid file size: [%lu]\n", size);
        rc = -1;
        goto cleanup;
    } 

    //truncate file to expected size
    rc = truncate((char*)fpath,(off_t)(sizeof(char) * size));
    if (rc) {
        printf("ERROR: Failed to truncate file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        goto cleanup;
    }

    //memory map the file
    fmap = (char*)mmap(NULL,
                       sizeof(char) * size,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       fd,
                       0);
    if (fmap == MAP_FAILED) {
        printf("ERROR: Failed to mmap file [%s]\n"
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
    memset(fmap, 'a', sizeof(char) * size);
    fmap[size-1] = '\0';
    rc = gettimeofday(&t2, NULL);
    if (rc) {
       printf("ERROR: Failed to measure time\n"
              "Cause: %s [%d]\n",
              strerror(errno), errno);
        goto cleanup;
    }
    elapsed_msec = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsed_msec += (t2.tv_usec - t1.tv_usec) / 1000.0;

    //signal remote process for completion
    rc = kill(rpid, SIGUSR1); 
    if (rc) {
        printf("ERROR: Failed to mmap file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        goto cleanup;
    }

    //print results
    printf("%lu bytes were written in %f microseconds through MMAP\n",
           size, elapsed_msec);

cleanup:
    _rc = 0;
    if (fd)
        _rc = close(fd);
    if (fmap)
        _rc |= munmap(fmap, sizeof(char) * size);
    if (_rc) {
        printf("ERROR: Failed to clean resources\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
               rc = -1;
    }
    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) < 0) {
            printf("ERROR: Failed to allow back SIGTERM\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
            _rc = -1;
    }
    rc |= _rc;
exit:
    return rc;
}
