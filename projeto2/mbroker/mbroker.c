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
#include <signal.h>
#include "../producer-consumer/producer-consumer.h"
#include "../utils/common.h"
#include "../utils/logging.h"
#include "../fs/operations.h"

typedef enum { PUB, SUB } client_type;

typedef struct {
    char *pipe_name;
    char *box_name; 
    int pipe;
    bool is_free;
    char buffer[MAX_REQUEST_SIZE];
    client_type type;
    pthread_mutex_t lock;
    pthread_cond_t flag;
    pthread_t thread;
} Session;

uint32_t max_sessions = 0;
Session *container; // where we are going to keep the list of sessions so that it can be used in other functions
size_t box_count = 0;
pthread_cond_t wait_messages_cond;
pc_queue_t *queue;


static void mbroker_exit(int sig) {
    if (sig == SIGINT) {
        fprintf(stdout, "\nExited Mbroker safely");
        return; // Resume execution at point of interruption
    }
}

/* Auxiliary functions for sorting the boxes*/
static int myCompare(const void* a, const void* b){
  return strcmp(((Box *)a)->box_name, ((Box *)b)->box_name);
}

/*
 * Reads (and guarantees that it reads correctly) a given number of bytes
 * from a pipe to a given buffer
 */
int read_buffer(int rx, char *buf, size_t to_read) {
    ssize_t ret;
    size_t read_so_far = 0;
    while (read_so_far < to_read) {
        ret = read(rx, buf + read_so_far, to_read - read_so_far);
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            return -1;
        }
        read_so_far += (size_t) ret;
    }
    return 0;
}

void send_message_to_box(Session *session){
    int pipe;
    pipe = open(session->pipe_name, O_RDONLY);
    if (pipe == -1){
        // erro
    }

    while (true){
        char buffer[MAX_REQUEST_SIZE];
        ssize_t ret = read(pipe, buffer, MAX_REQUEST_SIZE);

        if (ret > 0){
            char msg[MESSAGE_SIZE];
            memset(msg, '\0', strlen(msg));
            memcpy(msg, buffer + sizeof(uint8_t) , MESSAGE_SIZE * sizeof(char));

            int box = tfs_open(session->box_name, TFS_O_APPEND);
            if (box == -1){
                // erro
            }
            ssize_t w = tfs_write(box, msg, strlen(msg));
            if (w == -1){
                // erro
            }
            tfs_close(box);
            pthread_cond_broadcast(&wait_messages_cond);
        }
        if (ret == 0){
            // EOF
        }
    }

}

void read_box(Session* session){
    int box = tfs_open(session->box_name, TFS_O_APPEND);
    if (box == -1){
        //erro
    }
    int sub_pipe = open(session->pipe_name, O_WRONLY);
    if (sub_pipe == -1){
        //erro
    }

    // will send to sub pipe the initial state of the content in 
    while (true) {
        char buffer[MESSAGE_SIZE];
        ssize_t ret = tfs_read(box, buffer, MESSAGE_SIZE - 1);
        while (ret == 0){
            pthread_cond_wait(&wait_messages_cond, &session->lock);
            break;
        }

        if (ret == 0){ // para continuar a prox iteracao do ciclo
            continue;
        }

        if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        buffer[ret] = 0;

        char buffer_with_op_code[sizeof(uint8_t) + MESSAGE_SIZE];
        uint8_t op_code = SEND_MESSAGE; 
        memcpy(buffer_with_op_code, &op_code, sizeof(uint8_t));
        memset(buffer_with_op_code + 1, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(buffer_with_op_code + 1, buffer, strlen(buffer) * sizeof(char));

        if (write(sub_pipe, buffer_with_op_code, strlen(buffer)) == -1){
            // erro
            close(sub_pipe);
            exit(EXIT_FAILURE);
        }

        memset(buffer, 0, strlen(buffer));
        memset(buffer_with_op_code, 0, strlen(buffer_with_op_code));
    }
}

void case_pub_request(Session* session){
    int ret;
    char client_name[MAX_CLIENT_NAME];
    char box[BOX_NAME];
    int pipe;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    pipe = open(client_name, O_WRONLY);

    for(uint32_t i=0; i< max_sessions; i++){
        if(container[i].type == PUB && (strcmp(box, container[i].box_name) == 0)){
            ret = -1;
            if(write(pipe, &ret, sizeof(int)) > 0){
                return;
            };
        }
        if(container[i].type == PUB && (strcmp(client_name, container[i].pipe_name) == 0)){
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0){
                return;
            };
        }
    }
    for (int i=0; i < BOX_NAME; i++){
        if (strcmp(box, boxes[i].box_name) == 0){
            session->type = PUB;
            session->pipe = pipe;
            session->pipe_name = client_name;
            session->box_name = box;
            boxes[i].num_publishers++;
            ret = 0;
            if (write(pipe, &ret, sizeof(int)) > 0){
                send_message_to_box(session);
                close(pipe);
            };
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0){
                return;
            };
        }
    }
    ret = -1;
    if (write(pipe, &ret, sizeof(int)) > 0){
        return;
    };
}


