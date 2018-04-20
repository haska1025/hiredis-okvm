// The system hdr file
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <adapters/libuv.h>
// Myself hdr file
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"
#include "hiredis_okvm.h"

extern struct redis_okvm g_okvm;
extern struct redis_okvm_mgr g_mgr;

#define DEFAULT_CMD_LEN 512

// Reference:https://dzone.com/articles/high-availability-with-redis-sentinels-connecting

/***************************** The message ***************************/
struct redis_okvm_msg * redis_okvm_msg_alloc(int type, const char *data, int len)
{
    struct redis_okvm_msg *msg = malloc(sizeof(struct redis_okvm_msg) + len + 1);

    msg->type = type;
    msg->ref_count = 1;
    QUEUE_INIT(&msg->link);

    if (data){
        msg->data_len = len;
        // Copy the null terminator
        memcpy(msg->data, data, len+1);
    }else{
        msg->data_len = 0;
    }
    return msg;
}
void redis_okvm_msg_inc_ref(struct redis_okvm_msg *msg)
{
    __sync_add_and_fetch(&msg->ref_count, 1);
}
void redis_okvm_msg_free(struct redis_okvm_msg *msg)
{
    int refs = __sync_sub_and_fetch(&msg->ref_count, 1);
    if (refs == 0){
        free(msg);
    }
}
/**************************** The message queue ***************************/
static void redis_okvm_msg_queue_cb(uv_async_t *handle)
{
    struct redis_okvm_msg_queue *queue = (struct redis_okvm_msg_queue*)handle->data;
    struct redis_okvm_msg *msg = NULL;

    while(1){
        msg = redis_okvm_msg_queue_pop(queue); 
        if (!msg){
            break;
        }
        queue->handle_msg(queue->okvm_thr, msg);
        redis_okvm_msg_free(msg);
        msg = NULL;
    }
}

int redis_okvm_msg_queue_init(struct redis_okvm_msg_queue *queue,
    struct redis_okvm_thread *okvm_thr,
    void (*handle_msg)(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg))
{
    queue->okvm_thr = okvm_thr;
    queue->handle_msg = handle_msg;
    QUEUE_INIT(&queue->queue_head);
    uv_mutex_init(&queue->queue_mutex);

    // Register message queue
    queue->notify.data = queue;
    if (0 != uv_async_init(&queue->okvm_thr->loop, &queue->notify, redis_okvm_msg_queue_cb)){
        HIREDIS_OKVM_LOG_ERROR("Register notify for queue(%p) failed", queue);
        return REDIS_OKVM_ERROR;
    }
    return REDIS_OKVM_OK;
}
int redis_okvm_msg_queue_push(struct redis_okvm_msg_queue *queue, struct redis_okvm_msg *msg)
{
    uv_mutex_lock(&queue->queue_mutex);
    redis_okvm_msg_inc_ref(msg);
    QUEUE_INSERT_TAIL(&queue->queue_head, &msg->link);
    uv_mutex_unlock(&queue->queue_mutex);
    redis_okvm_msg_queue_notify(queue);

    return 0;
}
struct redis_okvm_msg *redis_okvm_msg_queue_pop(struct redis_okvm_msg_queue *queue)
{
    struct redis_okvm_msg *msg = NULL;
    QUEUE *ptr = NULL;

    uv_mutex_lock(&queue->queue_mutex);
    if (!QUEUE_EMPTY(&queue->queue_head)){
        ptr = QUEUE_HEAD(&queue->queue_head);
        msg = QUEUE_DATA(ptr, struct redis_okvm_msg, link);
        QUEUE_REMOVE(ptr);
    }
    uv_mutex_unlock(&queue->queue_mutex);

    return msg;
}
void redis_okvm_msg_queue_notify(struct redis_okvm_msg_queue *queue)
{
    uv_async_send(&queue->notify);
}

