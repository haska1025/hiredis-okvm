#include "../src/hiredis_okvm_thread.c"
#include "../src/hiredis_okvm.c"

void test_sentinel()
{
    int rc = 0;
    char *sentinel_ip = "127.0.0.1";
    char *sentinel_ips = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";

    int sentinel_port = 26379;
    struct redis_okvm_thread okvm_thr;
    struct redis_okvm_host_info *hi;
    QUEUE *qptr;
    struct redis_okvm_mgr mgr;

    hi = NULL;
    qptr = NULL;

    QUEUE_INIT(&mgr.slaves_head);
    QUEUE_INIT(&mgr.master.link);
    mgr.master.ip = NULL;
    mgr.master.port = 0;

    g_okvm.read_connections = 4;
    g_okvm.write_connections = 4;
    g_okvm.db_index = 2;
    g_okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    //g_okvm.redis_host = "127.0.0.1:26379;127.0.0.1:26380;127.0.0.1:26381";
    g_okvm.db_name = "tang-cluster";
    //g_okvm.db_name = "mymaster";


    // case 1
    HIREDIS_OKVM_LOG_INFO("Test case 1"); 
    rc = redis_okvm_mgr_get_slaves(&mgr, sentinel_ip, sentinel_port);
    QUEUE_FOREACH(qptr, &mgr.slaves_head){
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 2
    HIREDIS_OKVM_LOG_INFO("Test case 2"); 
    rc = redis_okvm_mgr_get_master(&mgr, sentinel_ip, sentinel_port);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", mgr.master.ip, mgr.master.port);

    // case 3
    HIREDIS_OKVM_LOG_INFO("Test case 3"); 
    // Ready to remove queue
    while(!QUEUE_EMPTY(&mgr.slaves_head)){
        qptr = QUEUE_HEAD(&mgr.slaves_head);
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        QUEUE_REMOVE(qptr);
        HIREDIS_OKVM_LOG_INFO("Remove slaves ip(%s) port(%d)", hi->ip, hi->port);
        free(hi);
        hi = NULL;
    }
    rc = redis_okvm_mgr_get_replicas(&mgr, sentinel_ips, redis_okvm_mgr_get_slaves);
    QUEUE_FOREACH(qptr, &mgr.slaves_head){
        hi = QUEUE_DATA(qptr, struct redis_okvm_host_info, link);
        HIREDIS_OKVM_LOG_INFO("Slaves ip(%s) port(%d)", hi->ip, hi->port);
    }
    // case 4
    HIREDIS_OKVM_LOG_INFO("Test case 4"); 
    free(mgr.master.ip);
    mgr.master.port = 0;
    rc = redis_okvm_mgr_get_replicas(&mgr, sentinel_ips, redis_okvm_mgr_get_master);
    HIREDIS_OKVM_LOG_INFO("Master ip(%s) port(%d)", mgr.master.ip, mgr.master.port);

    // clear
    g_okvm.read_connections = 0;
    g_okvm.write_connections = 0;
    g_okvm.db_index = 0;
    g_okvm.redis_host = NULL;
    g_okvm.db_name =  NULL;
}
void test_cmd()
{
    char *msg = NULL;
    char *msg2 = NULL;
    char *msgget = NULL;
    void *reply = NULL;

    int i = 0;
    int length = 0;
    char *field = NULL;

    msg = "hset test:1 name zhangsan";
    redis_okvm_write(msg);

    msg2 = "hset test:1 age 35";
    redis_okvm_write(msg2);

    msgget = "hgetall test:1";
    reply = redis_okvm_read(msgget);
    if (!reply){
        HIREDIS_OKVM_LOG_ERROR("cmd(%s) failed!", msgget);
        return;
    }

    length = redis_okvm_reply_length(reply);
    for (; i < length;){
        field = redis_okvm_reply_idxof_str(reply, i++);
        if (strcmp(field, "name") == 0){
            HIREDIS_OKVM_LOG_INFO("getall_test1 name = %s", redis_okvm_reply_idxof_str(reply, i++));
        }else if (strcmp(field, "age") == 0){
            HIREDIS_OKVM_LOG_INFO("getall_test1 age = %s", redis_okvm_reply_idxof_str(reply, i++));
        }else{
            i++;
        }
    }
}

void test_bulk_cmd()
{
    void *ctx = NULL;
    void *reply = NULL;
    int i = 0;
    int length = 0;
    char *field = NULL;

    ctx = redis_okvm_bulk_write_begin();
    if (!ctx){
        HIREDIS_OKVM_LOG_ERROR("Catn't get bulk write ctx");
        return;
    }
    redis_okvm_bulk_write(ctx, "hmset street:2 name kejierlu number 124 direction east-west");
    redis_okvm_bulk_write(ctx, "sadd street:2:person zhangsan2 lisi2 wangwu2");
    redis_okvm_bulk_write(ctx, "sadd street:2:signlight 1 2 3");
    redis_okvm_bulk_write_end(ctx);

    ctx = redis_okvm_bulk_read_begin();
    if (!ctx){
        HIREDIS_OKVM_LOG_ERROR("Can't get bulk read ctx");
        return;
    }

    redis_okvm_bulk_read(ctx, "hgetall street:2");
    redis_okvm_bulk_read(ctx, "smembers street:2:person");
    redis_okvm_bulk_read(ctx, "smembers street:2:signlight");

    reply = redis_okvm_bulk_read_reply(ctx);
    length = redis_okvm_reply_length(reply);
    for (i = 0; i < length;){
        field = redis_okvm_reply_idxof_str(reply, i++);
        if (strcmp(field, "name") == 0){
            HIREDIS_OKVM_LOG_INFO("street:2 name = %s", redis_okvm_reply_idxof_str(reply, i++));
        }else if (strcmp(field, "number") == 0){
            HIREDIS_OKVM_LOG_INFO("street:2 number = %s", redis_okvm_reply_idxof_str(reply, i++));
        }else if (strcmp(field, "direction") == 0){
            HIREDIS_OKVM_LOG_INFO("street:2 direction = %s", redis_okvm_reply_idxof_str(reply, i++));
        }else{
            i++;
        }
    }
    redis_okvm_reply_free(reply);

    reply = redis_okvm_bulk_read_reply(ctx);
    length = redis_okvm_reply_length(reply);
    for (i = 0; i < length;i++){
        HIREDIS_OKVM_LOG_INFO("street:2:person(%s)", redis_okvm_reply_idxof_str(reply, i));
    }
    redis_okvm_reply_free(reply);

    reply = redis_okvm_bulk_read_reply(ctx);
    length = redis_okvm_reply_length(reply);
    for (i = 0; i < length;i++){
        HIREDIS_OKVM_LOG_INFO("street:2:signlight (%s)", redis_okvm_reply_idxof_str(reply, i));
    }
    redis_okvm_reply_free(reply);

    redis_okvm_bulk_read_end(ctx);
}

int main( int argc, char ** argv)
{
    char *host = NULL; 
    int rc = 0;
    struct redis_okvm okvm;

    test_sentinel();

    INIT_HIREDIS_OKVM(&okvm);

    okvm.read_connections = 1;
    okvm.write_connections = 1;
    okvm.db_index = 2;
    okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    //okvm.redis_host = "127.0.0.1:26379;127.0.0.1:26380;127.0.0.1:26381";
    okvm.db_name = "tang-cluster";
    okvm.password = "d8e8fca2dc0f896fd7cb4cb0031ba249";
    //okvm.db_name = "mymaster";

    rc = redis_okvm_init(&okvm);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("Init okvm failed");
        redis_okvm_fini();
        return -1;
    }

    test_cmd();
    test_bulk_cmd();

    sleep(1);
    redis_okvm_fini();

    HIREDIS_OKVM_LOG_INFO("Test over");
}

