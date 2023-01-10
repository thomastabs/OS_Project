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
#include <pthread.h>
#include "../utils/common.h"
#include "logging.h"
#include "fs/operations.h"

typedef enum { PUB, SUB } client_type;

typedef struct {
    char *pipe_name; 
    int pipe;
    bool is_free;
    char buffer[MAX_REQUEST_SIZE];
    client_type type;
    pthread_mutex_t lock;
    pthread_cond_t flag;
    pthread_t thread;
} Session;

typedef struct 
{ 
    char *box_name;
    uint8_t last;
    uint64_t box_size;
    uint64_t num_publishers;
    uint64_t num_subscribers;
} Box;

uint32_t max_sessions;
Session *container; // where we are going to keep the list of sessions so that it can be used in other functions
pthread_cond_t wait_messages_cond;


void case_pub_request(Session* session){
    char client_name[MAX_CLIENT_NAME];
    char box[BOX_NAME];
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    for (int i=0; i < max_sessions; i++){
        if(container[i].type == PUB){
            
        }
    }
    
}

int init_threads(Session *sessions, int max_sessions){
    for (int i = 0; i< max_sessions; i++){
        sessions[i].is_free = true;
       	if (pthread_mutex_init(&sessions[i].lock, NULL) == -1) {
			return -1;
		}

        if (pthread_create(&sessions[i].thread, NULL, thread_function, (void *) sessions+i) != 0) {
            fprintf(stderr, "[ERR]: couldn't create threads: %s\n", strerror(errno));
			return -1;
		}
    }
}

void *thread_function(void *session, int max_sessions){
    Session *actual_session = (Session*) session;
    char op_code;

    while(true){
        pthread_mutex_lock(&actual_session->lock);
        while (actual_session->is_free) {
            pthread_cond_wait(&actual_session->flag, &actual_session->lock);
        }
        actual_session->is_free = false;
        memcpy(op_code, &actual_session->buffer, sizeof(char));

        switch (op_code) {
        case PUB_REQUEST:
            case_pub_request(actual_session);
            break;
        case SUB_REQUEST:
            case_sub_request(actual_session);
            break;
        case CREATE_BOX_REQUEST:
            case_create_box(actual_session);
            break;
        case REMOVE_BOX_REQUEST:
            case_remove_box(actual_session);
            break;
        case LIST_BOXES_REQUEST:
            case_list_box(actual_session);
            break;
        default: break;
        }
        pthread_mutex_unlock(&actual_session->lock);
    }
}


int main(int argc, char **argv) {    
    char *pipe_name = argv[1];
    max_sessions = atoi(argv[2]);
    container =(Session *) malloc(max_sessions * sizeof(Session));

    if(argc < 2){
        fprintf(stderr, "usage: mbroker <pipename> <max_sessions>\n");
    }


    if(tfs_init(NULL) != -1){
        fprintf(stderr, "Não foi possivel começar o servidor\n");
        return -1;
    }    

    printf("[INFO]: Starting TecnicoFS server with pipe called %s with %d sessions\n", pipe_name, max_sessions);

    // remove pipe if it does exist
    if (unlink(pipe_name) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipe_name,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create pipe and check if it exists
    if (mkfifo(pipe_name, 0640) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // open server pipe
    int server_pipe = open(pipe_name, O_RDONLY);
    if (server_pipe == -1){
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    init_threads(container, max_sessions);

    while(true){

        /* Read request */
        char buffer[MAX_REQUEST_SIZE];
        char op_code;
        if (read(server_pipe, &op_code, sizeof(char)) == -1) {
            return -1;
        }

        



    }

    close(server_pipe);
    tfs_unlink(pipe_name);
    return -1;
}
