#include "logging.h"
#include "operations.h"
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

// Helper function to send pub request
void send_pub_request(int tx, u_int8_t code, char *pub_pipename_arg, char *box_name_arg) {
    if (strlen(pub_pipename_arg) <= MAX_PIPE_NAME)
        pub_pipename_arg += '\0' * (MAX_PIPE_NAME - strlen(pub_pipename_arg));

    if (strlen(box_name_arg) <= MAX_BOX_NAME)
        box_name_arg += '\0' * (MAX_BOX_NAME - strlen(box_name_arg));

    char pub_request[4 + strlen(pub_pipename_arg) + strlen(box_name_arg)];

    sprintf(pub_request, " %d | %s | %s ");
    int w = write(tx, pub_request, strlen(pub_request));
    if (w == -1){
        fprintf(stderr, "Writing the request went wrong.\n");
        exit(EXIT_FAILURE);
    }

}

int main(int argc, char **argv) {
    char *register_pipename = argv[1]; 
    char *pub_pipename = argv[2];
    char *box_name = argv[3];

    if (argc > 4){
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

    // Open pipe for writing
    // This waits for someone to open it for reading
    int tx = open(register_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    send_pub_request(tx, 1, pub_pipename, box_name);

    return 0;
}
