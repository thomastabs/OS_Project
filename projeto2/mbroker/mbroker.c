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
#include <producer-consumer.h>
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
    bool is_free;
    char *box_name;
    uint8_t last;
    uint64_t box_size;
    uint64_t num_publishers;
    uint64_t num_subscribers;
} Box;

uint32_t max_sessions = 0;
Session *container; // where we are going to keep the list of sessions so that it can be used in other functions
Box boxes[BOX_NAME];
int box_count = 0;
pthread_cond_t wait_messages_cond;
pc_queue_t *queue;


void case_pub_request(Session* session){
    int ret;
    char client_name[MAX_CLIENT_NAME];
    char box[BOX_NAME];
    int pipe;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    pipe = open(client_name, O_WRONLY);
    for (int i=0; i < max_sessions; i++){
        if(container[i].type == PUB){
            ret = -1;
            if(write(pipe, &ret, sizeof(int)) == 0){
                close(pipe);
                return;
            }
        }
    }
    for (int i=0; i < BOX_NAME; i++){
        if (strcmp(box, boxes[i].box_name) == 0){
            session->type = PUB;
            session->pipe = pipe;
            session->pipe_name = client_name;
            ret = 0;
            if (write(pipe, &ret, sizeof(int)) == 0){
                close(pipe);
                return;
            }
        }
    }
    ret = -1;
    if(write(pipe, &ret, sizeof(int)) == 0){
        close(pipe);
        return;
    }
}

void case_sub_request(Session* session){
    int ret;
    char client_name[MAX_CLIENT_NAME];
    char box[BOX_NAME];
    int pipe;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 1 + MAX_CLIENT_NAME, BOX_NAME);
    pipe = open(client_name, O_WRONLY);
    if (session->type != PUB || session->type != SUB){
        for (int i=0; i < BOX_NAME; i++){
            if (strcmp(box, boxes[i].box_name) == 0){
                session->type = SUB;
                session->pipe = pipe;
                session->pipe_name = client_name;
                ret = 0;
                if(write(pipe, &ret, sizeof(int)) == 0){
                    close(pipe);
                    return;
                }
            }
        }
    }
    ret = -1;
    if(write(pipe, &ret, sizeof(int)) == 0){
        close(pipe);
        return;
    }
    return;
}

void case_create_box(Session* session){
    int ret = 0;
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
            
            write(client_pipe, response, strlen(response));
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
        
        write(client_pipe, response, strlen(response));
        close(client_pipe);
    }
    else {   
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, "\0", strlen(error_message) * sizeof(char));
        // preencer o campo do error message com \0

        write(client_pipe, response, strlen(response));
        close(client_pipe);
    }
}

void case_remove_box(Session* session){
    int ret = 0;
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
            strcpy(error_message, "There isn't a box with the specified box name.\n");

            char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
            memcpy(response, &op_code, sizeof(uint8_t));
            memset(response + 1, &ret, sizeof(int32_t));
            memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
            memcpy(response + 2, error_message, strlen(error_message) * sizeof(char));
            
            write(client_pipe, response, strlen(response));
            close(client_pipe);
            return;
        }
    }

    int box = tfs_unlink(box_name);
    if (box == -1){
        ret = -1;
        strcpy(error_message, "Couldnt delete the specified box.\n");
    }
    else {
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
        
        write(client_pipe, response, strlen(response));
        close(client_pipe)
    }
    else {   
        char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, "\0", strlen(error_message) * sizeof(char));
        // preencer o campo do error message com \0

        write(client_pipe, response, strlen(response));
        close(client_pipe);
    }
}

void case_list_box(Session* session){
    int ret;
    int pipe;
    char client_name[MAX_CLIENT_NAME];
    uint8_t op_code = LIST_BOXES_ANSWER;
    memcpy(client_name, session->buffer + 1, MAX_CLIENT_NAME);
    pipe = open(client_name, O_WRONLY);

    if(box_count = 0){
        char response[sizeof(uint8_t) + sizeof(uint8_t) + BOX_NAME * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, 1, 1 * sizeof(uint8_t));
        memset(response + 2, '\0', BOX_NAME * sizeof(char));

        if (write(pipe, &response, sizeof(response)) == -1) {
		    return -1;
	    }
    }
    else {
        qsort(boxes, box_count, sizeof(Box), myCompare); //sort the boxes
        char response[sizeof(uint8_t) + sizeof(uint8_t) + BOX_NAME * sizeof(char) 
                + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t)];
        
        for(int i=0; i < box_count; i++){
            memcpy(response, &op_code, sizeof(uint8_t));
            memcpy(response + 1, boxes[i].last, 1 * sizeof(uint8_t));
            memset(response + 2, '\0', BOX_NAME * sizeof(char));
            memcpy(response + 2, boxes[i].box_name, strlen(boxes[i].box_name) * sizeof(char));
            memcpy(response + 2 + BOX_NAME, boxes[i].box_size, sizeof(uint64_t));
            memcpy(response + 2 + BOX_NAME + sizeof(uint64_t), boxes[i].num_publishers, sizeof(uint64_t));
            memcpy(response + 2 + BOX_NAME + sizeof(uint64_t) + sizeof(uint64_t), boxes[i].num_subscribers, sizeof(uint64_t));

            if (write(pipe, &response, sizeof(response)) == -1) {
		        return -1;
	        }

            //memset(response, 0, strlen(response));
        }
    }
}



