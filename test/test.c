#include "../src/hiredis_okvm_thread.c"
#include "../src/hiredis_okvm.c"

int main( int argc, char ** argv)
{
    char *host = NULL; 
    int rc = 0;
    struct hiredis_okvm okvm;

    INIT_HIREDIS_OKVM(&okvm);

    okvm.connections = 0;
    okvm.db_index = 2;
    okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    okvm.db_name = "tang-cluster";

    rc = hiredis_okvm_init(&okvm);
    if (rc != 0)
        return -1;

    host = hiredis_okvm_thread_get_slaves("192.168.32.84", 26379);

    return 0;
}

