
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

#define PR_ERR(msg)     printf("ERROR[%s] %s : [%d] %s\n", __func__, msg, errno, strerror(errno)) 

struct file_ctx {
  size_t size;  
};

void usage(char* filename);

void usage(char* filename) {
    printf("Usage: %s [%s] [%s] [%s] [%s]\n"
           "Aborting...\n",
            filename,
            "ip",
            "port",
            "in_file",
            "out_file");
}

int main(int argc, char *argv[])
{

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

/***/

    int rc = 0, _rc = 0;
    char* ip, *fin_pth, *fout_pth;
    unsigned short port;
    int sock_fd, fin_fd, fout_fd;
    int b_to_recv, br_local, bw_local, br_remote, bw_remote;
    int b_written, b_left;
    char recv_buf[1024];
    char send_buf[1024];
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

/***/

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

    //open uotput file and truncate it to input file's size
    fout_fd = creat(fout_pth, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fin_fd < 0) {
        PR_ERR("Failed to open input file");
        rc = -1;
        goto cleanup;
    }
    rc = truncate(fout_pth,(off_t)(fin_st.st_size));
    if (rc) {
        PR_ERR("Failed to truncate file");
        goto cleanup;
    }

/***/

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
    rc = connect(sock_fd,(struct sockaddr *)&serv_attr, sizeof(serv_attr));
    if (rc) {
       PR_ERR("socket connect Failed");
       goto cleanup;
    } 

    //handoff file context to server
    memset(&ctx, '0', sizeof(struct file_ctx));
    ctx.size = (size_t)fin_st.st_size;
    b_left = sizeof(struct file_ctx);
    b_written = 0;
    while (b_left > 0) {
        b_written = write(sock_fd, &ctx, sizeof(struct file_ctx));
        if(b_written < 0) {
            PR_ERR("failed to write context to socket");
            rc = -1;
            goto cleanup;
        }
        b_left -= b_written;
    }
    
    br_local = 0;
    bw_remote = 0;
    b_to_recv = 0;
    memset(send_buf, '0',sizeof(send_buf));
    while (1) {

        //read from file and send to server
        br_local = read(fin_fd, send_buf, sizeof(send_buf));
        if(!br_local) //reach EOF
            break;
        if (br_local < 0) {
            PR_ERR("failed to read from local file");
            rc = -1;
            goto cleanup;
        }

        b_to_recv = br_local;
        while(br_local > 0) {
            bw_remote = write(sock_fd, send_buf, (size_t)br_local);
            if(bw_remote < 0) {
                PR_ERR("failed to write to socket");
                rc = -1;
                goto cleanup;
            }
            br_local -= bw_remote;
        }

        //read from server and write back to file
        while(b_to_recv > 0) {
            br_remote = read(sock_fd, recv_buf, (size_t)b_to_recv);
            if(!br_remote) //reach EOF
                break;
            if (br_remote < 0) {
                PR_ERR("failed to read from socket");
                rc = -1;
                goto cleanup;
            }
            b_to_recv -= br_remote;

            while(br_remote > 0) {
                bw_local = write(fout_fd, recv_buf, (size_t)br_remote);
                if(bw_local < 0) {
                    PR_ERR("failed to write to output file");
                    rc = -1;
                    goto cleanup;
                }
                br_remote -= bw_local;
            }
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
