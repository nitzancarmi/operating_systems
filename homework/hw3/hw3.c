#define _GNU_SOURCE
#include<sys/types.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<errno.h>
#include<pthread.h>
#include<time.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static pthread_cond_t wakeup_gc;

static int stop;
static int max_size;

typedef struct intlist_entry_t {
    int                     data;
    struct intlist_entry_t  *next;
    struct intlist_entry_t  *prev;
} intlist_entry_t;

typedef struct intlist_t {
    int              size;
    intlist_entry_t     *first;
    intlist_entry_t     *last;
    pthread_mutex_t     lock;
	pthread_mutexattr_t lock_attr;
    pthread_cond_t      nonempty;
} intlist_t;

/*Usage function*/
void usage(char* filename);

/*initialize the list. You may assume the argument is not a previously initialized or destroyed list*/
void intlist_init(intlist_t **list);

/*frees all memory used by the list, including any of its items*/
void intlist_destroy(intlist_t *list);

/*returns a new allocated list entry, or NULL on failure*/
intlist_entry_t* intlist_entry_create(int n);

/*destroys a single list entry*/
void intlist_entry_destroy(intlist_entry_t *entry);

/*destroys a linked list of entries, starting with head */
void intlist_multiple_entries_destroy(intlist_entry_t *head);

/*receives an int, and adds it to the head of the list*/
void intlist_push_head(intlist_t *list, int value);

/*removes an item from the tail, and returns its value*/
int intlist_pop_tail(intlist_t *list);

/*removes k items from the tail, without returning any value*/
void intlist_remove_last_k(intlist_t *list, int k);

/*returns the number of items currently in the list*/
int intlist_size(intlist_t *list);

/*returns the mutex used by this list*/
pthread_mutex_t* intlist_get_mutex(intlist_t *list);

/*function that should be run by a writer thread */
void* intlist_writer(void* list);

/*function that should be run by a reader thread */
void* intlist_reader(void* list);

/*function that should be run by the garbage collector thread */
void* intlist_garbage_collector(void* list);

/***/

void usage(char* filename) {
    printf("Usage: %s [%s] [%s] [%s] [%s]\n"
           "Aborting...\n",
            filename,
            "number_writers",
            "num_readers",
            "max_items",
            "duration");
}

void intlist_init(intlist_t **p_list) {
    int rc;
    intlist_t *list;

    /*allocate a list struct*/
    *p_list = (intlist_t *)malloc(sizeof(intlist_t));
    list = *p_list;
    if (!list) {
        printf("%s: Could not allocate memory for list",
               __func__);
        return;
    }
    memset(list, 0, sizeof(intlist_t));

    /*set mutex attributes (recursive)*/
	rc = pthread_mutexattr_init(&list->lock_attr);
    if (rc) {
        printf("%s: mutex attributes init failed\n",
               __func__);
        free(list);
        return;
    }
    rc = pthread_mutexattr_settype(&list->lock_attr,PTHREAD_MUTEX_RECURSIVE);
    if (rc) {
        printf("%s: mutex attributes set type failed\n",
               __func__);
        pthread_mutexattr_destroy(&list->lock_attr);
        free(list); 
        return;
    }

    /*set the mutex with configured attributes*/
    rc = pthread_mutex_init(&list->lock, &list->lock_attr);
    if (rc) {
        printf("%s: mutex init failed\n",
               __func__);
        pthread_mutexattr_destroy(&list->lock_attr);
        free(list); 
        return;
    }

    /*set non-empty list conditional variable*/
    rc = pthread_cond_init(&list->nonempty, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_mutex_destroy(&list->lock);
        pthread_mutexattr_destroy(&list->lock_attr);
        free(list); 
        return;
    }
}

void intlist_destroy(intlist_t *list) {
    int rc;
	rc = pthread_mutexattr_destroy(&list->lock_attr);
    if (rc) {
        printf("%s: mutex_attributes destroy failed\n",
               __func__);
        return;
    }

    rc  = pthread_cond_destroy(&list->nonempty);
    if (rc) {
        printf("%s: mutex condition destroy failed\n",
               __func__);
        return;
    }

    rc = pthread_mutex_destroy(&list->lock);
    if (rc) {
            printf("%s: mutex_destroy failed\n",
               __func__);
        return;
    }

    printf("size: %d\n", list->size);
    if(list->size)
        intlist_multiple_entries_destroy(list->first);
    free(list);
}

intlist_entry_t* intlist_entry_create(int n) {
    int rc;
    intlist_entry_t *entry;

    entry = (intlist_entry_t *) malloc (sizeof(intlist_entry_t));
    if (!entry) {
        printf("%s: Could not allocate memory for list entry",
               __func__);
        return entry;
    }
    memset(entry, 0, sizeof(intlist_entry_t));
    entry->data = n;
    return entry;
}

