#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <assert.h>

#define PR_ERR(msg)     printf("ERROR[%s] %s : [%d] %s\n", __func__, msg, errno, strerror(errno)) 

#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define RAND_DEV "/dev/urandom"

struct file_ctx {
    size_t  size;  
};

/*file descriptors to be visible to signal handler too*/
int    sock_fd;
int    key_fd;


void usage(char* filename);
int generate_keyfile(int fd, size_t size);

void usage(char* filename) {
    printf("Usage: %s [%s] [%s] [%s] (%s)\n"
           "Aborting...\n",
            filename,
            "port",
            "key_path",
            "key_length");
}

int read_and_write(int src, int tgt, ssize_t size) {
    int b_read, b_write;
    int buf[1024];

    while (size > 0) {
        b_read = read(src, buf, MIN(size, 1024));
        if(b_read < 0) {
            PR_ERR("failed to read from file");
            return -1;
        }
        size -= b_read;

        while(b_read > 0) {
            b_write = write(tgt, buf, b_read);
            if (b_write < 0) {
                PR_ERR("failed to write to file");
                return -1;
            }
            b_read -= b_write;
        }
    }

    return 0;
}

int generate_keyfile(int fd, ssize_t size) {
    if(!size)
        return -1;

    int rnd, rc;
    rnd = open(RAND_DEV,O_RDONLY);
    if(!rnd) {
        PR_ERR("failed to open random device");
        return -1;
    }

    rc = read_and_write(rnd, fd, size);
    if(rc) {
        PR_ERR("failed to create the random file");
        return -1;
    }

    rc = close(RAND_DEV);
    if (rc) {
        PR_ERR("failed to close random device");
        return -1;
    }

    return 0;
}

static void handle_sigint (int sig) {
    int rc;

    if(sock_fd) {
        rc = close(sock_fd);
        if (rc) {
            PR_ERR("failed to close socket fd");
            exit(rc);
        }
    }

    if(key_fd) {
        rc = close(key_fd);
        if (rc) {
            PR_ERR("failed to close key fd");
            exit(rc);
        }
    }

    exit(rc);
}

int handle_

int main(int argc, char *argv[])
{

    //validate 2/3 command line arguments given
    if (argc != 3 && argc != 4) {
        usage(argv[0]);
        return -1;
    }

    int conn_fd = 0;
    struct sockaddr_in serv_attr;  
    struct sigaction sa;
    char send_buf[1024];
    char recv_buf[1024];
    unsigned short port;
    char *key_path;
    int key_fd;
    size_t key_len = 0;
    int rc = 0, _rc = 0;
    sock_fd = 0;
    key_fd = 0;
    int frk;

    //parse cmd line variables
    port = (unsigned short)strtol(argv[1], NULL, 10);
    key_path = argv[2];
    if (argv[3])
        key_len = (size_t)strtol(argv[3], NULL,10);
    if (!port || !key_path) {
        PR_ERR("Invalid command-line arguments");
        usage(argv[0]);
        return rc;
    }

    //open key file (and create it, if needed)
    key_fd = creat(key_path, S_IRWXU | S_IRWXG | S_IRWXO);
    if (key_fd < 0) {
        PR_ERR("Failed to open key file");
        rc = -1;
        goto exit;
    }
    if(key_len) {
        rc = truncate(key_path,(off_t)(key_len));
        if (rc) {
            PR_ERR("Failed to truncate file");
            goto cleanup;
        }
        rc = generate_keyfile(key_fd, key_len);
        if (rc) {
            PR_ERR("Failed to generate random key");
            goto cleanup;
        }
    }

    //initialize a new socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        PR_ERR("failed to init socket");
        rc = sock_fd;
        goto cleanup;
    }

    //set server attributes
    memset(&serv_attr, '0', sizeof(serv_addr));
    serv_attr.sin_family = AF_INET;
    serv_attr.sin_port = htons(port); 
    serv_attr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = any local machine address

    rc = bind(sock_fd,(struct sockaddr*)&serv_attr, sizeof(serv_attr));
    if (rc) {
        PR_ERR("failed to bind socket");
        goto cleanup;
    }

    rc = listen(sock_fd, 10) {
    if (rc) {
        PR_ERR("failed to start listening in socket");
        goto cleanup;
    }

    //register signal SIGINT handler
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = 0;
    sa.sa_handler = handle_sigint;
    rc = sigaction(SIGINT, &sa, NULL);
    if (rc) {
        PR_ERR("failed to register SiGINT handler");
        goto cleanup;
    }

    //accept user requests for etternity
    while(1) {
        conn_fd = accept(sock_fd, NULL, NULL);
        if(conn_fd < 0){
            PR_ERR("failed to accept connections");
            goto cleanup;
        }

        //fork process and handle user in son
        frk = fork();
        if (frk < 0) {
            PR_ERR("failed to fork a process");
            goto cleanup;
        }
        if (!f) { //i.e child process
            handle_client_req(key_path, conn_fd);
            PR_ERR("execv failed");
            goto cleanup;
        }
    }
}
