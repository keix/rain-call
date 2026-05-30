#ifndef RAINCALL_H
#define RAINCALL_H

#include "moonquakes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAINCALL_VERSION "0.1.0"

typedef struct raincall_State raincall_State;
typedef struct raincall_Backend raincall_Backend;
typedef struct raincall_Reply raincall_Reply;

typedef enum raincall_ReplyType {
    RAINCALL_REPLY_STATUS,
    RAINCALL_REPLY_STRING,
    RAINCALL_REPLY_INTEGER,
    RAINCALL_REPLY_NIL,
    RAINCALL_REPLY_ARRAY,
    RAINCALL_REPLY_ERROR,
    RAINCALL_REPLY_DOUBLE,
    RAINCALL_REPLY_BOOL,
    RAINCALL_REPLY_MAP,
    RAINCALL_REPLY_SET,
    RAINCALL_REPLY_ATTR,
    RAINCALL_REPLY_PUSH,
    RAINCALL_REPLY_VERB,
    RAINCALL_REPLY_BIGNUM
} raincall_ReplyType;

struct raincall_Reply {
    raincall_ReplyType type;
    char *str;
    size_t len;
    long long integer;
    double number;
    size_t elements;
    raincall_Reply **element;
};

struct raincall_Backend {
    int (*call)(raincall_Backend *backend, int argc, const char **argv,
                size_t *argvlen, raincall_Reply **out);
    const char *(*error)(raincall_Backend *backend);
    void (*close)(raincall_Backend *backend);
};

raincall_State *raincall_open(const char *host, int port);
int raincall_install_state(mq_State *L, raincall_State *R);
int raincall_install(mq_State *L);
void raincall_close(raincall_State *R);
void raincall_reply_free(raincall_Reply *reply);

#ifdef __cplusplus
}
#endif

#endif /* RAINCALL_H */
