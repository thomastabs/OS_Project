/*
File System, project by:
Fábio Sobrinho nº103473
Tomá Taborda nº103641
Grupo 52
(Ler README)
*/

#include "../fs/operations.h"
#include "../producer-consumer/producer-consumer.h"
#include "../utils/common.h"
#include "../utils/logging.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { PUB, SUB } client_type;

typedef struct {
    char *pipe_name;               // pipe name associated with the client
    char *box_name;                // box name associated with it
    int pipe;                      // file descriptor of the pipe
    bool is_free;                  // boolean to help check if its free
    char buffer[MAX_REQUEST_SIZE]; // the buffer associated with the request
    client_type type;              // the client type
    pthread_mutex_t lock;          // the mutex lock behind the session
    pthread_t thread;              // and then the thread itself
} Session;

uint32_t max_sessions = 0; // corresponding to the number of threads
Session *container;        // where we are going to keep the list of
                           // sessions so that it can be used in other functions
size_t box_count = 0;      // number of boxes within our custom container
pthread_cond_t wait_messages_cond; // condition variable for messages
pc_queue_t *queue; // the pointer responsible for the queue in the
                   // producer-consumer files

// this is the signal handler for safely exiting Mbroker using Ctrl C
static void mbroker_exit(int sig) {
    if (sig == SIGINT) {
        return; // Resume execution at point of interruption
    }
}

/* Auxiliary function for sorting the boxes*/
static int myCompare(const void *a, const void *b) {
    return strcmp(((Box *)a)->box_name, ((Box *)b)->box_name);
}

/*
 * Reads (and guarantees that it reads correctly) a given number of bytes
 * from a pipe to a given buffer
 */
char *read_buffer(int rx, char *buf, size_t to_read) {
    ssize_t ret;
    size_t read_so_far = 0;
    while (read_so_far < to_read) {
        ret = read(rx, buf + read_so_far, to_read - read_so_far);
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        read_so_far += (size_t)ret;
    }
    return (buf);
}

// this function is responsible for opening a pipe, and sending the
// messages within to the respective box_name
void send_message_to_box(Session *session) {
    int pipe;
    pipe = open(session->pipe_name, O_RDONLY);
    if (pipe == -1) {
        fprintf(stdout, "Session pipe doesn't exist.\n");
        exit(EXIT_FAILURE);
    }

    while (true) {
        char buffer[MAX_REQUEST_SIZE];
        ssize_t ret = read(pipe, buffer, MAX_REQUEST_SIZE);
        // this saves the message within the pipe into a buffer
        // which will write inside the respective box

        if (ret > 0) {
            char msg[MESSAGE_SIZE];
            memset(msg, '\0', strlen(msg));
            memcpy(msg, buffer + sizeof(uint8_t), MESSAGE_SIZE * sizeof(char));

            int box = tfs_open(session->box_name, TFS_O_APPEND);
            if (box == -1) {
                fprintf(stdout, "Couldn't create a box.\n");
                close(pipe);
                exit(EXIT_FAILURE);
            }
            ssize_t w = tfs_write(box, msg, strlen(msg));
            if (w == -1) {
                fprintf(stdout, "Couldn't write in the box.\n");
                close(pipe);
                exit(EXIT_FAILURE);
            }
            tfs_close(box);
            pthread_cond_broadcast(&wait_messages_cond);
        }
        if (ret == 0) {
            // EOF - never will get here
        }
    }
}

