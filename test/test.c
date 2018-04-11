#include "../src/hiredis_okvm_thread.c"
#include "../src/hiredis_okvm.c"

void test_sentinel()
{
    int rc = 0;
    char *sentinel_ip = "127.0.0.1";
    char *sentinel_ips = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";

    int sentinel_port = 26379;
    struct hiredis_okvm_thread okvm_thr;
    struct hiredis_okvm_host_info *hi;
    QUEUE *qptr;

    hi = NULL;
    qptr = NULL;


    // case 1
    HIREDIS_OKVM_LOG_INFO("Test case 1"); 
    rc = hiredis_okvm_mgr_get_slaves(&g_mgr, sentinel_ip, sentinel_port);
    QUEUE_FOREACH(qptr, &g_mgr.slaves_head){
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 2
    HIREDIS_OKVM_LOG_INFO("Test case 2"); 
    rc = hiredis_okvm_mgr_get_master(&g_mgr, sentinel_ip, sentinel_port);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", g_mgr.master.ip, g_mgr.master.port);

    // case 3
    HIREDIS_OKVM_LOG_INFO("Test case 3"); 
    // Ready to remove queue
    while(!QUEUE_EMPTY(&g_mgr.slaves_head)){
        qptr = QUEUE_HEAD(&g_mgr.slaves_head);
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        QUEUE_REMOVE(qptr);
        HIREDIS_OKVM_LOG_INFO("Remove slaves ip(%s) port(%d)", hi->ip, hi->port);
        free(hi);
        hi = NULL;
    }
    rc = hiredis_okvm_mgr_get_replicas(&g_mgr, sentinel_ips, hiredis_okvm_mgr_get_slaves);
    QUEUE_FOREACH(qptr, &g_mgr.slaves_head){
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 4
    HIREDIS_OKVM_LOG_INFO("Test case 4"); 
    free(g_mgr.master.ip);
    g_mgr.master.port = 0;
    rc = hiredis_okvm_mgr_get_replicas(&g_mgr, sentinel_ips, hiredis_okvm_mgr_get_master);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", g_mgr.master.ip, g_mgr.master.port);
}

int main( int argc, char ** argv)
{
    char *host = NULL; 
    int rc = 0;
    struct hiredis_okvm okvm;

    INIT_HIREDIS_OKVM(&okvm);

    okvm.connections = 4;
    okvm.db_index = 2;
    //okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    okvm.redis_host = "127.0.0.1:26379;127.0.0.1:26380;127.0.0.1:26381";
    okvm.db_name = "tang-cluster";
    //okvm.db_name = "mymaster";

    rc = hiredis_okvm_init(&okvm);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("Init okvm failed");
        hiredis_okvm_fini();
        return -1;
    }

    test_sentinel();


    hiredis_okvm_fini();

    HIREDIS_OKVM_LOG_INFO("Test over");
}

