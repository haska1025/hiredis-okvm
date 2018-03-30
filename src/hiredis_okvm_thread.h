#ifndef _HIREDIS_OKVM_CONNPOOL_H_
#define _HIREDIS_OKVM_CONNPOOL_H_

#include <uv.h>
#include <hiredis.h>
#include "queue.h"

#define LEADER 1
#define FOLLOWER 2

#define HIREDIS_OKVM_MSG_INNER 1
#define HIREDIS_OKVM_MSG_READ 2
#define HIREDIS_OKVM_MSG_WRITE 3

enum{
    OKVM_INNER_CMD_STOP = 1,
    // wirter to do
    OKVM_INNER_CMD_CONNECT_SLAVE,
    // Reader to do
    OKVM_INNER_CMD_CONNECT_MASTER,
    // Just leader to do
    OKVM_INNER_CMD_CONNECT_SENTINEL,
};

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

    // Used to thread pool link list
    void *link[2];
    // Just used by leader
    void *slaves_head[2];
    // The master host info used for write
    struct hiredis_okvm_host_info master;
    // The slave host info used for read
    struct hiredis_okvm_host_info slave;
    // Used to talk with everyone about internal message.
    struct hiredis_okvm_msg_queue inner_queue;
    // The write message queue
    struct hiredis_okvm_msg_queue write_queue;
    // The read message queue
    struct hiredis_okvm_msg_queue read_queue;

    uv_loop_t loop;
    uv_thread_t worker;
    uv_mutex_t state_mutex;
    uv_cond_t state_cond;
    int state;
};

#define INIT_HIREDIS_OKVM_THREAD(okvm_thr) \
    do{\
        (okvm_thr)->write_ctx = NULL;\
        (okvm_thr)->read_ctx = NULL;\
        (okvm_thr)->role = 0;\
        QUEUE_INIT(&(okvm_thr)->link);\
        QUEUE_INIT(&(okvm_thr)->slaves_head);\
        uv_mutex_init(&(okvm_thr)->state_mutex);\
        uv_cond_init(&(okvm_thr)->state_cond);\
        (okvm_thr)->state = 0;\
    }while(0);



int hiredis_okvm_thread_start(struct hiredis_okvm_thread *okvm_thr);
int hiredis_okvm_thread_stop(struct hiredis_okvm_thread *okvm_thr);
int hiredis_okvm_thread_push(struct hiredis_okvm_thread *okvm_thr, struct hiredis_okvm_msg *msg);

struct hiredis_okvm_thread_pool
{
    void *thr_head[2];
    QUEUE *cur_thr;
    uv_mutex_t mutex;
};

int hiredis_okvm_thread_pool_init(struct hiredis_okvm_thread_pool *thr_pool);
int hiredis_okvm_thread_pool_push(struct hiredis_okvm_thread_pool *thr_pool, struct hiredis_okvm_msg *msg);
int hiredis_okvm_thread_pool_fini(struct hiredis_okvm_thread_pool *thr_pool);
#endif//_HIREDIS_OKVM_CONNPOOL_H_

