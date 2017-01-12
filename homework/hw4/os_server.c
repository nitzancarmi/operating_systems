#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h> 
#include <assert.h>

#define PR_ERR(msg)     printf("ERROR[%s] %s : [%d] %s\n", __func__, msg, errno, strerror(errno)) 

#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define RAND_DEV        "/dev/urandom"

struct file_ctx {
    size_t  size;  
};

/*file descriptors to be visible to signal handler too*/
int    sock_fd;
int    key_fd;


void usage(char* filename);
void generate_key(char* buf, size_t size);
int read_encrypt_write(int src, int tgt, size_t size, bool encrypt);
int create_random_keyfile(int fd, size_t size);
int create_keyfile(char *key_path, size_t key_len);
static void handle_sigint (int sig);
int handle_client_req(char *key_path);

void usage(char* filename) {
    printf("Usage: %s [%s] [%s] (%s)\n"
           "Aborting...\n",
            filename,
            "port",
            "key_path",
            "key_length");
}

void generate_key(char* buf, size_t size) {
    unsigned int i;
    int b_read = 0;
    char ch_buf;
    memset(buf, '0', size);

    for(i=0; i < size; i++) {
        b_read = read(key_fd, &ch_buf, sizeof(char));
        if (b_read < 0) {
            PR_ERR("failed to read from key file");
            break;
        }
        else {
            if (b_read == 0) {    //reach EOF
                i--;
                if (lseek(key_fd, 0, SEEK_SET) < 0)
                    PR_ERR("failed to reset offset");
            }
            else {                //read is ok
                buf[i] = ch_buf;
            }
        }
    }
}

int read_encrypt_write(int src, int tgt, size_t size, bool encrypt) {
    int b_read, b_write;
    char buf[1024];
    char key_buf[1024];
    int i;

    while (size > 0) {
        b_read = read(src, buf, MIN(size, 1024));
        if(b_read < 0) {
            PR_ERR("failed to read from file");
            return -1;
        }
        size -= (size_t)b_read;

        if (encrypt) {
            generate_key(key_buf, (size_t)b_read);
            for (i=0; i<b_read; i++)
                buf[i] ^= key_buf[i];
        }

        while(b_read > 0) {
            b_write = write(tgt, buf,(size_t)b_read);
            if (b_write < 0) {
                PR_ERR("failed to write to file");
                return -1;
            }
            b_read -= b_write;
        }
    }

    return 0;
}

int create_random_keyfile(int fd, size_t size) {
    if(!size)
        return -1;

    int rnd, rc;
    rnd = open(RAND_DEV,O_RDONLY);
    if(!rnd) {
        PR_ERR("failed to open random device");
        return -1;
    }

    rc = read_encrypt_write(rnd, fd, size, false);
    if(rc) {
        PR_ERR("failed to create the random file");
        return -1;
    }

    rc = close(rnd);
    if (rc) {
        PR_ERR("failed to close random device");
        return -1;
    }

    return 0;
}

int create_keyfile(char *key_path, size_t key_len) {
    int rc = 0;

    key_fd = creat(key_path, S_IWUSR|S_IWGRP|S_IWOTH);
    if (key_fd < 0) {
        PR_ERR("Failed to open key file");
        return -1;
    }

    rc = truncate(key_path,(off_t)(key_len));
    if (rc) {
        PR_ERR("Failed to truncate file");
        return rc;
    }

    rc = create_random_keyfile(key_fd, key_len);
    if (rc) {
        PR_ERR("Failed to generate random key");
        return rc;
    }

    rc = chmod(key_path, S_IRUSR|S_IRGRP|S_IROTH);
    rc = close(key_fd);
    if (rc) {
        PR_ERR("Failed to close key file");
        return rc;
    }
    key_fd = 0; //for later use

    return rc;
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

    printf("\n");
    exit(rc);
}

int handle_client_req(char *key_path) {
    struct file_ctx *ctx;
    int b_left, b_read;
    int i, rc, _rc;
    char buf[sizeof(struct file_ctx)];
    char *buf_p;

    //open key file
    key_fd = open(key_path, O_RDONLY, S_IRWXU|S_IRWXG|S_IRWXO);
    if (key_fd < 0) {
        PR_ERR("Failed to open key file");
        goto hdl_cleanup;
    }

    //get file attributes from client
    b_left = sizeof(struct file_ctx);
    b_read = 0;
    buf_p = (char*)buf;
    while (b_left > 0) {
        b_read = read(sock_fd, buf_p, sizeof(struct file_ctx));
        if(b_read < 0) {
            PR_ERR("failed to read context from socket");
            goto hdl_cleanup;
        }
        buf_p += b_read;
        b_left -= b_read;
    }
    ctx = (struct file_ctx *)buf;

    //read file from client
    rc = read_encrypt_write(sock_fd, sock_fd, ctx->size, true);
    if (rc) {
        PR_ERR("failed to read/write file via TCP");
        goto hdl_cleanup;
    }

hdl_cleanup:
    if(key_fd)
        _rc  = close(key_fd);
    if(sock_fd)
        _rc |= close(sock_fd);
    if (rc) {
        PR_ERR("Failed to close key/socket file descriptors");
        rc = rc ? rc:_rc;
    }
    return rc;
}

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
    size_t key_len = 0;
    int rc = 0, _rc = 0;
    int frk;
    key_fd = 0;
    sock_fd = 0;

    //parse cmd line variables
    port = (unsigned short)strtol(argv[1], NULL, 10);
    key_path = argv[2];
    if (argv[3])
        key_len = (size_t)strtol(argv[3], NULL,10);
    if (!port || !key_path) {
        PR_ERR("Invalid command-line arguments");
        usage(argv[0]);
        return -1;
    }

    //create key file (if needed)
    if(key_len) {
        rc = create_keyfile(key_path, key_len);
        if (rc) {
            PR_ERR("Failed to create key file");
            return rc;
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
    memset(&serv_attr, '0', sizeof(serv_attr));
    serv_attr.sin_family = AF_INET;
    serv_attr.sin_port = htons(port); 
    serv_attr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = any local machine address

    rc = bind(sock_fd,(struct sockaddr*)&serv_attr, sizeof(serv_attr));
    if (rc) {
        PR_ERR("failed to bind socket");
        goto cleanup;
    }

    rc = listen(sock_fd, 10);
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

    //accept user requests for eternity
    while(1) {
        conn_fd = accept(sock_fd, NULL, NULL);
        if(conn_fd < 0){
            PR_ERR("failed to accept connections");
            goto cleanup;
        }

        //child process should handle user requests
        frk = fork();
        if (frk < 0) {
            PR_ERR("failed to fork a process");
            goto cleanup;
        }
        if (frk == 0) { //i.e child process
            sock_fd = conn_fd;
            return handle_client_req(key_path);
            PR_ERR("child process failed");
            goto cleanup;
        }
    }

cleanup:
    if(key_fd)
        _rc  = close(key_fd);
    if(sock_fd)
        _rc |= close(sock_fd);
    if (rc) {
        PR_ERR("Failed to close key files");
        rc = rc ? rc:_rc;
    }
    return rc;
}
