#ifndef _HIREDIS_OKVM_H_
#define _HIREDIS_OKVM_H_

#define INIT_HIREDIS_OKVM(okvm) \
    do{\
        (okvm)->connections = 1;\
        (okvm)->db_index = 0;\
        (okvm)->redis_host = NULL;\
        (okvm)->db_name = NULL;\
    }while(0);

struct hiredis_okvm
{
    // The number of the connection between the hiredis and redis server.
    int connections;
    int db_index;
    /**
     * The sentinel host for redis cluster.
     * The fromat like : 192.168.1.2:26379;192.168.1.3:26379;192.168.1.4:26379
     */
    char *redis_host;
    /* The master group name for redis cluster */
    char *db_name;
};

extern int hiredis_okvm_init(struct hiredis_okvm *param);
extern int hiredis_okvm_fini();

extern int hiredis_okvm_do_async(const char *cmd);
extern void *hiredis_okvm_do_sync(const char *cmd);

extern void hiredis_okvm_set_log_level(int l);

#endif//_HIREDIS_OKVM_H_

