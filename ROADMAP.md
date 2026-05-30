# Rain-Call Roadmap

Rain-Call is moving in small boundary-first phases.

The rule is:

```text
Do not replace Lua first.
Prove the Moonquakes host boundary first.
Then move Redis call sites onto it.
```

## Phase I: Moonquakes Loadlib Boundary

Status: complete.

Goal:

```text
Moonquakes CLI
  -> package.loadlib
  -> libraincall.so
  -> raincall_install
  -> RAIN.CALL("PING")
  -> PONG
```

This phase proves that Rain-Call is not using a C driver bypass. The
capability is loaded through the normal Moonquakes `package.loadlib` boundary.

Completed surface:

- `libraincall.so` exports `raincall_install`
- Moonquakes can load `libraincall.so` with `package.loadlib`
- `raincall_install` installs the `RAIN` table
- `RAIN.CALL("PING")` runs from Moonquakes

## Phase II: TCP Redis Backend

Status: in progress.

Goal:

```text
Moonquakes Lua
  -> RAIN.CALL(cmd, ...)
  -> libraincall.so
  -> TCP socket
  -> RESP
  -> Redis command dispatch
```

TCP is the first real backend because it is Redis' external boundary. It keeps
standalone CLI testing honest before Rain-Call reaches into Redis internals.

### Phase II-a: TCP Backend

Status: in progress.

Target:

```lua
local install = package.loadlib("./build/raincall/libraincall.so", "raincall_install")
install()

print(RAIN.CALL("PING"))
print(RAIN.CALL("SET", "moon", "quake"))
print(RAIN.CALL("GET", "moon"))
```

Expected output:

```text
PONG
OK
quake
```

Implementation direction:

- Use hiredis for the first synchronous TCP backend
- Keep `RAIN.CALL(cmd, ...)` as the Lua API
- Keep Moonquakes free of Redis, RESP, and Rain-Call details

### Phase II-b: raincall_State

Status: in progress.

Target C API:

```c
raincall_State *raincall_open(const char *host, int port);
int raincall_install_state(mq_State *L, raincall_State *R);
void raincall_close(raincall_State *R);
```

Compatibility shim:

```c
int raincall_install(mq_State *L);
```

Reason:

```text
raincall_open()
  -> backend state

raincall_install_state()
  -> Moonquakes state receives a host capability
```

The clean long-term shape is state injection from the C host. The loadlib shim
exists so standalone Moonquakes scripts can keep proving the boundary before
the final host wiring is available.

Current backend shape:

```text
RAIN.CALL(cmd, ...)
  -> raincall_Backend.call()
  -> raincall_Reply
  -> Lua value
```

Backends do not write to the Moonquakes stack directly. The TCP backend copies
hiredis `redisReply` values into `raincall_Reply`; the future in-process
backend should copy captured Redis replies into the same intermediate type.

### Phase II-c: Redis Reply to Lua Value Conversion

Status: in progress.

Implemented conversion:

```text
Redis status   -> Lua string
Redis bulk     -> Lua string
Redis integer  -> Lua integer
Redis nil      -> Lua nil
Redis array    -> Lua table
RESP3 set      -> Lua array table
RESP3 push     -> Lua array table
RESP3 attr     -> Lua array table
RESP3 map      -> Lua array of { key = ..., value = ... } pairs
RESP3 bool     -> Lua boolean
RESP3 double   -> Lua number
RESP3 verbatim -> Lua string
RESP3 bignum   -> Lua string
```

Remaining conversion target:

```text
RESP3 map      -> direct keyed Lua table, once mq_settable exists
RESP3 verbatim -> tagged table, if scripts need content type metadata
RESP3 bignum   -> tagged table, if scripts need integer domain metadata
```

The first pass favors command execution over preserving every RESP3 semantic.
Rich RESP3 metadata can land after the basic path is stable.

### Phase II-d: Redis Error to Lua Error

Status: planned.

Target:

```text
Redis -ERR reply
  -> RAIN.CALL raises Lua error
```

This keeps Lua scripts from treating Redis command failures as normal values by
default. A later `RAIN.PCALL` can return errors as values if the scripting model
needs it.

## Phase III: Redis Protocol Entry

Status: in progress.

Goal:

```text
RAIN.FALL command
  -> Moonquakes pcall
  -> Redis client reply
```

This phase proves the reverse direction: Redis protocol input can enter
Moonquakes and return a Redis reply.

Implemented surface:

- `RAIN.FALL <lua-source> [arg...]`
- server-level Moonquakes state for the first pass
- Redis argv after the source is passed as Lua varargs
- Lua return values convert back to Redis replies
- Lua errors convert to Redis errors

Current return conversion:

```text
Lua nil      -> Redis null
Lua string   -> Redis bulk string
Lua integer  -> Redis integer
Lua number   -> Redis double
Lua boolean  -> Redis boolean
Lua array    -> Redis array
Lua error    -> Redis error
```

## Phase IV: In-Process Redis Backend

Status: planned.

Goal:

```text
RAIN.FALL command
  -> Moonquakes pcall
  -> RAIN.CALL(cmd, ...)
  -> in-process Redis backend
  -> Redis state mutation
```

The TCP backend proves the external contract. The in-process backend is the
real Redis fork integration.

Expected split:

```text
standalone Moonquakes CLI
  -> TCP backend

Redis fork
  -> in-process backend
```

The Lua API should remain the same:

```lua
RAIN.CALL(cmd, ...)
```

Only the backend behind `raincall_State` should change.

TCP backend caveat:

```text
RAIN.FALL on Redis A
  -> RAIN.CALL over TCP to Redis B
```

works for a separate backend. Synchronously calling Redis A from inside Redis A
over TCP can deadlock the event loop, so same-process callbacks require the
planned in-process backend.

## Phase V: Redis Command Surface

Status: in progress.

Target Redis commands:

```text
RAIN.FALL <lua-source> [arg...]
```

Purpose:

```text
RESP
  -> Redis command dispatch
  -> RAIN.FALL
  -> Rain-Call host
  -> Moonquakes pcall
  -> RAIN.CALL back into Redis
```

Possible later commands:

```text
RAIN.LOAD <name> <source>
RAIN.RUN <name> [arg...]
RAIN.DROP <name>
RAIN.LIST
```

`RAIN.CALL` remains reserved for the Lua-side Redis command boundary.

## Phase V: Lua 5.1 Path Removal

Status: planned.

Goal:

```text
Remove Redis Lua 5.1 compatibility path
Keep Rain-Call native scripting on RAIN.FALL / RAIN.CALL
```

This phase starts only after `RAIN.FALL` has proven the host boundary, backend
state model, reply conversion, and error behavior.

End state:

```text
Redis Lua 5.1 path removed
Moonquakes Lua 5.4 owns Rain-Call-native server-side scripting
Rain-Call host owns Redis capability injection
```

## Build Checkpoints

Redis baseline:

```sh
make -j2
```

Rain-Call host capability:

```sh
cmake -S utils/raincall -B build/raincall \
  -DMOONQUAKES_ROOT=/path/to/moonquakes

make -C build/raincall
```

Standalone TCP verification:

```sh
./src/redis-server --daemonize yes --bind 127.0.0.1 --port 6379 \
  --save "" --appendonly no --dir /tmp --pidfile /tmp/raincall-redis.pid

/path/to/moonquakes/zig-out/bin/moonquakes utils/raincall/lua/ping.lua

./src/redis-cli -h 127.0.0.1 -p 6379 SHUTDOWN NOSAVE
```
