#ifndef RAINCALL_H
#define RAINCALL_H

#include "moonquakes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAINCALL_VERSION "0.1.0"

int raincall_install(mq_State *L);

#ifdef __cplusplus
}
#endif

#endif /* RAINCALL_H */
