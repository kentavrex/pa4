#include <stdio.h>

#include "queue.h"

void clearQueue(struct RequestQueue *queue) {
	queue->size = 0;
}

struct Request getHead(const struct RequestQueue *queue) {
	if (queue->size > 0) return queue->heap[0];
    return (struct Request){0, 0};
}

int8_t compareRequests(struct Request first, struct Request second) {
    if (first.req_time > second.req_time) return 1;
    if (first.req_time < second.req_time) return -1;
    if (first.locpid > second.locpid) return 1;
    if (first.locpid < second.locpid) return -1;
    return 0;
}

static void siftDown(struct RequestQueue *queue, local_id index) {
    if (index >= 0 && index < queue->size) {
        while (2 * index + 1 < queue->size) {
            local_id left = 2 * index + 1;
            local_id right = left + 1;
            if (right < queue->size && compareRequests(queue->heap[left], queue->heap[right]) > 0) {
                if (compareRequests(queue->heap[index], queue->heap[right]) > 0) {
                    local_id t1 = queue->heap[index].locpid;
                    queue->heap[index].locpid = queue->heap[right].locpid;
                    queue->heap[right].locpid = t1;
                    timestamp_t t2 = queue->heap[index].req_time;
                    queue->heap[index].req_time = queue->heap[right].req_time;
                    queue->heap[right].req_time = t2;
                    index = right;
                } else break;
            } else if (compareRequests(queue->heap[index], queue->heap[left]) > 0) {
                local_id t1 = queue->heap[index].locpid;
                queue->heap[index].locpid = queue->heap[left].locpid;
                queue->heap[left].locpid = t1;
                timestamp_t t2 = queue->heap[index].req_time;
                queue->heap[index].req_time = queue->heap[left].req_time;
                queue->heap[left].req_time = t2;
                index = left;
            } else break;
        }
    }
}

void popHead(struct RequestQueue *queue) {
    if (queue->size > 0) {
        --queue->size;
        queue->heap[0].locpid = queue->heap[queue->size].locpid;
        queue->heap[0].req_time = queue->heap[queue->size].req_time;
        siftDown(queue, 0);
    }
}

static void siftUp(struct RequestQueue *queue, local_id index) {
    if (index > 0 && index < queue->size) {
        while (index > 0) {
            local_id parent = (index - 1) / 2;
            if (compareRequests(queue->heap[index], queue->heap[parent]) < 0) {
                local_id t1 = queue->heap[index].locpid;
                queue->heap[index].locpid = queue->heap[parent].locpid;
                queue->heap[parent].locpid = t1;
                timestamp_t t2 = queue->heap[index].req_time;
                queue->heap[index].req_time = queue->heap[parent].req_time;
                queue->heap[parent].req_time = t2;
                index = parent;
            } else break;
        }
    }
}

void pushRequest(struct RequestQueue *queue, struct Request request) {
	queue->heap[queue->size].locpid = request.locpid;
	queue->heap[queue->size].req_time = request.req_time;
    siftUp(queue, queue->size++);
}
