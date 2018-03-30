// The system hdr file
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Myself hdr file
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"
#include "hiredis_okvm.h"

extern struct hiredis_okvm g_okvm;

#define DEFAULT_CMD_LEN 512

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

static int hiredis_okvm_thread_parse_slaves_or_sentinels(struct hiredis_okvm_thread *okvm, redisReply *reply)
{
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

    return 0;
}

static int hiredis_okvm_thread_get_master(struct hiredis_okvm_thread *okvm, char *ip, int port)
{
    int rc = -1;
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    struct timeval timeout;
    char cmd[DEFAULT_CMD_LEN];

    // Wait for 500ms
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    ctx = redisConnectWithTimeout(ip, port, timeout);
    if (!ctx || ctx->err){
        HIREDIS_OKVM_LOG_ERROR("Connect to sentinel(%s:%d) failed(%s)", ip, port, ctx?ctx->errstr:"No error reason");
        return -1;
    }

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL get-master-addr-by-name %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        okvm->master.ip = strdup(reply->element[0]->str);
        okvm->master.port = atoi(reply->element[1]->str);
        rc = 0;
    }
    freeReplyObject(reply);
    return rc;
}

static int hiredis_okvm_thread_get_slaves(struct hiredis_okvm_thread *okvm, char *ip, int port)
{
    int rc = -1;
    redisContext* ctx = NULL;
    redisReply* reply = NULL;
    struct timeval timeout;
    char cmd[DEFAULT_CMD_LEN];
    char *host = NULL;

    // Wait for 500ms
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    ctx = redisConnectWithTimeout(ip, port, timeout);
    if (!ctx || ctx->err){
        HIREDIS_OKVM_LOG_ERROR("Connect to sentinel(%s:%d) failed(%s)", ip, port, ctx?ctx->errstr:"No error reason");
        return rc;
    }

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL slaves %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        hiredis_okvm_thread_parse_slaves_or_sentinels(okvm, reply);
        rc = 0;
    }

    freeReplyObject(reply);
    return rc;
}

static int hiredis_okvm_thread_get_replicas(struct hiredis_okvm_thread *okvm, 
        int (*fn)(struct hiredis_okvm_thread *okvm, char *ip, int port))
{
    int rc = -1;
    char *host_str;
    char *token, *str1;
    char *host, *port;
    char *save_ptr1, *save_ptr2;

    str1 = NULL;
    token = NULL;
    host_str = strdup(g_okvm.redis_host);
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
        rc = fn(okvm, host, atoi(port));
        if (rc == 0)
            break;//Get the host
    }

    free(host_str);
    host_str = NULL;

    return rc;
}

static void hiredis_okvm_thread_async_cb(uv_async_t* handle)
{
    // Do nothing
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)handle->data;
    okvm_thr->state = 0;
    uv_stop(&okvm_thr->loop);
}

static void hiredis_okvm_thread_inner_async_cb(uv_async_t *handle)
{
    struct hiredis_okvm_thread *okvm_thr = (struct hiredis_okvm_thread*)handle->data;
    struct hiredis_okvm_msg *msg = NULL;

    while(1){
        msg = hiredis_okvm_msg_queue_pop(&okvm_thr->inner_queue); 
        if (!msg){
            HIREDIS_OKVM_LOG_INFO
        }
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

    if (0 != uv_async_init(&okvm_thr->loop, &okvm_thr->notify, hiredis_okvm_thread_async_cb)){
        HIREDIS_OKVM_LOG_ERROR("Start worker thread. init async failed");
        return;
    }
    okvm_thr->notify.data = arg;

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
    uv_mutex_lock(&okvm_thr->state_mutex);
    okvm_thr->state = 0;
    uv_mutex_unlock(&okvm_thr->state_mutex);
    uv_async_send(&okvm_thr->notify);
    uv_thread_join(&okvm_thr->worker);
    return 0;
}

int hiredis_okvm_thread_push(struct hiredis_okvm_thread *okvm_thr, struct hiredis_okvm_msg *msg)
{
    if (msg->type == HIREDIS_OKVM_MSG_READ){
        return hiredis_okvm_msg_queue_push(&okvm_thr->read_queue, msg);
    }else if (msg->type == HIREDIS_OKVM_MSG_WRITE){
        return hiredis_okvm_msg_queue_push(&okvm_thr->write_queue, msg);
    }else{
        return hiredis_okvm_msg_queue_push(&okvm_thr->inner_queue, msg);
    }
}

int hiredis_okvm_thread_pool_init(struct hiredis_okvm_thread_pool *thr_pool)
{
    QUEUE_INIT(&thr_pool->thr_head);
    thr_pool->cur_thr = NULL;
    uv_mutex_init(&thr_pool->mutex);
    return 0;
}
int hiredis_okvm_thread_pool_push(struct hiredis_okvm_thread_pool *thr_pool, struct hiredis_okvm_msg *msg)
{
    struct hiredis_okvm_thread *thr = NULL;
    QUEUE *thr_ptr = NULL;

    uv_mutex_lock(&thr_pool->mutex);
    if (thr_pool->cur_thr){
        thr_pool->cur_thr = QUEUE_NEXT(thr_pool->cur_thr);
    }else{
        thr_pool->cur_thr = QUEUE_HEAD(&thr_pool->thr_head);
    }
    thr_ptr = thr_pool->cur_thr;
    uv_mutex_unlock(&thr_pool->mutex);

    thr = QUEUE_DATA(thr_ptr, struct hiredis_okvm_thread, link);
    return hiredis_okvm_thread_push(thr, msg);
}
int hiredis_okvm_thread_pool_fini(struct hiredis_okvm_thread_pool *thr_pool)
{
    return 0;
}

