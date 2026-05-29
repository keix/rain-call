/*
 * Rain-Call host capability installer for Moonquakes.
 *
 * Moonquakes is a neutral Lua 5.4 runtime; it knows nothing about Redis.
 * raincall_install() injects the RAIN table into a Moonquakes state, with
 * RAIN.CALL bound to a C callback that crosses back to the host.
 */

#ifndef RAINCALL_H
#define RAINCALL_H

#include "moonquakes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install the RAIN host capability into a Moonquakes state.
 *
 * Creates a new global table RAIN and registers RAIN.CALL as a C function.
 * After this call, Lua code in L can do:
 *
 *   RAIN.CALL("PING")
 *
 * Returns 0 on success.
 */
int raincall_install(mq_State *L);

#ifdef __cplusplus
}
#endif

#endif /* RAINCALL_H */
