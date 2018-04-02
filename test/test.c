#include "../src/hiredis_okvm_thread.c"
#include "../src/hiredis_okvm.c"

int main( int argc, char ** argv)
{
    char *host = NULL; 
    int rc = 0;
    struct hiredis_okvm okvm;
    struct hiredis_okvm_thread okvm_thr;
    struct hiredis_okvm_host_info *hi;
    QUEUE *qptr;

    hi = NULL;
    qptr = NULL;

    INIT_HIREDIS_OKVM(&okvm);
    QUEUE_INIT(&okvm_thr.slaves_head);

    okvm.connections = 0;
    okvm.db_index = 2;
    //okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    okvm.redis_host = "127.0.0.1:26379;127.0.0.1:26380;127.0.0.1:26381";
    //okvm.db_name = "tang-cluster";
    okvm.db_name = "mymaster";

    rc = hiredis_okvm_init(&okvm);
    if (rc != 0)
        return -1;

    // case 1
    HIREDIS_OKVM_LOG_INFO("Test case 1"); 
    rc = hiredis_okvm_thread_get_slaves(&okvm_thr, "127.0.0.1", 26379);
    QUEUE_FOREACH(qptr, &okvm_thr.slaves_head){
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 2
    HIREDIS_OKVM_LOG_INFO("Test case 2"); 
    rc = hiredis_okvm_thread_get_master(&okvm_thr, "127.0.0.1", 26379);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", okvm_thr.master.ip, okvm_thr.master.port);

    // case 3
    HIREDIS_OKVM_LOG_INFO("Test case 3"); 
    // Ready to remove queue
    while(!QUEUE_EMPTY(&okvm_thr.slaves_head)){
        qptr = QUEUE_HEAD(&okvm_thr.slaves_head);
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        QUEUE_REMOVE(qptr);
        HIREDIS_OKVM_LOG_INFO("Remove slaves ip(%s) port(%d)", hi->ip, hi->port);
        free(hi);
        hi = NULL;
    }
    rc = hiredis_okvm_thread_get_replicas(&okvm_thr, hiredis_okvm_thread_get_slaves);
    QUEUE_FOREACH(qptr, &okvm_thr.slaves_head){
        hi = QUEUE_DATA(qptr, struct hiredis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 4
    HIREDIS_OKVM_LOG_INFO("Test case 4"); 
    free(okvm_thr.master.ip);
    okvm_thr.master.port = 0;
    rc = hiredis_okvm_thread_get_replicas(&okvm_thr, hiredis_okvm_thread_get_master);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", okvm_thr.master.ip, okvm_thr.master.port);

    return 0;
}

