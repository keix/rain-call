/*
 * RAIN.FALL: Redis protocol entry into Moonquakes.
 *
 * v0 deliberately keeps RAIN.CALL on the TCP backend. This command proves the
 * reverse boundary first: Redis -> Moonquakes -> Redis client reply.
 */

#include "server.h"

#include "moonquakes.h"
#include "raincall.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mq_State *rainfall_state = NULL;
static raincall_State *rainfall_raincall_state = NULL;

typedef struct rainFallBackend {
    raincall_Backend base;
    client *caller;
    char error[256];
} rainFallBackend;

static rainFallBackend rainfall_backend = {0};

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

static raincall_Reply *rainFallReplyNew(raincall_ReplyType type) {
    raincall_Reply *reply = zcalloc(sizeof(*reply));
    if (reply == NULL) return NULL;
    reply->type = type;
    reply->free_alloc = zfree;
    return reply;
}

static raincall_Reply *rainFallReplyString(raincall_ReplyType type, const char *s,
                                           size_t len) {
    raincall_Reply *reply = rainFallReplyNew(type);
    if (reply == NULL) return NULL;
    reply->str = zmalloc(len + 1);
    if (reply->str == NULL) {
        raincall_reply_free(reply);
        return NULL;
    }
    memcpy(reply->str, s, len);
    reply->str[len] = '\0';
    reply->len = len;
    return reply;
}

static raincall_Reply *rainFallReplyCString(raincall_ReplyType type, const char *s) {
    return rainFallReplyString(type, s, strlen(s));
}

static int rainFallBackendError(rainFallBackend *backend, const char *err) {
    if (err == NULL || err[0] == '\0') err = "unknown error";
    snprintf(backend->error, sizeof(backend->error), "%s", err);
    return -1;
}

static int rainFallCommandIs(const char *arg, size_t len, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    return len == cmd_len && !strncasecmp(arg, cmd, len);
}

static int rainFallBackendCall(raincall_Backend *base, int argc, const char **argv,
                               size_t *argvlen, raincall_Reply **out) {
    rainFallBackend *backend = (rainFallBackend *)base;

    *out = NULL;

    if (backend->caller == NULL) {
        return rainFallBackendError(backend, "in-process backend has no caller");
    }
    if (argc < 1 || argv[0] == NULL) {
        return rainFallBackendError(backend, "RAIN.CALL: expected command name");
    }

    if (rainFallCommandIs(argv[0], argvlen[0], "PING")) {
        if (argc == 1) {
            *out = rainFallReplyCString(RAINCALL_REPLY_STATUS, "PONG");
        } else if (argc == 2) {
            *out = rainFallReplyString(RAINCALL_REPLY_STRING, argv[1], argvlen[1]);
        } else {
            return rainFallBackendError(backend, "wrong number of arguments for 'PING'");
        }
        return *out != NULL ? 0 : rainFallBackendError(backend, "out of memory");
    }

    if (rainFallCommandIs(argv[0], argvlen[0], "GET")) {
        robj *key;
        kvobj *kv;
        robj *value;
        robj *decoded;

        if (argc != 2) {
            return rainFallBackendError(backend, "wrong number of arguments for 'GET'");
        }

        key = createStringObject(argv[1], argvlen[1]);
        kv = lookupKeyRead(backend->caller->db, key);
        decrRefCount(key);

        if (kv == NULL) {
            *out = rainFallReplyNew(RAINCALL_REPLY_NIL);
            return *out != NULL ? 0 : rainFallBackendError(backend, "out of memory");
        }

        value = (robj *)kv;
        if (value->type != OBJ_STRING) {
            return rainFallBackendError(backend, "WRONGTYPE Operation against a key holding the wrong kind of value");
        }

        decoded = getDecodedObject(value);
        *out = rainFallReplyString(RAINCALL_REPLY_STRING, decoded->ptr, sdslen(decoded->ptr));
        decrRefCount(decoded);
        return *out != NULL ? 0 : rainFallBackendError(backend, "out of memory");
    }

    if (rainFallCommandIs(argv[0], argvlen[0], "SET")) {
        robj *key;
        robj *value;

        if (argc != 3) {
            return rainFallBackendError(backend, "wrong number of arguments for 'SET'");
        }

        key = createStringObject(argv[1], argvlen[1]);
        value = createStringObject(argv[2], argvlen[2]);
        setKey(backend->caller, backend->caller->db, key, &value, 0);
        notifyKeyspaceEvent(NOTIFY_STRING, "set", key, backend->caller->db->id);
        server.dirty++;
        decrRefCount(key);

        *out = rainFallReplyCString(RAINCALL_REPLY_STATUS, "OK");
        return *out != NULL ? 0 : rainFallBackendError(backend, "out of memory");
    }

    return rainFallBackendError(backend, "command is not allowed by the in-process v0 backend");
}

static const char *rainFallBackendLastError(raincall_Backend *base) {
    rainFallBackend *backend = (rainFallBackend *)base;
    return backend->error;
}

static void rainFallBackendClose(raincall_Backend *base) {
    UNUSED(base);
}

static mq_State *rainFallGetState(void) {
    if (rainfall_state != NULL) return rainfall_state;

    rainfall_state = mq_newstate();
    if (rainfall_state == NULL) return NULL;

    rainfall_backend.base.call = rainFallBackendCall;
    rainfall_backend.base.error = rainFallBackendLastError;
    rainfall_backend.base.close = rainFallBackendClose;

    rainfall_raincall_state = raincall_open_backend(&rainfall_backend.base);
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

    rainfall_backend.caller = c;
    base_top = mq_gettop(L);
    reader.source = c->argv[1]->ptr;
    reader.len = sdslen(c->argv[1]->ptr);
    reader.done = 0;

    status = mq_load(L, rainFallReadChunk, &reader, "=RAIN.FALL", "t");
    if (status != MQ_OK) {
        addReplyError(c, "RAIN.FALL: failed to load source");
        mq_settop(L, base_top);
        rainfall_backend.caller = NULL;
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
        rainfall_backend.caller = NULL;
        return;
    }

    rainFallReplyFromLua(c, L);
    mq_settop(L, base_top);
    rainfall_backend.caller = NULL;
}
