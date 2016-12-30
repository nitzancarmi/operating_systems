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
	pthread_mutex_t lock;
	pthread_mutexattr_t lock_attr;

    list = (intlist_t *)malloc(sizeof(intlist_t));
    if (!list) {
        printf("%s: Could not allocate memory for list",
               __func__);
        return;
    }
    memset(list, 0, sizeof(intlist_t));

	rc = pthread_mutexattr_init(&lock_attr);
    if (rc) {
        printf("%s: mutex attributes init failed\n",
               __func__);
        free(list);
        return;
    }

    rc = pthread_mutexattr_settype(&lock_attr,PTHREAD_MUTEX_RECURSIVE);
    if (rc) {
        printf("%s: mutex attributes set type failed\n",
               __func__);
        free(list);
        return;
    }

    rc = pthread_mutex_init(&list->lock, &lock_attr);
    if (rc) {
        printf("%s: mutex init failed\n",
               __func__);
        free(list);
        return;
    }

    rc = pthread_cond_init(&list->nonempty, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_mutex_destroy(&list->lock);
        free(list);
        return;
    }

    rc = pthread_cond_init(&list->nonfull, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_cond_destroy(&list->nonempty);
        pthread_mutex_destroy(&list->lock);
        free(list);
        return;
    }

    printf("nitzanc - unlocked\n");
    pthread_mutex_lock(&list->lock);
    printf("nitzanc - locked\n");
    pthread_mutex_unlock(&list->lock);
    printf("nitzanc - unlocked\n");

}

void intlist_destroy(intlist_t *list) {
    int rc;
    intlist_entry_t *curr, *next;

    rc = pthread_cond_destroy(&list->nonempty);
    if (rc) {
        printf("%s: condition destroy failed\n",
               __func__);
    }

    rc = pthread_cond_destroy(&list->nonfull);
    if (rc) {
        printf("%s: condition destroy failed\n",
               __func__);
    }

    rc = pthread_mutex_destroy(&list->lock);
    if (rc) {
        printf("%s: mutex destroy failed\n",
               __func__);
    }

    for (curr = list->first; curr != NULL; curr = next) {
        next = curr->next;
        intlist_entry_destroy(curr);
    }

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

void intlist_push_head(intlist_t *list, int value) {
    int rc;
    intlist_entry_t *entry = intlist_entry_create(value);
    if(!entry) {
        printf("%s: failed to create entry\n",
               __func__);
        return;
    }

    printf("outside 1\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        intlist_entry_destroy(entry);
        return;
    }
    printf("inside 1\n");
 
    //while list is full, no items can be pushed to list
    while(list->size == list->capacity) {
        rc = pthread_cond_wait(&list->nonfull, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return;
        }
    }
    /**CS**/
    list->size += 1;
    if (!list->first) { //i.e first element to be inserted
        list->first = entry;
        list->last = entry;
        pthread_cond_signal(&list->nonempty);
    } else { 
        entry->next = list->first;
        list->first->prev = entry;
        list->first = entry;
    }
    if (list->size == list->capacity)
        pthread_cond_signal(&wakeup_gc);
    /**CS-END**/

    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
    }
}

