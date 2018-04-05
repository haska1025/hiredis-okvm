#include <string.h>
#include <stdlib.h>
#include "hiredis_okvm.h"
#include "hiredis_okvm_thread.h"
#include "hiredis_okvm_log.h"

#define DEFAULT_CONN_NUM 1
#define MAX_CONN_NUM 64

// The global variable
struct hiredis_okvm g_okvm;
int g_loglevel = LOG_INFO;
static struct hiredis_okvm_thread **g_okvm_threads = NULL;


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

    g_okvm_threads = malloc(sizeof(struct hiredis_okvm_thread*) * conn_num);
    if (!g_okvm_threads)
        return -1;

    for (i = 0; i < conn_num; ++i) {
        g_okvm_threads[i] = NULL;
    }

    for (i = 0; i < conn_num; ++i) {
        g_okvm_threads[i] = malloc(sizeof(struct hiredis_okvm_thread));
        INIT_HIREDIS_OKVM_THREAD(g_okvm_threads[i]);
        rc = hiredis_okvm_thread_init(g_okvm_threads[i]);
        if (rc != 0)
            return rc;
    }

    return 0;
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

    for (i = 0; i < g_okvm.connections; ++i) {
        free(g_okvm_threads[i]);
        g_okvm_threads[i] = NULL;
    }

    free(g_okvm_threads);
    g_okvm_threads = NULL;

    return 0;
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


