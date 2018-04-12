#include <string.h>
#include <stdlib.h>
#include "hiredis_okvm.h"
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"

#define DEFAULT_CONN_NUM 1
#define MAX_CONN_NUM 64

// The global variable
struct redis_okvm g_okvm;
struct redis_okvm_mgr g_mgr;
int g_loglevel = LOG_INFO;

int redis_okvm_init(struct redis_okvm *param)
{
    int rc = 0;
    int i = 0;
    int conn_num = param->connections;

    if (!param){
        HIREDIS_OKVM_LOG_ERROR("Invalid param to init okvm");
        return -1;
    }
    if (!param->redis_host){
        HIREDIS_OKVM_LOG_ERROR("You must supply the redis host to init okvm");
        return -1;
    }

    INIT_HIREDIS_OKVM(&g_okvm);

    g_okvm.db_index = param->db_index;
    g_okvm.redis_host = strdup(param->redis_host);
    if (param->db_name){
        g_okvm.db_name = strdup(param->db_name);
    }
    if (param->password){
        g_okvm.password = strdup(param->password);
    }

    if (conn_num < DEFAULT_CONN_NUM)
        conn_num = DEFAULT_CONN_NUM;

    if (conn_num > MAX_CONN_NUM)
        conn_num = MAX_CONN_NUM;

    g_okvm.connections = conn_num;

    return hireids_okvm_mgr_init(&g_mgr, conn_num);
}
int redis_okvm_fini()
{
    int i = 0;

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

int redis_okvm_write(const char *cmd, int len)
{
    struct redis_okvm_msg *msg = redis_okvm_msg_alloc(OKVM_EXTERNAL_CMD_WRITE, cmd, len); 
    return redis_okvm_send_policy_send(&g_mgr.write_policy, msg);
}

void *redis_okvm_read(const char *cmd, int len)
{
    int rc = 0;
    redisReply *reply = NULL;
    struct redis_okvm_msg *msg = redis_okvm_msg_alloc(OKVM_EXTERNAL_CMD_WRITE, cmd, len); 

    rc = redis_okvm_send_policy_send(&g_mgr.read_policy, msg);
    if (rc != 0){
        redis_okvm_msg_free(msg);
        msg = NULL;
        return NULL;
    }

    reply = redis_okvm_msg_get_reply(msg);
    redis_okvm_msg_free(msg);
    msg = NULL;

    return reply;
}

void redis_okvm_reply_free(redis_okvm_reply *reply)
{
    freeReplyObject(reply);
}

// if has next element return 1; otherwise return 0.
int redis_okvm_reply_has_next(struct redis_okvm_reply_iterator *it)
{
    if (it->pos < 0)
        return 0;

    redisReply *reply = it->reply;
    if (reply->type == REDIS_REPLY_ARRAY && it->pos >= reply->elements)
        return 0;

    return 1;
}

struct redis_okvm_reply_iterator* redis_okvm_reply_get_iterator(redis_okvm_reply *reply)
{
    struct redis_okvm_reply_iterator *it = malloc(sizeof(struct redis_okvm_reply_iterator));
    it->reply = reply;
    // pos > 0 indicates has elements; pos < 0 indicates doesn't has element
    it->pos = 0;

    return it;
}

void redis_okvm_reply_free_iterator(struct redis_okvm_reply_iterator *it)
{
    free(it);
}

// return the new reply object
struct redis_okvm_reply_iterator * redis_okvm_reply_next(struct redis_okvm_reply_iterator *it)
{
    redisReply *reply = it->reply;
    return redis_okvm_reply_get_iterator(reply->element[it->pos++]);
}
// return int value
int redis_okvm_reply_next_int(struct redis_okvm_reply_iterator *it)
{
    redisReply *reply = it->reply;
    int i = it->pos;
    ++it->pos;
    return reply->element[i]->integer;
}
// return string value
char* redis_okvm_reply_next_str(struct redis_okvm_reply_iterator *it)
{
    redisReply *reply = it->reply;
    int i = it->pos;
    ++it->pos;
    return reply->element[i]->str;
}

int redis_okvm_reply_int(redis_okvm_reply *reply)
{
    redisReply *r = reply;
    return r->integer;
}
char* redis_okvm_reply_str(redis_okvm_reply *reply)
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


