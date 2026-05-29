/*
 * Rain-Call host capability installer for Moonquakes.
 *
 * Phase 0: RAIN.CALL("PING") returns a hardcoded "PONG".
 * RESP serialization and a real Redis bridge land in later commits.
 */

#include "raincall.h"

static int rain_call(mq_State *L) {
    size_t cmd_len = 0;
    const char *cmd = mq_tolstring(L, 1, &cmd_len);
    (void)cmd;
    (void)cmd_len;

    mq_pushstring(L, "PONG");
    return 1;
}

int raincall_install(mq_State *L) {
    mq_newtable(L);

    mq_pushcfunction(L, rain_call);
    mq_setfield(L, -2, "CALL");

    mq_setglobal(L, "RAIN");
    return 0;
}
