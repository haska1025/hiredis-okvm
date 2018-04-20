#ifndef _HIREDIS_OKVM_H_
#define _HIREDIS_OKVM_H_

#define REDIS_OKVM_OK 0
#define REDIS_OKVM_ERROR -1
#define REDIS_OKVM_CONN_FAILED -2
#define REDIS_OKVM_AUTH_FAILED -3
#define REDIS_OKVM_ROLE_FAILED -4

#define INIT_HIREDIS_OKVM(okvm) \
    do{\
        (okvm)->read_connections = 1;\
        (okvm)->write_connections = 1;\
        (okvm)->db_index = 0;\
        (okvm)->redis_host = NULL;\
        (okvm)->db_name = NULL;\
        (okvm)->password = NULL;\
    }while(0);

struct redis_okvm
{
    // The number of the connection between the hiredis and redis server.
    int read_connections;
    int write_connections;
    int db_index;
    /**
     * The sentinel host for redis cluster.
     * The fromat like : 192.168.1.2:26379;192.168.1.3:26379;192.168.1.4:26379
     */
    char *redis_host;
    /* The master group name for redis cluster */
    char *db_name;
    /* The password for redis server */
    char *password;
};

extern int redis_okvm_init(struct redis_okvm *param);
extern int redis_okvm_fini();

extern int redis_okvm_write(const char *cmd);
// Return the reply pointer 
extern void* redis_okvm_read(const char *cmd);

// Return the context pointer
extern void *redis_okvm_bulk_write_begin();
extern int redis_okvm_bulk_write(void *ctx, const char *cmd);
extern void redis_okvm_bulk_write_end(void *ctx);

// Return the context pointer
extern void *redis_okvm_bulk_read_begin();
extern int redis_okvm_bulk_read(void *ctx, const char *cmd);
void *redis_okvm_bulk_read_reply(void *ctx);
extern void redis_okvm_bulk_read_end(void *ctx);

// Return the reply pointer 
extern void redis_okvm_reply_free(void *reply);

extern int redis_okvm_reply_length(void *reply);
// return int value for reply by index from array 
extern int redis_okvm_reply_idxof_int(void *reply, int idx);
// return str value for reply by index from array 
extern char* redis_okvm_reply_idxof_str(void *reply, int idx);
// return object value for reply by index from array 
extern void* redis_okvm_reply_idxof_obj(void *reply, int idx);
// return int value for reply directly
extern int redis_okvm_reply_int(void *reply);
// return string value for reply directly
extern char* redis_okvm_reply_str(void *reply);

extern void redis_okvm_set_log_level(int l);

#endif//_HIREDIS_OKVM_H_

