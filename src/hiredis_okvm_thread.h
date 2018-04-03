#ifndef _HIREDIS_OKVM_CONNPOOL_H_
#define _HIREDIS_OKVM_CONNPOOL_H_

#include <uv.h>
#include <hiredis.h>

struct hiredis_okvm_host_info
{
    void *link[2];
    char *ip;
    int port;
};

struct hiredis_okvm_msg
{
    void *link[2];
    char *msg;
    // Read or Write
    int type;
};
struct hiredis_okvm_msg_queue
{
    void *queue_head[2];
    uv_async_t notify;
    uv_mutex_t queue_mutex;
};

int hiredis_okvm_msg_queue_init(struct hiredis_okvm_msg_queue *queue);
int hiredis_okvm_msg_queue_push(struct hiredis_okvm_msg_queue *queue, struct hiredis_okvm_msg *msg);
struct hiredis_okvm_msg *hiredis_okvm_msg_queue_pop(struct hiredis_okvm_msg_queue *queue);
static inline uv_async_t * hiredis_okvm_msg_queue_get_notify(struct hiredis_okvm_msg_queue *queue){return &queue->notify;}

struct hiredis_okvm_thread
{
    // The redis context which indicate the connection between the hiredis and the redis server.
    redisContext *write_ctx;
    redisContext *read_ctx;
    int role;

    // Just used by leader
    void *slaves_head[2];
    // The master host info used for write
    struct hiredis_okvm_host_info master;
    // The slave host info used for read
    struct hiredis_okvm_host_info slave;

    uv_loop_t loop;
    uv_thread_t worker;
    uv_mutex_t state_mutex;
    uv_cond_t state_cond;
    uv_async_t notify;
    int state;
};

#define INIT_HIREDIS_OKVM_THREAD(okvm_thr) \
    do{\
        (okvm_thr)->write_ctx = NULL;\
        (okvm_thr)->read_ctx = NULL;\
        uv_mutex_init(&(okvm_thr)->state_mutex);\
        uv_cond_init(&(okvm_thr)->state_cond);\
        (okvm_thr)->state = 0;\
    }while(0);



int hiredis_okvm_thread_start(struct hiredis_okvm_thread *okvm_thr);
int hiredis_okvm_thread_stop(struct hiredis_okvm_thread *okvm_thr);

#endif//_HIREDIS_OKVM_CONNPOOL_H_

