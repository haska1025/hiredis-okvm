// The system hdr file
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <adapters/libuv.h>
// Myself hdr file
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"
#include "hiredis_okvm.h"

extern struct hiredis_okvm g_okvm;
extern struct hiredis_okvm_mgr g_mgr;

#define DEFAULT_CMD_LEN 512

// Reference:https://dzone.com/articles/high-availability-with-redis-sentinels-connecting

/***************************** The message ***************************/
struct hiredis_okvm_msg * hiredis_okvm_msg_alloc(int type, char *data, int len)
{
    struct hiredis_okvm_msg *msg = malloc(sizeof(struct hiredis_okvm_msg) + len);
    QUEUE_INIT(&msg->link);
    msg->type = type;
    if (data){
        msg->data_len = len;
        memcpy(msg->data, data, len);
    }else{
        msg->data_len = 0;
    }
    return msg;
}

void hiredis_okvm_msg_free(struct hiredis_okvm_msg *msg)
{
    free(msg);
}

/**************************** The message queue ***************************/
int hiredis_okvm_msg_queue_init(struct hiredis_okvm_msg_queue *queue)
{
    QUEUE_INIT(&queue->queue_head);
    uv_mutex_init(&queue->queue_mutex);
    return 0;
}
int hiredis_okvm_msg_queue_push(struct hiredis_okvm_msg_queue *queue, struct hiredis_okvm_msg *msg)
{
    uv_mutex_lock(&queue->queue_mutex);
    QUEUE_INSERT_TAIL(&queue->queue_head, &msg->link);
    uv_mutex_unlock(&queue->queue_mutex);

    return 0;
}
struct hiredis_okvm_msg *hiredis_okvm_msg_queue_pop(struct hiredis_okvm_msg_queue *queue)
{
    struct hiredis_okvm_msg *msg = NULL;
    QUEUE *ptr = NULL;

    uv_mutex_lock(&queue->queue_mutex);
    if (!QUEUE_EMPTY(&queue->queue_head)){
        ptr = QUEUE_HEAD(&queue->queue_head);
        msg = QUEUE_DATA(ptr, struct hiredis_okvm_msg, link);
    }
    uv_mutex_unlock(&queue->queue_mutex);

    return msg;
}

/******************************* The redis async context *********************/
static void hiredis_okvm_async_cmd_callback(redisAsyncContext *c, void *r, void *privdata)
{
}

