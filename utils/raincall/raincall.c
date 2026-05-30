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
    char *host;
    int port;
    redisContext *redis;
};

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

static int raincall_raise(mq_State *L, const char *msg) {
    mq_pushstring(L, msg);
    return -1;
}

static int raincall_raise_redis(mq_State *L, raincall_State *R, const char *prefix) {
    char buf[512];
    const char *err = "unknown error";

    if (R != NULL && R->redis != NULL && R->redis->errstr[0] != '\0') {
        err = R->redis->errstr;
    }
    snprintf(buf, sizeof(buf), "%s: %s", prefix, err);
    return raincall_raise(L, buf);
}

raincall_State *raincall_open(const char *host, int port) {
    raincall_State *R;

    if (host == NULL || port <= 0 || port > 65535) return NULL;

    R = calloc(1, sizeof(*R));
    if (R == NULL) return NULL;

    R->host = raincall_strdup(host);
    R->port = port;
    if (R->host == NULL) {
        raincall_close(R);
        return NULL;
    }

    R->redis = redisConnect(host, port);
    if (R->redis == NULL || R->redis->err) {
        raincall_close(R);
        return NULL;
    }

    return R;
}

void raincall_close(raincall_State *R) {
    if (R == NULL) return;
    if (R->redis != NULL) redisFree(R->redis);
    free(R->host);
    free(R);
}

static raincall_State *raincall_get_state(void) {
    if (installed_state != NULL) return installed_state;
    if (default_state == NULL) {
        default_state = raincall_open("127.0.0.1", 6379);
    }
    return default_state;
}

static int raincall_push_array_reply(mq_State *L, redisReply *reply);

static int raincall_push_reply(mq_State *L, redisReply *reply) {
    switch (reply->type) {
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_VERB:
    case REDIS_REPLY_BIGNUM:
        mq_pushlstring(L, reply->str, reply->len);
        return 1;
    case REDIS_REPLY_INTEGER:
        mq_pushinteger(L, (mq_Integer)reply->integer);
        return 1;
    case REDIS_REPLY_NIL:
        mq_pushnil(L);
        return 1;
    case REDIS_REPLY_ERROR:
        mq_pushlstring(L, reply->str, reply->len);
        return -1;
    case REDIS_REPLY_ARRAY:
        return raincall_push_array_reply(L, reply);
    default:
        return raincall_raise(L, "RAIN.CALL: Redis reply type is not supported yet");
    }
}

static int raincall_push_array_reply(mq_State *L, redisReply *reply) {
    mq_newtable(L);

    for (size_t i = 0; i < reply->elements; i++) {
        int pushed = raincall_push_reply(L, reply->element[i]);
        if (pushed < 0) return pushed;
        mq_seti(L, -2, (mq_Integer)i + 1);
    }

    return 1;
}

static int rain_call(mq_State *L) {
    int argc = mq_gettop(L);
    const char **argv;
    size_t *argvlen;
    redisReply *reply;
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

    reply = redisCommandArgv(R->redis, argc, argv, argvlen);
    free(argv);
    free(argvlen);

    if (reply == NULL) {
        return raincall_raise_redis(L, R, "RAIN.CALL");
    }

    ret = raincall_push_reply(L, reply);
    freeReplyObject(reply);
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
