SET(T1HASRC     t1ha1.c
                t1ha2.c)

ADD_LIBRARY(rspamd-t1ha STATIC ${T1HASRC})
SET_TARGET_PROPERTIES(rspamd-t1ha PROPERTIES VERSION ${RSPAMD_VERSION})
ADD_DEFINITIONS("-DT1HA_USE_FAST_ONESHOT_READ=0")