static void authCallback(redisAsyncContext *c, void *r, void *privdata)
{
    redisReply *reply = r;
    struct hiredis_okvm_async_context *async_ctx = privdata;
    if (!reply){
        hiredis_okvm_async_context_fini(async_ctx);
    }else{
        if (strcmp(reply->str, "OK") != 0){
            hiredis_okvm_async_context_fini(async_ctx);
        }else{
            hiredis_okvm_async_context_check_role(async_ctx);
        }
        freeReplyObject(reply);
        reply = NULL;
    }
}
void hiredis_okvm_async_context_check_role_callback(redisAsyncContext *c, void *r, void *privdata)
{
    redisReply *reply = r;
    struct hiredis_okvm_async_context *async_ctx = privdata;

    if (!reply){
        hiredis_okvm_async_context_fini(async_ctx);
    }else{
        if(strcmp("master", reply->element[0]->str) == 0
         || strcmp("slave", reply->element[0]->str) == 0){
            async_ctx->state = OKVM_ESTABLISHED;
        }else{
            hiredis_okvm_async_context_fini(async_ctx);
        }
    }
}
static void connectCallback(const redisAsyncContext *c, int status)
{
    // If connection is ok, ready to auth or check role
    struct hiredis_okvm_async_context *async_ctx = c->data;
    if (status != REDIS_OK){
        hiredis_okvm_async_context_fini(async_ctx);
    }else{
        async_ctx->state = OKVM_CONNECTED;
        hiredis_okvm_async_context_auth(async_ctx);
    }
}
static void disconnectCallback(const redisAsyncContext *c, int status)
{
    hiredis_okvm_async_context_fini((struct hiredis_okvm_async_context*)c->data);
}
int hiredis_okvm_async_context_init(struct hiredis_okvm_async_context *async_ctx, struct hiredis_okvm_thread *thr)
{
    async_ctx->ctx = NULL;
    async_ctx->state = OKVM_CLOSED;
    async_ctx->okvm_thr = thr;
    return 0;
}
int hiredis_okvm_async_context_fini(struct hiredis_okvm_async_context *async_ctx)
{
    async_ctx->state = OKVM_CLOSED;
    if (async_ctx->ctx){
        // Need call ?
        // redisAsyncDisconnect(async_ctx->ctx);
        redisAsyncFree(async_ctx->ctx);
        async_ctx->ctx = NULL;
    }
    return 0;
}
int hiredis_okvm_async_context_connect(struct hiredis_okvm_async_context *async_ctx, char *ip, int port)
{
    redisAsyncContext *ctx = NULL;

    if (async_ctx->state == OKVM_CLOSED){
        ctx = redisAsyncConnect(ip, port);
        if (!ctx || ctx->err){
            HIREDIS_OKVM_LOG_ERROR("Connect to redis(%s:%d) failed(%s)", ip, port, ctx?ctx->errstr:"No error reason");
            if (ctx)redisAsyncFree(ctx);

            return -1;
        }

        redisLibuvAttach(ctx, &async_ctx->okvm_thr->loop);
        redisAsyncSetConnectCallback(ctx, connectCallback);
        redisAsyncSetDisconnectCallback(ctx,disconnectCallback);
        async_ctx->ctx = ctx;
        async_ctx->state = OKVM_CONNECTING;
        ctx->data = async_ctx;
    }

    return 0;
}
void hiredis_okvm_async_context_auth(struct hiredis_okvm_async_context *async_ctx)
{
    if (async_ctx->state == OKVM_CONNECTED){
        // do auth
        if (g_okvm.password){
            int rc = REDIS_OK;
            char cmd[DEFAULT_CMD_LEN];
            snprintf(cmd, DEFAULT_CMD_LEN, "AUTH %s", g_okvm.password);
            rc = redisAsyncCommand(async_ctx->ctx, authCallback, async_ctx, cmd);
            if (rc != REDIS_OK){ 
                hiredis_okvm_async_context_fini(async_ctx);
            }else{
                async_ctx->state = OKVM_AUTH;
            }
        }else{
            hiredis_okvm_async_context_check_role(async_ctx);
        }
    }
}
void hiredis_okvm_async_context_check_role(struct hiredis_okvm_async_context *async_ctx)
{
    if (async_ctx->state == OKVM_CONNECTED || async_ctx->state == OKVM_AUTH){
        // Check role
        int rc = redisAsyncCommand(async_ctx->ctx, hiredis_okvm_async_context_check_role_callback, async_ctx, "ROLE");
        if (rc != REDIS_OK){
            hiredis_okvm_async_context_fini(async_ctx);
        }else{
            async_ctx->state = OKVM_CHECK_ROLE;
        }
    }
}
int hiredis_okvm_async_context_execute(struct hiredis_okvm_async_context *async_ctx, struct hiredis_okvm_msg *msg)
{
    return redisAsyncCommand(async_ctx->ctx, hiredis_okvm_async_cmd_callback, msg, msg->data);
}

/******************************* The redis host information *********************/
// The timeout unit is milliseconds.
static redisContext *hiredis_okvm_thread_connect(char *ip, int port, int timeout)
{
    redisContext* ctx = NULL;
    struct timeval tv_timeout;

    tv_timeout.tv_sec = timeout/1000;
    tv_timeout.tv_usec = (timeout%1000)*1000;
    ctx = redisConnectWithTimeout(ip, port, tv_timeout);
    if (!ctx || ctx->err){
        HIREDIS_OKVM_LOG_ERROR("Connect to sentinel(%s:%d) failed(%s)", ip, port, ctx?ctx->errstr:"No error reason");
        if (ctx)redisFree(ctx);

        return NULL;
    }
    return ctx;
}

static int hiredis_okvm_thread_connect_master(void *data, char *ip, int port)
{
    struct hiredis_okvm_thread *thr = data;
    return hiredis_okvm_async_context_connect(&thr->write_ctx, ip, port);
}

static int hiredis_okvm_thread_connect_slave(void *data, char *ip, int port)
{
    struct hiredis_okvm_thread *thr = data;
    return hiredis_okvm_async_context_connect(&thr->read_ctx, ip, port);
}

