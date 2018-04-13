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
    struct redis_okvm_msg *msg = malloc(sizeof(struct redis_okvm_msg) + len);

    msg->type = type;
    msg->ref_count = 1;
    QUEUE_INIT(&msg->link);
    uv_mutex_init(&msg->msg_mutex);
    uv_cond_init(&msg->msg_cond);
    msg->reply = NULL;
    if (data){
        msg->data_len = len;
        memcpy(msg->data, data, len);
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
void redis_okvm_msg_set_reply(struct redis_okvm_msg *msg, redisReply *reply)
{
    uv_mutex_lock(&msg->msg_mutex); 
    msg->reply = reply;
    uv_mutex_unlock(&msg->msg_mutex); 
    uv_cond_signal(&msg->msg_cond);
}
void *redis_okvm_msg_get_reply(struct redis_okvm_msg *msg)
{
    void *reply = NULL;

    uv_mutex_lock(&msg->msg_mutex); 
    while(!msg->reply){
        uv_cond_wait(&msg->msg_cond, &msg->msg_mutex);
    }
    reply = msg->reply;
    uv_mutex_unlock(&msg->msg_mutex); 
    return reply;
}
/**************************** The message queue ***************************/
int redis_okvm_msg_queue_init(struct redis_okvm_msg_queue *queue)
{
    QUEUE_INIT(&queue->queue_head);
    uv_mutex_init(&queue->queue_mutex);
    return 0;
}
int redis_okvm_msg_queue_push(struct redis_okvm_msg_queue *queue, struct redis_okvm_msg *msg)
{
    uv_mutex_lock(&queue->queue_mutex);
    redis_okvm_msg_inc_ref(msg);
    QUEUE_INSERT_TAIL(&queue->queue_head, &msg->link);
    uv_mutex_unlock(&queue->queue_mutex);
    uv_async_send(&queue->notify);

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

/******************************* The redis async context *********************/
static void redis_okvm_async_cmd_callback(redisAsyncContext *c, void *r, void *privdata);
static int redis_okvm_async_context_auth(struct redis_okvm_async_context *async_ctx)
{
    // do auth
    int rc = REDIS_OKVM_OK;
    if (g_okvm.password){
        char cmd[DEFAULT_CMD_LEN];
        snprintf(cmd, DEFAULT_CMD_LEN, "AUTH %s", g_okvm.password);
        rc = redisAsyncCommand(async_ctx->ctx, redis_okvm_async_cmd_callback, async_ctx, cmd);
        if (rc == REDIS_OK){ 
            async_ctx->state = OKVM_CHECK_AUTH;
            rc = REDIS_OKVM_WAIT_REPLY;
        }else{
            rc = REDIS_OKVM_ERROR;
        }
    }else{
        async_ctx->state = OKVM_ROLE;
    }

    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) auth rc(%d).", async_ctx, rc);
    return rc;
}
static int redis_okvm_async_context_check_auth(struct redis_okvm_async_context *async_ctx, redisReply *reply)
{
    int rc = REDIS_OKVM_OK;

    if (strcmp(reply->str, "OK") == 0){
        async_ctx->state = OKVM_ROLE;
    }else{
        rc = REDIS_OKVM_ERROR; 
    }

    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) check auth rc(%d).", async_ctx, rc);
    return rc;
}
static int redis_okvm_async_context_check_role(struct redis_okvm_async_context *async_ctx, redisReply *reply)
{
    int rc = REDIS_OKVM_OK;

    if(strcmp("master", reply->element[0]->str) == 0
            || strcmp("slave", reply->element[0]->str) == 0){
        async_ctx->state = OKVM_ESTABLISHED;
    }else{
        rc = REDIS_OKVM_ERROR; 
    }

    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) check role rc(%d).", async_ctx, rc);

    return rc;
}

static int redis_okvm_async_context_role(struct redis_okvm_async_context *async_ctx)
{
    // Check role
    int rc = redisAsyncCommand(async_ctx->ctx, redis_okvm_async_cmd_callback, async_ctx, "ROLE");
    if (rc == REDIS_OK){
        async_ctx->state = OKVM_CHECK_ROLE;
        rc = REDIS_OKVM_WAIT_REPLY;
    }else{
        rc = REDIS_OKVM_ERROR;
    }

    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) role rc(%d).", async_ctx, rc);

    return rc;
}

