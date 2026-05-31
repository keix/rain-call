/*
 * RAIN.FALL: Redis protocol entry into Moonquakes.
 *
 * RAIN.CALL uses an in-process backend when installed by redis-server.
 */

#include "server.h"

#include "moonquakes.h"
#include "raincall.h"
#include "raincall_inprocess.h"

#include <stdio.h>

static mq_State *rainfall_state = NULL;
static raincall_State *rainfall_raincall_state = NULL;
static raincall_InprocessBackend *rainfall_backend = NULL;

typedef struct rainFallReader {
    const char *source;
    size_t len;
    int done;
} rainFallReader;

static const char *rainFallReadChunk(mq_State *L, void *ud, size_t *size) {
    rainFallReader *reader = ud;
    UNUSED(L);

    if (reader->done) {
        *size = 0;
        return NULL;
    }

    reader->done = 1;
    *size = reader->len;
    return reader->source;
}

static mq_State *rainFallGetState(void) {
    if (rainfall_state != NULL) return rainfall_state;

    rainfall_state = mq_newstate();
    if (rainfall_state == NULL) return NULL;

    rainfall_backend = raincall_inprocess_backend_new();
    if (rainfall_backend == NULL) {
        mq_close(rainfall_state);
        rainfall_state = NULL;
        return NULL;
    }

    rainfall_raincall_state = raincall_open_backend(raincall_inprocess_backend_base(rainfall_backend));
    if (rainfall_raincall_state == NULL) {
        mq_close(rainfall_state);
        rainfall_state = NULL;
        return NULL;
    }

    raincall_install_state(rainfall_state, rainfall_raincall_state);
    return rainfall_state;
}

static void rainFallReplyFromLua(client *c, mq_State *L);

static void rainFallReplyTable(client *c, mq_State *L) {
    int isnum = 0;
    mq_Integer len;

    mq_len(L, -1);
    len = mq_tointegerx(L, -1, &isnum);
    mq_pop(L, 1);

    if (!isnum || len < 0) {
        addReplyError(c, "RAIN.FALL: table return is not an array");
        return;
    }

    addReplyArrayLen(c, (long)len);
    for (mq_Integer i = 1; i <= len; i++) {
        mq_geti(L, -1, i);
        rainFallReplyFromLua(c, L);
        mq_pop(L, 1);
    }
}

static void rainFallReplyFromLua(client *c, mq_State *L) {
    int t = mq_type(L, -1);

    switch (t) {
    case MQ_TNIL:
        addReplyNull(c);
        break;
    case MQ_TBOOLEAN:
        addReplyBool(c, mq_toboolean(L, -1));
        break;
    case MQ_TNUMBER:
        if (mq_isinteger(L, -1)) {
            addReplyLongLong(c, (long long)mq_tointeger(L, -1));
        } else {
            addReplyDouble(c, (double)mq_tonumber(L, -1));
        }
        break;
    case MQ_TSTRING: {
        size_t len = 0;
        const char *s = mq_tolstring(L, -1, &len);
        addReplyBulkCBuffer(c, s, len);
        break;
    }
    case MQ_TTABLE:
        rainFallReplyTable(c, L);
        break;
    default:
        addReplyErrorFormat(c, "RAIN.FALL: unsupported Lua return type: %s",
                            mq_typename(L, t));
        break;
    }
}

void rainFallCommand(client *c) {
    mq_State *L = rainFallGetState();
    int base_top;
    int status;
    rainFallReader reader;

    if (L == NULL) {
        addReplyError(c, "RAIN.FALL: failed to create Moonquakes state");
        return;
    }

    raincall_inprocess_backend_set_caller(rainfall_backend, c);
    base_top = mq_gettop(L);
    reader.source = c->argv[1]->ptr;
    reader.len = sdslen(c->argv[1]->ptr);
    reader.done = 0;

    status = mq_load(L, rainFallReadChunk, &reader, "=RAIN.FALL", "t");
    if (status != MQ_OK) {
        addReplyError(c, "RAIN.FALL: failed to load source");
        mq_settop(L, base_top);
        raincall_inprocess_backend_set_caller(rainfall_backend, NULL);
        return;
    }

    for (int i = 2; i < c->argc; i++) {
        mq_pushlstring(L, c->argv[i]->ptr, sdslen(c->argv[i]->ptr));
    }

    status = mq_pcall(L, c->argc - 2, 1, 0);
    if (status != MQ_OK) {
        size_t len = 0;
        const char *err = mq_tolstring(L, -1, &len);

        if (err != NULL) {
            addReplyErrorFormat(c, "RAIN.FALL: %.*s", (int)len, err);
        } else {
            addReplyError(c, "RAIN.FALL: Lua error");
        }
        mq_settop(L, base_top);
        raincall_inprocess_backend_set_caller(rainfall_backend, NULL);
        return;
    }

    rainFallReplyFromLua(c, L);
    mq_settop(L, base_top);
    raincall_inprocess_backend_set_caller(rainfall_backend, NULL);
}