void intlist_entry_destroy(intlist_entry_t *entry) {
    free(entry);
}

void intlist_multiple_entries_destroy(intlist_entry_t *head) {
    intlist_entry_t *curr, *next;
    int i; //nitzanc

    for (curr = head; curr != NULL; curr = next) {
        printf("deleting %d-th element\n", i++);
        next = curr->next;
        intlist_entry_destroy(curr);
    }
}

void intlist_push_head(intlist_t *list, int value) {
    int rc;
    intlist_entry_t *entry = intlist_entry_create(value);
    if(!entry) {
        printf("%s: failed to create entry\n",
               __func__);
        return;
    }

    printf("writer: try to take lock...\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        intlist_entry_destroy(entry);
        return;
    }
    printf("writer: taken!\n");
 
    /**CS**/
    if (list->size == 0) { //i.e first element to be inserted
        list->first = entry;
        list->last = entry;
    } else {
        entry->next = list->first;
        list->first->prev = entry;
        list->first = entry;
    }
    list->size += 1;
    pthread_cond_signal(&list->nonempty);
    /**CS-END**/

    printf("writer: releasing lock...\n");
    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
    }
}

int intlist_pop_tail(intlist_t *list) {
    int rc, ret;
    intlist_entry_t *last;

    printf("reader: try to take lock...\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }
    printf("reader: taken!\n");
    //while list is empty, no items can be popped
    while(list->size == 0 && !stop) {
        printf("reader: entering wait...\n");
        rc = pthread_cond_wait(&list->nonempty, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return rc;
        }
        printf("reader: wake up from wait\n"); 
    }
    printf("reader: taken!\n");

    /**CS**/
    last = list->last;
    if (list->last)
        list->last = list->last->prev;
    if (list->last)
        list->last->next = NULL;
    list->size -= (list->first != NULL);
    /**CS-END**/

    printf("reader: releasing lock\n");
    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

    ret = last ? last->data:-1;
    intlist_entry_destroy(last);
    printf("destroyed. size: %d\n", list->size);
    return ret;
}

void intlist_remove_last_k(intlist_t *list, int k) {
    int rc, i, eff_size;
    intlist_entry_t *cutoff;

    printf("try to take lock in last k...\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return;
    }
    printf("last_k: taken!\n");

    /**CS**/
    i = k;
    eff_size = MIN(k, list->size);
    cutoff = list->last;
    for (i = 0; i < eff_size-1; i++)
        cutoff = cutoff->prev;
    list->size -= eff_size;
    list->last = cutoff ? cutoff->prev:NULL;
    if(list->last)
        list->last->next = NULL;
    list->first = list->size ? list->first:NULL;
    /**CS-END**/

    
    printf("last_k: releasing lock...\n");
    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return;
    }

    if(list->size)
        intlist_multiple_entries_destroy(cutoff);
    printf("finished destroying...\n");
}

int intlist_size(intlist_t *list) {
    return list->size;
}

pthread_mutex_t* intlist_get_mutex(intlist_t *list) {
    return &list->lock;
}

void* intlist_garbage_collector(void* void_list) {
    int rc = 0;
    int size, num_to_delete;
    intlist_t *list = (intlist_t*)void_list;
    pthread_mutex_t *lock = intlist_get_mutex(list);

    while(!stop) {

        printf("gc: try to take lock...\n"); 
        rc = pthread_mutex_lock(lock);
        if (rc) {
                printf("%s: mutex lock failed\n",
                   __func__);
                pthread_exit(&rc);
        }
        printf("gc: taken!\n"); 

        while (((size = intlist_size(list)) < max_size) && !stop) {
            printf("gc: entering wait...\n");
            rc = pthread_cond_wait(&wakeup_gc, lock);
            if (rc) {
                printf("%s: mutex conditional wait failed\n",
                       __func__);
                pthread_exit(&rc);
            }
            printf("gc: wakeup from lock\n");
        }
        printf("gc: taken!\n");
        if(stop) {
            rc = pthread_mutex_unlock(&list->lock);
            if (rc) {
                    printf("%s: mutex unlock failed\n",
                           __func__);
                    pthread_exit(&rc);
            }
            break;
        }

        /***CS***/
        printf("################# GARBAGE COLLECTOR###################\n");
        num_to_delete = size/2 + size%2;
        intlist_remove_last_k(list, num_to_delete);
        printf("GC – %d items removed from the list\n", num_to_delete);
        /***CS-END***/

        rc = pthread_mutex_unlock(&list->lock);
        if (rc) {
                printf("%s: mutex unlock failed\n",
                   __func__);
                pthread_exit(&rc);
        }

    }
    printf("closing gc...\n");
    pthread_exit(&rc);
}

