#include "../fs/operations.h"
#include "../utils/common.h"
#include "../utils/logging.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int new_msgs_read = 0; // for the new messages read within the session pipe

int send_sub_request(int server_pipe, char *client_pipe, char *box) {
    int flag;
    char server_request[sizeof(uint8_t) + 2 + MAX_CLIENT_NAME * sizeof(char) +
                        BOX_NAME * sizeof(char)];
    uint8_t op_code = SUB_REQUEST;
    // this is very similar to the pub request, the only thing that changes is
    // the op_code so the format will be this, for example:
    // '\002|../manager1|box'

    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '|', sizeof(char));
    memset(server_request + 2, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 2, client_pipe, strlen(client_pipe) * sizeof(char));

    memset(server_request + 2 + strlen(client_pipe), '|', sizeof(char));
    memset(server_request + 3 + strlen(client_pipe), '\0',
           BOX_NAME * sizeof(char));
    memcpy(server_request + 3 + strlen(client_pipe), box,
           strlen(box) * sizeof(char));

    // after the series of memcpys and memsets lets check if the box exists
    // within our custom box container
    for (int i = 0; i < BOX_NAME; i++) {
        if (strcmp(boxes[i].box_name, box) != 0) {
            flag = 1;
            continue;
        }
        flag = 0;
        break;
    }

    // if there isn't a box with the specified name then
    // it returns -1
    if (flag == 1) {
        return -1;
    }

    /* Send request to server */
    if (write(server_pipe, &server_request, sizeof(server_request)) == -1) {
        return -1;
    }

    int c_pipe = open(client_pipe, O_RDONLY);
    int response;
    if (read(c_pipe, &response, sizeof(response)) == -1 || errno == EPIPE) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        close(c_pipe);
        return -1;
    }

    close(c_pipe);
    return response;
}

// this is the signal handler that will print a message when Ctrl C is pressed
static void sig_handler(int sig) {
    if (sig == SIGINT) {
        fprintf(stdout, "\nExited Publisher with %d new messages recieved\n",
                new_msgs_read);
        return; // Resume execution at point of interruption
    }
}

int main(int argc, char **argv) {
    char *register_pipename = argv[1];
    char *sub_pipename = argv[2];
    char *box_name = argv[3];

    // checks if the number of arguments is correct
    if (argc < 4) {
        fprintf(stderr,
                "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");
        exit(EXIT_FAILURE);
    }

    // before doing anything, it checks the sizes of the pub pipe name and the
    // box
    if (strlen(box_name) > BOX_NAME || strlen(sub_pipename) > MAX_CLIENT_NAME) {
        fprintf(stderr, "Max variable size achieved.\n");
        exit(EXIT_FAILURE);
    }

    // Verifies if the named pipe exists and creates it
    if (mkfifo(sub_pipename, 0640) == -1 && errno == EEXIST) {
        fprintf(stderr, "Named Pipe already exists.");
        exit(EXIT_FAILURE);
    }

    // open server pipe for writing the sub request
    int server_pipe = open(register_pipename, O_WRONLY);
    if (server_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // lets send the sub request to the server and check if it was sent corretly
    if (send_sub_request(server_pipe, sub_pipename, box_name) == 0) {

        // lets open the sub pipe for reading
        int sub_pipe = open(sub_pipename, O_RDONLY);
        if (sub_pipe == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        while (true) {
            char buffer[MESSAGE_SIZE];
            ssize_t ret = read(sub_pipe, buffer, MESSAGE_SIZE - 1);
            // reads from the pipe, and saves the bytes read in the ret and in
            // the buffer
            if (ret == 0) {
                // ret == 0 indicates EOF
                // but it has to keep reading, to see if new messages have been
                // wrote
                continue;
            } else if (ret == -1) {
                // ret == -1 indicates error
                fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            // saves the last letter as a \0
            buffer[ret] = 0;
            new_msgs_read++;
            fprintf(stdout, "%s\n", buffer + 1);
            // and then with the buffer + 1, we separate the op_code from the
            // actual message

            // during the while and the program itself, we will check if
            // Ctrl C is pressed, if so the number of subscribers, associated
            // with the box is decremented and then we close the pipe and unlink
            // it
            if (signal(SIGINT, sig_handler) == SIG_ERR) {
                for (int i = 0; i < BOX_NAME; i++) {
                    if (strcmp(boxes[i].box_name, box_name) == 0) {
                        boxes[i].num_subscribers--;
                        break;
                    }
                }
                close(sub_pipe);
                unlink(sub_pipename);
                exit(EXIT_SUCCESS);
            }
        }
    } else {
        // if the request is denied, then the pipename is deleted
        fprintf(stderr, "Request Denied. Deleting session pipe.");
        unlink(sub_pipename);
        exit(EXIT_FAILURE);
    }
}