void case_sub_request(Session* session){
    int ret;
    char client_name[MAX_CLIENT_NAME];
    char box[BOX_NAME];
    int pipe;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    pipe = open(client_name, O_WRONLY);
    if (session->type != PUB && session->type != SUB){
        for(uint32_t i=0; i< max_sessions; i++){
            if(container[i].type == SUB && (strcmp(client_name, container[i].pipe_name) == 0)){
                ret = -1;
                if(write(pipe, &ret, sizeof(int)) > 0){
                    return;
                };
            }
        }
        for (int i=0; i < BOX_NAME; i++){
            if (strcmp(box, boxes[i].box_name) == 0){
                session->type = SUB;
                session->pipe = pipe;
                session->pipe_name = client_name;
                session->box_name = box;
                boxes[i].num_subscribers++;
                
                ret = 0;
                if(write(pipe, &ret, sizeof(int)) > 0){
                    read_box(session);
                    close(pipe);
                    return;
                };
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0){
                return;
            };
        }
    }
        ret = -1;
        if (write(pipe, &ret, sizeof(int)) > 0){
            return;
        };
    }
    ret = -1;
    if (write(pipe, &ret, sizeof(int)) > 0){
        return;
    };
}

void case_create_box(Session* session){
    int32_t ret = 0;
    int client_pipe;
    char error_message[MESSAGE_SIZE];
    char client_name[MAX_CLIENT_NAME];
    char box_name[BOX_NAME];
    uint8_t op_code = CREATE_BOX_ANSWER;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box_name, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    client_pipe = open(client_name, O_WRONLY);
    for (int i=0; i < BOX_NAME; i++){
        // lets check if there is any box with the same name 
        if (strcmp(box_name, boxes[i].box_name) == 0){
            ret = -1; 
            strcpy(error_message, "There is already a box with this name.\n");

            char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
            memcpy(response, &op_code, sizeof(uint8_t));
            memcpy(response + 1, &ret, sizeof(int32_t));
            memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
            memcpy(response + 2, error_message, strlen(error_message) * sizeof(char));
            
            if (write(client_pipe, response, strlen(response)) > 0){
                close(client_pipe);
            }
            close(client_pipe);
            return;
        }
    }
    int box = tfs_open(box_name, TFS_O_CREAT);
    if (box == -1){
        ret = -1;
        strcpy(error_message, "Couldnt create a box.\n");
    }
    else {
        for (int i=0; i < BOX_NAME; i++){
            if(boxes[i].is_free){
                boxes[i].box_name = box_name;
                boxes[i].box_size = 1024;
                boxes[i].is_free = false;
                boxes[i].last = 1;
                boxes[i].num_publishers = 0;
                boxes[i].num_subscribers = 0;
                boxes[i-1].last = 0;
                box_count++;
                tfs_close(box);
                ret = 0;
                break;
            }
        }
    }

    if (ret == -1){
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message, strlen(error_message) * sizeof(char));
        
        if (write(client_pipe, response, strlen(response)) > 0){
            close(client_pipe);
        }
        close(client_pipe);
    }
    else {   
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, "\0", strlen(error_message) * sizeof(char));
        // preencer o campo do error message com \0

        if (write(client_pipe, response, strlen(response)) > 0){
            close(client_pipe);
        }
        close(client_pipe);
    }
}