int intlist_pop_tail(intlist_t *list) {
    int rc, ret;
    intlist_entry_t *last;

    printf("outside 2\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }
    printf("inside 2\n");

    //while list is empty, no items can be popped from list
    while(!list->size) {
        rc = pthread_cond_wait(&list->nonempty, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return rc;
        }
    }

    /**CS**/
    last = list->last;
    list->last = list->last->prev;
    list->last->next = NULL;
    if ((list->size--) == list->capacity)
        pthread_cond_signal(&list->nonfull);
    /**CS-END**/

    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

    ret = last->data;
    intlist_entry_destroy(last);
    return ret;
}

void intlist_remove_last_k(intlist_t *list, int k) {
    int rc;
    intlist_t *deleted = NULL;
    intlist_entry_t *cutoff;

    /*create a new (local) list
     *elements from shared list should me moved into it
     *and them destroyed locally */
    intlist_init(deleted);

    printf("outside 3\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return;
    }
    printf("inside 3\n");

    /**CS**/
    deleted->size = MIN(k,list->size);
    if (k < list->size) {
        deleted->last = list->last;
        for(cutoff = list->last; (k--)>0; cutoff = cutoff->prev);
        list->last = cutoff;
        deleted->first = cutoff->next;
        list->last->next = NULL;
        deleted->first->prev = NULL;
        list->size -= k;
    } else {
        deleted->first = list->first;
        list->first = NULL;
        deleted->last = list->last;
        list->last = NULL;
        list->size = 0;
    }
    /**CS-END**/

    rc = pthread_mutex_unlock(&list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return;
    }

    intlist_destroy(deleted);
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

    printf("outside 4\n");
    rc = pthread_mutex_lock(&list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
            pthread_exit(&rc);
    }
    printf("inside 4\n");

    while(1) {
        rc = pthread_cond_wait(&wakeup_gc, &list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            pthread_exit(&rc);
        }
        num_to_delete = (list->size)/2 + (list->size)%2;
        intlist_remove_last_k(list, num_to_delete);
        printf("GC – %d items removed from the list", num_to_delete);
    }

    pthread_exit(&rc);
}

void* intlist_writer(void* void_list) {
    srand((unsigned int)time(NULL));
    intlist_t *list = (intlist_t*)void_list;
    while(1) {
        pthread_testcancel();
        intlist_push_head(list, rand());
    }

    //shouldn't get here
    pthread_exit(NULL);
}

void* intlist_reader(void* void_list) {
    intlist_t *list = (intlist_t*)void_list;
    while(1) {
        pthread_testcancel();
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
    int rc, i;
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
    rnum = 0; wnum = 0; //nitzanc

    /*initialize list and set thread arrays*/
    intlist_init(&list);
    list.capacity = max_size;
    pthread_t readers_threads[rnum];
    pthread_t writers_threads[wnum];

    printf("nitzanc - unlocked\n");
    pthread_mutex_lock(&list.lock);
    printf("nitzanc - locked\n");
    pthread_mutex_unlock(&list.lock);
    printf("nitzanc - unlocked\n");
    printf("finishing...\n"); return rc; //nitzanc

    /*initialize garbage collector thread*/
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

    /*clean all running threads*/
    printf("outside 0\n");
    rc = pthread_mutex_lock(&list.lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        goto cleanup;
    }
    printf("inside 0\n");

    //now main holds all resources, safe to cancel threads
    rc = pthread_cancel(gc);
    for (i=0; i<rnum; rc |= pthread_cancel(readers_threads[i++]));
    for (i=0; i<wnum; rc |= pthread_cancel(writers_threads[i++]));
    if (rc) {
        printf("%s: calling threads cancellation failed\n",
               __func__);
        goto cleanup;
    }


    //wait for all threads to finish, and join them
    rc = pthread_join(gc, &exit_status);
    for(i=0; i<rnum; rc |= pthread_join(readers_threads[i++], &exit_status));
    for(i=0; i<wnum; rc |= pthread_join(writers_threads[i++], &exit_status));
    if (rc) {
        printf("%s: pthread_join() failed with status [%ld]: [%d] %s\n",
                __func__,(long)exit_status, rc, strerror(rc));
        goto cleanup;
    }

    //now only one thread is active - can safely nulock mutex
    rc = pthread_mutex_unlock(&list.lock);
    if (rc) {
            printf("%s: mutex unlock failed\n",
               __func__);
        goto cleanup;
    }
    //print list size & elements
    printf("list size: %d\n", list.size);
    printf("list elements:");
    for(curr = list.first; curr; curr = curr->next)
        printf(" %d", curr->data);

cleanup:
    intlist_destroy(&list);
    pthread_cond_destroy(&wakeup_gc);
exit: 
    pthread_exit(&rc);
}
