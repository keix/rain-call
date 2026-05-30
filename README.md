# Rain-Call

Rain-Call is an experimental Redis fork powered by Moonquakes, a Lua 5.4
runtime.

It explores a new command boundary:

```text
RAIN.FALL enters Moonquakes.
RAIN.CALL exits Moonquakes into Redis.
```

The Redis-facing capability is not built into Moonquakes.  
It is installed from the outside, by the Rain-Call host.

## Execution flow
Moonquakes executes Lua, but Redis remains the owner of Redis state.  
When Lua code needs Redis data or mutation, the request goes through Redis command execution.

```text
Redis client
  -> RESP
  -> RAIN.FALL
  -> Moonquakes
      -> RAIN.CALL(...)
      -> Redis command execution
      -> Redis state
  -> Lua return value
  -> Redis reply
```

## Why Rain-Call?

Rain-Call names the moment when falling input becomes a call.

Redis receives bytes.  
RESP gives them structure.  
Moonquakes gives them execution.

Rain is the event. Call is the boundary.

## Goals

- Start from Redis 8 as a stable baseline
- Build the Moonquakes Lua 5.4 runtime as a neutral engine
- Install `RAIN.CALL` as a host-provided capability — not as a Moonquakes built-in
- Keep the original Redis Lua engine untouched during this phase
- Eventually remove the original Lua 5.1 scripting path

## Non-goals

Rain-Call is not intended to be a drop-in Redis replacement.

It may intentionally diverge from Redis behavior, APIs, commands, internals, build structure, and compatibility guarantees.

Compatibility with Redis is useful as a reference point, but it is not the primary constraint of this project.

## Scripting model

Redis exposes server-side programmability through an embedded Lua 5.1 interpreter, used by Lua scripts and functions.

Rain-Call adds a parallel engine — Moonquakes — embedding Lua 5.4.

Moonquakes itself is a neutral Lua 5.4 runtime. It knows nothing about Redis, RESP, or RAIN. The Redis-facing capability is injected from the outside, by the Rain-Call host, through the Moonquakes C API.

```text
Moonquakes              Lua 5.4 runtime only
                        exposes mq_* C API

Rain-Call host          installs the RAIN table
                        owns the Redis-side bridge
                        owns RAIN.CALL implementation
```

`RAIN.CALL` is therefore not a Moonquakes built-in. It is a host-provided Lua function that forwards a Redis command name and arguments through the Rain-Call bridge.

Without the host, `RAIN.CALL` fails explicitly:

```lua
RAIN.CALL("PING")
-- error: RAIN.CALL: host bridge is not installed
```

That failure is correct behavior. Moonquakes does not assume any host.

Phasing of the call sites:

```text
Phase 1:
  EVAL       -> Redis Lua 5.1 engine
  RAIN.CALL  -> Moonquakes Lua 5.4, with TCP Redis backend

Phase 2:
  RAIN.FALL  -> Moonquakes Lua 5.4
  RAIN.CALL  -> Redis command execution
  Redis fork -> in-process Redis backend

Phase 3:
  Original Redis Lua 5.1 scripting path removed
```

The old scripting path is removed only after the Moonquakes engine boundary is proven by `RAIN.CALL`.

## Commands

The v0 surface is deliberately minimal:

```text
RAIN.FALL <lua-source> [arg...]
```

`RAIN.FALL` enters Moonquakes from the Redis protocol side.  

Examples:

```redis
RAIN.FALL "return RAIN.CALL('PING')"
RAIN.FALL "return RAIN.CALL('SET', 'moon', 'quake')"
RAIN.FALL "return RAIN.CALL('GET', 'moon')"
```

During the TCP backend phase, these callback examples should target a separate
Redis backend. The final Redis fork path replaces the TCP callback with an
in-process backend so `RAIN.CALL` can safely call back into the same server.

From Lua, Redis command execution is exposed as:

```lua
RAIN.CALL("PING")
RAIN.CALL("SET", "moon", "quake")
RAIN.CALL("GET", "moon")
```

`RAIN.CALL` is reserved for Lua code running inside Moonquakes.  

```text
RAIN.FALL  (Redis command)   RESP -> Moonquakes
RAIN.CALL  (Lua function)    Moonquakes -> Redis command execution
```

Internally:

```text
RESP
  -> command dispatch
  -> RAIN.FALL
  -> Moonquakes pcall
       Lua chunk calls RAIN.CALL(...)
       libraincall sends the command through a Redis backend
       Redis executes the command
  -> RESP reply
```

Later versions may add function loading or registry commands. They are not part of the v0 command surface.

## Components

Rain-Call splits responsibility across layered units:

```text
libmoonquakes.so   Moonquakes VM engine
                   Lua 5.4 runtime
                   exposes mq_* C API
                   knows nothing about Redis, RESP, or RAIN

libraincall.so     Rain-Call host capability
                   uses mq_* C API to install the RAIN table
                   provides RAIN.CALL as a C function
                   owns the bridge to Redis command execution
```

During standalone development:

```text
Moonquakes CLI loads libraincall through package.loadlib.
libraincall installs the RAIN capability into the current Moonquakes state.
RAIN.CALL calls Redis through the Rain-Call TCP backend.
```

Inside the Redis fork:

```text
Redis initializes the Rain-Call host.
Rain-Call owns or receives a Moonquakes state.
Rain-Call installs the RAIN capability.
RAIN.CALL crosses back into Redis command execution.
```

The same shape extends to future hosts. Each host installs its own capability without touching Moonquakes core:

```text
libraincall.so       installs RAIN.CALL                  (Redis command bridge)
libhowlingmoon.so    installs HTTP / upstream capability (nginx)
libfragilemoon.so    installs Fragile handler capability
```

Moonquakes core stays clean.

## Philosophy

When rain falls, the moon quakes. When the moon quakes, rain calls.

## Status

Early experimental fork.

Current baseline:

- Redis 8

Milestones:

1. Build Redis unchanged.
2. Run `redis-server`.
3. Verify `redis-cli PING`.
4. Build the Moonquakes engine alongside the existing Lua 5.1 engine.
5. Install the Rain-Call host capability and prove `RAIN.CALL`.
6. Prove Redis command execution through `RAIN.CALL`.
7. Add `RAIN.FALL` on the Redis protocol side.
8. Remove the original Lua 5.1 scripting path.

## License

Rain-Call is based on Redis 8.  
Redis 8 is available under a tri-license: RSALv2, SSPLv1, or AGPLv3.

Rain-Call chooses AGPLv3 for this fork.  
All Rain-Call modifications are distributed under AGPLv3 unless otherwise stated.  
Original Redis copyright notices, license files, and attribution are preserved.

## Attribution

Rain-Call is based on Redis.

This project is not affiliated with Redis Ltd. Redis is a trademark of Redis Ltd.