// this function reads the box and writes its initial content in the respective
// pipe
void read_box(Session *session) {
    int box = tfs_open(session->box_name, TFS_O_APPEND);
    if (box == -1) {
        fprintf(stdout, "Couldn't open the box.\n");
        exit(EXIT_FAILURE);
    }

    // lets open the pipe for writing the content of the box in question
    int sub_pipe = open(session->pipe_name, O_WRONLY);
    if (sub_pipe == -1) {
        fprintf(stdout, "Session pipe doesn't exist.\n");
        exit(EXIT_FAILURE);
    }

    // will send to sub pipe the initial state of the content in the box
    while (true) {
        char buffer[MESSAGE_SIZE];
        ssize_t ret = tfs_read(box, buffer, MESSAGE_SIZE - 1);
        while (ret == 0) {
            pthread_cond_wait(&wait_messages_cond, &session->lock);
            // if ret == 0, then we have to wait for a new message
            break;
        }

        if (ret == 0) { // to go to the next iteration of the cycle
            continue;
        }

        if (ret == -1) {
            // ret == -1 indicates error
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // sets the last thing to '\0'
        buffer[ret] = 0;

        // then with this sequence of memcpys and memsets, we will fuse
        // the op_code with the buffer in question, to send it to the pipe
        char buffer_with_op_code[sizeof(uint8_t) + MESSAGE_SIZE];
        uint8_t op_code = SEND_MESSAGE;
        memcpy(buffer_with_op_code, &op_code, sizeof(uint8_t));
        memset(buffer_with_op_code + 1, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(buffer_with_op_code + 1, buffer, strlen(buffer) * sizeof(char));

        if (write(sub_pipe, buffer_with_op_code, strlen(buffer)) == -1) {
            fprintf(stdout, "Couldn't write in the box.\n");
            close(sub_pipe);
            exit(EXIT_FAILURE);
        }

        // cleans the buffers
        memset(buffer, 0, strlen(buffer));
        memset(buffer_with_op_code, 0, strlen(buffer_with_op_code));
    }
}

void case_pub_request(Session *session) {
    int ret; // ret -> this simbolizes the return value that
    char client_name[MAX_CLIENT_NAME]; // is written in the pipe to check
    char box[BOX_NAME];                // if it was denied or not
    int pipe;
    // saves the thing in its respective new arrays
    memcpy(client_name, session->buffer + 2, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 3 + strlen(client_name), BOX_NAME);
    pipe = open(client_name, O_WRONLY);

    for (uint32_t i = 0; i < max_sessions; i++) {
        // then we check if there is any pub already with this box name
        if (container[i].type == PUB &&
            (strcmp(box, container[i].box_name) == 0)) {
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0) {
                return;
            };
        }
        // then we check if there is any pub already with this pipe name
        if (container[i].type == PUB &&
            (strcmp(client_name, container[i].pipe_name) == 0)) {
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0) {
                return;
            };
        }
    }
    // after verifying that everything is ok, then we will process to
    // save the respective values in the respective containers
    // and then we write the ret value if the operation was sucessfull
    for (int i = 0; i < BOX_NAME; i++) {
        if (strcmp(box, boxes[i].box_name) == 0) {
            session->type = PUB;
            session->pipe = pipe;
            session->pipe_name = client_name;
            session->box_name = box;
            boxes[i].num_publishers++;
            ret = 0;
            if (write(pipe, &ret, sizeof(int)) > 0) {
                send_message_to_box(session);
                close(pipe);
            };
            ret = -1;
            if (write(pipe, &ret, sizeof(int)) > 0) {
                return;
            };
        }
    }
    ret = -1;
    if (write(pipe, &ret, sizeof(int)) > 0) {
        return;
    };
}

void case_sub_request(Session *session) {
    int ret; // ret -> this simbolizes the return value that
    char client_name[MAX_CLIENT_NAME]; // is written in the pipe to check
    char box[BOX_NAME];                // if it was denied or not
    int pipe;

    // saves the thing in its respective new arrays
    memcpy(client_name, session->buffer + 2, MAX_CLIENT_NAME);
    memcpy(box, session->buffer + 3 + strlen(client_name), BOX_NAME);

    // then we open the pipe for writing
    pipe = open(client_name, O_WRONLY);

    // with this if we verify if the session type is NULL, if so we continue
    if (session->type != PUB && session->type != SUB) {
        for (uint32_t i = 0; i < max_sessions; i++) {

            // lets check if there is any sub with this pipe name
            if (container[i].type == SUB &&
                (strcmp(client_name, container[i].pipe_name) == 0)) {
                ret = -1;
                if (write(pipe, &ret, sizeof(int)) > 0) {
                    return;
                };
            }
        }
        // if everything is alright until now, then we will save the
        // respective values in the respective containers
        for (int i = 0; i < BOX_NAME; i++) {
            if (strcmp(box, boxes[i].box_name) == 0) {
                session->type = SUB;
                session->pipe = pipe;
                session->pipe_name = client_name;
                session->box_name = box;
                boxes[i].num_subscribers++;

                ret = 0;
                if (write(pipe, &ret, sizeof(int)) > 0) {
                    read_box(session);
                    close(pipe);
                    return;
                };
            }
        }
        ret = -1;
        if (write(pipe, &ret, sizeof(int)) > 0) {
            return;
        };
    }
    ret = -1;
    if (write(pipe, &ret, sizeof(int)) > 0) {
        return;
    };
}

