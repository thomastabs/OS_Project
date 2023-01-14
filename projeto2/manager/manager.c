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
#include "../utils/logging.h"

int client_pipe;
int server_pipe;

static void print_usage() {
    fprintf(stderr, "usage: \n"
                    "   manager <register_pipe> <pipe_name> create <box_name>\n"
                    "   manager <register_pipe> <pipe_name> remove <box_name>\n"
                    "   manager <register_pipe> <pipe_name> list\n");
}

int send_request_create_box(char* server_name, char* client_name, char* box){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = CREATE_BOX_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_name, strlen(client_name) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    if ((server_pipe = open(server_name, O_WRONLY)) == -1) {
		return -1;
	}
    /* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) > 0) {
		close(server_pipe);
	}
    else 
        return -1;

    /* Receives answer */
    char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
    if ((client_pipe = open(client_name, O_RDONLY)) == -1) {
		return -1;
	}


    if (read(client_pipe, &response, sizeof(response)) == -1){
        close(client_pipe);
        return -1;
    }

    close(client_pipe);

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

int send_request_remove_box(char* server_name, char* client_name, char* box){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = REMOVE_BOX_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_name, strlen(client_name) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    if ((server_pipe = open(server_name, O_WRONLY)) == -1) {
		return -1;
	}

    /* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) > 0) {
		close(server_pipe);
	}
    else 
        return -1;

    /* Receives answer */
    char response[sizeof(uint8_t) + sizeof(int32_t) + MESSAGE_SIZE * sizeof(char)];
    if ((client_pipe = open(client_name, O_RDONLY)) == -1) {
		return -1;
	}

    if (read(client_pipe, &response, sizeof(response)) == -1){
        close(client_pipe);
        return -1;
    }

    close(client_pipe);

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

int send_request_list_box(char* server_name, char* client_name){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char)];
    uint8_t op_code = LIST_BOXES_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_name, strlen(client_name) * sizeof(char));

    if ((server_pipe = open(server_name, O_WRONLY)) == -1) {
		return -1;
	}
    
	/* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) > 0) {
		close(server_pipe);
	}
    else 
        return -1;
    /* Receives answer */
    
    //como esta resposta será de uma certa forma uma variável, iremos meter um pico (1030 neste caso)
    char response[MAX_REQUEST_SIZE];

    if ((client_pipe = open(client_name, O_RDONLY)) == -1) {
		return -1;
	}

    if (read(client_pipe, &response, sizeof(response)) == -1){
        close(client_pipe);
        return -1;
    }

    close(client_pipe);

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

    char *server_name = argv[1];
    char *pipe_name = argv[2];
    char *type_command = argv[3];
    char *box_name = argv[4];

    // check if we can create or remove a message box with the commands given 
    if ((strcmp(type_command, "create") == 0 || strcmp(type_command, "remove") == 0) && argc == 4){
        print_usage();
        return -1;
    }

    if (strcmp(type_command, "list") == 0 && argc == 5){
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

    // check what command we need to do
    if (strcmp(type_command, "create") == 0){
        send_request_create_box(server_name, pipe_name, box_name);
    }

    if (strcmp(type_command, "remove") == 0){
        send_request_remove_box(server_name, pipe_name, box_name);
    }

    if (strcmp(type_command, "list") == 0){
        send_request_list_box(server_name, pipe_name);
    }

    unlink(pipe_name);
    return 0;
}