/******************************* The redis context *********************/
// The timeout unit is milliseconds.
static redisContext *redis_okvm_context_connect(char *ip, int port, int timeout)
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

int redis_okvm_context_init(struct redis_okvm_context *ctx)
{
    ctx->ctx = NULL;
    ctx->bulk_count = 0;
    QUEUE_INIT(&ctx->link);
    return REDIS_OKVM_OK;
}
int redis_okvm_context_fini(struct redis_okvm_context *ctx)
{
    HIREDIS_OKVM_LOG_INFO("Redis okvm async context(%p) fini status(%d)", ctx, ctx->state);
    ctx->state = OKVM_CLOSED;
    if (ctx->ctx){
        // Need call ?
        redisFree(ctx->ctx);
        ctx->ctx = NULL;
    }
    return REDIS_OKVM_OK;
}
int redis_okvm_context_connect_server(struct redis_okvm_context *okvm_ctx, char *ip, int port)
{
    int rc = REDIS_OKVM_OK;
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    struct timeval timeout;
    char cmd[DEFAULT_CMD_LEN];

    // Wait for 500ms
    ctx = redis_okvm_context_connect(ip, port, 500); 
    if (!ctx)
        return REDIS_OKVM_CONN_FAILED;

    okvm_ctx->ctx = ctx;
    // do auth
    if (g_okvm.password){
        snprintf(cmd, DEFAULT_CMD_LEN, "AUTH %s", g_okvm.password);
        reply = redis_okvm_context_execute(okvm_ctx, cmd);
        if (reply && strcmp(reply->str, "OK") != 0){
            rc = REDIS_OKVM_AUTH_FAILED;
            goto err;
        }
        freeReplyObject(reply);
        reply = NULL;
    }
    // Check role
    reply = redis_okvm_context_execute(okvm_ctx, "ROLE");
    if(reply && strcmp("master", reply->element[0]->str) != 0
        && (strcmp("slave", reply->element[0]->str) != 0)){
        rc = REDIS_OKVM_ROLE_FAILED;
    }
err:
    HIREDIS_OKVM_LOG_ERROR("Connect to server result(%d) err(%s). ip(%s) port(%d) password(%s)",
            rc,
            reply?reply->str:"No reason",
            ip,
            port,
            g_okvm.password?g_okvm.password:"No password");

    if (rc != REDIS_OKVM_OK){
        redisFree(ctx);
        okvm_ctx->ctx = NULL;
    }
    freeReplyObject(reply);

    return rc;
}

redisReply* redis_okvm_context_execute(struct redis_okvm_context *ctx, const char *cmd)
{
    redisReply* reply = NULL;
    HIREDIS_OKVM_LOG_INFO("ctx(%p) execute message(%s)", ctx, cmd);
    reply = redisCommand(ctx->ctx, cmd);
    if (!reply || reply->type == REDIS_REPLY_ERROR){
        // We must disconnect. Read to reconnect.
        HIREDIS_OKVM_LOG_ERROR("redisCommand excuete cmd(%s) failed.", cmd);
        if (reply) freeReplyObject(reply);
        return NULL;
    }
    return reply;
}

void* redis_okvm_context_get_reply(struct redis_okvm_context *ctx)
{
    void *reply = NULL;

    if (ctx->bulk_count <= 0){
        ctx->bulk_count = 0;
        return NULL;
    }

    if (redisGetReply(ctx->ctx, &reply) != REDIS_OK){
        // We must disconnect. Read to reconnect.
        ctx->bulk_count = 0;
        return NULL;
    }

    ctx->bulk_count--;
    return reply;
}
int redis_okvm_context_append(struct redis_okvm_context *ctx, const char *cmd)
{
    int rc = REDIS_OK;

    rc = redisAppendCommand(ctx->ctx, cmd);
    if (rc == REDIS_OK){
        ctx->bulk_count++;
        rc = REDIS_OKVM_OK;
    }else{
        // We must disconnect. Read to reconnect.
    }
    return REDIS_OKVM_ERROR;
}
/******************************* The okvm thread *********************/
static void redis_okvm_thread_handle_inner_msg(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg)
{
    if (msg->type == OKVM_CMD_STOP){
        okvm_thr->state = 0;
        uv_stop(&okvm_thr->loop);
    } else if (msg->type == OKVM_CMD_CONNECT_SENTINEL){
        redis_okvm_mgr_init_sentinel(&g_mgr);
    }
}