static int redis_okvm_async_inner_msg_do(struct redis_okvm_async_context *async_ctx, redisReply *reply)
{
    int rc = REDIS_OKVM_OK; 
    do{
        switch(async_ctx->state){
            case OKVM_CONNECTING:
                async_ctx->state = OKVM_CONNECTED;
                break;
            case OKVM_CONNECTED:
                async_ctx->state = OKVM_AUTH;
                break;
            case OKVM_AUTH:
                rc = redis_okvm_async_context_auth(async_ctx);
                break;
            case OKVM_CHECK_AUTH:
                rc = redis_okvm_async_context_check_auth(async_ctx, reply);
                break;
            case OKVM_ROLE:
                rc = redis_okvm_async_context_role(async_ctx);
                break;
            case OKVM_CHECK_ROLE:
                rc = redis_okvm_async_context_check_role(async_ctx, reply);
                break;
            default:
                rc = REDIS_OKVM_ERROR;
        }
    }while(rc == REDIS_OKVM_OK && async_ctx->state != OKVM_ESTABLISHED);

    return rc;
}
static void redis_okvm_async_cmd_callback(redisAsyncContext *c, void *r, void *privdata)
{
    redisReply *reply = r;
    struct redis_okvm_async_context *async_ctx = c->data;
    int rc = REDIS_OKVM_OK;

    // The reply is error
    if (!reply || reply->type == REDIS_REPLY_ERROR){
        rc = REDIS_OKVM_ERROR;
        goto err;
    }

    // The reply is success
    if (async_ctx->state == OKVM_ESTABLISHED){
        // The user-defined message
        redis_okvm_msg_set_reply(privdata, reply);
        // Can't free redisReply
        return;
    }
    // The internal message
    rc = redis_okvm_async_inner_msg_do(async_ctx, reply);
err:
    if (rc != REDIS_OKVM_OK && rc != REDIS_OKVM_WAIT_REPLY){
        redis_okvm_async_context_fini(async_ctx);
    }
    // We can't free the reply.
    //freeReplyObject(reply);
    //reply = NULL;
}

static void connectCallback(const redisAsyncContext *c, int status)
{
    // If connection is ok, ready to auth or check role
    struct redis_okvm_async_context *async_ctx = c->data;
    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) connect callback. status(%d)", c, status);
    if (status != REDIS_OK){
        redis_okvm_async_context_fini(async_ctx);
    }else{
        redis_okvm_async_inner_msg_do(async_ctx, NULL);
    }
}
static void disconnectCallback(const redisAsyncContext *c, int status)
{
    HIREDIS_OKVM_LOG_INFO("Redis async context(%p) disconnect callback. status(%d)", c, status);
    redis_okvm_async_context_fini((struct redis_okvm_async_context*)c->data);
}
int redis_okvm_async_context_init(struct redis_okvm_async_context *async_ctx, struct redis_okvm_thread *thr)
{
    async_ctx->ctx = NULL;
    async_ctx->state = OKVM_CLOSED;
    async_ctx->okvm_thr = thr;
    return 0;
}
int redis_okvm_async_context_fini(struct redis_okvm_async_context *async_ctx)
{
    HIREDIS_OKVM_LOG_INFO("Redis okvm async context(%p) fini status(%d)", async_ctx, async_ctx->state);
    async_ctx->state = OKVM_CLOSED;
    if (async_ctx->ctx){
        // Need call ?
        // redisAsyncDisconnect(async_ctx->ctx);
        //redisAsyncFree(async_ctx->ctx);
        //async_ctx->ctx = NULL;
    }
    return 0;
}
int redis_okvm_async_context_connect(struct redis_okvm_async_context *async_ctx, char *ip, int port)
{
    redisAsyncContext *ctx = NULL;

    if (async_ctx->state != OKVM_CLOSED){
        HIREDIS_OKVM_LOG_ERROR("Connect to redis(%s:%d) failed. the state(%d) is error", ip, port, async_ctx->state);
        return REDIS_OKVM_ERROR;
    }

    ctx = redisAsyncConnect(ip, port);
    if (!ctx || ctx->err){
        HIREDIS_OKVM_LOG_ERROR("Connect to redis(%s:%d) failed(%s)", ip, port, ctx?ctx->errstr:"No error reason");
        if (ctx)redisAsyncFree(ctx);

        return REDIS_OKVM_ERROR;
    }

    redisLibuvAttach(ctx, &async_ctx->okvm_thr->loop);
    redisAsyncSetConnectCallback(ctx, connectCallback);
    redisAsyncSetDisconnectCallback(ctx,disconnectCallback);
    async_ctx->ctx = ctx;
    async_ctx->state = OKVM_CONNECTING;
    ctx->data = async_ctx;

    HIREDIS_OKVM_LOG_ERROR("Connect to redis(%s:%d) success", ip, port);

    return 0;
}

