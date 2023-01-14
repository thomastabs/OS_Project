#include "../utils/logging.h"
#include "../utils/common.h"
#include "../fs/operations.h"
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

#define MAX_PIPE_NAME 256
#define MAX_BOX_NAME 32
#define MAX_MSG_SIZE 1024   

// Helper function to send pub request
int send_pub_request(int server_pipe, char* client_name, char* box) {
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = PUB_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_name, strlen(client_name) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    /* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) == -1) {
		return -1;
	}

    int client_pipe = open(client_name, O_RDONLY);
    if (client_pipe == -1){
        return -1;
    }
    int response;
    if (read(client_pipe, &response, sizeof(response)) == -1 || errno == EPIPE){
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return response;
}

int send_pub_msg(int session_pipe, char *pub_pipename, char *msg){
    char pub_msg[sizeof(uint8_t) +  MAX_MSG_SIZE * sizeof(char) + MAX_PIPE_NAME * sizeof(char)];
    uint8_t op_code = SEND_MESSAGE;
    memcpy(pub_msg, &op_code, sizeof(uint8_t));
    memset(pub_msg + 1, '\0', MAX_MSG_SIZE * sizeof(char));
    memcpy(pub_msg + 1, msg, strlen(msg) * sizeof(char));
    memset(pub_msg + 1 + MAX_MSG_SIZE, '\0', MAX_PIPE_NAME * sizeof(char));
    memcpy(pub_msg + 1 + MAX_MSG_SIZE, pub_pipename, strlen(pub_pipename) * sizeof(char));

    // write to the the client session pipe
    if (write(session_pipe, pub_msg, strlen(pub_msg)) == -1){
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char *register_pipename = argv[1]; 
    char *pub_pipename = argv[2];
    char *box_name = argv[3];

    if (argc < 4){
        fprintf(stderr, "usage: pub <register_pipe_name> <box_name>\n");
        exit(EXIT_FAILURE);
    }

    if (strlen(box_name) > MAX_BOX_NAME || strlen(pub_pipename) > MAX_PIPE_NAME){
        fprintf(stderr, "Max variable size achieved.\n");
        exit(EXIT_FAILURE);
    }
    
    // Verifies if the named pipe exists and creates it 
    if (mkfifo(pub_pipename, 0640) == -1 && errno == EEXIST){
        fprintf(stderr, "Named Pipe already exists.");
        exit(EXIT_FAILURE);
    }

    // Open session pipe for writing
    // This waits for someone to open it for reading
    int server_pipe = open(register_pipename, O_WRONLY);
    if (server_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    
    if (send_pub_request(server_pipe, pub_pipename, box_name) == -1){
        fprintf(stderr, "Request Denied. %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } 
    else {
        // Open pipe for writing
        int session_pipe = open(pub_pipename, O_WRONLY);
        if (session_pipe == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        char input[MAX_MSG_SIZE];
	    memset(input, 0, MAX_MSG_SIZE);

        while (fgets(input, MAX_MSG_SIZE, stdin)) {
            /* if line too long, truncate and swallow the rest of the line */
            if (strlen(input) >= MAX_MSG_SIZE - 1) {
                input[MAX_MSG_SIZE - 1] = '\0';
                while (getchar() != '\n' && !feof(stdin))
                    ;
            }

            input[strcspn(input, "\n")] = 0;
            if (send_pub_msg(session_pipe, pub_pipename, input) == -1){
                fprintf(stderr, "Writing went wrong.\n");
                close(session_pipe);
                unlink(pub_pipename);
                exit(EXIT_FAILURE);
            }
            //depois de ser clicado enter, um input é preenchido e 
            //assim vai para a proxima linha 
	    }

        // when the EOF is pressed with the Crtl D, then the session pipe will be closed
        for(int i = 0; i< BOX_NAME; i++){
            if (strcmp(boxes[i].box_name,box_name) == 0){
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
