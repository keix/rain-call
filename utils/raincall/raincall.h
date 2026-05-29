#ifndef RAINCALL_H
#define RAINCALL_H

#include "moonquakes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAINCALL_VERSION "0.1.0"

typedef struct raincall_State raincall_State;

raincall_State *raincall_open(const char *host, int port);
int raincall_install_state(mq_State *L, raincall_State *R);
int raincall_install(mq_State *L);
void raincall_close(raincall_State *R);

#ifdef __cplusplus
}
#endif

#endif /* RAINCALL_H */
