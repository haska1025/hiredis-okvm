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
    int ref_count;
    void *link[2];
    redisReply *reply;
    uv_mutex_t msg_mutex;
    uv_cond_t msg_cond;
    int data_len;
    char data[0];
};
struct hiredis_okvm_msg * hiredis_okvm_msg_alloc(int type, const char *data, int len);
void hiredis_okvm_msg_inc_ref(struct hiredis_okvm_msg *msg);
void hiredis_okvm_msg_free(struct hiredis_okvm_msg *msg);
void hireids_okvm_msg_set_reply(struct hiredis_okvm_msg *msg, redisReply *reply);
void *hiredis_okvm_msg_get_reply(struct hiredis_okvm_msg *msg);

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
int hiredis_okvm_async_context_connect(struct hiredis_okvm_async_context *async_ctx, char *ip, int port);
void hiredis_okvm_async_context_auth(struct hiredis_okvm_async_context *async_ctx);
void hiredis_okvm_async_context_check_role(struct hiredis_okvm_async_context *async_ctx);
int hiredis_okvm_async_context_execute(struct hiredis_okvm_async_context *async_ctx, struct hiredis_okvm_msg *msg);


struct hiredis_okvm_thread
{
    struct hiredis_okvm_async_context read_ctx;
    struct hiredis_okvm_async_context write_ctx;
    int role;

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

struct hiredis_okvm_send_policy
{
    struct hiredis_okvm_thread **threads;
    int max_len;
    int cur_idx;
    uv_mutex_t mutex;
};

int hiredis_okvm_send_policy_init(struct hiredis_okvm_send_policy *policy);
int hiredis_okvm_send_policy_send(struct hiredis_okvm_send_policy *policy, struct hiredis_okvm_msg *msg);
int hiredis_okvm_send_policy_fini(struct hiredis_okvm_send_policy *policy);

struct hiredis_okvm_mgr
{
    int threads_nr;
    struct hiredis_okvm_thread **threads;
    struct hiredis_okvm_send_policy read_policy;
    struct hiredis_okvm_send_policy write_policy;
    // The next two member just used by leader
    // The slave host info used for read
    void *slaves_head[2];
    // The master host info used for write
    struct hiredis_okvm_host_info master;
};

int hiredis_okvm_mgr_init(struct hiredis_okvm_mgr *mgr, int thr_num);
int hiredis_okvm_mgr_fini(struct hiredis_okvm_mgr *mgr);
int hiredis_okvm_mgr_parse_slaves_or_sentinels(struct hiredis_okvm_mgr *okvm, redisReply *reply);
int hiredis_okvm_mgr_get_master(void *data, char *ip, int port);
int hiredis_okvm_mgr_get_slaves(void *data, char *ip, int port);
int hiredis_okvm_mgr_get_replicas(void *data, 
        char *host_str,
        int (*fn)(void *data, char *ip, int port));
struct hiredis_okvm_msg * hiredis_okvm_mgr_create_inner_msg(struct hiredis_okvm_host_info *host, int cmd);
int hiredis_okvm_mgr_init_sentinel(struct hiredis_okvm_mgr *okvm);
int hiredis_okvm_mgr_broadcast(struct hiredis_okvm_mgr *okvm, struct hiredis_okvm_msg *msg);


#endif//_HIREDIS_OKVM_CONNPOOL_H_