static void hiredis_okvm_thread_process_inner_msg(struct hiredis_okvm_thread *okvm_thr, struct hiredis_okvm_msg *msg)
{
    if (msg->type == OKVM_INNER_CMD_STOP){
        okvm_thr->state = 0;
        uv_stop(&okvm_thr->loop);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_SLAVE){
        hiredis_okvm_mgr_get_replicas(okvm_thr, msg->data, hiredis_okvm_thread_connect_slave);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_MASTER){
        hiredis_okvm_mgr_get_replicas(okvm_thr, msg->data, hiredis_okvm_thread_connect_master);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_SENTINEL){
        hiredis_okvm_mgr_init_sentinel(&g_mgr);
    }
}
static void hiredis_okvm_thread_inner_async_cb(uv_async_t *handle)
{
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)handle->data;
    struct hiredis_okvm_msg *msg = NULL;

    while(1){
        msg = hiredis_okvm_msg_queue_pop(&okvm_thr->inner_queue); 
        if (!msg){
            break;
        }
        hiredis_okvm_thread_process_inner_msg(okvm_thr, msg);
        hiredis_okvm_msg_free(msg);
        msg = NULL;
    }
}

static void hiredis_okvm_thread_read_async_cb(uv_async_t *handle)
{
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)handle->data;
}

static void hiredis_okvm_thread_write_async_cb(uv_async_t *handle)
{
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)handle->data;
}
static void hiredis_okvm_thr_svc(void *arg)
{
    uv_async_t *notify = NULL;
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)arg;

    HIREDIS_OKVM_LOG_INFO("Enter the okvm thread(%p)", arg);

    if (0 != uv_loop_init(&okvm_thr->loop)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread failed");
        return;
    }
    // Register inner message queue
    notify = hiredis_okvm_msg_queue_get_notify(&okvm_thr->inner_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, hiredis_okvm_thread_inner_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init inner async failed");
        return;
    }
    // Register read message queue
    notify = hiredis_okvm_msg_queue_get_notify(&okvm_thr->read_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, hiredis_okvm_thread_read_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init read async failed");
        return;
    }
    // Register write message queue
    notify = hiredis_okvm_msg_queue_get_notify(&okvm_thr->write_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, hiredis_okvm_thread_write_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init inner async failed");
        return;
    }

    uv_mutex_lock(&okvm_thr->state_mutex);
    okvm_thr->state = 1;
    uv_mutex_unlock(&okvm_thr->state_mutex);
    uv_cond_broadcast(&okvm_thr->state_cond);

    uv_run(&okvm_thr->loop, UV_RUN_DEFAULT);
    uv_loop_close(&okvm_thr->loop);

    HIREDIS_OKVM_LOG_INFO("Leave the okvm thread(%p)", arg);
}

int hiredis_okvm_thread_init(struct hiredis_okvm_thread *okvm_thr)
{
    hiredis_okvm_async_context_init(&okvm_thr->write_ctx, okvm_thr);
    hiredis_okvm_async_context_init(&okvm_thr->read_ctx, okvm_thr);
    okvm_thr->role = 0;

    hiredis_okvm_msg_queue_init(&okvm_thr->inner_queue);
    hiredis_okvm_msg_queue_init(&okvm_thr->read_queue);
    hiredis_okvm_msg_queue_init(&okvm_thr->write_queue);
    uv_mutex_init(&okvm_thr->state_mutex);
    uv_cond_init(&okvm_thr->state_cond);
    okvm_thr->state = 0;

    return 0;
}

int hiredis_okvm_thread_start(struct hiredis_okvm_thread *okvm_thr)
{
    // Create thread
    int rc = 0;

    if (okvm_thr->state == 1)
        return 0;

    uv_mutex_lock(&okvm_thr->state_mutex);
    if (okvm_thr->state == 1){
        uv_mutex_unlock(&okvm_thr->state_mutex);
        return 0;
    }
    uv_mutex_unlock(&okvm_thr->state_mutex);

    rc = uv_thread_create(&okvm_thr->worker, hiredis_okvm_thr_svc, (void*)okvm_thr); 
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("Start iothread failed rc(%d)", rc);
        return rc;
    }

    uv_mutex_lock(&okvm_thr->state_mutex);
    while( okvm_thr->state != 1){
        rc = uv_cond_timedwait(&okvm_thr->state_cond, &okvm_thr->state_mutex, 150 * 1e6);
        if (rc != 0){
            break;
        }
    }
    uv_mutex_unlock(&okvm_thr->state_mutex);

    return rc;
}

