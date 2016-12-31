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
static pthread_t locked_thid;

typedef struct intlist_entry_t {
    int                     data;
    struct intlist_entry_t  *next;
    struct intlist_entry_t  *prev;
} intlist_entry_t;

typedef struct intlist_t {
    int                 size;
    int                 capacity;
    intlist_entry_t     *first;
    intlist_entry_t     *last;
    pthread_mutex_t     lock;
	pthread_mutexattr_t lock_attr;
    pthread_cond_t      nonempty;
    pthread_cond_t      nonfull;
} intlist_t;

/*Usage function*/
void usage(char* filename);

/*initialize the list. You may assume the argument is not a previously initialized or destroyed list*/
void intlist_init(intlist_t *list);

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

void intlist_init(intlist_t *list) {
    int rc;

    list = (intlist_t *)malloc(sizeof(intlist_t));
    if (!list) {
        printf("%s: Could not allocate memory for list",
               __func__);
        return;
    }
    list->size = 0;
}

void intlist_destroy(intlist_t *list) {
    int rc;
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
    entry->data = n;
    return entry;
}

void intlist_entry_destroy(intlist_entry_t *entry) {
    free(entry);
}

void intlist_multiple_entries_destroy(intlist_entry_t *head) {
    intlist_entry_t *curr, *next;

    for (curr = head; curr != NULL; curr = next) {
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

    printf("writer: taking lock...\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        intlist_entry_destroy(entry);
        return;
    }
    printf("writer: taken!\n");
 
    //while list is full, no items can be pushed to list
    while(list->size == list->capacity) {
        printf("writer: entering wait...\n");
        rc = pthread_cond_wait(&list->nonfull, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return;
        }
    }
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
    if (list->size == list->capacity) {
        pthread_cond_signal(&wakeup_gc);
    }
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

    printf("reader: taking lock...\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }
    printf("reader: taken!\n");
    //while list is empty, no items can be popped from list
    while(list->size == 0) {
        printf("reader: entering wait...\n");
        rc = pthread_cond_wait(&list->nonempty, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return rc;
        }
        printf("reader: wake up from wait\n"); 
    }

    /**CS**/
    last = list->last;
    list->last = list->last->prev;
    if (list->last)
        list->last->next = NULL;
    list->size--;
    /**CS-END**/

    printf("reader: releasing lock\n");
    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

    pthread_cond_signal(&list->nonfull);
    ret = last->data;
    intlist_entry_destroy(last);
    return ret;
}

void intlist_remove_last_k(intlist_t *list, int k) {
    int rc, i, eff_size;
    intlist_entry_t *cutoff;

    /*create a new (local) list
     *elements from shared list should me moved into it
     *and them destroyed locally */

    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return;
    }

    /**CS**/
    i = k;
    eff_size = MIN(k, list->size);
    for (i = 0; i < eff_size; i++) {
        list->last = list->last->prev;
    }
    list->size -= eff_size;
    /**CS-END**/

    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return;
    }

    for (i = 0; i<eff_size; i++)
        pthread_cond_signal(&list->nonfull);
}

int intlist_size(intlist_t *list) {
    return list->size;
}

pthread_mutex_t* intlist_get_mutex(intlist_t *list) {
    return &list->lock;
}

void* intlist_garbage_collector(void* void_list) {
    int rc = 0;
    int num_to_delete;
    intlist_t *list = (intlist_t *)void_list;

    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
            pthread_exit(&rc);
    }

    int i; //nitzanc
    while(1) {
        while (list->size != list->capacity) {
            printf("gc: entering wait...\n");
            rc = pthread_cond_wait(&wakeup_gc, &list->lock);
            if (rc) {
                printf("%s: mutex conditional wait failed\n",
                       __func__);
                pthread_exit(&rc);
            }
            printf("gc: wakeup from lock\n");
        }
        printf("#################GARBAGE COLLECTOR %i###################\n", i++);
        num_to_delete = (list->size)/2 + (list->size)%2;
        intlist_remove_last_k(list, num_to_delete);
        printf("GC – %d items removed from the list\n", num_to_delete);
    }

    pthread_exit(&rc);
}

void* intlist_writer(void* void_list) {
    srand((unsigned int)time(NULL));
    intlist_t *list = (intlist_t*)void_list;
    while(1) {
        intlist_push_head(list, rand());
    }
    pthread_exit(NULL);
}

void* intlist_reader(void* void_list) {
    intlist_t *list = (intlist_t*)void_list;
    while(1) {
        intlist_pop_tail(list);
    }

    //shouldn't get here
    pthread_exit(NULL);
}

/***/