void case_create_box(Session *session) {
    int32_t ret = 0;
    int client_pipe;
    char error_message[MESSAGE_SIZE];
    char client_name[MAX_CLIENT_NAME];
    char box_name[BOX_NAME];
    uint8_t op_code = CREATE_BOX_ANSWER;

    // lets take the values out of the buffer, and store them in these arrays
    // for later
    memcpy(client_name, session->buffer + 2, MAX_CLIENT_NAME);
    memcpy(box_name, session->buffer + 3 + strlen(client_name), BOX_NAME);

    // then we open the pipe for writing
    client_pipe = open(client_name, O_WRONLY);
    for (int i = 0; i < box_count; i++) {
        // lets check if there is any box with the same name
        // if yes then an answer will be created and sent to the client pipe
        if (strcmp(box_name, boxes[i].box_name) == 0) {
            ret = -1;
            strcpy(error_message, "There is already a box with this name.\n");

            char response[sizeof(uint8_t) + sizeof(int32_t) +
                          MESSAGE_SIZE * sizeof(char)];
            memcpy(response, &op_code, sizeof(uint8_t));
            memcpy(response + 1, &ret, sizeof(int32_t));
            memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
            memcpy(response + 2, error_message,
                   strlen(error_message) * sizeof(char));

            if (write(client_pipe, response, strlen(response)) > 0) {
                close(client_pipe);
            }
            close(client_pipe);
            return;
        }
    }
    // then if everything is alright until now, we create the box
    // and proceed to save its values in the box container of the system
    int box = tfs_open(box_name, TFS_O_CREAT);
    if (box == -1) {
        ret = -1;
        strcpy(error_message, "Couldnt create a box.\n");
    } else {
        for (int i = 0; i < BOX_NAME; i++) {
            // if there is any box free, then it will continue
            if (boxes[i].is_free) {
                boxes[i].box_name = box_name;
                boxes[i].box_size = 1024;
                boxes[i].is_free = false;
                boxes[i].last = 1;
                boxes[i].num_publishers = 0;
                boxes[i].num_subscribers = 0;
                boxes[i - 1].last = 0;
                box_count++;
                tfs_close(box);
                ret = 0;
                break;
            }
            ret = -1;
            strcpy(error_message, "There is no space left in box container.\n");
            continue;
        }
    }

    if (ret == -1) {
        // if ret is -1, then a message will be created with the
        // value of the error_message that has been given so far
        char response[sizeof(uint8_t) + sizeof(int32_t) +
                      MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message,
               strlen(error_message) * sizeof(char));

        if (write(client_pipe, response, strlen(response)) > 0) {
            close(client_pipe);
        }
        close(client_pipe);
    } else {
        // if everything went well, then instead of the error_message we put
        // '\0'
        char response[sizeof(uint8_t) + sizeof(int32_t) +
                      MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, "\0", strlen(error_message) * sizeof(char));

        // then we write it to the client pipe
        if (write(client_pipe, response, strlen(response)) > 0) {
            close(client_pipe);
        }
        close(client_pipe);
    }
}

