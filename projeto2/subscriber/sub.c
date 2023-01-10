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

int send_sub_request(char* server_pipe, char* client_pipe, char* box){
    char server_request[sizeof(uint8_t) + MAX_CLIENT_NAME * sizeof(char) + BOX_NAME * sizeof(char)];
    uint8_t op_code = SUB_REQUEST; 
    memcpy(server_request, &op_code, sizeof(uint8_t));
    memset(server_request + 1, '\0', MAX_CLIENT_NAME * sizeof(char));
    memcpy(server_request + 1, client_pipe, strlen(client_pipe) * sizeof(char));
    memset(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), '\0', BOX_NAME * sizeof(char));
    memcpy(server_request + 1 + MAX_CLIENT_NAME * sizeof(char), box, strlen(box) * sizeof(char));

    /* Send request to server */
	if (write(server_pipe, &server_request, sizeof(server_request)) == -1) {
		return -1;
	}

    int response;
    if (read(client_pipe, &response, sizeof(response)) == -1 || errno == EPIPE){
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }

    return response;
}


int main(int argc, char **argv) {
    char *register_pipename = argv[1]; 
    char *pub_pipename = argv[2];
    char *box_name = argv[3];

    if (argc < 4){
        fprintf(stderr, "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");
        exit(EXIT_FAILURE);
    }

    if (strlen(box_name) > BOX_NAME || strlen(pub_pipename) > MAX_CLIENT_NAME){
        fprintf(stderr, "Max variable size achieved.\n");
        exit(EXIT_FAILURE);
    }
    
    // Verifies if the named pipe exists and creates it 
    if (mkfifo(pub_pipename, 0640) == -1 && errno == EEXIST){
        fprintf(stderr, "Named Pipe already exists.");
        exit(EXIT_FAILURE);
    }

    send_sub_request(register_pipename, pub_pipename, box_name);



    return -1;
}
