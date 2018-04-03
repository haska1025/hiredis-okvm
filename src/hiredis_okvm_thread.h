#ifndef _HIREDIS_OKVM_CONNPOOL_H_
#define _HIREDIS_OKVM_CONNPOOL_H_

#include <uv.h>
#include <hiredis.h>
#include <async.h>
#include "queue.h"

#define LEADER 1
#define FOLLOWER 2

enum{
    OKVM_INNER_CMD_STOP = 1,
    // wirter to do
    OKVM_INNER_CMD_CONNECT_SLAVE,
    // Reader to do
    OKVM_INNER_CMD_CONNECT_MASTER,
    // Just leader to do
    OKVM_INNER_CMD_CONNECT_SENTINEL,
    // External message
    OKVM_EXTERNAL_CMD_READ = 10001,
    OKVM_EXTERNAL_CMD_WRITE,
};

struct hiredis_okvm_host_info
{
    void *link[2];
    char *ip;
    int port;
};

struct hiredis_okvm_msg
{
    // Read or Write or inner
    int type;
    void *link[2];
    int data_len;
    char data[0];
};
struct hiredis_okvm_msg * hiredis_okvm_msg_alloc(int type, char *data, int len);
void hiredis_okvm_msg_free(struct hiredis_okvm_msg *msg);

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

enum
{
    OKVM_CLOSED=1,
    OKVM_CONNECTING,
    OKVM_CONNECTED,
    OKVM_AUTH,
    OKVM_CHECK_ROLE,
    OKVM_ESTABLISHED
};

struct hiredis_okvm_thread;
struct hiredis_okvm_async_context
{
    // The redis context which indicate the connection between the hiredis and the redis server.
    redisAsyncContext *ctx;
    // The context status
    int state;
    struct hiredis_okvm_thread *okvm_thr;
};

int hiredis_okvm_async_context_init(struct hiredis_okvm_async_context *async_ctx, struct hiredis_okvm_thread *thr);
int hiredis_okvm_async_context_fini(struct hiredis_okvm_async_context *async_ctx);
int hiredis_okvm_async_context_connect(struct hiredis_okvm_async_context *async_ctx);
int hiredis_okvm_async_context_auth(struct hiredis_okvm_async_context *async_ctx);
int hiredis_okvm_async_context_check_role(struct hiredis_okvm_async_context *async_ctx);
int hiredis_okvm_async_context_execute(struct hiredis_okvm_async_context *async_ctx, struct hiredis_okvm_msg *msg);


struct hiredis_okvm_thread
{
    struct hiredis_okvm_async_context read_ctx;
    struct hiredis_okvm_async_context write_ctx;
    int role;

    // Used to read thread pool link list
    void *read_link[2];
    // Used to write thread pool link list
    void *write_link[2];

    // The next two member just used by leader
    // The slave host info used for read
    void *slaves_head[2];
    // The master host info used for write
    struct hiredis_okvm_host_info master;

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

int hiredis_okvm_thread_init(struct hiredis_okvm_thread *okvm_thr);
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

