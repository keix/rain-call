#ifndef RAINCALL_INPROCESS_H
#define RAINCALL_INPROCESS_H

#include "server.h"
#include "raincall.h"

typedef struct raincall_InprocessBackend raincall_InprocessBackend;

raincall_InprocessBackend *raincall_inprocess_backend_new(void);
raincall_Backend *raincall_inprocess_backend_base(raincall_InprocessBackend *backend);
void raincall_inprocess_backend_set_caller(raincall_InprocessBackend *backend, client *caller);

#endif /* RAINCALL_INPROCESS_H */
