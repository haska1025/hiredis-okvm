// The system hdr file
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Myself hdr file
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"
#include "hiredis_okvm.h"
#include "queue.h"

extern struct hiredis_okvm g_okvm;

#define DEFAULT_CMD_LEN 512

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
int hiredis_okvm_thread_init(struct hiredis_okvm_thread *okvm_thr)
{
    // Create thread
    return 0;
}

int hiredis_okvm_thread_fini(struct hiredis_okvm_thread *okvm_thr)
{
    return 0;
}