int main ( int argc, char *argv[]) {

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

    intlist_t list;
    intlist_entry_t *curr;
    int rc, i, _rc;
    int wnum, rnum;
    int max_size;
    time_t duration;
    pthread_t gc;
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

    /*initialize list and set thread arrays*/
    intlist_init(&list);
    list.size = 0;
    list.capacity = max_size;
    pthread_t readers_threads[rnum];
    pthread_t writers_threads[wnum];

    /*set mutex attributes (as recursive)*/
	rc = pthread_mutexattr_init(&list.lock_attr);
    if (rc) {
        printf("%s: mutex attributes init failed\n",
               __func__);
        goto cleanup;
    }
    rc = pthread_mutexattr_settype(&list.lock_attr,PTHREAD_MUTEX_RECURSIVE);
    if (rc) {
        printf("%s: mutex attributes set type failed\n",
               __func__);
        goto cleanup;
    }

    /*set the mutex with configured attributes*/
    rc = pthread_mutex_init(&list.lock, &list.lock_attr);
    if (rc) {
        printf("%s: mutex init failed\n",
               __func__);
    }

    /*set non-empty list conditional variable*/
    rc = pthread_cond_init(&list.nonempty, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_mutex_destroy(&list.lock);
    }

    /*set non-full list conditional variable*/
    rc = pthread_cond_init(&list.nonfull, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_cond_destroy(&list.nonempty);
        pthread_mutex_destroy(&list.lock);
    }

    /* initialize garbage collector thread, with another
     * conditional variable to wake it up (in case of 
     * a full list */
    rc = pthread_cond_init(&wakeup_gc, NULL);
    if (rc) {
        printf("%s: GC condition init failed\n",
               __func__);
        intlist_destroy(&list);
        return rc;
    }
    rc = pthread_create(&gc, NULL, intlist_garbage_collector,(void*)&list);
    if (rc) {
        printf("%s: GC thread creation failed\n",
               __func__);
        goto cleanup;
    }

    /*generate readers and writers threads*/
    for (i=0; i < rnum; i++) {
        rc = pthread_create(&readers_threads[i], NULL, intlist_reader, (void*)&list);
        if (rc) {
            printf("%s: reader %d thread creation failed\n",
                   __func__, i);
            goto cleanup;
        }
    }
    for (i=0; i < wnum; i++) {
        rc = pthread_create(&writers_threads[i], NULL, intlist_writer, (void*)&list);
        if (rc) {
            printf("%s: writer %d thread creation failed\n",
                   __func__, i);
            goto cleanup;
        }
    }

    /*sleep for a given time */
    sleep(duration);

    /*get to lock in order to stop all other running threads*/

    /***/
    //nitzanc
    printf("admin: try to take lock...\n");
    rc = pthread_mutex_lock(&list.lock);
    printf("admin: taken!\n");

    printf("=====Started Cleaning!=====\n");
    intlist_remove_last_k(&list, list.size);
    list.capacity = 0;

    printf("admin: sending all cancel requests\n");
    rc = pthread_cancel(gc);
    for (i=0; i<rnum; i++)
        rc |= pthread_cancel(readers_threads[i]);
    for (i=0; i<wnum; i++)
        rc |= pthread_cancel(writers_threads[i]);

    printf("admin: releasing lock\n");
    rc = pthread_mutex_unlock(&list.lock);
    printf("admin: released\n");

    /***/

    printf("admin: joining reader...\n");
    for(i=0; i<rnum; i++) {
        rc |= pthread_join(readers_threads[i], &exit_status);
        printf("admin: reader joined! exit status: %ld\n", (long)exit_status);
    }
    printf("admin: joining writer...\n");
    for(i=0; i<wnum; i++) {
        rc |= pthread_join(writers_threads[i], &exit_status);
        printf("admin: writer joined! exit status: %ld\n", (long)exit_status);
    }

    printf("admin: signaling gc...\n");
    pthread_cond_signal(&wakeup_gc); 
    printf("admin: joining gc...\n");
    rc = pthread_join(gc, &exit_status);
    printf("admin: gc joined! exit status: %ld\n", (long)exit_status);

    printf("finishing...\n"); return 0; //nitzanc

    /* print list size & elements, as measured in exact time,
     * before closing threads might corrupt results */
    printf("list size: %d\n", list.size);
    printf("list elements:");
    curr = list.first;
    for(i=0; i<list.size; i++) {
        printf("%d%c", curr->data, (i==list.size-1) ? '\n' : ' ');
        curr = curr->next;
    }

cleanup:
    //try to clean mutex resources
	_rc = pthread_mutexattr_destroy(&list.lock_attr);
    if (_rc) {
            printf("%s: mutexattr_destroy failed\n",
               __func__);
        rc = rc ? rc : _rc;
        goto exit;
    }
    _rc  = pthread_cond_destroy(&list.nonempty);
    _rc |= pthread_cond_destroy(&list.nonfull);
    _rc |= pthread_cond_destroy(&wakeup_gc);
    if (_rc) {
            printf("%s: cond_destroy failed\n",
               __func__);
        rc = rc ? rc : _rc;
        goto exit;
    }
    _rc = pthread_mutex_destroy(&list.lock);
    if (_rc) {
            printf("%s: mutex_destroy failed\n",
               __func__);
        rc = rc ? rc : _rc;
        goto exit;
    }

    //destroy list itself
    intlist_destroy(&list);
exit: 
    pthread_exit(&rc);
}
