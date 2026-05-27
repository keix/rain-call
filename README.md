# Rain-Call

Rain-Call is an experimental fork of Redis 8.

It embeds Moonquakes — a Lua 5.4 scripting engine — beside the existing
Redis Lua engine, and over time moves the existing call sites onto it.

```text
Redis receives RESP.
RAIN.CALL crosses the command boundary.
Moonquakes executes.
Redis mutates its own state.
```

## Names

```text
Rain-Call         display name / proper noun in prose
rain-call         repository / project / package identifier
raincall          daemon / executable
rain-call.so      Redis-side command extension
libmoonquakes.so  Moonquakes VM engine
```

## Goals

- Start from Redis 8 as a stable baseline
- Build a Moonquakes scripting engine inside a Redis-like server
- Add `RAIN.CALL` as the first command bound to the Moonquakes engine
- Keep the original Redis Lua engine untouched during this phase
- Eventually move `EVAL` onto the Moonquakes engine and remove Lua 5.1

## Non-goals

Rain-Call is not intended to be a drop-in Redis replacement.

It may intentionally diverge from Redis behavior, APIs, commands, internals, build structure, and compatibility guarantees.

Compatibility with Redis is useful as a reference point, but it is not the primary constraint of this project.

## Scripting model

Redis exposes server-side programmability through an embedded Lua 5.1 interpreter, used by Lua scripts and functions.

Rain-Call adds a parallel engine — Moonquakes — embedding Lua 5.4.

The unit of work is the engine, not the command. `RAIN.CALL` is a thin command shell over the Moonquakes engine. Other commands may bind to the same engine later.

```text
Phase 1 (current):
  EVAL     -> Redis Lua 5.1 engine
  RAIN.CALL  -> Moonquakes Lua 5.4 engine

Phase 2 (planned, after the boundary is proven):
  EVAL     -> Moonquakes Lua 5.4 engine
  RAIN.CALL  -> Moonquakes Lua 5.4 engine
  (Redis Lua 5.1 engine removed)
```

`EVAL` moves only after the Moonquakes engine boundary is proven by `RAIN.CALL`.

## Commands

The v0 surface is deliberately minimal:

```text
RAIN.LOAD <name> <source>     register a Moonquakes function under <name>
RAIN.CALL <name> [arg...]     invoke a registered function
```

Example:

```redis
RAIN.LOAD incr "
function main(key)
  return redis.call('INCR', key)
end
"

RAIN.CALL incr counter
```

Internally:

```text
RESP
  -> command dispatch
  -> RAIN.CALL
  -> lookup loaded function
  -> Moonquakes pcall
  -> redis.call(...)
  -> Redis memory mutation
  -> RESP reply
```

Later versions may add `RAIN.DROP <name>` and `RAIN.LIST`.

## Components

Rain-Call splits responsibility across two loadable units:

```text
rain-call.so       Redis-side command extension, or built-in fork component
                   registers RAIN.* commands
                   owns the boundary between RESP and the VM

libmoonquakes.so   Moonquakes VM engine
                   embeds Lua 5.4
                   knows nothing about RESP or Redis internals
```

The two-tier split keeps the boundary explicit:

```text
Redis loads Rain-Call.
Rain-Call calls Moonquakes.
Moonquakes decides the mutation.
```

## Philosophy

Rain-Call treats Redis as a well-structured POSIX daemon:

```text
socket events
  -> RESP
  -> command dispatch
  -> memory mutation
```

Moonquakes adds another layer:

```text
socket events
  -> RESP
  -> command dispatch
  -> Moonquakes engine
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
5. Add `RAIN.CALL` as the first caller of the Moonquakes engine.
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
