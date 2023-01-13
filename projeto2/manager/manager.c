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
#include "../utils/common.h"
#include "logging.h"

static void print_usage() {
    fprintf(stderr, "usage: \n"
                    "   manager <register_pipe> <pipe_name> create <box_name>\n"
                    "   manager <register_pipe> <pipe_name> remove <box_name>\n"
                    "   manager <register_pipe> <pipe_name> list\n");
}

int send_request_create_box(char* server_pipe, char* client_pipe, char* box){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = CREATE_BOX_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_pipe, strlen(client_pipe) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    int s_pipe = open(server_pipe, O_WRONLY);
    if (s_pipe == -1){
        fprintf(stderr, "Couldn't open the pipe");
        return -1;
    }
    /* Send request to server */
	if (write(s_pipe, &server_request, sizeof(server_request)) > 0) {
		close(s_pipe);
	}
    else 
        return -1;


    //parte de resposta no servidor

    /* Receives answer */
    char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
    int c_pipe = open(client_pipe, O_RDONLY);
    if (c_pipe == -1){
        return -1;
    }

    if (read(c_pipe, &response, sizeof(response)) == -1){
        close(c_pipe);
        return -1;
    }

    // converts the return code inside the response to a int32_t character
    int32_t ret;
    memcpy(&ret, response + 1, sizeof(int32_t));

    // Verifies what the answer was
    if (ret == -1){
        fprintf(stdout,"ERROR %.*s\n", 1024, response + 5); //error_message
        return -1;
    }
    fprintf(stdout, "OK\n");
    return 0;
}

int send_request_remove_box(char* server_pipe, char* client_pipe, char* box){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = REMOVE_BOX_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_pipe, strlen(client_pipe) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    int s_pipe = open(server_pipe, O_WRONLY);
    if (s_pipe == -1){
        fprintf(stderr, "Couldn't open the pipe");
        return -1;
    }

    /* Send request to server */
	if (write(s_pipe, &server_request, sizeof(server_request)) > 0) {
		close(s_pipe);
	}
    else 
        return -1;

    //parte de resposta no servidor

    /* Receives answer */
    char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
    int c_pipe = open(client_pipe, O_RDONLY);
    if (c_pipe == -1){
        return -1;
    }

    if (read(c_pipe, &response, sizeof(response)) == -1){
        close(c_pipe);
        return -1;
    }

    // converts the return code inside the response to a int32_t character
    int32_t ret;
    memcpy(&ret, response + 1, sizeof(int32_t));

    // Verifies what the answer was
    if (ret == -1){
        fprintf(stdout,"ERROR %.*s\n", 1024, response + 5);
        return -1;
    }
    fprintf(stdout, "OK\n");
    return 0;
}

int send_request_list_box(char* server_pipe, char* client_pipe){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char)];
    uint8_t op_code = LIST_BOXES_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_pipe, strlen(client_pipe) * sizeof(char));

    int s_pipe = open(server_pipe, O_WRONLY);
    if (s_pipe == -1){
        fprintf(stderr, "Couldn't open the pipe");
        return -1;
    }
    
	/* Send request to server */
	if (write(s_pipe, &server_request, sizeof(server_request)) > 0) {
		close(s_pipe);
	}
    else 
        return -1;

    //parte de resposta no servidor

    /* Receives answer */
    
    //como esta resposta será de uma certa forma uma variável, iremos meter um pico (1030 neste caso)
    char response[MAX_REQUEST_SIZE];
    int c_pipe = open(client_pipe, O_RDONLY);
    if (c_pipe == -1){
        return -1;
    }

    if (read(c_pipe, &response, sizeof(response)) == -1){
        close(c_pipe);
        return -1;
    }


    // Verifies what the answer was
    if (strlen(response) == 2){
        fprintf(stdout, "NO BOXES FOUND\n");
        return -1;
    }
    for(size_t i = 0; i < strlen(response); i+=58){
        fprintf(stdout, "%.1s %.32s %.8s %.8s %.8s\n", 
        response+i+1, 
        response+i+2, 
        response+i+34, 
        response+i+42, 
        response+i+50); // 58 being the size of each response of the request
    } 

    return 0;
}


int main(int argc, char **argv) {
    if(argc < 4){
        print_usage();
        return -1;
    }

    char *server_pipe = argv[1];
    char *pipe_name = argv[2];
    char *type_command = argv[3];
    char *box_name = argv[4];

    // check if we can create or remove a message box with the commands given 
    if ((strcmp(type_command, "create") || strcmp(type_command, "remove")) && argc == 4){
        print_usage();
        return -1;
    }

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

    /* Open server pipe */
	if (open(server_pipe, O_WRONLY) == -1) {
		return -1;
	}

    // check what command we need to do
    if (strcmp(type_command, "create")){
        send_request_create_box(server_pipe, pipe_name, box_name);
    }

    if (strcmp(type_command, "remove")){
        send_request_remove_box(server_pipe, pipe_name, box_name);
    }

    if (strcmp(type_command, "list")){
        send_request_list_box(server_pipe, pipe_name);
    }

    
    WARN("unimplemented"); // TODO: implement
    return -1;
}
