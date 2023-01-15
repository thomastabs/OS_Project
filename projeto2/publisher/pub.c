#include "../fs/operations.h"
#include "../utils/common.h"
#include "../utils/logging.h"
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

// Helper function to send pub request
int send_pub_request(int server_pipe, char *client_name, char *box) {
    int flag;
    // lets define the server request string
    char server_request[sizeof(uint8_t) + 2 + MAX_CLIENT_NAME * sizeof(char) +
                        BOX_NAME * sizeof(char)];
    uint8_t op_code = PUB_REQUEST;
    memcpy(server_request, &op_code, sizeof(uint8_t));

    memset(server_request + 1, '|', sizeof(char));
    memset(server_request + 2, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 2, client_name, strlen(client_name) * sizeof(char));

    memset(server_request + 2 + strlen(client_name), '|', sizeof(char));
    memset(server_request + 3 + strlen(client_name), '\0',
           BOX_NAME * sizeof(char));
    memcpy(server_request + 3 + strlen(client_name), box,
           strlen(box) * sizeof(char));
    // with this type of format it will be easy to separate each value from each
    // other so the pub request will have for example this format:
    // '\001|../manager1|box'

    // with this for loop we can check if the box in question is, in fact, in
    // the system and within our custom array that is responsible to hold the
    // arrays
    for (int i = 0; i < BOX_NAME; i++) {
        if (strcmp(boxes[i].box_name, box) != 0) {
            flag = 1;
            continue;
        }
        flag = 0;
        break;
    }

    // this flag attribute, will simbolize if its possible or not
    // to do the request itself
    if (flag == 1) {
        return -1;
    }
    /* Send request to server */
    if (write(server_pipe, &server_request, sizeof(server_request)) == -1) {
        return -1;
    }

    /* Open the client pipe for reading */
    int client_pipe = open(client_name, O_RDONLY);
    if (client_pipe == -1) {
        return -1;
    }

    // if the response read which was made by the server is -1, then it returns
    // -1
    int response;
    if (read(client_pipe, &response, sizeof(response)) == -1 ||
        errno == EPIPE) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        close(client_pipe);
        return -1;
    }

    // and then we close the pipe
    close(client_pipe);
    return response;
}

int send_pub_msg(int session_pipe, char *msg) {
    char pub_msg[sizeof(uint8_t) + MESSAGE_SIZE * sizeof(char)];
    uint8_t op_code = SEND_MESSAGE;
    // lets create the format for the pub message
    // in which consists in the op_code and then the message itself
    memcpy(pub_msg, &op_code, sizeof(uint8_t));
    memset(pub_msg + 1, '\0', MESSAGE_SIZE * sizeof(char));
    memcpy(pub_msg + 1, msg, strlen(msg) * sizeof(char));

    // write to the the client session pipe
    if (write(session_pipe, pub_msg, strlen(pub_msg)) == -1) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char *register_pipename = argv[1];
    char *pub_pipename = argv[2];
    char *box_name = argv[3];

    // checks if the number of arguments is correct
    if (argc < 4) {
        fprintf(stderr, "usage: pub <register_pipe_name> <box_name>\n");
        exit(EXIT_FAILURE);
    }

    // before doing anything, it checks the sizes of the pub pipe name and the
    // box
    if (strlen(box_name) > BOX_NAME || strlen(pub_pipename) > MAX_CLIENT_NAME) {
        fprintf(stderr, "Max variable size achieved.\n");
        exit(EXIT_FAILURE);
    }

    // Verifies if the named pipe exists and creates it
    if (mkfifo(pub_pipename, 0640) == -1 && errno == EEXIST) {
        fprintf(stderr, "Named Pipe already exists.");
        exit(EXIT_FAILURE);
    }

    // Open session pipe for writing
    // This waits for someone to open it for reading
    int server_pipe = open(register_pipename, O_WRONLY);
    if (server_pipe == -1) {
        fprintf(stderr, "[ERR]: lmao: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // after being opened for reading in the other side
    // lets send the pub request to the server and check if it was sent corretly
    if (send_pub_request(server_pipe, pub_pipename, box_name) == -1) {
        // if not, it should exit the program
        fprintf(stderr, "Request Denied. %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else {
        // if yes then, lets open the session pipe for writing
        int session_pipe = open(pub_pipename, O_WRONLY);
        if (session_pipe == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // so lets start reading from stdin by
        // creating a buffer and then clean it
        char input[MESSAGE_SIZE];
        memset(input, 0, MESSAGE_SIZE);

        while (fgets(input, MESSAGE_SIZE, stdin)) {
            /* if line too long, truncate and swallow the rest of the line */
            if (strlen(input) >= MESSAGE_SIZE - 1) {
                input[MESSAGE_SIZE - 1] = '\0';
                while (getchar() != '\n' && !feof(stdin))
                    ;
            }

            input[strcspn(input, "\n")] = 0;
            if (send_pub_msg(session_pipe, input) == -1) {
                fprintf(stderr, "Writing went wrong.\n");
                close(session_pipe);
                unlink(pub_pipename);
                exit(EXIT_FAILURE);
            }
            // after clicking enter, the buffer is filled and so
            // the message will be sent to the pub pipe, after replacing
            // the \n by \0, and so it continues to the next line
        }

        // when the EOF is pressed with the Crtl D, then the session pipe will
        // be closed, right after reducing the number of publishers connected to
        // the box in question since the pub session will end
        for (int i = 0; i < BOX_NAME; i++) {
            if (strcmp(boxes[i].box_name, box_name) == 0) {
                boxes[i].num_publishers--;
                break;
            }
        }

        fprintf(stderr, "[INFO]: closing session pipe\n");
        close(session_pipe);
        unlink(pub_pipename);

        return 0;
    }
}
