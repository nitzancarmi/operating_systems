#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 

#define PR_ERR(msg)     printf("ERROR [%s] %s : [%d] %s\n", __func__, msg, errno, strerror(errno)) 

#define BUF_SIZE        4096

#define BUF_SIZE        4096

struct file_ctx {
  size_t size;  
};

void usage(char* filename);
int handoff_file_ctx(struct file_ctx *ctx, int socket);
int transfer_buffer(int fin, int fout, size_t size);

void usage(char* filename) {
    printf("Usage: %s [%s] [%s] [%s] [%s]\n"
           "Aborting...\n",
            filename,
            "ip",
            "port",
            "in_file",
            "out_file");
}

int handoff_file_ctx(struct file_ctx *ctx, int socket) {
    int b_left, b_written;
 
    b_left = sizeof(struct file_ctx);
    b_written = 0;
    while (b_left > 0) {
        b_written = write(socket, ctx, sizeof(struct file_ctx));
        if(b_written < 0) {
            PR_ERR("failed to write context to socket");
            return -1;
        }
        b_left -= b_written;
    }
    return 0;
}

int transfer_buffer(int fin, int fout, size_t size) {
    int b_read, b_write;
    char buf[size];
    int res = 0;

    //read from input file into buffer
    b_read = read(fin, buf, sizeof(buf));
    if(!b_read) //reach EOF
        return 0;
    if (b_read < 0) {
        PR_ERR("failed to read from local file");
        return -1;
    }
    res = b_read;

    //write buffer to output file
    while(b_read > 0) {
        b_write = write(fout, buf, (size_t)b_read);
        if(b_write < 0) {
            PR_ERR("failed to write to socket");
            return -1;
        }
        b_read -= b_write;
    }

    return res;
}

int main(int argc, char *argv[])
{

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

    int rc = 0, _rc = 0;
    char *ip, *fin_pth, *fout_pth;
    unsigned short port;
    int sock_fd, fin_fd, fout_fd;
    int b_send, b_recv;
    struct stat fin_st;
    struct sockaddr_in serv_attr;
    struct file_ctx ctx;

    //parse cmd line variables
    ip      = argv[1];
    port    = (unsigned short)strtol(argv[2], NULL, 10);
    fin_pth     = argv[3];
    fout_pth    = argv[4];
    if (!ip || !port || !fin_pth || !fout_pth) {
        PR_ERR("Invalid command-line arguments");
        usage(argv[0]);
        return rc;
    }

    //open input file
    fin_fd = open(fin_pth, O_RDONLY);
    if (fin_fd < 0) {
        PR_ERR("Failed to open input file");
        rc = -1;
        goto exit;
    }
    if(stat(fin_pth, &fin_st) || !fin_st.st_size) {
        PR_ERR("bad input file stats");
        rc = -1;
        goto cleanup;    
    }

    //open output file and truncate it to input file's size
    fout_fd = creat(fout_pth, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fin_fd < 0) {
        PR_ERR("Failed to open output file");
        rc = -1;
        goto cleanup;
    }
    rc = truncate(fout_pth, (off_t)(fin_st.st_size));
    if (rc) {
        PR_ERR("Failed to truncate output file");
        goto cleanup;
    }

    //initialize a new socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        PR_ERR("failed to init socket");
        rc = sock_fd;
        goto cleanup;
    }

    //connect to server
    memset(&serv_attr, '0', sizeof(serv_attr)); 
    serv_attr.sin_family = AF_INET;
    serv_attr.sin_port = htons(port);
    serv_attr.sin_addr.s_addr = inet_addr(ip);
    rc = connect(sock_fd, (struct sockaddr *)&serv_attr,
                                sizeof(serv_attr));
    if (rc) {
       PR_ERR("socket connect Failed");
       goto cleanup;
    } 

    //handoff file context to server
    memset(&ctx, '0', sizeof(struct file_ctx));
    ctx.size = (size_t)fin_st.st_size;
    rc = handoff_file_ctx(&ctx, sock_fd);
    if (rc) {
       PR_ERR("failed to send file context to server");
       goto cleanup;
    }

    /* main communication with server */
    b_send = 0;
    b_recv = 0;
    while (1) {

        //read from input and send over socket
        b_send = transfer_buffer(fin_fd, sock_fd, BUF_SIZE);
        if(!b_send) //reach EOF
            break;
        if (b_send < 0) {
            PR_ERR("failed to send buffer to server");
            rc = -1;
            goto cleanup;
        }

        //read from socket and write back to output file
        b_recv = transfer_buffer(sock_fd, fout_fd,(size_t)b_send);
        if (b_recv < 0) {
            PR_ERR("failed to receive buffer from server");
            rc = -1;
            goto cleanup;
        }
    }

cleanup:
    if(sock_fd)
        _rc |= close(sock_fd);
    if(fin_fd)
        _rc |= close(fin_fd);
    if(fout_fd)
        _rc |= close(fout_fd);
    if (_rc) {
        PR_ERR("failed to close file descriptors");
        rc = rc ? rc:_rc;
    }
exit:
    return rc;
}
