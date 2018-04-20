#ifndef _HIREDIS_OKVM_CONNPOOL_H_
#define _HIREDIS_OKVM_CONNPOOL_H_

#include <uv.h>
#include <hiredis.h>
#include <async.h>
#include "queue.h"

enum{
    OKVM_CMD_STOP = 1,
    // Just leader to do
    OKVM_CMD_CONNECT_SENTINEL,
};

struct redis_okvm_host_info
{
    void *link[2];
    char *ip;
    int port;
};

struct redis_okvm_msg
{
    // Read or Write or inner
    int type;
    int ref_count;
    void *link[2];

    int data_len;
    char data[0];
};
struct redis_okvm_msg * redis_okvm_msg_alloc(int type, const char *data, int len);
void redis_okvm_msg_inc_ref(struct redis_okvm_msg *msg);
void redis_okvm_msg_free(struct redis_okvm_msg *msg);
void reids_okvm_msg_set_reply(struct redis_okvm_msg *msg, redisReply *reply);
int redis_okvm_msg_get_reply(struct redis_okvm_msg *msg);

struct redis_okvm_thread;

struct redis_okvm_msg_queue
{
    struct redis_okvm_thread *okvm_thr;
    void (*handle_msg)(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg);
    void *queue_head[2];
    uv_async_t notify;
    uv_mutex_t queue_mutex;
};

int redis_okvm_msg_queue_init(struct redis_okvm_msg_queue *queue,
    struct redis_okvm_thread *okvm_thr,
    void (*handle_msg)(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg));

int redis_okvm_msg_queue_push(struct redis_okvm_msg_queue *queue, struct redis_okvm_msg *msg);
struct redis_okvm_msg *redis_okvm_msg_queue_pop(struct redis_okvm_msg_queue *queue);
static inline uv_async_t * redis_okvm_msg_queue_get_notify(struct redis_okvm_msg_queue *queue){return &queue->notify;}
void redis_okvm_msg_queue_notify(struct redis_okvm_msg_queue *queue);

enum
{
    OKVM_CLOSED=1,      // 1
    OKVM_CONNECTED,     // 2
};

struct redis_okvm_context
{
    // The redis context which indicate the connection between the hiredis and the redis server.
    redisContext *ctx;
    int bulk_count;
    // The context status
    int state;
    void *link[2];
};

int redis_okvm_context_init(struct redis_okvm_context *ctx);
int redis_okvm_context_fini(struct redis_okvm_context *ctx);
int redis_okvm_context_connect_server(struct redis_okvm_context *ctx, char *ip, int port);
redisReply* redis_okvm_context_execute(struct redis_okvm_context *ctx, const char *cmd);
void* redis_okvm_context_get_reply(struct redis_okvm_context *ctx);
int redis_okvm_context_append(struct redis_okvm_context *ctx, const char *cmd);

struct redis_okvm_thread
{
    // Used to talk with everyone about internal message.
    struct redis_okvm_msg_queue inner_queue;

    uv_loop_t loop;
    uv_thread_t worker;
    uv_mutex_t state_mutex;
    uv_cond_t state_cond;
    int state;
};

int redis_okvm_thread_init(struct redis_okvm_thread *okvm_thr);
int redis_okvm_thread_start(struct redis_okvm_thread *okvm_thr);
int redis_okvm_thread_stop(struct redis_okvm_thread *okvm_thr);
int redis_okvm_thread_push(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg);

struct redis_okvm_pool
{
    int ctx_count;
    void *ctx_head[2];

    uv_mutex_t mutex;
    uv_cond_t cond;
};

int redis_okvm_pool_init(struct redis_okvm_pool *pool);
int redis_okvm_pool_create(struct redis_okvm_pool *pool, struct redis_okvm_host_info *host, int conns);
int redis_okvm_pool_push(struct redis_okvm_pool *pool, struct redis_okvm_context *ctx);
struct redis_okvm_context *redis_okvm_pool_pop(struct redis_okvm_pool *pool);
struct redis_okvm_context *redis_okvm_pool_timed_pop(struct redis_okvm_pool *pool, int timeout);
int redis_okvm_pool_fini(struct redis_okvm_pool *pool);

struct redis_okvm_mgr
{
    int threads_nr;
    struct redis_okvm_thread **threads;
    struct redis_okvm_pool read_pool;
    struct redis_okvm_pool write_pool;
    // The next two member just used by leader
    // The slave host info used for read
    void *slaves_head[2];
    // The master host info used for write
    struct redis_okvm_host_info master;
};

int redis_okvm_mgr_init(struct redis_okvm_mgr *mgr, int thr_num);
int redis_okvm_mgr_fini(struct redis_okvm_mgr *mgr);
int redis_okvm_mgr_parse_slaves_or_sentinels(struct redis_okvm_mgr *okvm, redisReply *reply);
int redis_okvm_mgr_get_master(void *data, char *ip, int port);
int redis_okvm_mgr_get_slaves(void *data, char *ip, int port);
int redis_okvm_mgr_get_replicas(void *data, 
        char *host_str,
        int (*fn)(void *data, char *ip, int port));
int redis_okvm_mgr_init_sentinel(struct redis_okvm_mgr *okvm);
int redis_okvm_mgr_broadcast(struct redis_okvm_mgr *okvm, struct redis_okvm_host_info *host, int cmd);

#endif//_HIREDIS_OKVM_CONNPOOL_H_

