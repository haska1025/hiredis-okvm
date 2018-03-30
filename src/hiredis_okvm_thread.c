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

static char * hiredis_okvm_thread_get_master(char *ip, int port)
{
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
        return NULL;
    }

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL get-master-addr-by-name %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        // 
        host = malloc(strlen(reply->element[0]->str) + strlen(reply->element[1]->str) + 2);
        host[0] = '\0';
        strcat(host, reply->element[0]->str);
        strcat(host, ":");
        strcat(host, reply->element[1]->str);
        return host;
    }
    return NULL;
}

static char *hiredis_okvm_thread_get_slaves(char *ip, int port)
{
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
        return NULL;
    }

    snprintf(cmd, DEFAULT_CMD_LEN, "SENTINEL slaves %s", g_okvm.db_name? g_okvm.db_name:"mymaster");

    reply = redisCommand(ctx, cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements > 1){
        int i = 0;
        for (; i < reply->elements; i++){
            HIREDIS_OKVM_LOG_INFO("element:%s", reply->element[i]->str);
            printf("element:%s\n", reply->element[i]->str);
        }
        /* 
        host = malloc(strlen(reply->element[0]->str) + strlen(reply->element[1]->str) + 2);
        host[0] = '\0';
        strcat(host, reply->element[0]->str);
        strcat(host, ":");
        strcat(host, reply->element[1]->str);
        return host;
        */
    }

    HIREDIS_OKVM_LOG_ERROR("Get slave from sentinel(%s:%d) failed(%s).", ip, port, reply?reply->str:"No reason");
    return NULL;
}

static char * hiredis_okvm_thread_get_replicas( char * (*fn)(char *ip, int port))
{
    char *host_str;
    char *token, *str1;
    char *host, *port;
    char *save_ptr1, *save_ptr2;
    char *rs;

    str1 = NULL;
    token = NULL;
    host_str = strdup(g_okvm.redis_host);
    host = NULL;
    port = NULL;
    save_ptr1 = NULL;
    save_ptr2 = NULL;
    rs = NULL;

    if (!fn)
        return NULL;

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
        rs = fn(host, atoi(port));
        if (rs != NULL)
            break;//Get the host
    }

    free(host_str);
    host_str = NULL;

    return rs;
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

