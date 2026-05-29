# Rain-Call

Rain-Call is an experimental fork of Redis 8.

It embeds Moonquakes — a Lua 5.4 scripting engine — beside the existing
Redis Lua engine, and over time moves the existing call sites onto it.

The Redis-facing capability is not built into Moonquakes. It is installed
from the outside, by the Rain-Call host.

```text
Redis receives RESP.
RAIN.CALL crosses the command boundary.
Moonquakes executes.
The Rain-Call host bridge mutates Redis state.
```

## Why Rain-Call?

Rain-Call names the moment when falling input becomes a call.

Redis receives bytes. RESP gives them structure.  
`RAIN.CALL` crosses the command boundary. Moonquakes executes.

Rain is the event. Call is the boundary.

## Goals

- Start from Redis 8 as a stable baseline
- Build the Moonquakes Lua 5.4 runtime as a neutral engine
- Install `RAIN.CALL` as a host-provided capability — not as a Moonquakes built-in
- Keep the original Redis Lua engine untouched during this phase
- Eventually move `EVAL` onto the Moonquakes engine and remove Lua 5.1

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

`RAIN.CALL` is therefore not a Moonquakes built-in. It is a host-provided Lua library — a thin Lua wrapper (`rain.lua`) over a C function (`RAIN._call`) that the host installs into the Moonquakes state.

Without the host, `RAIN.CALL` fails explicitly:

```lua
RAIN.CALL("PING")
-- error: RAIN.CALL: host bridge is not installed
```

That failure is correct behavior. Moonquakes does not assume any host.

Phasing of the call sites:

```text
Phase 1 (current):
  EVAL       -> Redis Lua 5.1 engine
  RAIN.CALL  -> Moonquakes Lua 5.4 (with RAIN capability injected)

Phase 2 (planned, after the boundary is proven):
  EVAL       -> Moonquakes Lua 5.4 (with RAIN capability injected)
  RAIN.CALL  -> Moonquakes Lua 5.4
  (Redis Lua 5.1 engine removed)
```

`EVAL` moves only after the Moonquakes engine boundary is proven by `RAIN.CALL`.

## Commands

The v0 surface is deliberately minimal:

```text
RAIN.LOAD <name> <source>     register a Moonquakes function under <name>
RAIN.CALL <name> [arg...]     invoke a registered function
```

The Redis-level `RAIN.CALL` command and the Lua-level `RAIN.CALL` function share a name on purpose: both express the same boundary, crossed from opposite sides.

```text
RAIN.CALL  (Redis command)   RESP -> Moonquakes pcall
RAIN.CALL  (Lua function)    Moonquakes -> Redis state, via libraincall
```

Example:

```redis
RAIN.LOAD incr "
function main(key)
  return RAIN.CALL('INCR', key)
end
"

RAIN.CALL incr counter
```

Internally:

```text
RESP
  -> command dispatch
  -> libraincall handler
  -> lookup loaded function
  -> Moonquakes pcall
       Lua chunk calls RAIN.CALL(...)
       rain.lua delegates to RAIN._call (host C function)
       libraincall executes against Redis state
  -> RESP reply
```

Later versions may add `RAIN.DROP <name>` and `RAIN.LIST`.

## Components

Rain-Call splits responsibility across layered units:

```text
libmoonquakes.so   Moonquakes VM engine
                   Lua 5.4 runtime
                   exposes mq_* C API
                   knows nothing about Redis, RESP, or RAIN

libraincall.so     Rain-Call host capability
                   uses mq_* C API to install the RAIN table
                   provides RAIN._call as a C function
                   owns the bridge to Redis internals

rain.lua           Lua-side wrapper
                   defines RAIN.CALL on top of RAIN._call
                   adds type checks and a "host bridge not installed" path
```

The boundary is enforced by direction:

```text
Redis loads libraincall.
libraincall creates a Moonquakes state via libmoonquakes.
libraincall installs the RAIN capability into that state.
libraincall loads rain.lua.
RAIN.CALL runs inside Moonquakes,
  but the actual Redis call goes back out through libraincall.
```

The same shape extends to future hosts. Each host installs its own capability without touching Moonquakes core:

```text
libraincall.so       installs RAIN.CALL                  (Redis)
libhowlingmoon.so    installs HTTP / upstream capability (nginx)
libfragilemoon.so    installs Fragile handler capability
```

Moonquakes core stays clean.

## Philosophy

Rain-Call treats Redis as a well-structured POSIX daemon:

```text
socket events
  -> RESP
  -> command dispatch
  -> memory mutation
```

Moonquakes adds another layer, mediated by the Rain-Call host:

```text
socket events
  -> RESP
  -> command dispatch
  -> libraincall host
  -> Moonquakes engine (with RAIN capability injected)
  -> memory mutation
```

The goal is to make that boundary explicit, then prove it under load, then collapse the old path onto it.

The migration rule is:

```text
Do not replace Lua first.
Build the Moonquakes engine beside it.
Then move the call site.
```

Stated as the end-state of the fork:

```text
Redis keeps its Lua.
Moonquakes gets its own call.
Rain-Call host owns the Redis bridge.
When the boundary is proven, EVAL can move.
```

## Status

Early experimental fork.

Current baseline:

- Redis 8

Milestones:

1. Build Redis unchanged.
2. Run `redis-server`.
3. Verify `redis-cli PING`.
4. Build the Moonquakes engine alongside the existing Lua 5.1 engine.
5. Install the Rain-Call host capability and add `RAIN.CALL` as its first caller.
6. Move `EVAL` onto the Moonquakes engine and remove Lua 5.1.

## License

Rain-Call is based on Redis 8.

Redis 8 is available under a tri-license:
RSALv2, SSPLv1, or AGPLv3.

Rain-Call chooses AGPLv3 for this fork.

All Rain-Call modifications are distributed under AGPLv3
unless otherwise stated.

Original Redis copyright notices, license files, and attribution are preserved.

## Attribution

Rain-Call is based on Redis.

This project is not affiliated with Redis Ltd. Redis is a trademark of Redis Ltd.
