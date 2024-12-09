#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "context.h"
#include "ipc.h"
#include "pa2345.h"
#include "pipes.h"

static timestamp_t lamport_time = 0;

timestamp_t get_lamport_time() {
	return lamport_time;
}

int request_cs(const void * self) {
	struct Context *ctx = (struct Context*)self;
	++lamport_time;
	pushRequest(&ctx->reqs, (struct Request){ctx->locpid, get_lamport_time()});
	Message request;
	request.s_header.s_magic = MESSAGE_MAGIC;
	request.s_header.s_type = CS_REQUEST;
	request.s_header.s_payload_len = 0;
	request.s_header.s_local_time = get_lamport_time();
	if (send_multicast(ctx, &request)) {
		//fprintf(stderr, "Child %d: failed to send CS_REQUEST message\n", ctx->locpid);
		return 1;
	}
	local_id replies = 0;
    int8_t rep_arr[MAX_PROCESS_ID+1];
    for (local_id i = 1; i <= ctx->children; ++i) {
        replies += ctx->rec_done[i];
        rep_arr[i] = ctx->rec_done[i];
    }
    if (!rep_arr[ctx->locpid]) {
        ++replies;
        rep_arr[ctx->locpid] = 1;
    }
	while (replies < ctx->children) {
		Message msg;
		while (receive_any(ctx, &msg)) {}
		switch (msg.s_header.s_type) {
			case CS_REQUEST:
				if (ctx->mutexl) {
					if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
					++lamport_time;
					pushRequest(&ctx->reqs, (struct Request){ctx->msg_sender, msg.s_header.s_local_time});
					if (getHead(&ctx->reqs).locpid == ctx->msg_sender) {
						++lamport_time;
						Message reply;
						reply.s_header.s_magic = MESSAGE_MAGIC;
						reply.s_header.s_type = CS_REPLY;
						reply.s_header.s_payload_len = 0;
						reply.s_header.s_local_time = get_lamport_time();
						if (send(ctx, getHead(&ctx->reqs).locpid, &reply)) {
							//fprintf(stderr, "Child %d: failed to Send CS_REPLY message\n", ctx->locpid);
							return 2;
						}
					}
				}
				break;
			case CS_REPLY:
                if (!rep_arr[ctx->msg_sender]) {
                    if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                    ++lamport_time;
                    rep_arr[ctx->msg_sender] = 1;
                    ++replies;
                }
				break;
			case CS_RELEASE:
				if (ctx->mutexl) {
					if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
					++lamport_time;
					popHead(&ctx->reqs);
                    local_id next = getHead(&ctx->reqs).locpid;
                    if (next > 0 && next != ctx->locpid) {
                        ++lamport_time;
                        Message reply;
                        reply.s_header.s_magic = MESSAGE_MAGIC;
                        reply.s_header.s_type = CS_REPLY;
                        reply.s_header.s_payload_len = 0;
                        reply.s_header.s_local_time = get_lamport_time();
                        if (send(ctx, next, &reply)) {
                            //fprintf(stderr, "Child %d: failed to sEnd CS_REPLY message\n", ctx->locpid);
                            return 3;
                        }
                    }
				}
				break;
			case DONE:
				if (ctx->num_done < ctx->children) {
					if (!ctx->rec_done[ctx->msg_sender]) {
						if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
						++lamport_time;
						ctx->rec_done[ctx->msg_sender] = 1;
						++ctx->num_done;
						if (ctx->num_done == ctx->children) {
							printf(log_received_all_done_fmt, get_lamport_time(), ctx->locpid);
							fprintf(ctx->events, log_received_all_done_fmt, get_lamport_time(), ctx->locpid);
						}
						if (!rep_arr[ctx->msg_sender]) {
                            rep_arr[ctx->msg_sender] = 1;
                            ++replies;
                        }
					}
				}
				break;
			default: break;
		}
	}
	return 0;
}

int release_cs(const void * self) {
    struct Context *ctx = (struct Context*)self;
    popHead(&ctx->reqs);
    ++lamport_time;
    Message release;
    release.s_header.s_magic = MESSAGE_MAGIC;
    release.s_header.s_type = CS_RELEASE;
    release.s_header.s_payload_len = 0;
    release.s_header.s_local_time = get_lamport_time();
    if (send_multicast(ctx, &release)) {
        //fprintf(stderr, "Child %d: failed to send CS_RELEASE message\n", ctx->locpid);
        return 1;
    }
    local_id next = getHead(&ctx->reqs).locpid;
    if (next > 0) {
        ++lamport_time;
        Message reply;
        reply.s_header.s_magic = MESSAGE_MAGIC;
        reply.s_header.s_type = CS_REPLY;
        reply.s_header.s_payload_len = 0;
        reply.s_header.s_local_time = get_lamport_time();
        if (send(ctx, next, &reply)) {
            //fprintf(stderr, "Child %d: failed to seNd CS_REPLY message\n", ctx->locpid);
            return 2;
        }
    }
    return 0;
}

