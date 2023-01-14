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
#include <signal.h>
#include "../utils/common.h"
#include "../fs/operations.h"
#include "../utils/logging.h"


#define MAX_PIPE_NAME 256
#define MAX_BOX_NAME 32
#define MAX_MSG_SIZE 1024 

int new_msgs_read = 0; // for the new messages read within the session pipe

int send_sub_request(int server_pipe, char* client_name, char* box){
    int flag;
    char server_request[sizeof(uint8_t) + strlen(client_name) + strlen(box) + 2];
    uint8_t op_code = SUB_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', strlen(client_name + 1)); 
    memcpy(server_request + 1, client_name, strlen(client_name) + 1);
    memset(server_request + 1 + strlen(client_name), '\0', strlen(box) + 1);
    memcpy(server_request + 1 + strlen(client_name), box, strlen(box) + 1);

    for(int i=0; i< BOX_NAME; i++){
        if (strcmp(boxes[i].box_name, box) != 0){
            flag = 1;
            continue;
        }
        flag = 0;
        break;
    }

    if (flag == 1){
        return -1;
    }

    /* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) == -1) {
		return -1;
	}


    int c_pipe = open(client_name, O_RDONLY);
    int response;
    if (read(c_pipe, &response, sizeof(response)) == -1 || errno == EPIPE){
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }

    return response;
}

static void sig_handler(int sig) {
    if (sig == SIGINT) {
        fprintf(stdout, "\nExited Publisher with %d new messages recieved\n", new_msgs_read);
        return; // Resume execution at point of interruption
    }
}


int main(int argc, char **argv) {
    char *register_pipename = argv[1]; 
    char *sub_pipename = argv[2];
    char *box_name = argv[3];

    if (argc != 4){
        fprintf(stderr, "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");
        exit(EXIT_FAILURE);
    }

    if (strlen(box_name) > BOX_NAME || strlen(sub_pipename) > MAX_CLIENT_NAME){
        fprintf(stderr, "Max variable size achieved.\n");
        exit(EXIT_FAILURE);
    }
    
    // Verifies if the named pipe exists and creates it 
    if (mkfifo(sub_pipename, 0640) == -1 && errno == EEXIST){
        fprintf(stderr, "Named Pipe already exists.");
        exit(EXIT_FAILURE);
    }

    // open server pipe for writing the sub request
    int server_pipe = open(register_pipename, O_WRONLY);
    if (server_pipe == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (send_sub_request(server_pipe, sub_pipename, box_name) == 0){

        int sub_pipe = open(sub_pipename, O_RDONLY);
        if (sub_pipe == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        while (true){
            char buffer[MAX_MSG_SIZE];
            ssize_t ret = read(sub_pipe, buffer, MAX_MSG_SIZE - 1);
            if (ret == 0) {
                // ret == 0 indicates EOF   
                // but it has to keep reading, to see if new messages have been wrote
                continue;
            } else if (ret == -1) {
                // ret == -1 indicates error
                fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            buffer[ret] = 0;
            new_msgs_read++;
            fprintf(stdout, "%s\n", buffer + 1); // separa o opcode da msg 

            if (signal(SIGINT, sig_handler) == SIG_ERR) {
                for(int i = 0; i< BOX_NAME; i++){
                    if (strcmp(boxes[i].box_name,box_name) == 0){
                        boxes[i].num_subscribers--;
                        break;
                    }
                }
                close(sub_pipe);
                unlink(sub_pipename);
                exit(EXIT_SUCCESS);
            }
        }
    }
    else {
        fprintf(stderr, "Request Denied. Deleting session pipe.");
        unlink(sub_pipename);
        exit(EXIT_FAILURE);
    }
}
