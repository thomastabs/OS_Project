#include "logging.h"

static void print_usage() {
    fprintf(stderr, "usage: \n"
                    "   manager <register_pipe> <pipe_name> create <box_name>\n"
                    "   manager <register_pipe> <pipe_name> remove <box_name>\n"
                    "   manager <register_pipe> <pipe_name> list\n");
}

int main(int argc, char **argv) {
    if(argc < 4){
        print_usage();
    }

    char *server_pipe = argv[1];
    char *pipe_name = argv[2];
    char *type_command = argv[3];
    char *box_name = argv[4];

    
    
    WARN("unimplemented"); // TODO: implement
    return -1;
}