int hiredis_okvm_thread_stop(struct hiredis_okvm_thread *okvm_thr)
{
    struct hiredis_okvm_msg *msg = NULL;

    uv_mutex_lock(&okvm_thr->state_mutex);
    okvm_thr->state = 0;
    uv_mutex_unlock(&okvm_thr->state_mutex);

    msg = hiredis_okvm_msg_alloc(OKVM_INNER_CMD_STOP, NULL, 0);
    hiredis_okvm_thread_push(okvm_thr, msg);
    uv_thread_join(&okvm_thr->worker);
    return 0;
}

int hiredis_okvm_thread_push(struct hiredis_okvm_thread *okvm_thr, struct hiredis_okvm_msg *msg)
{
    if (msg->type == OKVM_EXTERNAL_CMD_READ){
        return hiredis_okvm_msg_queue_push(&okvm_thr->read_queue, msg);
    }else if (msg->type == OKVM_EXTERNAL_CMD_WRITE){
        return hiredis_okvm_msg_queue_push(&okvm_thr->write_queue, msg);
    }else{
        return hiredis_okvm_msg_queue_push(&okvm_thr->inner_queue, msg);
    }
}

int hiredis_okvm_send_policy_init(struct hiredis_okvm_send_policy *policy)
{
    policy->threads = NULL;
    policy->cur_idx = 0;
    policy->max_len = 0;
    uv_mutex_init(&policy->mutex);
    return 0;
}
int hiredis_okvm_send_policy_push(struct hiredis_okvm_send_policy *policy, struct hiredis_okvm_msg *msg)
{
    struct hiredis_okvm_thread *thr = NULL;

    uv_mutex_lock(&policy->mutex);
    thr = policy->threads[policy->cur_idx++];
    policy->cur_idx %= policy->max_len;
    uv_mutex_unlock(&policy->mutex);

    return hiredis_okvm_thread_push(thr, msg);
}
int hiredis_okvm_send_policy_fini(struct hiredis_okvm_send_policy *policy)
{
    return 0;
}
/********************************* The okvm mgr **************************************/
int hireids_okvm_mgr_init(struct hiredis_okvm_mgr *mgr, int thr_num)
{
    int rc = 0;
    int i = 0;

    mgr->threads_nr = thr_num;
    mgr->threads = malloc(sizeof(struct hiredis_okvm_thread*) * thr_num);
    if (!mgr->threads)
        return -1;

    for (i = 0; i < thr_num; ++i) {
        mgr->threads[i] = NULL;
    }

    for (i = 0; i < thr_num; ++i) {
        mgr->threads[i] = malloc(sizeof(struct hiredis_okvm_thread));
        rc = hiredis_okvm_thread_init(mgr->threads[i]);
        if (rc != 0)
            return rc;
    }

    hiredis_okvm_send_policy_init(&mgr->read_policy);
    hiredis_okvm_send_policy_init(&mgr->write_policy);

    QUEUE_INIT(&mgr->slaves_head);
    return hiredis_okvm_mgr_init_sentinel(mgr);
}
int hiredis_okvm_mgr_fini(struct hiredis_okvm_mgr *mgr)
{
    int i = 0;

    for (i = 0; i < mgr->threads_nr; ++i) {
        free(mgr->threads[i]);
        mgr->threads[i] = NULL;
    }

    free(mgr->threads);
    mgr->threads = NULL;

    return 0;
}

struct hiredis_okvm_msg * hiredis_okvm_mgr_create_inner_msg(struct hiredis_okvm_host_info *host, int cmd)
{
    struct hiredis_okvm_msg *msg = NULL;
    // The type for port saved is int, the maximum length for the decimal digit int is less than 11.
    int host_len = strlen(host->ip) + 12;
    msg = hiredis_okvm_msg_alloc(cmd, NULL, host_len);
    snprintf(msg->data, host_len, "%s:%d", host->ip, host->port);

    return msg;
}