static void redis_okvm_thr_svc(void *arg)
{
    struct redis_okvm_thread *okvm_thr = (struct redis_okvm_thread*)arg;

    HIREDIS_OKVM_LOG_INFO("Enter the okvm thread(%p)", arg);

    if (0 != uv_loop_init(&okvm_thr->loop)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread failed");
        return;
    }

    redis_okvm_msg_queue_init(&okvm_thr->inner_queue,
            okvm_thr,
            redis_okvm_thread_handle_inner_msg);

    uv_mutex_lock(&okvm_thr->state_mutex);
    okvm_thr->state = 1;
    uv_mutex_unlock(&okvm_thr->state_mutex);
    uv_cond_broadcast(&okvm_thr->state_cond);

    uv_run(&okvm_thr->loop, UV_RUN_DEFAULT);
    uv_loop_close(&okvm_thr->loop);

    HIREDIS_OKVM_LOG_INFO("Leave the okvm thread(%p)", arg);
}

int redis_okvm_thread_init(struct redis_okvm_thread *okvm_thr)
{
    uv_mutex_init(&okvm_thr->state_mutex);
    uv_cond_init(&okvm_thr->state_cond);
    okvm_thr->state = 0;

    return 0;
}

int redis_okvm_thread_start(struct redis_okvm_thread *okvm_thr)
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

    rc = uv_thread_create(&okvm_thr->worker, redis_okvm_thr_svc, (void*)okvm_thr); 
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

int redis_okvm_thread_stop(struct redis_okvm_thread *okvm_thr)
{
    struct redis_okvm_msg *msg = NULL;

    uv_mutex_lock(&okvm_thr->state_mutex);
    okvm_thr->state = 0;
    uv_mutex_unlock(&okvm_thr->state_mutex);

    msg = redis_okvm_msg_alloc(OKVM_CMD_STOP, NULL, 0);
    redis_okvm_thread_push(okvm_thr, msg);
    redis_okvm_msg_free(msg);
    msg = NULL;
    uv_thread_join(&okvm_thr->worker);
    return 0;
}

int redis_okvm_thread_push(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg)
{
    return redis_okvm_msg_queue_push(&okvm_thr->inner_queue, msg);
}

int redis_okvm_pool_init(struct redis_okvm_pool *pool)
{
    pool->ctx_count = 0;
    QUEUE_INIT(&pool->ctx_head);
    uv_mutex_init(&pool->mutex);
    uv_cond_init(&pool->cond);
    return 0;
}

int redis_okvm_pool_push(struct redis_okvm_pool *pool, struct redis_okvm_context *ctx)
{
    uv_mutex_lock(&pool->mutex);
    QUEUE_INSERT_TAIL(&pool->ctx_head, &ctx->link);
    uv_mutex_unlock(&pool->mutex);

    uv_cond_signal(&pool->cond);

    return REDIS_OKVM_OK;
}
struct redis_okvm_context *redis_okvm_pool_pop(struct redis_okvm_pool *pool)
{
    struct redis_okvm_context *ctx = NULL;
    QUEUE *ptr = NULL;

    uv_mutex_lock(&pool->mutex);
    while (QUEUE_EMPTY(&pool->ctx_head)){
        uv_cond_wait(&pool->cond, &pool->mutex);
    }

    ptr = QUEUE_HEAD(&pool->ctx_head);
    ctx = QUEUE_DATA(ptr, struct redis_okvm_context, link);
    QUEUE_REMOVE(ptr);