int main(int argc, char * argv[]) {
    struct Context ctx;
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [--mutexl] -p N [--mutexl]\n", argv[0]);
        return 1;
    }
    ctx.mutexl = 0;
    int8_t rp = 0;
    for (int i = 1; i < argc; ++i) {
        if (rp) {
            ctx.children = atoi(argv[i]);
            rp = 0;
        }
        if (strcmp(argv[i], "--mutexl") == 0) ctx.mutexl = 1;
        else if (strcmp(argv[i], "-p") == 0) rp = 1;
    }
    if (initPipes(&ctx.pipes, ctx.children+1, O_NONBLOCK, pipes_log)) {
        fputs("Parent: failed to create pipes\n", stderr);
        return 2;
    }
    FILE *evt = fopen(events_log, "w");
    fclose(evt);
    ctx.events = fopen(events_log, "a");
    for (local_id i = 1; i <= ctx.children; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            ctx.locpid = i;
            break;
        }
        if (pid < 0) {
            fprintf(stderr, "Parent: failed to create child process %d\n", i);
            closePipes(&ctx.pipes);
            fclose(ctx.events);
            return 3;
        }
        ctx.locpid = PARENT_ID;
    }
    closeUnusedPipes(&ctx.pipes, ctx.locpid);
    if (ctx.locpid == PARENT_ID) {
        for (local_id i = 1; i <= ctx.children; ++i) ctx.rec_started[i] = 0;
        ctx.num_started = 0;
        for (local_id i = 1; i <= ctx.children; ++i) ctx.rec_done[i] = 0;
        ctx.num_done = 0;
        while (ctx.num_started < ctx.children || ctx.num_done < ctx.children) {
            Message msg;
            while (receive_any(&ctx, &msg)) {}
            switch (msg.s_header.s_type) {
                case STARTED:
                    if (ctx.num_started < ctx.children) {
                        if (!ctx.rec_started[ctx.msg_sender]) {
                            if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                            ++lamport_time;
                            ctx.rec_started[ctx.msg_sender] = 1;
                            ++ctx.num_started;
                            if (ctx.num_started == ctx.children) {
                                printf(log_received_all_started_fmt, get_lamport_time(), ctx.locpid);
                                fprintf(ctx.events, log_received_all_started_fmt, get_lamport_time(), ctx.locpid);
                            }
                        }
                    }
                    break;
                case DONE:
                    if (ctx.num_done < ctx.children) {
                        if (!ctx.rec_done[ctx.msg_sender]) {
                            if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                            ++lamport_time;
                            ctx.rec_done[ctx.msg_sender] = 1;
                            ++ctx.num_done;
                            if (ctx.num_done == ctx.children) {
                                printf(log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                                fprintf(ctx.events, log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                            }
                        }
                    }
                    break;
                default: break;
            }
            fflush(ctx.events);
        }
        for (local_id i = 0; i < ctx.children; ++i) wait(NULL);
    } else {
        clearQueue(&ctx.reqs);
        ++lamport_time;
        Message started;
        started.s_header.s_magic = MESSAGE_MAGIC;
        started.s_header.s_type = STARTED;
        started.s_header.s_local_time = get_lamport_time();
        sprintf(started.s_payload, log_started_fmt, get_lamport_time(), ctx.locpid, getpid(), getppid(), 0);
        started.s_header.s_payload_len = strlen(started.s_payload);
        puts(started.s_payload);
        fputs(started.s_payload, ctx.events);
        if (send_multicast(&ctx, &started)) {
            fprintf(stderr, "Child %d: failed to send STARTED message\n", ctx.locpid);
            closePipes(&ctx.pipes);
            fclose(ctx.events);
            return 4;
        }
        for (local_id i = 1; i <= ctx.children; ++i) ctx.rec_started[i] = (i == ctx.locpid);
        ctx.num_started = 1;
        for (local_id i = 1; i <= ctx.children; ++i) ctx.rec_done[i] = 0;
        ctx.num_done = 0;
        int8_t active = 1;
        while (active || ctx.num_done < ctx.children) {
            Message msg;
            while (receive_any(&ctx, &msg)) {}
            switch (msg.s_header.s_type) {
                case STARTED:
                    if (ctx.num_started < ctx.children) {
                        if (!ctx.rec_started[ctx.msg_sender]) {
                            if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                            ++lamport_time;
                            ctx.rec_started[ctx.msg_sender] = 1;
                            ++ctx.num_started;
                            if (ctx.num_started == ctx.children) {
                                printf(log_received_all_started_fmt, get_lamport_time(), ctx.locpid);
                                fprintf(ctx.events, log_received_all_started_fmt, get_lamport_time(), ctx.locpid);
                                for (int16_t i = 1; i <= ctx.locpid * 5; ++i) {
                                    char log[50];
                                    sprintf(log, log_loop_operation_fmt, ctx.locpid, i, ctx.locpid * 5);
                                    if (ctx.mutexl) {
                                        int status = request_cs(&ctx);
                                        if (status) {
                                            fprintf(stderr, "Child %d: request_cs() resulted %d\n", ctx.locpid, status);
                                            closePipes(&ctx.pipes);
                                            fclose(ctx.events);
                                            return 100;
                                        }
                                    }
                                    print(log);
                                    if (ctx.mutexl) {
                                        int status = release_cs(&ctx);
                                        if (status) {
                                            fprintf(stderr, "Child %d: release_cs() resulted %d\n", ctx.locpid, status);
                                            closePipes(&ctx.pipes);
                                            fclose(ctx.events);
                                            return 101;
                                        }
                                    }
                                }
                                ++lamport_time;
                                Message done;
                                done.s_header.s_magic = MESSAGE_MAGIC;
                                done.s_header.s_type = DONE;
                                sprintf(done.s_payload, log_done_fmt, get_lamport_time(), ctx.locpid, 0);
                                done.s_header.s_payload_len = strlen(done.s_payload);
                                done.s_header.s_local_time = get_lamport_time();
                                puts(done.s_payload);
                                fputs(done.s_payload, ctx.events);
                                if (send_multicast(&ctx, &done)) {
                                    fprintf(stderr, "Child %d: failed to send DONE message\n", ctx.locpid);
                                    closePipes(&ctx.pipes);
                                    fclose(ctx.events);
                                    return 5;
                                }
                                ctx.rec_done[ctx.locpid] = 1;
                                ++ctx.num_done;
                                if (ctx.num_done == ctx.children) {
                                    printf(log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                                    fprintf(ctx.events, log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                                }
                                active = 0;
                            }
                        }
                    }
                    break;
                case CS_REQUEST:
                    if (active && ctx.mutexl) {
                        if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                        ++lamport_time;
                        pushRequest(&ctx.reqs, (struct Request){ctx.msg_sender, msg.s_header.s_local_time});
                        if (getHead(&ctx.reqs).locpid == ctx.msg_sender) {
                            ++lamport_time;
                            Message reply;
                            reply.s_header.s_magic = MESSAGE_MAGIC;
                            reply.s_header.s_type = CS_REPLY;
                            reply.s_header.s_payload_len = 0;
                            reply.s_header.s_local_time = get_lamport_time();
                            if (send(&ctx, getHead(&ctx.reqs).locpid, &reply)) {
                                fprintf(stderr, "Child %d: failed to send CS_REPLY message\n", ctx.locpid);
                                closePipes(&ctx.pipes);
                                fclose(ctx.events);
                                return 6;
                            }
                        }
                    }
                    break;
                case CS_RELEASE:
                    if (active && ctx.mutexl) {
                        if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                        ++lamport_time;
                        popHead(&ctx.reqs);
                        local_id next = getHead(&ctx.reqs).locpid;
                        if (next > 0 && next != ctx.locpid) {
                            ++lamport_time;
                            Message reply;
                            reply.s_header.s_magic = MESSAGE_MAGIC;
                            reply.s_header.s_type = CS_REPLY;
                            reply.s_header.s_payload_len = 0;
                            reply.s_header.s_local_time = get_lamport_time();
                            if (send(&ctx, next, &reply)) {
                                fprintf(stderr, "Child %d: failed to send CS_REPLY message\n", ctx.locpid);
                                closePipes(&ctx.pipes);
                                fclose(ctx.events);
                                return 7;
                            }
                        }
                    }
                    break;
                case DONE:
                    if (ctx.num_done < ctx.children) {
                        if (!ctx.rec_done[ctx.msg_sender]) {
                            if (lamport_time < msg.s_header.s_local_time) lamport_time = msg.s_header.s_local_time;
                            ++lamport_time;
                            ctx.rec_done[ctx.msg_sender] = 1;
                            ++ctx.num_done;
                            if (ctx.num_done == ctx.children) {
                                printf(log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                                fprintf(ctx.events, log_received_all_done_fmt, get_lamport_time(), ctx.locpid);
                            }
                        }
                    }
                    break;
                default: break;
            }
            fflush(ctx.events);
        }
    }
    closePipes(&ctx.pipes);
    fclose(ctx.events);
    return 0;
}
