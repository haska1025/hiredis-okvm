#ifndef _HIREDIS_OKVM_LOG_H_
#define _HIREDIS_OKVM_LOG_H_

#include <syslog.h>

extern int redis_okvm_get_log_level();

#define HIREDIS_OKVM_LOG(level,fmt, ...) if (redis_okvm_get_log_level() >= level) syslog(LOG_LOCAL3|level, "%s:%d:(%s): "fmt , __FILE__,__LINE__,__FUNCTION__, ##__VA_ARGS__);

#define HIREDIS_OKVM_LOG_ERROR(fmt, ...)  HIREDIS_OKVM_LOG(LOG_ERR, fmt, ##__VA_ARGS__)
#define HIREDIS_OKVM_LOG_WARNING(fmt, ...)  HIREDIS_OKVM_LOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define HIREDIS_OKVM_LOG_INFO(fmt, ...)  HIREDIS_OKVM_LOG(LOG_INFO, fmt, ##__VA_ARGS__)
#define HIREDIS_OKVM_LOG_DEBUG(fmt, ...)  HIREDIS_OKVM_LOG(LOG_DEBUG, fmt, ##__VA_ARGS__)

#endif//_HIREDIS_OKVM_LOG_H_


