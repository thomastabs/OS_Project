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

#define MAX_BUFFER (1024)

int pcq_create(pc_queue_t *queue, size_t capacity){
    queue->pcq_buffer = malloc(MAX_BUFFER);
    queue->pcq_capacity = capacity;
    queue->pcq_current_size = 0;
    queue->pcq_head = 0;
    queue->pcq_tail = 0;

    pthread_mutex_init(&queue->pcq_current_size_lock, NULL);
    pthread_mutex_init(&queue->pcq_head_lock, NULL);
    pthread_mutex_init(&queue->pcq_tail_lock, NULL);
    pthread_mutex_init(&queue->pcq_pusher_condvar_lock, NULL);
    pthread_mutex_init(&queue->pcq_popper_condvar_lock, NULL);
    pthread_cond_init(&queue->pcq_pusher_condvar, NULL);
    pthread_cond_init(&queue->pcq_popper_condvar, NULL);

    return 0;
}

int pcq_destroy(pc_queue_t *queue){
    free(queue->pcq_buffer);

    pthread_mutex_destroy(&queue->pcq_current_size_lock);
    pthread_mutex_destroy(&queue->pcq_head_lock);
    pthread_mutex_destroy(&queue->pcq_tail_lock);
    pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
    pthread_mutex_destroy(&queue->pcq_popper_condvar_lock );
    pthread_cond_destroy(&queue->pcq_pusher_condvar);
    pthread_cond_destroy(&queue->pcq_popper_condvar);

    return 0;
}

int pcq_enqueue(pc_queue_t *queue, void *elem){}