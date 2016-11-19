#define _GNU_SOURCE
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

#define BUF_SIZE 512

/*Resources Declerations */
struct file {
    char   *path;               //path to file
    int    id;                  //file descriptor
    struct stat stats;                  //file descriptor
    char   buf[BUF_SIZE];       
};

struct dir {
    char *path;
    DIR *id;
    struct dirent *entry;
};

int encrypt_file(struct file *src, struct file *key, struct file *tgt);

int generate_key(struct file *key);

void usage(char* filename);

int is_ignored(char* filename);

int create_files(struct file **src,
                   struct file **key,
                   struct file **tgt,
                   struct dir **src_dir,
                   struct dir **tgt_dir);

void destroy_files(struct file **src,
                   struct file **key,
                   struct file **tgt,
                   struct dir **src_dir,
                   struct dir **tgt_dir);

int clear_temp_resources(struct file *src,
                         struct file *key,
                         struct file *tgt);
/***/

void usage(char* filename) {
    printf("Usage: %s <%s> <%s> <%s>\n"
           "Aborting...\n",
            filename,
            "source",
            "key",
            "target");
}

int create_files(struct file **src,
                   struct file **key,
                   struct file  **tgt,
                   struct dir **src_dir,
                   struct dir **tgt_dir) {

    *src = (struct file *) malloc (sizeof(struct file));
    *key = (struct file *) malloc (sizeof(struct file));
    *tgt = (struct file *) malloc (sizeof(struct file));
    *src_dir = (struct dir *) malloc (sizeof(struct dir));
    *tgt_dir = (struct dir *) malloc (sizeof(struct dir));

    if ( !(*src) || !(*tgt) || !(*key) || !(*src_dir) || !(*tgt_dir) ) {
        return 1;
    }

    memset(*src, 0, sizeof(struct file));
    memset(*tgt, 0, sizeof(struct file));
    memset(*key, 0, sizeof(struct file));
    memset(*src_dir, 0, sizeof(struct dir));
    memset(*tgt_dir, 0, sizeof(struct dir));

    return 0;
}

void destroy_files(struct file **src,
                   struct file **key,
                   struct file **tgt,
                   struct dir **src_dir,
                   struct dir **tgt_dir) {

    free(*src);
    free(*key);
    free(*tgt);
    free(*src_dir);
    free(*tgt_dir);

}

int clear_temp_resources(struct file *src,
                         struct file *key,
                         struct file *tgt) {

    int _rc = 0;
    if (lseek(key->id, 0, SEEK_SET) < 0)
        return 1;
    _rc |= close(src->id);
    _rc |= close(tgt->id);
    if (_rc)
        return 1;
    free(src->path);
    free(tgt->path);
    return 0;
}

int generate_key(struct file *key) {
    int i, b_read = 0;
    char ch_buf;

    for(i=0; i < BUF_SIZE;) {
        b_read = read(key->id, &ch_buf, sizeof(char));
        if (b_read < 0) { //error on read
            printf("ERROR: Failed to read from file [%s]\n"
                   "Cause: %s [%d]\n",
                   key->path, strerror(errno), errno);
            return EXIT_FAILURE;
        }
        else {
            if (b_read == 0) {    //reach EOF
                if (lseek(key->id, 0, SEEK_SET) < 0) {
                    printf("ERROR: Failed to reset offset in file [%s]\n"
                           "Cause: %s [%d]\n",
                           key->path, strerror(errno), errno);
                    return 1;
                }
            } else                //read is ok
                key->buf[i++] = ch_buf;
        }
    }
    return EXIT_SUCCESS;
}

