#ifndef __CONTEXT__H
#define __CONTEXT__H

#include <stdio.h>

#include "ipc.h"
#include "pipes.h"
#include "queue.h"

struct Context {
    local_id children, locpid, msg_sender;
    struct Pipes pipes;
    local_id num_started, num_done;
    int8_t rec_started[MAX_PROCESS_ID+1];
    int8_t rec_done[MAX_PROCESS_ID+1];
    FILE *events;
    int8_t mutexl;
    struct RequestQueue reqs;
};

#endif