    uv_mutex_unlock(&pool->mutex);

    return ctx;
}

struct redis_okvm_context *redis_okvm_pool_timed_pop(struct redis_okvm_pool *pool, int timeout)
{
    struct redis_okvm_context *ctx = NULL;
    QUEUE *ptr = NULL;
    int rc = 0;

    uv_mutex_lock(&pool->mutex);
    while (QUEUE_EMPTY(&pool->ctx_head)){
        rc = uv_cond_timedwait(&pool->cond, &pool->mutex, timeout * 1e6);
        if (rc != 0)
            break;
    }

    if (rc == 0){
        ptr = QUEUE_HEAD(&pool->ctx_head);
        ctx = QUEUE_DATA(ptr, struct redis_okvm_context, link);
        QUEUE_REMOVE(ptr);
    }

    uv_mutex_unlock(&pool->mutex);

    return ctx;
}

int redis_okvm_pool_create(struct redis_okvm_pool *pool, struct redis_okvm_host_info *host, int conns)
{
    int rc = REDIS_OKVM_OK;
    int i = 0;
    for (; i < conns; ++i){
        struct redis_okvm_context *ctx = malloc(sizeof(struct redis_okvm_context));
        redis_okvm_context_init(ctx);
        rc = redis_okvm_context_connect_server(ctx, host->ip, host->port);
        if (rc == 0){
            redis_okvm_pool_push(pool, ctx);
        }
    }
    return 0;
}

int redis_okvm_pool_fini(struct redis_okvm_pool *pool)
{
    return 0;
}
/********************************* The okvm mgr **************************************/
int redis_okvm_mgr_init(struct redis_okvm_mgr *mgr, int thr_num)
{
    int rc = 0;
    int i = 0;

    if (thr_num <= 0)
        thr_num = 1;
    if (thr_num > 32)
        thr_num = 32;

    mgr->threads_nr = thr_num;
    mgr->threads = malloc(sizeof(struct redis_okvm_thread*) * thr_num);
    if (!mgr->threads){
        HIREDIS_OKVM_LOG_ERROR("okvm mgr init to malloc theads failed");
        return -1;
    }

    for (i = 0; i < thr_num; ++i) {
        mgr->threads[i] = NULL;
    }

    for (i = 0; i < thr_num; ++i) {
        mgr->threads[i] = malloc(sizeof(struct redis_okvm_thread));
        redis_okvm_thread_init(mgr->threads[i]);
        redis_okvm_thread_start(mgr->threads[i]);
        if (rc != 0)
            return rc;
    }

    redis_okvm_pool_init(&mgr->read_pool);
    redis_okvm_pool_init(&mgr->write_pool);

    QUEUE_INIT(&mgr->slaves_head);
    QUEUE_INIT(&mgr->master.link);
    mgr->master.ip = NULL;
    mgr->master.port = 0;
    return redis_okvm_mgr_init_sentinel(mgr);
}
int redis_okvm_mgr_fini(struct redis_okvm_mgr *mgr)
{
    int i = 0;
    struct redis_okvm_host_info *hi = NULL;
    QUEUE *qptr = NULL;

    for (i = 0; i < mgr->threads_nr; ++i) {
        redis_okvm_thread_stop(mgr->threads[i]);
        free(mgr->threads[i]);
        mgr->threads[i] = NULL;
    }

    free(mgr->threads);
    mgr->threads = NULL;
    mgr->threads_nr = 0;

    while(!QUEUE_EMPTY(&mgr->slaves_head)){
        qptr = QUEUE_HEAD(&mgr->slaves_head);
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        QUEUE_REMOVE(qptr);
        HIREDIS_OKVM_LOG_INFO("Remove slaves ip(%s) port(%d)", hi->ip, hi->port);
        free(hi);
        hi = NULL;
    }
    if (mgr->master.ip){
        free(mgr->master.ip);
        mgr->master.ip = NULL;
    }
    mgr->master.port = 0;

    return 0;
}
int redis_okvm_mgr_init_sentinel(struct redis_okvm_mgr *okvm)
{
    int rc = 0;
    QUEUE *qptr = NULL;
    struct redis_okvm_host_info *hi = NULL;

    rc = redis_okvm_mgr_get_replicas(okvm, g_okvm.redis_host, redis_okvm_mgr_get_master);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("No master exist. the sentinel(%s)", g_okvm.redis_host);
        return -1;
    }
    rc = redis_okvm_mgr_get_replicas(okvm, g_okvm.redis_host, redis_okvm_mgr_get_slaves);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("No slave exist. the sentinel(%s)", g_okvm.redis_host);
    }

    // Dispatch master and slave ip to everyone.
    redis_okvm_pool_create(&okvm->write_pool, &okvm->master, g_okvm.write_connections);

    // Slaves not exist, dispatch master for read
    QUEUE_FOREACH(qptr, &okvm->slaves_head){
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
        redis_okvm_pool_create(&okvm->read_pool, hi, g_okvm.read_connections);
    }

    return rc;
}

