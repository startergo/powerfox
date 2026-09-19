#ifndef _POWERFOX_STRING_SHIM_
#define _POWERFOX_STRING_SHIM_
#include_next <string.h>
#include <sys/cdefs.h>
__BEGIN_DECLS
size_t strnlen(const char *__s, size_t __maxlen);
__END_DECLS
#endif
