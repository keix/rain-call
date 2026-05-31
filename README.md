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

Rain-Call keeps the original Redis Lua path separate while the Moonquakes boundary is being proven. `RAIN.FALL` and `RAIN.CALL` are Rain-Call-native entry points, not compatibility wrappers for Redis Lua scripting.

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

From standalone Moonquakes, `RAIN.CALL` uses the TCP backend. Inside `RAIN.FALL`, the Redis fork uses an in-process backend so `RAIN.CALL` can call back into the same server without blocking the event loop.

The in-process backend executes Redis commands through an isolated internal client, captures the Redis reply, and converts it through the shared `raincall_Reply` path.

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
       raincall_Backend executes the command
       Redis executes the command through TCP or an internal client
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
                   owns the backend-independent reply conversion

TCP backend        standalone Moonquakes -> external Redis

in-process backend Redis fork -> isolated internal client -> call()
```

During standalone development:

```text
Moonquakes CLI loads libraincall through package.loadlib.
libraincall installs the RAIN capability into the current Moonquakes state.
RAIN.CALL calls Redis through the Rain-Call TCP backend.
```

Inside the Redis fork:

```text
Redis initializes a Moonquakes state for RAIN.FALL.
Rain-Call installs the RAIN capability with an in-process backend.
RAIN.CALL executes Redis commands through an isolated internal client.
Redis replies are captured and converted through raincall_Reply.
```

The internal backend does not reuse the outer Redis client context. It creates
an isolated command context, runs Redis command execution, captures the reply,
and returns it to Moonquakes as a Lua value.

Moonquakes core stays clean.

## Philosophy

When rain falls, the moon quakes. When the moon quakes, rain calls.

## Current status

Early experimental fork.

Current baseline:

- Redis 8

Implemented:

- Moonquakes can load `libraincall.so` with `package.loadlib`
- `RAIN.CALL` can call Redis through the TCP backend from standalone Moonquakes
- Redis exposes `RAIN.FALL <lua-source> [arg...]`
- `RAIN.FALL` runs Lua source inside Moonquakes
- `RAIN.CALL` inside `RAIN.FALL` uses an in-process backend
- The in-process backend uses an isolated internal Redis client and `call()`
- Redis replies are captured, converted to `raincall_Reply`, then to Lua values

Proven examples:

```redis
RAIN.FALL "return RAIN.CALL('PING')"
RAIN.FALL "RAIN.CALL('SET','moon','quake'); return RAIN.CALL('GET','moon')"
RAIN.FALL "local cmds = RAIN.CALL('COMMAND','LIST'); return {type(cmds), #cmds}"
```

Planned:

- Expand and harden in-process command policy
- Improve reply conversion coverage where needed
- Add loading or registry commands if they remain useful
- Eventually remove the original Lua 5.1 scripting path

## License

Rain-Call is based on Redis 8.  
Redis 8 is available under a tri-license: RSALv2, SSPLv1, or AGPLv3.

Rain-Call chooses AGPLv3 for this fork.  
All Rain-Call modifications are distributed under AGPLv3 unless otherwise stated.  
Original Redis copyright notices, license files, and attribution are preserved.

## Attribution

Rain-Call is based on Redis.  
This project is not affiliated with Redis Ltd. Redis is a trademark of Redis Ltd.
