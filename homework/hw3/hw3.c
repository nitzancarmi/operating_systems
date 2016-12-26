#define _GNU_SOURCE
#include<sys/types.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<errno.h>
#include <linux/mutex.h>
#include<pthread.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

struct intlist_entry {
    int data;
    intlist_entry *next;
    intlist_entry *prev;
};
typedef struct intlist_entry intlist_entry;

struct intlist_attr {
    int size;
    int capacity;
    intlist_entry *first;
    intlist_entry *last;
    pthread_mutex_t *lock;
    pthread_cond_t *nonempty;
    pthread_cond_t *nonfull;
};
typedef struct intlist_attr intlist_attr;

/*Usage function*/
void usage(char* filename);

/*initialize the list. You may assume the argument is not a previously initialized or destroyed list*/
void intlist_init(intlist_attr *list);

/*frees all memory used by the list, including any of its items*/
void intlist_destroy(intlist_attr *list);

/*returns a new allocated list entry, or NULL on failure*/
intlist_entry intlist_entry_create(int n);

/*destroys a single list entry*/
void intlist_entry_destroy(intlist_entry *entry);

/*receives an int, and adds it to the head of the list*/
void intlist_push_head(int n, intlist_attr *list);

/*removes an item from the tail, and returns its value*/
int intlist_pop_tail(intlist_attr *list);

/*removes k items from the tail, without returning any value*/
void intlist_remove_last_k(intlist_attr *list, int k);

/*returns the number of items currently in the list*/
int intlist_size(intlist_attr *list);

/*returns the mutex used by this list*/
pthread_mutex_t* intlist_get_mutex(intlist_attr *list);

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

void intlist_init(intlist_attr *list) {
    int rc;
    *list = (intlist_attr)malloc(sizeof(intlist_attr));
    if (!*list) {
        printf("%s: Could not allocate memory for list",
               __func__);
        return;
    }
    memset(list, 0, sizeof(intlist_attr));

    rc = pthread_mutex_init(list->lock);
    if (rc) {
        printf("%s: mutex init failed\n",
               __func__);
        free(*list);
        *list = NULL;
        return;
    }

    rc = pthread_cond_init(list->nonempty, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_mutex_destroy(list->lock);
        free(*list);
        *list = NULL;
        return;
    }

    rc = pthread_cond_init(list->nonfull, NULL);
    if (rc) {
        printf("%s: condition init failed\n",
               __func__);
        pthread_cond_destroy(list->nonempty);
        pthread_mutex_destroy(list->lock);
        free(*list);
        *list = NULL;
        return;
    }
}

void intlist_destroy(intlist_attr *list) {
    int rc;
    intlist_entry *curr, *next;

    rc = pthread_cond_destroy(list->nonempty);
    if (rc) {
        printf("%s: condition destroy failed\n",
               __func__);
    }

    rc = pthread_cond_destroy(list->nonfull);
    if (rc) {
        printf("%s: condition destroy failed\n",
               __func__);
    }

    rc = pthread_mutex_destroy(list->lock);
    if (rc) {
        printf("%s: mutex destroy failed\n",
               __func__);
    }

    for (curr = list->first; curr != NULL; curr = next) {
        next = curr->next;
        intlist_entry_destroy(curr);
    }

    free(*list);
}

intlist_entry intlist_entry_create(int n) {
    int rc;
    intlist_entry entry = (intlist_entry) malloc (sizeof(intlist_entry));
    if (!entry) {
        printf("%s: Could not allocate memory for list entry",
               __func__);
        return NULL;
    }
    memset(&entry, 0, sizeof(intlist_entry));
    entry->data = n;
    return entry;
}

void intlist_entry_destroy(intlist_entry *entry) {
    free(*entry);
}

void intlist_push_head(intlist_attr *list, int value) {
    int rc;
    intlist_entry entry = intlist_entry_create(value);
    if(!entry) {
        printf("%s: failed to create entry\n",
               __func__);
        return;
    }

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        intlist_entry_destroy(entry);
        return;
    }
 
    //while list is full, no items can be pushed to list
    while(list->size == list->capacity) {
        rc = pthread_cond_wait(list->nonfull, list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return rc;
    }
    /**CS**/
    list->size += 1;
    if(!list->first) { //i.e first element to be inserted
        list->first = entry;
        list->last = entry;
        pthread_cond_signal(list->nonempty);
    } else { 
        entry->next = list->first;
        list->first->prev = entry;
        list->first = entry;
    }
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
    }
}

int intlist_pop_tail(intlist_attr *list);
    int rc, ret;
    intlist_entry *last;

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }

    //while list is empty, no items can be popped from list
    while(!list->size) {
        rc = pthread_cond_wait(list->nonempty, list->lock);
        if (rc) {
            printf("%s: mutex conditional wait failed\n",
                   __func__);
            return rc;
    }

    /**CS**/
    last = list->last;
    list->last = list->last->prev;
    list->last->next = NULL;
    if ((list->size--) == list->capacity)
        pthread_cond_signal(list->nonfull);
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

    ret = last->data;
    intlist_entry_destroy(last);
    return ret;
}

void intlist_remove_last_k(intlist_attr *list, int k) {
    int rc;
    intlist_attr *deleted;
    intlist_entry *cutoff;

    rc = intlist_init(deleted);
    if (rc) {
        printf("%s: Failed to initiate deleted list\n",
               __func__);
        return;
    }

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return;
    }

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
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return;
    }

    rc = intlist_destroy(deleted);
    if (rc) {
        printf("%s: failed to destroy cutoffed entries\n",
               __func__);
        return;
    }
}

int intlist_size(intlist_attr *list) {
    return list->size;
}

pthread_mutex_t* intlist_get_mutex(intlist_attr *list) {
    return list->lock;
}

/***/

int main ( int argc, char *argv[]) {

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

    intlint_attr *list;
    int rc;
    int wnum, rnum;
    int max_size;
    time_t duration;
    pthread_t gc;
    pthread_cond_t *gc_wakeup;

    wnum        = (int)strtol(argv[1], NULL, 10);
    rnum        = (int)strtol(argv[2], NULL, 10);
    max_size    = (int)strtol(argv[3], NULL, 10);
    duration    = (time_t)strtol(argv[4], NULL, 10);
    if (!wnum || !rnum || !max_size || !duration) {
        printf("ERROR: Invalid command-line arguments\n");
        usage(argv[0]);
        rc = -1;
    }

    /*initialize list*/
    intlist_init(list);
    list->capacity = max_size;

    /*initialize garbage collector*/
    rc = pthread_cond_init(gc_wakeup, NULL);
    if (rc) {
        printf("%s: GC condition init failed\n",
               __func__);
        return rc;
    }
    rc = pthread_create(&gc, NULL, garbage_collector, &gc_wakeup);
    if (rc) {
        printf("%s: GC thread creation failed\n",
               __func__);
        return rc;
    }

    
 
    return rc;
}
