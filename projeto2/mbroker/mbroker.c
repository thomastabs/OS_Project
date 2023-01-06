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
#include "logging.h"
#include "fs/operations.h"

typedef enum {PUB, SUB} client_type;

typedef struct {
    char session_name;
    int pipe;
    client_type client;
} Session;

int main(int argc, char **argv) {
    if(argc < 2){
        fprintf(stderr, "usage: mbroker <pipename> <max_sessions>\n");
    }
    
    char *pipe_name = argv[1];
    int max_sessions = atoi(argv[2]);
    Session sessions[max_sessions]; 

    if(tfs_init(NULL) != -1){
        fprintf(stderr, "Não foi possivel começar o servidor\n");
        return -1;
    }    
    		
            //signal(SIGPIPE, SIG_IGN);

    printf("[INFO]: Starting TecnicoFS server with pipe called %s with %d sessions\n", pipe_name, max_sessions);

    // remove pipe if it does exist
    if (unlink(pipe_name) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipe_name,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create pipe
    if (mkfifo(pipe_name, 0640) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // server request
    int server_pipe = open(pipe_name, O_WRONLY);
    if (server_pipe == -1){
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    start_sessions();
    




    close(server_pipe);
    tfs_unlink(pipe_name);
    return -1;
}
