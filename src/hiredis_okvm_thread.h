#ifndef _HIREDIS_OKVM_CONNPOOL_H_
#define _HIREDIS_OKVM_CONNPOOL_H_

#include <uv.h>
#include <hiredis.h>

struct hiredis_okvm_thread
{
    // The redis context which indicate the connection between the hiredis and the redis server.
    redisContext *write_ctx;
    redisContext *read_ctx;
    int role;

    uv_loop_t loop;
    uv_thread_t worker;
    uv_mutex_t state_mutex;
    uv_cond_t state_cond;
    uv_async_t notify;
};

#define INIT_HIREDIS_OKVM_THREAD(okvm_thr) \
    do{\
        (okvm_thr)->write_ctx = NULL;\
        (okvm_thr)->read_ctx = NULL;\
    }while(0);



int hiredis_okvm_thread_init(struct hiredis_okvm_thread *okvm_thr);
int hiredis_okvm_thread_fini(struct hiredis_okvm_thread *okvm_thr);

#endif//_HIREDIS_OKVM_CONNPOOL_H_

