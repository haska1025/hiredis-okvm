#include <string.h>
#include <stdlib.h>
#include "hiredis_okvm.h"
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"

// The global variable
struct redis_okvm g_okvm;
struct redis_okvm_mgr g_mgr;
int g_loglevel = LOG_INFO;

int redis_okvm_init(struct redis_okvm *param)
{
    int rc = 0;

    if (!param){
        HIREDIS_OKVM_LOG_ERROR("Invalid param to init okvm");
        return -1;
    }
    if (!param->redis_host){
        HIREDIS_OKVM_LOG_ERROR("You must supply the redis host to init okvm");
        return -1;
    }

    INIT_HIREDIS_OKVM(&g_okvm);

    g_okvm.read_connections = param->read_connections;
    g_okvm.write_connections = param->write_connections;
    g_okvm.db_index = param->db_index;
    g_okvm.redis_host = strdup(param->redis_host);
    if (param->db_name){
        g_okvm.db_name = strdup(param->db_name);
    }
    if (param->password){
        g_okvm.password = strdup(param->password);
    }

    // Just create one thread
    return redis_okvm_mgr_init(&g_mgr, 1);
}
int redis_okvm_fini()
{
    if (g_okvm.redis_host){
        free(g_okvm.redis_host);
        g_okvm.redis_host = NULL;
    }
    if (g_okvm.db_name){
        free(g_okvm.db_name);
        g_okvm.db_name = NULL;
    }
    if (g_okvm.password){
        free(g_okvm.password);
        g_okvm.password = NULL;
    }

    return redis_okvm_mgr_fini(&g_mgr);
}

int redis_okvm_write(const char *cmd)
{
    redisReply *reply = NULL;
    struct redis_okvm_context *ctx = NULL;

    ctx = redis_okvm_pool_pop(&g_mgr.write_pool);
    if (!ctx){
        HIREDIS_OKVM_LOG_ERROR("Get context for write cmd(%s) failed!", cmd);
        return REDIS_OKVM_ERROR;
    }
    reply = redis_okvm_context_execute(ctx, cmd);

    redis_okvm_pool_push(&g_mgr.write_pool, ctx);

    if (reply){
        freeReplyObject(reply);
        reply = NULL;
        return REDIS_OKVM_OK;
    }
    return REDIS_OKVM_ERROR;
}

void* redis_okvm_read(const char *cmd)
{
    redisReply *reply = NULL;
    struct redis_okvm_context *ctx = NULL;

    ctx = redis_okvm_pool_pop(&g_mgr.read_pool);
    if (!ctx){
        HIREDIS_OKVM_LOG_ERROR("Get context for read cmd(%s) failed!", cmd);
        return NULL;
    }

    reply = redis_okvm_context_execute(ctx, cmd);
    redis_okvm_pool_push(&g_mgr.write_pool, ctx);

    return reply;
}
void *redis_okvm_bulk_write_begin()
{
    // Borrow a context from write pool
    return redis_okvm_pool_pop(&g_mgr.write_pool);
}
int redis_okvm_bulk_write(void *ctx, const char *cmd)
{
    return redis_okvm_context_append(ctx, cmd);
}
void redis_okvm_bulk_write_end(void *ctx)
{
    void *reply = NULL;
    // Repay the context to write pool
    redis_okvm_pool_push(&g_mgr.write_pool, ctx);
    do{
        reply = redis_okvm_context_get_reply(ctx);
        freeReplyObject(reply);
    } while (reply);
}

void *redis_okvm_bulk_read_begin()
{
    // Borrow a context from read pool
    return redis_okvm_pool_pop(&g_mgr.read_pool);
}
int redis_okvm_bulk_read(void *ctx, const char *cmd)
{
    return redis_okvm_context_append(ctx, cmd);
}
void redis_okvm_bulk_read_end(void *ctx)
{
    void *reply = NULL;
    // Repay the context to read pool
    redis_okvm_pool_push(&g_mgr.read_pool, ctx);
    do{
        reply = redis_okvm_context_get_reply(ctx);
        freeReplyObject(reply);
    } while (reply);
}

void *redis_okvm_bulk_read_reply(void *ctx)
{
    return redis_okvm_context_get_reply(ctx);
}

void redis_okvm_reply_free(void *reply)
{
    freeReplyObject(reply);
}
int redis_okvm_reply_length(void *reply)
{
    redisReply *r = reply;
    return r->elements;
}

int redis_okvm_reply_idxof_int(void *reply, int idx)
{
    redisReply *r = reply;
    return r->element[idx]->integer;
}

char* redis_okvm_reply_idxof_str(void *reply, int idx)
{
    redisReply *r = reply;
    return r->element[idx]->str;
}
void* redis_okvm_reply_idxof_obj(void *reply, int idx)
{
    redisReply *r = reply;
    return r->element[idx];
}
int redis_okvm_reply_int(void *reply)
{
    redisReply *r = reply;
    return r->integer;
}
char* redis_okvm_reply_str(void *reply)
{
    redisReply *r = reply;
    return r->str;
}

void redis_okvm_set_log_level(int l)
{
    g_loglevel = l;
}
int redis_okvm_get_log_level()
{
    return g_loglevel;
}