void case_remove_box(Session *session) {
    int32_t ret = 0;
    int client_pipe;
    char error_message[MESSAGE_SIZE];
    char client_name[MAX_CLIENT_NAME];
    char box_name[BOX_NAME];
    uint8_t op_code = REMOVE_BOX_ANSWER;
    // treat request and opens client pipe
    memcpy(client_name, session->buffer + 2, MAX_CLIENT_NAME);
    memcpy(box_name, session->buffer + 3 + strlen(client_name), BOX_NAME);
    client_pipe = open(client_name, O_WRONLY);
    for (int i = 0; i < box_count; i++) {
        // lets check if we can find the box within the box list
        if (strcmp(box_name, boxes[i].box_name) != 0) {
            ret = -1;
            continue;
        }
        ret = 0;
        break;
    }

    // if there are no boxes, send message back to client and write in the pipe
    if (ret == -1) {
        strcpy(error_message,
               "There isn't a box with the specified box name.\n");

        char response[sizeof(uint8_t) + sizeof(int32_t) +
                      MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message,
               strlen(error_message) * sizeof(char));

        if (write(client_pipe, response, strlen(response)) > 0) {
            close(client_pipe);
        }
        return;
    }

    // removes the box by unlinking from the TFS
    int box = tfs_unlink(box_name);
    if (box == -1) {
        ret = -1;
        strcpy(error_message, "Couldnt delete the specified box.\n");
    } else {

        for (int i = 0; i < max_sessions; i++) {
            // closes and unlinks the pipes that were related to that box
            if (strcmp(container[i].box_name, box_name) == 0) {
                close(container[i].pipe);
                unlink(container[i].pipe_name);
                container[i].type = -1;
                container[i].pipe = 0;
                container[i].pipe_name = NULL;
                container[i].box_name = NULL;
            }
        }
        for (int i = 0; i < BOX_NAME; i++) {
            // clears the box from the list
            if (strcmp(boxes[i].box_name, box_name)) {
                strcpy(boxes[i].box_name, box_name);
                boxes[i].box_size = 0;
                boxes[i].is_free = true;
                boxes[i].last = 0;
                boxes[i].num_publishers = 0;
                boxes[i].num_subscribers = 0;
                boxes[i - 1].last = 1;
                box_count--;
                ret = 0;
            }
        }
    }

    if (ret == -1) {
        // if anyting wasn't according to plan, creates error message and writes
        // to client pipe
        char response[sizeof(uint8_t) + sizeof(int32_t) +
                      MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memset(response + 2, '\0', MESSAGE_SIZE * sizeof(char));
        memcpy(response + 2, error_message,
               strlen(error_message) * sizeof(char));

        if (write(client_pipe, response, strlen(response)) > 0) {
            close(client_pipe);
        }
    } else {
        // creates error message and writes to client pipe
        char response[sizeof(uint8_t) + sizeof(int32_t) +
                      MESSAGE_SIZE * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &ret, sizeof(int32_t));
        memcpy(response + 2, "\0", sizeof(char));
        // fills error message with /0

        if (write(client_pipe, response, strlen(response)) > 0) {
            close(client_pipe);
        }
    }
}

void case_list_box(Session *session) {
    int client_pipe;
    char client_name[MAX_CLIENT_NAME];
    uint8_t op_code = LIST_BOXES_ANSWER;
    // treat request and opens client pipe
    memcpy(client_name, session->buffer + 2, MAX_CLIENT_NAME);
    client_pipe = open(client_name, O_WRONLY);

    if (box_count == 0) { // if there are no boxes in the system, create answer
                          // and writes to pipe
        uint8_t i = 1;
        char response[sizeof(uint8_t) + sizeof(uint8_t) +
                      BOX_NAME * sizeof(char)];
        memcpy(response, &op_code, sizeof(uint8_t));
        memcpy(response + 1, &i, 1 * sizeof(uint8_t));
        memset(response + 2, '\0', BOX_NAME * sizeof(char));

        if (write(client_pipe, &response, sizeof(response)) > 0) {
            close(client_pipe);
        }
    } else { // if there are, sort the boxes
        qsort(boxes, box_count, sizeof(Box), myCompare); // sort the boxes
        char response[sizeof(uint8_t) + sizeof(uint8_t) +
                      BOX_NAME * sizeof(char) + sizeof(uint64_t) +
                      sizeof(uint64_t) + sizeof(uint64_t)];

        // for every existing box in the list, adds to the request
        for (int i = 0; i < box_count; i++) {
            memcpy(response, &op_code, sizeof(uint8_t));
            memcpy(response + 1, &boxes[i].last, 1 * sizeof(uint8_t));
            memset(response + 2, '\0', BOX_NAME * sizeof(char));
            memcpy(response + 2, boxes[i].box_name,
                   strlen(boxes[i].box_name) * sizeof(char));
            memcpy(response + 2 + strlen(boxes[i].box_name), &boxes[i].box_size,
                   sizeof(uint64_t));
            memcpy(response + 2 + strlen(boxes[i].box_name) + sizeof(uint64_t),
                   &boxes[i].num_publishers, sizeof(uint64_t));
            memcpy(response + 2 + strlen(boxes[i].box_name) + sizeof(uint64_t) +
                       sizeof(uint64_t),
                   &boxes[i].num_subscribers, sizeof(uint64_t));
            // cleans the buffer
            memset(response, 0, strlen(response));
        }
        // finally, write to pipe
        if (write(client_pipe, &response, sizeof(response)) > 0) {
            close(client_pipe);
        }
    }
}

void *thread_function(void *session) {
    Session *actual_session = (Session *)session;
    uint8_t op_code;

    while (true) {
        // removes request associated to this thread and adda to the session
        // buffer
        char *request = (char *)pcq_dequeue(queue);
        memcpy(&actual_session->buffer, request, MAX_REQUEST_SIZE);
        actual_session->is_free = false;
        memcpy(&op_code, &actual_session->buffer, sizeof(uint8_t));

        // use the op_code to check what operation do we need to do
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
        // gets avaliable for new requests
        actual_session->is_free = true;
    }
}

int init_threads() {
    // for every session, iniciate mutex and create thread
    for (uint32_t i = 0; i < max_sessions; i++) {
        container[i].is_free = true;
        if (pthread_mutex_init(&container[i].lock, NULL) == -1) {
            return -1;
        }

        if (pthread_create(&container[i].thread, NULL, &thread_function,
                           (void *)&container[i]) != 0) {
            fprintf(stderr, "[ERR]: couldn't create threads: %s\n",
                    strerror(errno));
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    // Check if we have enough arguments to start mbroker
    if (argc < 2) {
        fprintf(stderr, "usage: mbroker <pipename> <max_sessions>\n");
    }

    // get information from argv
    char *pipe_name = argv[1];
    max_sessions = (uint32_t)atoi(argv[2]);
    container = (Session *)malloc(max_sessions * sizeof(Session));
    queue = malloc(sizeof(pc_queue_t));

    // begins TFS and creates the queue
    if (tfs_init(NULL) == -1) {
        fprintf(stderr, "Não foi possivel começar o servidor\n");
        return -1;
    }

    if (pcq_create(queue, max_sessions) == -1) {
        fprintf(stderr, "Impossível fazer pedidos\n");
        return -1;
    }

    printf("[INFO]: Starting TecnicoFS server with pipe called %s with %d "
           "sessions\n",
           pipe_name, max_sessions);

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
    if (server_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // start "max_sessions" threads
    init_threads();

    while (true) {
        char buffer[MAX_REQUEST_SIZE];
        char buffer2[MAX_REQUEST_SIZE];
        char client_name[MAX_CLIENT_NAME];
        uint8_t op_code;
        uint8_t exception = LIST_BOXES_REQUEST;
        Session *current_session;

        // read request
        if (read(server_pipe, &op_code, sizeof(uint8_t)) == -1) {
            return -1;
        }
        /*
        Every request is the same except for listing the boxes, so there is
        different ways of treating these cases
        */
        if (op_code == exception) {
            for (int i = 0; i < max_sessions; i++) {
                if (container[i].pipe_name == NULL) {
                    // get the thread we are going to access, put the
                    // information read into a buffer associate the pipe_name
                    // into the thread and adds request into queue
                    current_session = &container[i];
                    memcpy(buffer, &op_code, sizeof(char));
                    memset(client_name, '\0', MAX_CLIENT_NAME);
                    read_buffer(server_pipe, buffer + 1, MAX_CLIENT_NAME);
                    memcpy(client_name, buffer + 1, MAX_CLIENT_NAME);
                    current_session->pipe_name = client_name;

                    pcq_enqueue(queue, buffer);
                    pthread_cond_signal(&queue->pcq_pusher_condvar);
                    break;
                }
            }
        } else {
            for (int i = 0; i < max_sessions; i++) {
                if (container[i].pipe_name == NULL) {
                    // get the thread we are going to access, put the
                    // information read into a buffer associate the pipe_name
                    // into the thread and adds request into queue
                    current_session = &container[i];

                    memcpy(buffer, &op_code, sizeof(char));

                    strcpy(buffer2, read_buffer(server_pipe, buffer + 1,
                                                MAX_CLIENT_NAME + BOX_NAME));
                    strcpy(client_name, strtok(buffer2 + 1, "|"));

                    current_session->pipe_name = client_name;
                    current_session->is_free = false;

                    char string[strlen(buffer)];
                    memcpy(string, buffer, strlen(buffer));
                    pcq_enqueue(queue, string);
                    pthread_cond_signal(&queue->pcq_pusher_condvar);
                    break;
                }
            }
        }

        // if signal is called, end the queue, freeing the information and
        // unlink the pipes
        if (signal(SIGINT, mbroker_exit) == SIG_ERR) {
            pcq_destroy(queue);
            free(queue);
            free(container);

            close(server_pipe);
            unlink(pipe_name);
            exit(EXIT_SUCCESS);
        }
    }
    // never reaches this case
    close(server_pipe);
    unlink(pipe_name);
    return -1;
}