int encrypt_file(struct file *src, struct file *key, struct file *tgt) {

    ssize_t bytes_read, bytes_needed, bytes_written;
    bytes_needed = sizeof(src->buf);
    int i;

    while((bytes_read = read(src->id, src->buf,(size_t)bytes_needed)) > 0) {

        //generate key   
        if (generate_key(key)) {
            printf("ERROR: Failed to generate key from file [%s]\n"
                   "Cause: %s [%d]\n",
                   key->path, strerror(errno), errno);
            return EXIT_FAILURE;
        }

        //encrypt bytes into tgt buffer
        for (i=0; i < bytes_read; i++) {
            tgt->buf[i] = src->buf[i] ^ key->buf[i];
        }

        //write buffer into file
        bytes_written = write(tgt->id, tgt->buf, (size_t)bytes_read);
        if (bytes_written < bytes_read) {
            printf("ERROR: Failed to write to file [%s]\n"
                   "Cause: %s [%d]\n",
                   tgt->path, strerror(errno), errno);
            return EXIT_FAILURE;
        }
        
    }

    if (bytes_read < 0) { //ended with read error
        printf("ERROR: Failed to read from file [%s]\n"
               "Cause: %s [%d]\n",
               src->path, strerror(errno), errno);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int is_ignored(char* filename) {
    char *ignored[] = { ".", ".."};
    int length = 2;
    int i;

    for (i=0; i<length; i++) {
        if(!strcmp(filename, ignored[i]))
            return 1;
    }
    return 0;
}

int main ( int argc, char *argv[]) {

    //validate 3 command line arguments given
    if (argc != 4) {
        usage(argv[0]);
        goto exit;
    }

    //declerations:
    struct file *src, *tgt, *key;
    struct dir *src_dir, *tgt_dir;
    int rc = 0, _rc = 0;
    mode_t RO = S_IRUSR | S_IRGRP | S_IROTH;
    mode_t RW = S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    char path_buf[BUF_SIZE];
    create_files(&src, &key, &tgt, &src_dir, &tgt_dir);

    //open key file (if exist)
    key->path = strdup(argv[2]);
    key->id = open(key->path, (int)RO);
    if (key->id < 0) {
        printf("ERROR: Failed to open file [%s]\n"
               "Cause: %s [%d]\n",
               key->path, strerror(errno), errno);
        rc = 1;
        goto cleanup;
    }
    if(stat(key->path, &key->stats) || !key->stats.st_size) {
        printf("ERROR: File stats are bad for file [%s]\n"
               "Cause: %s [%d]\n",
               key->path, strerror(errno), errno);
        rc = 1;
        goto cleanup;    
    }

    //opens source dir
    src_dir->path = strdup(argv[1]);
    src_dir->id = opendir(src_dir->path);
    if (!src_dir->id) {
        printf("ERROR: Failed to open dir [%s]\n"
        "Cause: %s [%d]\n",
        src_dir->path, strerror(errno), errno);            
        rc = 1;
        goto cleanup;
    }

    //opens target dir
    tgt_dir->path = strdup(argv[3]);
    tgt_dir->id = opendir(tgt_dir->path);
    if (!tgt_dir->id) {
        rc = mkdir(tgt_dir->path, RW);
        if (rc) {
            printf("ERROR: Failed to create dir [%s]\n"
                   "Cause: %s [%d]\n",
                   tgt_dir->path, strerror(errno), errno);
            rc = 1;
            goto cleanup;
        }
    }

    //iterate over src dir files
    while ((src_dir->entry = readdir(src_dir->id))) {

        //open source file
        if(is_ignored(src_dir->entry->d_name))
            continue;
        sprintf(path_buf, "%s/%s", src_dir->path, src_dir->entry->d_name);
        src->path = strdup(path_buf);
        src->id = open(src->path,(int)RO);
        if (src->id < 0) {
            printf("ERROR: Failed to open source file [%s]\n"
                   "Cause: %s [%d]\n",
                   src->path, strerror(errno), errno);
            clear_temp_resources(src, key, tgt);
            rc = 1;
            continue;
        }
        if(stat(src->path, &src->stats) || !src->stats.st_size) {
            printf("WARNING: File stats are bad for file [%s]\n"
                   "Skipping file...\n"
                   "Cause: %s [%d]\n",
                   src->path, strerror(errno), errno);
            clear_temp_resources(src, key, tgt);
            rc = 1;
            continue;
        }

        //open target file
        sprintf(path_buf, "%s/%s", tgt_dir->path, src_dir->entry->d_name);
        tgt->path = strdup(path_buf);
        tgt->id = creat(tgt->path, RW);
        if (tgt->id < 0) {
            printf("ERROR: Failed to open target file [%s]\n"
                   "Cause: %s [%d]\n",
                   tgt->path, strerror(errno), errno);
            clear_temp_resources(src, key, tgt);
            rc = 1;
            goto cleanup;
        }
        if (lseek(tgt->id, 0, SEEK_SET) < 0) { //overwrite if file exist
            printf("ERROR: Failed to reset offset in file [%s]\n"
                   "Cause: %s [%d]\n",
                   tgt->path, strerror(errno), errno);
            clear_temp_resources(src, key, tgt);
            rc = 1;
            goto cleanup;
        }

        //encrypt file
        rc = encrypt_file(src, key, tgt);
        if (rc) {
            printf("ERROR: Failed to encrypt file [%s]\n"
                   "Cause: %s [%d]\n",
                   src->path, strerror(errno), errno);
            clear_temp_resources(src, key, tgt);
            goto cleanup;
        }

        //reset conditions
        rc = clear_temp_resources(src, key, tgt);
        if (rc) {
            printf("ERROR: Failed to clean resources for files\n"
                   "Cause: %s [%d]\n",
                   strerror(errno), errno);
            goto cleanup;
        }
    }

cleanup:
    free(key->path);
    free(src_dir->path);
    free(tgt_dir->path);
    if (key->id)
        _rc = close(key->id);
    if (src_dir->id)
        _rc |= closedir(src_dir->id);
    if (tgt_dir->id)
        _rc |= closedir(tgt_dir->id);
    if (_rc) {
        printf("ERROR: Failed to close files on exit\n"
               "Cause: %s [%d]\n",
               strerror(errno), errno);
        rc = 1;
    }
    destroy_files(&src, &key, &tgt, &src_dir, &tgt_dir);
exit:
    return (rc ? EXIT_FAILURE: EXIT_SUCCESS);
}
