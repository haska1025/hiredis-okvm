#include <string.h>
#include <stdlib.h>
#include "hiredis_okvm.h"
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"

#define DEFAULT_CONN_NUM 1
#define MAX_CONN_NUM 64

// The global variable
struct hiredis_okvm g_okvm;
struct hiredis_okvm_mgr g_mgr;
int g_loglevel = LOG_INFO;

int hiredis_okvm_init(struct hiredis_okvm *param)
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
int hiredis_okvm_fini()
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

    return hiredis_okvm_mgr_fini(&g_mgr);
}

int hiredis_okvm_do_async(const char *cmd)
{
    return 0;
}
void *hiredis_okvm_do_sync(const char *cmd)
{
    return NULL;
}

void hiredis_okvm_set_log_level(int l)
{
    g_loglevel = l;
}
int hiredis_okvm_get_log_level()
{
    return g_loglevel;
}


