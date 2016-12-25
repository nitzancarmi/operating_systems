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

struct intlist_entry {
    int data;
    intlist_entry *next;
    intlist_entry *prev;
};
typedef struct intlist_entry intlist_entry;

struct intlist_attr {
    size_t size;
    intlist_entry *first;
    intlist_entry *last;
    pthread_mutex_t *lock;
};
typedef struct intlist_attr intlist_attr;

//global conditional variable for garbage collector;
pthread_cond_t gc = NULL;

/*Usage function*/
void usage(char* filename);

/*initialize the list. You may assume the argument is not a previously initialized or destroyed list*/
int intlist_init(intlist_attr *list);

/*frees all memory used by the list, including any of its items*/
int intlist_destroy(intlist_attr *list);

/*returns a new allocated list entry, or NULL on failure*/
intlist_entry intlist_entry_create(int n);

/*destroys a single list entry*/
void intlist_entry_destroy(intlist_entry *entry);

/*receives an int, and adds it to the head of the list*/
int intlist_push_head(int n, intlist_attr *list);

/*removes an item from the tail, and returns its value*/
int intlist_pop_tail(intlist_attr *list);

/*removes k items from the tail, without returning any value*/
void intlist_remove_last_k(intlist_attr *list, size_t k);

/*returns the number of items currently in the list*/
size_t intlist_size(intlist_attr *list);

/*returns the mutex used by this list*/
pthread_mutex_t intlist_get_mutex(intlist_attr *list);

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

int intlist_init(intlist_attr *list) {
    int rc;
    *list = (intlist_attr)malloc(sizeof(intlist_attr));
    if (!*list) {
        printf("%s: Could not allocate memory for list",
               __func__);
        return -1;
    }
    memset(list, 0, sizeof(intlist_attr));

    rc = pthread_mutex_init(list->lock);
    if (rc) {
        printf("%s: mutex init failed\n",
               __func__);
        free(*list);
        return rc;
    }

    return 0;
}

int intlist_destroy(intlist_attr *list) {
    int rc;
    rc = pthread_mutex_destroy(list->lock);
    if (rc) {
        printf("%s: mutex destroy failed\n",
               __func__);
        return rc;
    }
    for (curr = list->last; list->first;) {
        intlist_entry_destroy(curr);
        
    }
    return rc;
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

int intlist_push_head(int n, intlist_attr *list) {
    int rc;
    intlist_entry entry = intlist_entry_create(n);
    if(!entry) {
        printf("%s: failed to create entry\n",
               __func__);
        return -1;
    }

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
        printf("%s: mutex lock failed\n",
               __func__);
        intlist_entry_destroy(entry); 
        return rc;
    }
 
    /**CS**/
    entry->next = list->first;
    if(list->first)
        list->first->prev = entry;
    else //first element to be inserted
        list->last = entry;
    list->first = entry;
    list->size += 1;
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        intlist_entry_destroy(entry); 
        return rc;
    }

    return rc;
}

int intlist_pop_tail(intlist_attr *list);
    int rc;
    intlist_entry *last;

    if(!list || !list->last) {
            printf("%s: list is empty or not exist\n",
               __func__);
        return -1;
    
    }

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }

    /**CS**/
    last = list->last;
    list->last = list->last->prev;
    list->last->next = NULL;
    list->size -= 1;
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

    intlist_entry_destroy(last);
    return rc;
}

/*removes k items from the tail, without returning any value*/
void intlist_remove_last_k(intlist_attr *list, size_t k) {
    int rc;
    intlist_attr *deleted;
    intlist_entry *cutoff;

    if (k > list->size) {
        printf("%s: num to delete [%lu] is bigger than list size [%lu]\n",
               __func__, k, list->size);
        return -1;
    }

    rc = intlist_init(deleted);
    if (rc) {
        printf("%s: Failed to initiate deleted list\n",
               __func__);
        return rc;
    }
    deleted->size = k;

    rc = pthread_mutex_lock(list->lock);
    if (rc) {
            printf("%s: mutex lock failed\n",
               __func__);
        return rc;
    }

    /**CS**/
    if (k == list->size) {
        deleted->first = list->first;
        list->first = NULL;
        deleted->last = list->last;
        list->last = NULL;
        list->size = 0;
    } else {
        deleted->last = list->last;
        for(cutoff = list->last; (k--)>0; cutoff = cutoff->prev);
        list->last = cutoff;
        deleted->first = cutoff->next;
        cutoff->next = NULL;
        deleted->first->prev = NULL; 
    /**CS-END**/

    rc = pthread_mutex_unlock(list->lock);
    if (rc) {
        printf("%s: mutex unlock failed\n",
               __func__);
        return rc;
    }

}

/*returns the number of items currently in the list*/
size_t intlist_size(intlist_attr *list);

/*returns the mutex used by this list*/
pthread_mutex_t intlist_get_mutex(intlist_attr *list);

/***/

int main ( int argc, char *argv[]) {

    //validate 4 command line arguments given
    if (argc != 5) {
        usage(argv[0]);
        return -1;
    }

    int rc;
    int wnum, rnum;
    size_t max_size;
    time_t duration;

    wnum        = (int)strtol(argv[1], NULL, 10);
    rnum        = (int)strtol(argv[2], NULL, 10);
    max_size    = (size_t)strtol(argv[3], NULL, 10);
    duration    = (time_t)strtol(argv[4], NULL, 10);
    if (!wnum || !rnum || !max_size || !duration) {
        printf("ERROR: Invalid command-line arguments\n");
        usage(argv[0]);
        rc = -1;
    }
 
    return 0;
}