int redis_okvm_async_context_execute(struct redis_okvm_async_context *async_ctx, struct redis_okvm_msg *msg)
{
    return redisAsyncCommand(async_ctx->ctx, redis_okvm_async_cmd_callback, msg, msg->data);
}

/******************************* The redis host information *********************/
// The timeout unit is milliseconds.
static redisContext *redis_okvm_thread_connect(char *ip, int port, int timeout)
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

static int redis_okvm_thread_connect_master(void *data, char *ip, int port)
{
    struct redis_okvm_thread *thr = data;
    return redis_okvm_async_context_connect(&thr->write_ctx, ip, port);
}

static int redis_okvm_thread_connect_slave(void *data, char *ip, int port)
{
    struct redis_okvm_thread *thr = data;
    return redis_okvm_async_context_connect(&thr->read_ctx, ip, port);
}

static void redis_okvm_thread_process_inner_msg(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg)
{
    if (msg->type == OKVM_INNER_CMD_STOP){
        okvm_thr->state = 0;
        uv_stop(&okvm_thr->loop);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_SLAVE){
        redis_okvm_mgr_get_replicas(okvm_thr, msg->data, redis_okvm_thread_connect_slave);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_MASTER){
        redis_okvm_mgr_get_replicas(okvm_thr, msg->data, redis_okvm_thread_connect_master);
    } else if (msg->type == OKVM_INNER_CMD_CONNECT_SENTINEL){
        redis_okvm_mgr_init_sentinel(&g_mgr);
    }
}
static void redis_okvm_thread_inner_async_cb(uv_async_t *handle)
{
    struct redis_okvm_thread *okvm_thr = (struct redis_okvm_thread*)handle->data;
    struct redis_okvm_msg *msg = NULL;

    while(1){
        msg = redis_okvm_msg_queue_pop(&okvm_thr->inner_queue); 
        if (!msg){
            break;
        }
        redis_okvm_thread_process_inner_msg(okvm_thr, msg);
        redis_okvm_msg_free(msg);
        msg = NULL;
    }
}

static void redis_okvm_thread_read_async_cb(uv_async_t *handle)
{
    struct redis_okvm_thread *okvm_thr = (struct redis_okvm_thread*)handle->data;
}