int redis_okvm_mgr_parse_slaves_or_sentinels(struct redis_okvm_mgr *okvm, redisReply *reply)
{
    int i = 0;
    for (; i < reply->elements; ++i){
        int j = 0;
        redisReply *item = reply->element[i];
        struct redis_okvm_host_info *hi = malloc(sizeof(struct redis_okvm_host_info));
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

    return 0;
}
int redis_okvm_mgr_get_master(void *data, char *ip, int port)
{
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    char cmd[DEFAULT_CMD_LEN];
    struct redis_okvm_mgr *okvm = data;
    // Wait for 500ms
    ctx = redis_okvm_context_connect(ip, port, 500); 
    if (!ctx)
        return -1;

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL get-master-addr-by-name %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 1){
        HIREDIS_OKVM_LOG_ERROR("Get master by cmd(%s) failed err(%s)", cmd, reply?reply->str:"No reason"); 
        return -1;
    }

    okvm->master.ip = strdup(reply->element[0]->str);
    okvm->master.port = atoi(reply->element[1]->str);
    freeReplyObject(reply);
    reply = NULL;

    redisFree(ctx);
    ctx = NULL;

    return 0;
}

int redis_okvm_mgr_get_slaves(void *data, char *ip, int port)
{
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    struct timeval timeout;
    char cmd[DEFAULT_CMD_LEN];
    char *host = NULL;
    struct redis_okvm_mgr *okvm = data;
    // Wait for 500ms
    ctx = redis_okvm_context_connect(ip, port, 500); 
    if (!ctx)
        return -1;

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL slaves %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 1){
        HIREDIS_OKVM_LOG_ERROR("Get slaves by cmd(%s) failed err(%s)", cmd, reply?reply->str:"No reason"); 
        return -1;
    }

    redis_okvm_mgr_parse_slaves_or_sentinels(okvm, reply);

    freeReplyObject(reply);
    reply = NULL;

    redisFree(ctx);
    ctx = NULL;

    return 0;
}

int redis_okvm_mgr_get_replicas(void *data, 
        char *host_str,
        int (*fn)(void *data, char *ip, int port))
{
    int rc = -1;
    char *token, *str1;
    char *host, *port;
    char *save_ptr1, *save_ptr2;
    char *dup_host_str = strdup(host_str);

    str1 = NULL;
    token = NULL;
    host = NULL;
    port = NULL;
    save_ptr1 = NULL;
    save_ptr2 = NULL;

    if (!fn){
        free(dup_host_str);
        dup_host_str;
        return -1;
    }

    for (str1 = dup_host_str; ; str1 = NULL){
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

    free(dup_host_str);
    dup_host_str;

    return rc;
}

