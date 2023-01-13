#include<stdlib.h>
#include<stdint.h>

#ifndef COMMON_H
#define COMMON_H

#define MAX_CLIENT_NAME (256)
#define BOX_NAME (32)
#define MESSAGE_SIZE (1024)

/* 
    maximum size that a request can have
    in this project, the maximum request size will be 1030
    (situation where the client sends and recieves messages)
 */
#define MAX_REQUEST_SIZE (1300)

/* operation codes (for client-server requests) */
enum {
	PUB_REQUEST = 1,
	SUB_REQUEST = 2,
	CREATE_BOX_REQUEST = 3,
	CREATE_BOX_ANSWER = 4,
	REMOVE_BOX_REQUEST= 5,
	REMOVE_BOX_ANSWER = 6,
	LIST_BOXES_REQUEST = 7,
    LIST_BOXES_ANSWER = 8,
    SEND_MESSAGE = 9,
    RECEIVE_MESSAGE = 10
};



#endif