static void redis_okvm_thread_write_async_cb(uv_async_t *handle)
{
    struct redis_okvm_thread *okvm_thr = (struct redis_okvm_thread*)handle->data;
}
static void redis_okvm_thr_svc(void *arg)
{
    uv_async_t *notify = NULL;
    struct redis_okvm_thread *okvm_thr = (struct redis_okvm_thread*)arg;

    HIREDIS_OKVM_LOG_INFO("Enter the okvm thread(%p)", arg);

    if (0 != uv_loop_init(&okvm_thr->loop)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread failed");
        return;
    }
    // Register inner message queue
    notify = redis_okvm_msg_queue_get_notify(&okvm_thr->inner_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, redis_okvm_thread_inner_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init inner async failed");
        return;
    }
    // Register read message queue
    notify = redis_okvm_msg_queue_get_notify(&okvm_thr->read_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, redis_okvm_thread_read_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init read async failed");
        return;
    }
    // Register write message queue
    notify = redis_okvm_msg_queue_get_notify(&okvm_thr->write_queue);
    notify->data = arg;
    if (0 != uv_async_init(&okvm_thr->loop, notify, redis_okvm_thread_write_async_cb)){
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

int redis_okvm_thread_init(struct redis_okvm_thread *okvm_thr)
{
    redis_okvm_async_context_init(&okvm_thr->write_ctx, okvm_thr);
    redis_okvm_async_context_init(&okvm_thr->read_ctx, okvm_thr);
    okvm_thr->role = 0;

    redis_okvm_msg_queue_init(&okvm_thr->inner_queue);
    redis_okvm_msg_queue_init(&okvm_thr->read_queue);
    redis_okvm_msg_queue_init(&okvm_thr->write_queue);
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

    msg = redis_okvm_msg_alloc(OKVM_INNER_CMD_STOP, NULL, 0);
    redis_okvm_thread_push(okvm_thr, msg);
    redis_okvm_msg_free(msg);
    msg = NULL;
    uv_thread_join(&okvm_thr->worker);
    return 0;
}

int redis_okvm_thread_push(struct redis_okvm_thread *okvm_thr, struct redis_okvm_msg *msg)
{
    if (msg->type == OKVM_EXTERNAL_CMD_READ){
        return redis_okvm_msg_queue_push(&okvm_thr->read_queue, msg);
    }else if (msg->type == OKVM_EXTERNAL_CMD_WRITE){
        return redis_okvm_msg_queue_push(&okvm_thr->write_queue, msg);
    }else{
        return redis_okvm_msg_queue_push(&okvm_thr->inner_queue, msg);
    }
}

int redis_okvm_send_policy_init(struct redis_okvm_send_policy *policy)
{
    policy->threads = NULL;
    policy->cur_idx = 0;
    policy->max_len = 0;
    uv_mutex_init(&policy->mutex);
    return 0;
}
int redis_okvm_send_policy_send(struct redis_okvm_send_policy *policy, struct redis_okvm_msg *msg)
{
    struct redis_okvm_thread *thr = NULL;

    uv_mutex_lock(&policy->mutex);
    thr = policy->threads[policy->cur_idx++];
    policy->cur_idx %= policy->max_len;
    uv_mutex_unlock(&policy->mutex);

    return redis_okvm_thread_push(thr, msg);
}
int redis_okvm_send_policy_fini(struct redis_okvm_send_policy *policy)
{
    return 0;
}
/********************************* The okvm mgr **************************************/
int hireids_okvm_mgr_init(struct redis_okvm_mgr *mgr, int thr_num)
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

    redis_okvm_send_policy_init(&mgr->read_policy);
    redis_okvm_send_policy_init(&mgr->write_policy);

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

    while(!QUEUE_EMPTY(&mgr->slaves_head)){
        qptr = QUEUE_HEAD(&mgr->slaves_head);
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        QUEUE_REMOVE(qptr);
        HIREDIS_OKVM_LOG_INFO("Remove slaves ip(%s) port(%d)", hi->ip, hi->port);
        free(hi);
        hi = NULL;
    }

    return 0;
}

int redis_okvm_mgr_broadcast(struct redis_okvm_mgr *okvm, struct redis_okvm_host_info *host, int cmd)
{
    int i = 0;
    for (; i < okvm->threads_nr; ++i){
        struct redis_okvm_msg *msg = NULL;
        // The type for port saved is int, the maximum length for the decimal digit int is less than 11.
        int host_len = strlen(host->ip) + 12;
        msg = redis_okvm_msg_alloc(cmd, NULL, host_len);
        snprintf(msg->data, host_len, "%s:%d", host->ip, host->port);
        redis_okvm_thread_push(okvm->threads[i], msg);
        redis_okvm_msg_free(msg);
        msg = NULL;
    }
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
    redis_okvm_mgr_broadcast(okvm, &okvm->master, OKVM_INNER_CMD_CONNECT_MASTER);

    // Slaves not exist, dispatch master for read
    QUEUE_FOREACH(qptr, &okvm->slaves_head){
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
        redis_okvm_mgr_broadcast(okvm, hi, OKVM_INNER_CMD_CONNECT_SLAVE);
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
    ctx = redis_okvm_thread_connect(ip, port, 500); 
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
    ctx = redis_okvm_thread_connect(ip, port, 500); 
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