void* intlist_writer(void* void_list) {
    srand((unsigned int)time(NULL));
    intlist_t *list = (intlist_t*)void_list;
    while(!stop) {
        intlist_push_head(list, rand());
        if (intlist_size(list) >= max_size)
            pthread_cond_signal(&wakeup_gc);
    }
    pthread_exit(NULL);
}

void* intlist_reader(void* void_list) {
    intlist_t *list = (intlist_t*)void_list;
    while(!stop) {
        intlist_pop_tail(list);
    }
    printf("closing reader..\n");
    pthread_exit(NULL);
}

/***/

int main ( int argc, char *argv[]) {

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

    intlist_t *list;
    intlist_entry_t *curr;
    int rc, i, _rc, fsize;
    int wnum, rnum;
    time_t duration;
    void* exit_status;

    wnum        = (int)strtol(argv[1], NULL, 10);
    rnum        = (int)strtol(argv[2], NULL, 10);
    max_size    = (int)strtol(argv[3], NULL, 10);
    duration    = (time_t)strtol(argv[4], NULL, 10);
    if (!wnum || !rnum || !max_size || !duration) {
        printf("ERROR: Invalid command-line arguments\n");
        usage(argv[0]);
        return rc;
    }

    /*initialize list and set thread variables*/
    intlist_init(&list);
    pthread_mutex_t *lock = intlist_get_mutex(list);
    pthread_t readers_threads[rnum];
    pthread_t writers_threads[wnum];
    pthread_t gc;
    stop = 0;

    /* initialize garbage collector thread, with another
     * conditional variable to wake it up (full list) */
    rc = pthread_cond_init(&wakeup_gc, NULL);
    if (rc) {
        printf("%s: GC condition init failed\n",
               __func__);
        intlist_destroy(list);
        goto exit;
    }
    rc = pthread_create(&gc, NULL, intlist_garbage_collector,(void*)list);
    if (rc) {
        printf("%s: GC thread creation failed\n",
               __func__);
        goto cleanup;
    }

    /*generate readers and writers threads*/
    for (i=0; i < rnum; i++) {
        rc = pthread_create(&readers_threads[i], NULL, intlist_reader, (void*)list);
        if (rc) {
            printf("%s: reader %d thread creation failed\n",
                   __func__, i);
            goto cleanup;
        }
    }
    for (i=0; i < wnum; i++) {
        rc = pthread_create(&writers_threads[i], NULL, intlist_writer, (void*)list);
        if (rc) {
            printf("%s: writer %d thread creation failed\n",
                   __func__, i);
            goto cleanup;
        }
    }

    /*sleep for a given time, and signal all threads to stop */
    sleep(duration);
    stop = 1;

    /* take the lock, and get size and elements out of list */
    rc = pthread_mutex_lock(lock); 
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        goto cleanup;
    }

    /***CS***/
    fsize = intlist_size(list);
    printf("finished running for %lu seconds\n", duration);
    printf("list size: %d\n", fsize);
    printf("list elements:\n");
    for(i=0; i<fsize; i++)
        printf("%d%c",
               intlist_pop_tail(list),
               (i+1 == fsize) ? '\n':' ');
    /***CS-END***/

    rc = pthread_mutex_unlock(lock); 
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        goto cleanup;
    }

    /* make sure all threads won't "wait" and never close */
    pthread_cond_signal(&wakeup_gc);
    for(i=0; i<rnum; i++)
        intlist_push_head(list, 0);

    /*started joining*/
    printf("admin: joining writer...\n");
    for(i=0; i<wnum; i++) {
        rc |= pthread_join(writers_threads[i], &exit_status);
        printf("admin: writer joined! exit status: %ld\n", (long)exit_status);
    }
    printf("admin: joining reader...\n");
    for(i=0; i<rnum; i++) {
        rc |= pthread_join(readers_threads[i], &exit_status);
        printf("admin: reader joined! exit status: %ld\n", (long)exit_status);
    }
    printf("admin: joining gc...\n");
    rc = pthread_join(gc, &exit_status);
    printf("admin: gc joined! exit status: %ld\n", (long)exit_status);

cleanup:
    _rc = pthread_cond_destroy(&wakeup_gc);
    if (_rc) {
            printf("%s: cond_destroy failed\n",
               __func__);
        rc = rc ? rc : _rc;
        goto exit;
    }
    printf("Destroying %d...\n", list->size);
    intlist_destroy(list);
exit: 
    pthread_exit(&rc);
}
