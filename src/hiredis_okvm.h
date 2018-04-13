#ifndef _HIREDIS_OKVM_H_
#define _HIREDIS_OKVM_H_

#define REDIS_OKVM_OK 0
#define REDIS_OKVM_ERROR -1
#define REDIS_OKVM_WAIT_REPLY -2

#define INIT_HIREDIS_OKVM(okvm) \
    do{\
        (okvm)->connections = 1;\
        (okvm)->db_index = 0;\
        (okvm)->redis_host = NULL;\
        (okvm)->db_name = NULL;\
        (okvm)->password = NULL;\
    }while(0);

struct redis_okvm
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
    /* The password for redis server */
    char *password;
};

// The redis reply type for object key-value model
typedef void redis_okvm_reply;
struct redis_okvm_reply_iterator
{
    redis_okvm_reply *reply;
    int pos;
};

extern int redis_okvm_init(struct redis_okvm *param);
extern int redis_okvm_fini();

extern int redis_okvm_write(const char *cmd, int len);
extern redis_okvm_reply * redis_okvm_read(const char *cmd, int len);
extern void redis_okvm_reply_free(redis_okvm_reply *reply);

// if has next element return 1; otherwise return 0.
extern int redis_okvm_reply_has_next(struct redis_okvm_reply_iterator *it); 
extern struct redis_okvm_reply_iterator* redis_okvm_reply_get_iterator(redis_okvm_reply *reply); 
extern void redis_okvm_reply_free_iterator(struct redis_okvm_reply_iterator *it);

// return the new reply object
extern struct redis_okvm_reply_iterator * redis_okvm_reply_next(struct redis_okvm_reply_iterator *it);
// return int value by iterator
extern int redis_okvm_reply_next_int(struct redis_okvm_reply_iterator *it);
// return string value by iterator
extern char* redis_okvm_reply_next_str(struct redis_okvm_reply_iterator *it);

// return int value for reply directly
extern int redis_okvm_reply_int(redis_okvm_reply *reply);
// return string value for reply directly
extern char* redis_okvm_reply_str(redis_okvm_reply *reply);

extern void redis_okvm_set_log_level(int l);

#endif//_HIREDIS_OKVM_H_