void case_remove_box(Session* session){
    int32_t ret = 0;
    int client_pipe;
    char error_message[MESSAGE_SIZE];
    char client_name[MAX_CLIENT_NAME];
    char box_name[BOX_NAME];
    uint8_t op_code = REMOVE_BOX_ANSWER;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box_name, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    client_pipe = open(client_name, O_WRONLY);
    for (int i=0; i < BOX_NAME; i++){
        // lets check if we can find the box within the box list
        if (strcmp(box_name, boxes[i].box_name) != 0){
            ret = -1;
            continue;
        }
        ret = 0;
        break;
    }

    if (ret == -1){
        strcpy(error_message, "There isn't a box with the specified box name.\n");

        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message, strlen(error_message) * sizeof(char));
        
        if (write(client_pipe, response, strlen(response)) > 0){
            close(client_pipe);
        }
        return;
    }

    int box = tfs_unlink(box_name);
    if (box == -1){
        ret = -1;
        strcpy(error_message, "Couldnt delete the specified box.\n");
    }
    else {
        for (int i=0; i < max_sessions; i++){
            if(strcmp(container[i].box_name, box_name) == 0){
                close(container[i].pipe);
                unlink(container[i].pipe_name);
                container[i].type = -1;
                container[i].pipe = 0;
                container[i].pipe_name = NULL;
                container[i].box_name = NULL;
            }
        }
        for (int i=0; i < BOX_NAME; i++){
            if(strcmp(boxes[i].box_name, box_name)){
                strcpy(boxes[i].box_name, box_name);
                boxes[i].box_size = 0;
                boxes[i].is_free = true;
                boxes[i].last = 0;
                boxes[i].num_publishers = 0;
                boxes[i].num_subscribers = 0;
                boxes[i-1].last = 1;
                box_count--;
                ret = 0;
                
            }
        }
    }

    if (ret == -1){
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message, strlen(error_message) * sizeof(char));
        
        if (write(client_pipe, response, strlen(response)) > 0){
            close(client_pipe);
        }
    }
    else {   
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memcpy(response + 2, "\0", sizeof(char));
        // preencer o campo do error message com \0

        if (write(client_pipe, response, strlen(response)) > 0){
            close(client_pipe);
        }
    }
}

void case_list_box(Session* session){
    int client_pipe;
    char client_name[MAX_CLIENT_NAME];
    uint8_t op_code = LIST_BOXES_ANSWER;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    client_pipe = open(client_name, O_WRONLY);

    if(box_count == 0){
        uint8_t i = 1;
        char response[sizeof(uint8_t) + sizeof(uint8_t) + BOX_NAME * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &i, 1 * sizeof(uint8_t));
        memset(response + 2, '\0', BOX_NAME * sizeof(char));

        if (write(client_pipe, &response, sizeof(response)) > 0) {
		    close(client_pipe);
	    }
    }
    else {
        qsort(boxes, box_count, sizeof(Box), myCompare); //sort the boxes
        char response[sizeof(uint8_t) + sizeof(uint8_t) + BOX_NAME * sizeof(char) 
                + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t)];
        
        for(int i=0; i < box_count; i++){
            memcpy(response, &op_code, sizeof(uint8_t));
            memcpy(response + 1, &boxes[i].last, 1 * sizeof(uint8_t));
            memset(response + 2, '\0', BOX_NAME * sizeof(char));
            memcpy(response + 2, boxes[i].box_name, strlen(boxes[i].box_name) * sizeof(char));
            memcpy(response + 2 + BOX_NAME, &boxes[i].box_size, sizeof(uint64_t));
            memcpy(response + 2 + BOX_NAME + sizeof(uint64_t), &boxes[i].num_publishers, sizeof(uint64_t));
            memcpy(response + 2 + BOX_NAME + sizeof(uint64_t) + sizeof(uint64_t), &boxes[i].num_subscribers, sizeof(uint64_t));

            if (write(client_pipe, &response, sizeof(response)) > 0) {
		        close(client_pipe);
	        }

            //memset(response, 0, strlen(response));
        }
    }
}

