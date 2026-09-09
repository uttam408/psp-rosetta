/* pool.h — fixed-size object pools. No malloc/free at runtime; the world calls
 * <name>_put via Entity.recycle when an entity dies. get() returns NULL when the
 * pool is exhausted (the spawn is simply dropped). */
#ifndef RT_POOL_H
#define RT_POOL_H

#include <string.h>

#define POOL(NAME, TYPE, CAP)                                                   \
    static TYPE  NAME##_slot[CAP];                                              \
    static TYPE *NAME##_free[CAP];                                              \
    static int   NAME##_nfree = -1;                                             \
    static TYPE *NAME##_get(void) {                                             \
        if (NAME##_nfree < 0) {                                                 \
            for (int _i = 0; _i < (CAP); _i++) NAME##_free[_i] = &NAME##_slot[_i]; \
            NAME##_nfree = (CAP);                                               \
        }                                                                       \
        if (NAME##_nfree == 0) return 0;                                        \
        TYPE *_p = NAME##_free[--NAME##_nfree];                                 \
        memset(_p, 0, sizeof *_p);                                              \
        return _p;                                                             \
    }                                                                           \
    static void NAME##_put(void *p) {                                          \
        if (NAME##_nfree >= 0 && NAME##_nfree < (CAP))                          \
            NAME##_free[NAME##_nfree++] = (TYPE *)p;                            \
    }

#endif
