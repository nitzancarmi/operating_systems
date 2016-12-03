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
    printf("Usage: %s\n"
           "Aborting...\n",
            filename);
}

static void handle_sigusr1 (int sig) {
  
    char *fmap;
    char fpath[1024] = {'\0'};;
    struct  timeval t1, t2;
    ssize_t fsize;
    int rc, fd;
    double elapsed_msec;
    struct stat fstat;
    memset(&fstat, 0, sizeof(struct stat));
 
    //open file
    sprintf((char*)fpath, "%s/%s", PIPE_PATH, PIPE_FILENAME);
    fd = open(fpath, O_RDWR | O_CREAT);
    if (fd < 0) {
        printf("ERROR: Failed to open file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = 1;
        goto cleanup;
    }

    //determine file size
    rc = stat(fpath, &fstat);
    if (rc) {
        printf("ERROR: Failed to acquire file stats\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
        goto cleanup;
    }
    fsize = fstat.st_size;

    //start time measurements
    rc = gettimeofday(&t1, NULL);
    if (rc) {
       printf("ERROR: Failed to measure time\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
        goto cleanup;
    }

    //memory map the file
    fmap = (char*)mmap(NULL,
                       (size_t)fsize,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       fd,
                       0);
    if (fmap == MAP_FAILED) {
        printf("ERROR: Failed to mmap file [%s]\n"
               "Cause: %s [%d]\n",
               fpath, strerror(errno), errno);
        rc = 1;
        goto cleanup;
    }

    //count the number of a's (and single '\0' at the end)
    int i = 0, count = 1;
    while(fmap[i]) {
        if(fmap[i++] == 'a')
            count++;
    }

    // finish measurement   
    rc = gettimeofday(&t2, NULL);
    if (rc) {
       printf("error: failed to measure time\n"
               "cause: %s [%d]\n",
               strerror(errno), errno);
        goto cleanup;
    }
    elapsed_msec = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsed_msec += (t2.tv_usec - t1.tv_usec) / 1000.0;

    //print results
    printf("%d bytes were read in %f microseconds through MMAP\n",
           count, elapsed_msec);

cleanup:
    rc = unlink(fpath);
    if(rc) {
       printf("error: failed to unlink file [%s]\n"
               "cause: %s [%d]\n",
               fpath, strerror(errno), errno);
    }
    exit(0);
}

int main ( int argc, char *argv[]) {

    //validate 3 command line arguments given
    if (argc != 1) {
        usage(argv[0]);
        return 1;
    }

    int rc;
    //declerations:
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
//    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handle_sigusr1;
    rc = sigaction(SIGUSR1, &sa, NULL);
    if (rc) {
        printf("error: failed to create sigaction\n"
               "cause: %s [%d]\n",
               strerror(errno), errno);
        return 1;
       }

    while(1) {
        sleep(2);
    }

    //shouldn't ever get here
    return 1;
}
