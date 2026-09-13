#ifndef POWERFOX_OS_LOG_COMPAT_H
#define POWERFOX_OS_LOG_COMPAT_H
#include <stdint.h>
typedef struct { void* dummy; }* os_log_t;
typedef uint8_t os_log_type_t;
#define OS_LOG_DEFAULT ((os_log_t)0)
#define OS_LOG_TYPE_DEFAULT 0x00
#define OS_LOG_TYPE_INFO 0x01
#define OS_LOG_TYPE_DEBUG 0x02
#define OS_LOG_TYPE_ERROR 0x10
#define OS_LOG_TYPE_FAULT 0x11
#define os_log_create(subsystem, category) ((os_log_t)0)
#define os_log(log, format, ...)
#define os_log_with_type(log, type, format, ...)
#define os_log_info(log, format, ...)
#define os_log_debug(log, format, ...)
#define os_log_error(log, format, ...)
#define os_log_fault(log, format, ...)
#endif