void *thread_function(void *session){
    Session *actual_session = (Session*) session;
    uint8_t op_code;

    while(true){
        pthread_mutex_lock(&actual_session->lock);
        while (actual_session->is_free) {
            pthread_cond_wait(&actual_session->flag, &actual_session->lock);
        }
        void* request = pcq_dequeue(queue);
        memcpy(&actual_session->buffer, request, MAX_REQUEST_SIZE);
        actual_session->is_free = false;
        memcpy(&op_code, &actual_session->buffer, sizeof(uint8_t));

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
            default:
                break;
        } 
        actual_session->is_free = true;
        pthread_mutex_unlock(&actual_session->lock);
    }
}


int init_threads() {
    for (uint32_t i=0; i < max_sessions; i++){
        container[i].is_free = true;
       	if (pthread_mutex_init(&container[i].lock, NULL) == -1) {
			return -1;
		}

        if (pthread_cond_init(&container[i].flag, NULL) == -1){
            return -1;
        }

        if (pthread_create(&container[i].thread, NULL, thread_function, (void *) &container[i]) != 0) {
            fprintf(stderr, "[ERR]: couldn't create threads: %s\n", strerror(errno));
			return -1;
		}
    }
    return 0;
}


int main(int argc, char **argv) {    
    if(argc < 2){
        fprintf(stderr, "usage: mbroker <pipename> <max_sessions>\n");
    }

    char *pipe_name = argv[1];
    max_sessions = (uint32_t) atoi(argv[2]);
    container =(Session *) malloc(max_sessions * sizeof(Session));
    queue = malloc(sizeof(pc_queue_t));

    if (tfs_init(NULL) == -1){
        fprintf(stderr, "Não foi possivel começar o servidor\n");
        return -1;
    }    

    if (pcq_create(queue, max_sessions) == -1){
        fprintf(stderr, "Impossível fazer pedidos\n");
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

    init_threads();

    while(true){
        char buffer[MAX_REQUEST_SIZE];
        char content3[MAX_CLIENT_NAME];
        uint8_t op_code;
        Session *current_session;
        if (read(server_pipe, &op_code, sizeof(uint8_t)) == -1) {
            return -1;
        }
        if (op_code == LIST_BOXES_REQUEST){
            for(int i = 0; i < max_sessions; i++){
                if(container[i].pipe_name == NULL){
                    current_session = &container[i];
                    pthread_mutex_lock(&current_session->lock);
                    memcpy(buffer, &op_code, sizeof(char));
                    read_buffer(server_pipe, buffer + 1, MAX_CLIENT_NAME);
                    memcpy(content3, buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = content3;
                   
                    pcq_enqueue(queue, buffer);
                    pthread_cond_signal(&current_session->flag);
                    pthread_mutex_unlock(&current_session->lock);
                    break;
                }
            }
        }
        else{
            for (int i = 0; i < max_sessions; i++){
                if(container[i].pipe_name == NULL){
                    current_session = &container[i];
                    pthread_mutex_lock(&current_session->lock);
                    memcpy(buffer, &op_code, sizeof(char));
                    read_buffer(server_pipe, buffer  + 1, MAX_CLIENT_NAME + BOX_NAME);
                    memcpy(content3, buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = content3;
                    pcq_enqueue(queue, buffer);
                    pthread_cond_signal(&current_session->flag);
                    pthread_mutex_unlock(&current_session->lock);
                    break;
                }
            }
        }

        if (signal(SIGINT, mbroker_exit) == SIG_ERR){
            pcq_destroy(queue);
            free(queue);
            free(container);
            
            close(server_pipe);
            unlink(pipe_name);
            exit(EXIT_SUCCESS);
        }

    }
    close(server_pipe);
    unlink(pipe_name);
    return -1;
}