int init_threads(Session *sessions) {
    for (uint32_t i=0; i < max_sessions; i++){
        sessions[i].is_free = true;
       	if (pthread_mutex_init(&sessions[i].lock, NULL) == -1) {
			return -1;
		}

        if (pthread_cond_init(&sessions[i].flag, NULL) == -1){
            return -1;
        }

        if (pthread_create(&sessions[i].thread, NULL, thread_function, (void *) sessions+i) != 0) {
            fprintf(stderr, "[ERR]: couldn't create threads: %s\n", strerror(errno));
			return -1;
		}
    }
    return 0;
}

void *thread_function(void *session){
    Session *actual_session = (Session*) session;
    char op_code;

    while(true){
        pthread_mutex_lock(&actual_session->lock);
        while (actual_session->is_free) {
            pthread_cond_wait(&actual_session->flag, &actual_session->lock);
        }
        actual_session->is_free = false;
        memcpy(op_code, &actual_session->buffer, sizeof(char));

        // mudar isto para tbm ter os ecrever e ler mensagem
        // + fazer as respostas
        switch (op_code) {
        case PUB_REQUEST:
            case_pub_request(actual_session);
        case SUB_REQUEST:
            case_sub_request(actual_session);
        case CREATE_BOX_REQUEST:
            case_create_box(actual_session);
        case REMOVE_BOX_REQUEST:
            case_remove_box(actual_session);
        case LIST_BOXES_REQUEST:
            case_list_box(actual_session);
        }
        actual_session->is_free = true;
        pthread_mutex_unlock(&actual_session->lock);
    }
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

    if (pcq_create(&queue, max_sessions) == -1){
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

    init_threads(container);

    while(true){
        char content[MESSAGE_SIZE];
        char content2[BOX_NAME];
        char content3[MAX_CLIENT_NAME];
        uint8_t op_code;
        Session *current_session;
        if (read(server_pipe, &op_code, sizeof(uint8_t)) == -1) {
            return -1;
        }

        if (op_code == SEND_MESSAGE || op_code == RECEIVE_MESSAGE){
            for (int i = 0; i < max_sessions; i++){
                if (container[i].type == PUB){
                    pthread_mutex_lock(&current_session->lock);
                    current_session = &container[i];
                    memcpy(current_session->buffer, &op_code, sizeof(char));
                    read_buffer(server_pipe, current_session->buffer + 1, MAX_CLIENT_NAME);
                    memcpy(container, current_session->buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = content;
                    break;
                }
            }
            read(server_pipe, &content, MESSAGE_SIZE * sizeof(char));
            read(server_pipe, &content3, MAX_CLIENT_NAME * sizeof(char));
            //...

        }

        if (op_code = LIST_BOXES_REQUEST){
            for(int i = 0; i < max_sessions; i++){
                if(container[i].is_free){
                    pthread_mutex_lock(&current_session->lock);
                    current_session = &container[i];
                    memcpy(current_session->buffer, &op_code, sizeof(char));
                    read_buffer(server_pipe, current_session->buffer + 1, MAX_CLIENT_NAME);
                    memcpy(container, current_session->buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = content;
                    break;
                }
            }
            pcq_enqueue(&queue, current_session->buffer);

            pthread_cond_signal(&current_session->flag);

            pthread_mutex_unlock(&current_session->lock);

        }
        else{
            for (int i = 0; i < max_sessions; i++){
                if(container[i].is_free){
                    pthread_mutex_lock(&current_session->lock);
                    current_session = &container[i];
                    read_buffer(server_pipe, current_session->buffer  + 1, MAX_CLIENT_NAME + BOX_NAME);
                    memcpy(container, current_session->buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = content;
                    break;
                }
            }
            pcq_enqueue(&queue, current_session->buffer);

            pthread_cond_signal(&current_session->flag);

            pthread_mutex_unlock(&current_session->lock);
        }
        (read(server_pipe + 1, &content, ))

        for (int i = 0; i < max_sessions; i++){
            if(container[i].is_free){
                current_session = &container[i];
            }
        }

        /*
        1- ler conteúdos;
        2- associar esses conteúdos a uma sessão que esteja livre;
        3- sinalizar a sessão que vai ficar ocupada (acontece sempre, reasons innit);
        4- adiciona à queue;
        */



        



    }

    close(server_pipe);
    tfs_unlink(pipe_name);
    return -1;
}

/* Auxiliary functions for sorting the boxes*/
static int myCompare(const void* a, const void* b)
{
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