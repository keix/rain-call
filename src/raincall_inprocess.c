#include "raincall_inprocess.h"

#include "call_reply.h"
#include "redismodule.h"

#include <strings.h>

struct raincall_InprocessBackend {
    raincall_Backend base;
    client *caller;
    char error[256];
};

static void raincall_inprocess_zfree(void *ptr) {
    zfree(ptr);
}

static raincall_Reply *raincall_reply_new(raincall_ReplyType type) {
    raincall_Reply *reply = zcalloc(sizeof(*reply));
    if (reply == NULL) return NULL;
    reply->type = type;
    reply->free_alloc = raincall_inprocess_zfree;
    return reply;
}

static raincall_Reply *raincall_reply_string(raincall_ReplyType type, const char *s,
                                             size_t len) {
    raincall_Reply *reply = raincall_reply_new(type);
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

static raincall_Reply *raincall_reply_error(const char *s) {
    return raincall_reply_string(RAINCALL_REPLY_ERROR, s, strlen(s));
}

static int raincall_inprocess_error(raincall_InprocessBackend *backend, const char *err) {
    if (err == NULL || err[0] == '\0') err = "unknown error";
    snprintf(backend->error, sizeof(backend->error), "%s", err);
    return -1;
}

static int raincall_command_is(const char *arg, size_t len, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    return len == cmd_len && !strncasecmp(arg, cmd, len);
}

static int raincall_command_is_denylisted(int argc, const char **argv, size_t *argvlen) {
    UNUSED(argc);

    if (argvlen[0] >= 5 && !strncasecmp(argv[0], "RAIN.", 5)) return 1;

    return raincall_command_is(argv[0], argvlen[0], "EVAL") ||
           raincall_command_is(argv[0], argvlen[0], "EVALSHA") ||
           raincall_command_is(argv[0], argvlen[0], "SCRIPT") ||
           raincall_command_is(argv[0], argvlen[0], "FUNCTION") ||
           raincall_command_is(argv[0], argvlen[0], "FCALL") ||
           raincall_command_is(argv[0], argvlen[0], "FCALL_RO") ||
           raincall_command_is(argv[0], argvlen[0], "MULTI") ||
           raincall_command_is(argv[0], argvlen[0], "EXEC") ||
           raincall_command_is(argv[0], argvlen[0], "WATCH") ||
           raincall_command_is(argv[0], argvlen[0], "DISCARD") ||
           raincall_command_is(argv[0], argvlen[0], "UNWATCH") ||
           raincall_command_is(argv[0], argvlen[0], "SUBSCRIBE") ||
           raincall_command_is(argv[0], argvlen[0], "PSUBSCRIBE") ||
           raincall_command_is(argv[0], argvlen[0], "SSUBSCRIBE") ||
           raincall_command_is(argv[0], argvlen[0], "BLPOP") ||
           raincall_command_is(argv[0], argvlen[0], "BRPOP") ||
           raincall_command_is(argv[0], argvlen[0], "BRPOPLPUSH") ||
           raincall_command_is(argv[0], argvlen[0], "BZPOPMIN") ||
           raincall_command_is(argv[0], argvlen[0], "BZPOPMAX") ||
           raincall_command_is(argv[0], argvlen[0], "XREAD") ||
           raincall_command_is(argv[0], argvlen[0], "CLIENT") ||
           raincall_command_is(argv[0], argvlen[0], "CONFIG") ||
           raincall_command_is(argv[0], argvlen[0], "SHUTDOWN") ||
           raincall_command_is(argv[0], argvlen[0], "DEBUG") ||
           raincall_command_is(argv[0], argvlen[0], "MODULE") ||
           raincall_command_is(argv[0], argvlen[0], "AUTH") ||
           raincall_command_is(argv[0], argvlen[0], "SELECT");
}

static int raincall_create_argv(int argc, const char **argv, size_t *argvlen,
                                robj ***out) {
    robj **objects = zcalloc(sizeof(*objects) * argc);
    if (objects == NULL) return C_ERR;

    for (int i = 0; i < argc; i++) {
        objects[i] = createStringObject(argv[i], argvlen[i]);
        if (objects[i] == NULL) {
            for (int j = 0; j < i; j++) decrRefCount(objects[j]);
            zfree(objects);
            return C_ERR;
        }
    }

    *out = objects;
    return C_OK;
}

static CallReply *raincall_capture_reply(client *c) {
    sds proto = sdsnewlen(c->buf, c->bufpos);
    c->bufpos = 0;

    while (listLength(c->reply)) {
        clientReplyBlock *o = listNodeValue(listFirst(c->reply));
        proto = sdscatlen(proto, o->buf, o->used);
        listDelNode(c->reply, listFirst(c->reply));
    }

    CallReply *reply = callReplyCreate(proto, c->deferred_reply_errors, NULL);
    c->deferred_reply_errors = NULL;
    return reply;
}

static raincall_ReplyType raincall_reply_type_from_call_reply(int type) {
    switch (type) {
    case REDISMODULE_REPLY_STRING:
        return RAINCALL_REPLY_STRING;
    case REDISMODULE_REPLY_ERROR:
        return RAINCALL_REPLY_ERROR;
    case REDISMODULE_REPLY_INTEGER:
        return RAINCALL_REPLY_INTEGER;
    case REDISMODULE_REPLY_ARRAY:
        return RAINCALL_REPLY_ARRAY;
    case REDISMODULE_REPLY_NULL:
        return RAINCALL_REPLY_NIL;
    case REDISMODULE_REPLY_MAP:
        return RAINCALL_REPLY_MAP;
    case REDISMODULE_REPLY_SET:
        return RAINCALL_REPLY_SET;
    case REDISMODULE_REPLY_BOOL:
        return RAINCALL_REPLY_BOOL;
    case REDISMODULE_REPLY_DOUBLE:
        return RAINCALL_REPLY_DOUBLE;
    case REDISMODULE_REPLY_BIG_NUMBER:
        return RAINCALL_REPLY_BIGNUM;
    case REDISMODULE_REPLY_VERBATIM_STRING:
        return RAINCALL_REPLY_VERB;
    case REDISMODULE_REPLY_ATTRIBUTE:
        return RAINCALL_REPLY_ATTR;
    default:
        return RAINCALL_REPLY_ERROR;
    }
}

static raincall_Reply *raincall_reply_from_call_reply(CallReply *src) {
    int type = callReplyType(src);
    raincall_Reply *dst = raincall_reply_new(raincall_reply_type_from_call_reply(type));
    size_t len;
    const char *s;

    if (dst == NULL) return NULL;

    switch (type) {
    case REDISMODULE_REPLY_STRING:
    case REDISMODULE_REPLY_ERROR:
        s = callReplyGetString(src, &len);
        if (s != NULL && len > 0) {
            dst->str = zmalloc(len + 1);
            if (dst->str == NULL) goto oom;
            memcpy(dst->str, s, len);
            dst->str[len] = '\0';
            dst->len = len;
        }
        break;
    case REDISMODULE_REPLY_INTEGER:
        dst->integer = callReplyGetLongLong(src);
        break;
    case REDISMODULE_REPLY_BOOL:
        dst->integer = callReplyGetBool(src);
        break;
    case REDISMODULE_REPLY_DOUBLE:
        dst->number = callReplyGetDouble(src);
        break;
    case REDISMODULE_REPLY_BIG_NUMBER:
        s = callReplyGetBigNumber(src, &len);
        if (s != NULL && len > 0) {
            dst->str = zmalloc(len + 1);
            if (dst->str == NULL) goto oom;
            memcpy(dst->str, s, len);
            dst->str[len] = '\0';
            dst->len = len;
        }
        break;
    case REDISMODULE_REPLY_VERBATIM_STRING: {
        const char *format;
        s = callReplyGetVerbatim(src, &len, &format);
        UNUSED(format);
        if (s != NULL && len > 0) {
            dst->str = zmalloc(len + 1);
            if (dst->str == NULL) goto oom;
            memcpy(dst->str, s, len);
            dst->str[len] = '\0';
            dst->len = len;
        }
        break;
    }
    case REDISMODULE_REPLY_ARRAY:
    case REDISMODULE_REPLY_SET:
    case REDISMODULE_REPLY_ATTRIBUTE: {
        dst->elements = callReplyGetLen(src);
        if (dst->elements == 0) break;
        dst->element = zcalloc(sizeof(*dst->element) * dst->elements);
        if (dst->element == NULL) goto oom;
        for (size_t i = 0; i < dst->elements; i++) {
            CallReply *child = type == REDISMODULE_REPLY_SET ?
                callReplyGetSetElement(src, i) : callReplyGetArrayElement(src, i);
            dst->element[i] = raincall_reply_from_call_reply(child);
            if (dst->element[i] == NULL) goto oom;
        }
        break;
    }
    case REDISMODULE_REPLY_MAP: {
        dst->elements = callReplyGetLen(src) * 2;
        if (dst->elements == 0) break;
        dst->element = zcalloc(sizeof(*dst->element) * dst->elements);
        if (dst->element == NULL) goto oom;
        for (size_t i = 0; i < callReplyGetLen(src); i++) {
            CallReply *key = NULL;
            CallReply *value = NULL;
            if (callReplyGetMapElement(src, i, &key, &value) != C_OK) goto oom;
            dst->element[i * 2] = raincall_reply_from_call_reply(key);
            dst->element[i * 2 + 1] = raincall_reply_from_call_reply(value);
            if (dst->element[i * 2] == NULL || dst->element[i * 2 + 1] == NULL) goto oom;
        }
        break;
    }
    case REDISMODULE_REPLY_NULL:
        break;
    default:
        raincall_reply_free(dst);
        return raincall_reply_error("unsupported Redis reply type");
    }

    return dst;

oom:
    raincall_reply_free(dst);
    return NULL;
}

static int raincall_inprocess_reply_error(raincall_Reply **out, sds err) {
    *out = raincall_reply_string(RAINCALL_REPLY_ERROR, err, sdslen(err));
    sdsfree(err);
    return *out != NULL ? 0 : -1;
}

static int raincall_inprocess_call(raincall_Backend *base, int argc, const char **argv,
                                   size_t *argvlen, raincall_Reply **out) {
    raincall_InprocessBackend *backend = (raincall_InprocessBackend *)base;
    client *c = NULL;
    robj **objects = NULL;
    sds err = NULL;
    CallReply *call_reply = NULL;

    *out = NULL;

    if (backend->caller == NULL) {
        return raincall_inprocess_error(backend, "in-process backend has no caller");
    }
    if (argc < 1 || argv[0] == NULL) {
        *out = raincall_reply_error("RAIN.CALL: expected command name");
        return *out != NULL ? 0 : raincall_inprocess_error(backend, "out of memory");
    }
    if (raincall_command_is_denylisted(argc, argv, argvlen)) {
        *out = raincall_reply_error("command is not allowed by the in-process v0 backend");
        return *out != NULL ? 0 : raincall_inprocess_error(backend, "out of memory");
    }

    if (raincall_create_argv(argc, argv, argvlen, &objects) != C_OK) {
        return raincall_inprocess_error(backend, "out of memory");
    }

    c = createClient(NULL);
    c->flags |= CLIENT_MODULE | CLIENT_DENY_BLOCKING;
    c->db = backend->caller->db;
    c->user = backend->caller->user;
    c->argv = objects;
    c->argc = c->argv_len = argc;
    c->resp = backend->caller->resp;

    moduleCallCommandFilters(c);
    c->cmd = c->lastcmd = c->realcmd = lookupCommand(c->argv, c->argc);

    if (!commandCheckExistence(c, &err)) {
        int ret = raincall_inprocess_reply_error(out, err);
        freeClient(c);
        return ret == 0 ? 0 : raincall_inprocess_error(backend, "out of memory");
    }
    if (!commandCheckArity(c->cmd, c->argc, &err)) {
        int ret = raincall_inprocess_reply_error(out, err);
        freeClient(c);
        return ret == 0 ? 0 : raincall_inprocess_error(backend, "out of memory");
    }

    uint64_t cmd_flags = getCommandFlags(c);
    if (cmd_flags & (CMD_NOSCRIPT | CMD_BLOCKING | CMD_ADMIN | CMD_PUBSUB)) {
        *out = raincall_reply_error("command is not allowed by the in-process v0 backend");
        freeClient(c);
        return *out != NULL ? 0 : raincall_inprocess_error(backend, "out of memory");
    }

    int acl_errpos = 0;
    if (ACLCheckAllUserCommandPerm(backend->caller->user, c->cmd, c->argv, c->argc, NULL, &acl_errpos) != ACL_OK) {
        UNUSED(acl_errpos);
        *out = raincall_reply_error("NOPERM this user has no permissions to run the command");
        freeClient(c);
        return *out != NULL ? 0 : raincall_inprocess_error(backend, "out of memory");
    }

    call(c, CMD_CALL_FULL);
    if (c->flags & CLIENT_BLOCKED) {
        freeClient(c);
        return raincall_inprocess_error(backend, "in-process backend command blocked");
    }

    call_reply = raincall_capture_reply(c);
    *out = raincall_reply_from_call_reply(call_reply);
    freeCallReply(call_reply);
    freeClient(c);

    return *out != NULL ? 0 : raincall_inprocess_error(backend, "out of memory");
}

static const char *raincall_inprocess_last_error(raincall_Backend *base) {
    raincall_InprocessBackend *backend = (raincall_InprocessBackend *)base;
    return backend->error;
}

static void raincall_inprocess_close(raincall_Backend *base) {
    raincall_InprocessBackend *backend = (raincall_InprocessBackend *)base;
    zfree(backend);
}

raincall_InprocessBackend *raincall_inprocess_backend_new(void) {
    raincall_InprocessBackend *backend = zcalloc(sizeof(*backend));
    if (backend == NULL) return NULL;

    backend->base.call = raincall_inprocess_call;
    backend->base.error = raincall_inprocess_last_error;
    backend->base.close = raincall_inprocess_close;
    return backend;
}

raincall_Backend *raincall_inprocess_backend_base(raincall_InprocessBackend *backend) {
    return &backend->base;
}

void raincall_inprocess_backend_set_caller(raincall_InprocessBackend *backend, client *caller) {
    backend->caller = caller;
}
