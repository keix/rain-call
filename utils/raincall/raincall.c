/*
 * Rain-Call host capability installer for Moonquakes.
 *
 * Phase II: RAIN.CALL dispatches through a TCP Redis backend.
 */

#include "raincall.h"

#include "hiredis.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct raincall_State {
    raincall_Backend *backend;
};

typedef struct raincall_TcpBackend {
    raincall_Backend base;
    char *host;
    int port;
    redisContext *redis;
    char error[256];
} raincall_TcpBackend;

static raincall_State *default_state = NULL;
static raincall_State *installed_state = NULL;

static char *raincall_strdup(const char *s) {
    size_t len;
    char *copy;

    if (s == NULL) return NULL;
    len = strlen(s);
    copy = malloc(len + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static raincall_Reply *raincall_reply_new(raincall_ReplyType type) {
    raincall_Reply *reply = calloc(1, sizeof(*reply));
    if (reply == NULL) return NULL;
    reply->type = type;
    return reply;
}

void raincall_reply_free(raincall_Reply *reply) {
    if (reply == NULL) return;
    for (size_t i = 0; i < reply->elements; i++) {
        raincall_reply_free(reply->element[i]);
    }
    free(reply->element);
    free(reply->str);
    free(reply);
}

static int raincall_raise(mq_State *L, const char *msg) {
    mq_pushstring(L, msg);
    return -1;
}

static int raincall_raise_redis(mq_State *L, raincall_State *R, const char *prefix) {
    char buf[512];
    const char *err = "unknown error";

    if (R != NULL && R->backend != NULL && R->backend->error != NULL) {
        const char *backend_error = R->backend->error(R->backend);
        if (backend_error != NULL && backend_error[0] != '\0') err = backend_error;
    }
    snprintf(buf, sizeof(buf), "%s: %s", prefix, err);
    return raincall_raise(L, buf);
}

static void raincall_tcp_set_error(raincall_TcpBackend *tcp, const char *err) {
    if (err == NULL || err[0] == '\0') err = "unknown error";
    snprintf(tcp->error, sizeof(tcp->error), "%s", err);
}

static raincall_ReplyType raincall_reply_type_from_hiredis(int type) {
    switch (type) {
    case REDIS_REPLY_STATUS:
        return RAINCALL_REPLY_STATUS;
    case REDIS_REPLY_STRING:
        return RAINCALL_REPLY_STRING;
    case REDIS_REPLY_INTEGER:
        return RAINCALL_REPLY_INTEGER;
    case REDIS_REPLY_NIL:
        return RAINCALL_REPLY_NIL;
    case REDIS_REPLY_ARRAY:
        return RAINCALL_REPLY_ARRAY;
    case REDIS_REPLY_ERROR:
        return RAINCALL_REPLY_ERROR;
    case REDIS_REPLY_DOUBLE:
        return RAINCALL_REPLY_DOUBLE;
    case REDIS_REPLY_BOOL:
        return RAINCALL_REPLY_BOOL;
    case REDIS_REPLY_MAP:
        return RAINCALL_REPLY_MAP;
    case REDIS_REPLY_SET:
        return RAINCALL_REPLY_SET;
    case REDIS_REPLY_ATTR:
        return RAINCALL_REPLY_ATTR;
    case REDIS_REPLY_PUSH:
        return RAINCALL_REPLY_PUSH;
    case REDIS_REPLY_VERB:
        return RAINCALL_REPLY_VERB;
    case REDIS_REPLY_BIGNUM:
        return RAINCALL_REPLY_BIGNUM;
    default:
        return RAINCALL_REPLY_ERROR;
    }
}

static raincall_Reply *raincall_reply_from_hiredis(redisReply *src) {
    raincall_Reply *dst;

    if (src == NULL) return NULL;

    dst = raincall_reply_new(raincall_reply_type_from_hiredis(src->type));
    if (dst == NULL) return NULL;

    dst->integer = src->integer;
    dst->number = src->dval;

    if (src->str != NULL && src->len > 0) {
        dst->str = malloc(src->len + 1);
        if (dst->str == NULL) {
            raincall_reply_free(dst);
            return NULL;
        }
        memcpy(dst->str, src->str, src->len);
        dst->str[src->len] = '\0';
        dst->len = src->len;
    }

    if (src->elements > 0) {
        dst->element = calloc(src->elements, sizeof(*dst->element));
        if (dst->element == NULL) {
            raincall_reply_free(dst);
            return NULL;
        }
        dst->elements = src->elements;
        for (size_t i = 0; i < src->elements; i++) {
            dst->element[i] = raincall_reply_from_hiredis(src->element[i]);
            if (dst->element[i] == NULL) {
                raincall_reply_free(dst);
                return NULL;
            }
        }
    }

    return dst;
}

static int raincall_tcp_call(raincall_Backend *backend, int argc, const char **argv,
                             size_t *argvlen, raincall_Reply **out) {
    raincall_TcpBackend *tcp = (raincall_TcpBackend *)backend;
    redisReply *redis_reply;

    *out = NULL;
    redis_reply = redisCommandArgv(tcp->redis, argc, argv, argvlen);
    if (redis_reply == NULL) {
        if (tcp->redis != NULL && tcp->redis->errstr[0] != '\0') {
            raincall_tcp_set_error(tcp, tcp->redis->errstr);
        } else {
            raincall_tcp_set_error(tcp, "Redis command failed");
        }
        return -1;
    }

    *out = raincall_reply_from_hiredis(redis_reply);
    freeReplyObject(redis_reply);
    if (*out == NULL) {
        raincall_tcp_set_error(tcp, "out of memory");
        return -1;
    }

    return 0;
}

static const char *raincall_tcp_error(raincall_Backend *backend) {
    raincall_TcpBackend *tcp = (raincall_TcpBackend *)backend;
    return tcp->error;
}

static void raincall_tcp_close(raincall_Backend *backend) {
    raincall_TcpBackend *tcp = (raincall_TcpBackend *)backend;
    if (tcp == NULL) return;
    if (tcp->redis != NULL) redisFree(tcp->redis);
    free(tcp->host);
    free(tcp);
}

static raincall_Backend *raincall_tcp_open(const char *host, int port) {
    raincall_TcpBackend *tcp;

    if (host == NULL || port <= 0 || port > 65535) return NULL;

    tcp = calloc(1, sizeof(*tcp));
    if (tcp == NULL) return NULL;

    tcp->base.call = raincall_tcp_call;
    tcp->base.error = raincall_tcp_error;
    tcp->base.close = raincall_tcp_close;
    tcp->host = raincall_strdup(host);
    tcp->port = port;
    if (tcp->host == NULL) {
        raincall_tcp_close(&tcp->base);
        return NULL;
    }

    tcp->redis = redisConnect(host, port);
    if (tcp->redis == NULL || tcp->redis->err) {
        if (tcp->redis != NULL) raincall_tcp_set_error(tcp, tcp->redis->errstr);
        raincall_tcp_close(&tcp->base);
        return NULL;
    }

    return &tcp->base;
}

raincall_State *raincall_open(const char *host, int port) {
    raincall_State *R;

    R = calloc(1, sizeof(*R));
    if (R == NULL) return NULL;

    R->backend = raincall_tcp_open(host, port);
    if (R->backend == NULL) {
        free(R);
        return NULL;
    }

    return R;
}

void raincall_close(raincall_State *R) {
    if (R == NULL) return;
    if (R->backend != NULL) R->backend->close(R->backend);
    free(R);
}

static raincall_State *raincall_get_state(void) {
    if (installed_state != NULL) return installed_state;
    if (default_state == NULL) {
        default_state = raincall_open("127.0.0.1", 6379);
    }
    return default_state;
}

static int raincall_push_aggregate_reply(mq_State *L, raincall_Reply *reply);
static int raincall_push_map_reply(mq_State *L, raincall_Reply *reply);

static int raincall_push_reply(mq_State *L, raincall_Reply *reply) {
    switch (reply->type) {
    case RAINCALL_REPLY_STATUS:
    case RAINCALL_REPLY_STRING:
    case RAINCALL_REPLY_VERB:
    case RAINCALL_REPLY_BIGNUM:
        mq_pushlstring(L, reply->str != NULL ? reply->str : "", reply->len);
        return 1;
    case RAINCALL_REPLY_INTEGER:
        mq_pushinteger(L, (mq_Integer)reply->integer);
        return 1;
    case RAINCALL_REPLY_DOUBLE:
        mq_pushnumber(L, (mq_Number)reply->number);
        return 1;
    case RAINCALL_REPLY_BOOL:
        mq_pushboolean(L, reply->integer != 0);
        return 1;
    case RAINCALL_REPLY_NIL:
        mq_pushnil(L);
        return 1;
    case RAINCALL_REPLY_ERROR:
        mq_pushlstring(L, reply->str != NULL ? reply->str : "", reply->len);
        return -1;
    case RAINCALL_REPLY_ARRAY:
    case RAINCALL_REPLY_SET:
    case RAINCALL_REPLY_ATTR:
    case RAINCALL_REPLY_PUSH:
        return raincall_push_aggregate_reply(L, reply);
    case RAINCALL_REPLY_MAP:
        return raincall_push_map_reply(L, reply);
    default:
        return raincall_raise(L, "RAIN.CALL: Redis reply type is not supported yet");
    }
}

static int raincall_push_aggregate_reply(mq_State *L, raincall_Reply *reply) {
    mq_newtable(L);

    for (size_t i = 0; i < reply->elements; i++) {
        int pushed = raincall_push_reply(L, reply->element[i]);
        if (pushed < 0) return pushed;
        mq_seti(L, -2, (mq_Integer)i + 1);
    }

    return 1;
}

static int raincall_push_map_reply(mq_State *L, raincall_Reply *reply) {
    mq_newtable(L);

    for (size_t i = 0, pair = 1; i + 1 < reply->elements; i += 2, pair++) {
        mq_newtable(L);

        int pushed = raincall_push_reply(L, reply->element[i]);
        if (pushed < 0) return pushed;
        mq_setfield(L, -2, "key");

        pushed = raincall_push_reply(L, reply->element[i + 1]);
        if (pushed < 0) return pushed;
        mq_setfield(L, -2, "value");

        mq_seti(L, -2, (mq_Integer)pair);
    }

    return 1;
}

static int rain_call(mq_State *L) {
    int argc = mq_gettop(L);
    const char **argv;
    size_t *argvlen;
    raincall_Reply *reply;
    raincall_State *R;
    int ret;

    if (argc < 1) {
        return raincall_raise(L, "RAIN.CALL: expected command name");
    }

    R = raincall_get_state();
    if (R == NULL) {
        return raincall_raise(L, "RAIN.CALL: Redis backend is not connected");
    }

    argv = calloc((size_t)argc, sizeof(*argv));
    argvlen = calloc((size_t)argc, sizeof(*argvlen));
    if (argv == NULL || argvlen == NULL) {
        free(argv);
        free(argvlen);
        return raincall_raise(L, "RAIN.CALL: out of memory");
    }

    for (int i = 0; i < argc; i++) {
        argv[i] = mq_tolstring(L, i + 1, &argvlen[i]);
        if (argv[i] == NULL) {
            free(argv);
            free(argvlen);
            return raincall_raise(L, "RAIN.CALL: command arguments must be strings or numbers");
        }
    }

    if (R->backend == NULL || R->backend->call == NULL) {
        free(argv);
        free(argvlen);
        return raincall_raise(L, "RAIN.CALL: Redis backend is not connected");
    }

    ret = R->backend->call(R->backend, argc, argv, argvlen, &reply);
    free(argv);
    free(argvlen);

    if (ret != 0 || reply == NULL) {
        return raincall_raise_redis(L, R, "RAIN.CALL");
    }

    ret = raincall_push_reply(L, reply);
    raincall_reply_free(reply);
    return ret;
}

static int rain_connect(mq_State *L) {
    size_t host_len = 0;
    const char *host = mq_tolstring(L, 1, &host_len);
    int isnum = 0;
    mq_Integer port = mq_tointegerx(L, 2, &isnum);
    char *host_copy;
    raincall_State *R;

    if (host == NULL || host_len == 0 || !isnum) {
        return raincall_raise(L, "RAIN.CONNECT: expected host and port");
    }
    if (host_len > 255 || port <= 0 || port > 65535) {
        return raincall_raise(L, "RAIN.CONNECT: invalid host or port");
    }

    host_copy = malloc(host_len + 1);
    if (host_copy == NULL) {
        return raincall_raise(L, "RAIN.CONNECT: out of memory");
    }
    memcpy(host_copy, host, host_len);
    host_copy[host_len] = '\0';

    R = raincall_open(host_copy, (int)port);
    free(host_copy);
    if (R == NULL) {
        return raincall_raise(L, "RAIN.CONNECT: Redis backend is not connected");
    }

    if (default_state != NULL) raincall_close(default_state);
    default_state = R;
    installed_state = R;

    mq_pushboolean(L, 1);
    return 1;
}

int raincall_install_state(mq_State *L, raincall_State *R) {
    installed_state = R;

    mq_newtable(L);

    mq_pushcfunction(L, rain_call);
    mq_setfield(L, -2, "CALL");

    mq_pushcfunction(L, rain_connect);
    mq_setfield(L, -2, "CONNECT");

    mq_setglobal(L, "RAIN");
    return 0;
}

int raincall_install(mq_State *L) {
    return raincall_install_state(L, default_state);
}
