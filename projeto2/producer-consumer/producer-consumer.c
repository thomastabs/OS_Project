#include "producer-consumer.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BUFFER (1300)

int pcq_create(pc_queue_t *queue, size_t capacity) {
    // allocates space, initialize variables
    queue->pcq_buffer = malloc(MAX_BUFFER);
    queue->pcq_capacity = capacity;
    queue->pcq_current_size = 0;
    queue->pcq_head = 0;
    queue->pcq_tail = 0;

    // init mutexes and condition variables
    pthread_mutex_init(&queue->pcq_current_size_lock, NULL);
    pthread_mutex_init(&queue->pcq_head_lock, NULL);
    pthread_mutex_init(&queue->pcq_tail_lock, NULL);
    pthread_mutex_init(&queue->pcq_pusher_condvar_lock, NULL);
    pthread_mutex_init(&queue->pcq_popper_condvar_lock, NULL);
    pthread_cond_init(&queue->pcq_pusher_condvar, NULL);
    pthread_cond_init(&queue->pcq_popper_condvar, NULL);

    return 0;
}

int pcq_destroy(pc_queue_t *queue) {
    // free space given
    free(queue->pcq_buffer);

    // destroy mutexes
    pthread_mutex_destroy(&queue->pcq_current_size_lock);
    pthread_mutex_destroy(&queue->pcq_head_lock);
    pthread_mutex_destroy(&queue->pcq_tail_lock);
    pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
    pthread_mutex_destroy(&queue->pcq_popper_condvar_lock);
    pthread_cond_destroy(&queue->pcq_pusher_condvar);
    pthread_cond_destroy(&queue->pcq_popper_condvar);

    return 0;
}

int pcq_enqueue(pc_queue_t *queue, void *elem) {
    // Lock the mutexes used for "enqueuing" an element
    pthread_mutex_lock(&queue->pcq_current_size_lock);
    pthread_mutex_lock(&queue->pcq_tail_lock);
    pthread_mutex_lock(&queue->pcq_pusher_condvar_lock);

    // Wait until the queue isn't full
    while (queue->pcq_current_size >= queue->pcq_capacity) {
        pthread_cond_wait(&queue->pcq_pusher_condvar,
                          &queue->pcq_pusher_condvar_lock);
    }

    // Put the element at the back of the queue
    queue->pcq_buffer[queue->pcq_tail] = elem;

    // Increase the tail and size of the queue
    queue->pcq_tail = (queue->pcq_tail + 1) % queue->pcq_capacity;
    queue->pcq_current_size++;

    // Notify waiting threads (so that we can able to dequeue)
    pthread_cond_signal(&queue->pcq_popper_condvar);

    // Unlock locks
    pthread_mutex_unlock(&queue->pcq_pusher_condvar_lock);
    pthread_mutex_unlock(&queue->pcq_tail_lock);
    pthread_mutex_unlock(&queue->pcq_current_size_lock);

    return 0;
}

void *pcq_dequeue(pc_queue_t *queue) {
    void *result;

    // Lock the mutexes used for "dequeuing" an element
    pthread_mutex_lock(&queue->pcq_current_size_lock);
    pthread_mutex_lock(&queue->pcq_head_lock);
    pthread_mutex_lock(&queue->pcq_popper_condvar_lock);
    while (queue->pcq_current_size == 0) {
        // Wait until an element is inserted in the queue
        pthread_cond_wait(&queue->pcq_pusher_condvar,
                          &queue->pcq_popper_condvar_lock);
    }

    // Retrieve the element at the front of the queue
    result = queue->pcq_buffer[queue->pcq_head];
    queue->pcq_head = (queue->pcq_head + 1) % queue->pcq_capacity;
    queue->pcq_current_size--;

    // notify threads and unlock locks
    pthread_cond_signal(&queue->pcq_pusher_condvar);
    pthread_mutex_unlock(&queue->pcq_popper_condvar_lock);
    pthread_mutex_unlock(&queue->pcq_head_lock);
    pthread_mutex_unlock(&queue->pcq_current_size_lock);
    return result;
}
