#ifndef _POWERFOX_ASSERT_SHIM_
#define _POWERFOX_ASSERT_SHIM_
#include_next <assert.h>
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && \
    !defined(__cplusplus) && !defined(static_assert)
#define static_assert(cond, msg) _Static_assert(cond, msg)
#endif
#endif
