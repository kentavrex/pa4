#include "ipc.h"

struct Request {
    local_id locpid;
    timestamp_t req_time;
};

struct RequestQueue {
    struct Request heap[MAX_PROCESS_ID];
    local_id size;
};

void clearQueue(struct RequestQueue *queue);
struct Request getHead(const struct RequestQueue *queue);
int8_t compareRequests(struct Request first, struct Request second);
void popHead(struct RequestQueue *queue);
void pushRequest(struct RequestQueue *queue, struct Request request);