int hiredis_okvm_mgr_broadcast(struct hiredis_okvm_mgr *okvm, struct hiredis_okvm_msg *msg)
{
    int i = 0;
    for (; i < okvm->threads_nr; ++i){
        hiredis_okvm_thread_push(okvm->threads[i], msg);
    }
    return 0;
}
int hiredis_okvm_mgr_init_sentinel(struct hiredis_okvm_mgr *okvm)
{
    int rc = 0;
    struct hiredis_okvm_msg *msg = NULL;
    QUEUE *qptr = NULL;
    struct hiredis_okvm_host_info *hi = NULL;
    char *host_str = strdup(g_okvm.redis_host);

    rc = hiredis_okvm_mgr_get_replicas(okvm, host_str, hiredis_okvm_mgr_get_master);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("No master exist. the sentinel(%s", host_str);
        free(host_str);
        return -1;
    }
    rc = hiredis_okvm_mgr_get_replicas(okvm, host_str, hiredis_okvm_mgr_get_slaves);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("No slave exist. the sentinel(%s", host_str);
    }
    free(host_str);

    // Dispatch master and slave ip to everyone.
    msg = hiredis_okvm_mgr_create_inner_msg(&okvm->master, OKVM_INNER_CMD_CONNECT_MASTER);
    hiredis_okvm_mgr_broadcast(okvm, msg);

    QUEUE_FOREACH(qptr, &okvm->slaves_head){
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
        msg = hiredis_okvm_mgr_create_inner_msg(hi, OKVM_INNER_CMD_CONNECT_SLAVE);
        hiredis_okvm_mgr_broadcast(okvm, msg);
    }
    return rc;
}

int hiredis_okvm_mgr_parse_slaves_or_sentinels(struct hiredis_okvm_mgr *okvm, redisReply *reply)
{
    int rc = 0;
    int i = 0;
    for (; i < reply->elements; ++i){
        int j = 0;
        redisReply *item = reply->element[i];
        struct hiredis_okvm_host_info *hi = malloc(sizeof(struct hiredis_okvm_host_info));
        QUEUE_INIT(&hi->link);
        for (; j < item->elements; ++j){
            if (strcmp(item->element[j]->str, "ip") == 0){
                // Get value
                ++j;
                hi->ip = strdup(item->element[j]->str);
            }else if (strcmp(item->element[j]->str, "port") == 0){
                // Get value
                ++j;
                hi->port = atoi(item->element[j]->str);
            }else{
                // Ignore the value
                ++j;
            }
        }

        QUEUE_INSERT_TAIL(&okvm->slaves_head, &hi->link);
    }

    return rc;
}
int hiredis_okvm_mgr_get_master(void *data, char *ip, int port)
{
    int rc = -1;
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    char cmd[DEFAULT_CMD_LEN];
    struct hiredis_okvm_mgr *okvm = data;
    // Wait for 500ms
    ctx = hiredis_okvm_thread_connect(ip, port, 500); 
    if (!ctx)
        return -1;

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL get-master-addr-by-name %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        okvm->master.ip = strdup(reply->element[0]->str);
        okvm->master.port = atoi(reply->element[1]->str);
    }
    freeReplyObject(reply);
    return rc;
}

int hiredis_okvm_mgr_get_slaves(void *data, char *ip, int port)
{
    int rc = -1;
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    struct timeval timeout;
    char cmd[DEFAULT_CMD_LEN];
    char *host = NULL;
    struct hiredis_okvm_mgr *okvm = data;
    // Wait for 500ms
    ctx = hiredis_okvm_thread_connect(ip, port, 500); 
    if (!ctx)
        return -1;

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL slaves %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        hiredis_okvm_mgr_parse_slaves_or_sentinels(okvm, reply);
        rc = 0;
    }

    freeReplyObject(reply);
    return rc;
}

int hiredis_okvm_mgr_get_replicas(void *data, 
        char *host_str,
        int (*fn)(void *data, char *ip, int port))
{
    int rc = -1;
    char *token, *str1;
    char *host, *port;
    char *save_ptr1, *save_ptr2;

    str1 = NULL;
    token = NULL;
    host = NULL;
    port = NULL;
    save_ptr1 = NULL;
    save_ptr2 = NULL;

    if (!fn)
        return -1;

    for (str1 = host_str; ; str1 = NULL){
        token = strtok_r(str1, ";", &save_ptr1);
        if (token == NULL)
            break;

        host = strtok_r(token, ":", &save_ptr2);
        if (host == NULL)
            continue;

        port = strtok_r(NULL, ":", &save_ptr2);
        if (port == NULL)
            continue;

        // Ready to connect sentinel
        rc = fn(data, host, atoi(port));
        if (rc == 0)
            break;//Get the host
    }

    return rc;
}

