# Rain-Call Design

## 1. Purpose

Rain-Call is an experimental Redis fork that embeds Moonquakes as a Lua 5.4
runtime.

It does not treat Redis Lua compatibility as the primary goal. Rain-Call
defines a Rain-Call-native boundary between Redis protocol input, Moonquakes
execution, and Redis command execution.

## 2. Core Boundaries

```text
Redis protocol side
  -> RAIN.FALL

Moonquakes Lua side
  -> RAIN.CALL

Redis state side
  -> Redis command execution
```

The design sentence is:

```text
RAIN.FALL enters Moonquakes.
RAIN.CALL exits Moonquakes into Redis.
```

## 3. Direction Of Calls

Redis to Moonquakes:

```text
RAIN.FALL <lua-source> [arg...]
```

`RAIN.FALL` runs Lua source inside Moonquakes.

Moonquakes to Redis:

```lua
RAIN.CALL("GET", "key")
RAIN.CALL("SET", "key", "value")
RAIN.CALL("COMMAND", "LIST")
```

`RAIN.CALL` calls Redis commands through the Rain-Call host bridge.

## 4. Why RAIN.CALL Is Not A Redis Protocol Command

`RAIN.CALL` is reserved for the Lua-side Redis command boundary.

Using `RAIN.CALL` for both directions would make the call direction ambiguous:

```text
Redis -> Moonquakes
Moonquakes -> Redis
```

Therefore:

```text
RAIN.FALL  = outside -> inside
RAIN.CALL  = inside -> outside
```

## 5. Host Capability Model

Moonquakes does not know Redis.

Rain-Call installs the `RAIN` capability from outside through the Moonquakes C
API.

```text
libmoonquakes.so
  Lua 5.4 runtime only
  exposes mq_* C API
  knows nothing about Redis, RESP, or RAIN

libraincall.so
  installs RAIN table
  provides RAIN.CALL
  owns Redis bridge
```

`RAIN.CALL` is split into three layers:

```text
RAIN.CALL(cmd, ...)
  -> raincall_Backend.call()
  -> raincall_Reply
  -> Lua value
```

Backends do not push directly onto the Moonquakes stack. They only produce
`raincall_Reply`. This keeps TCP and future in-process command execution behind
the same reply conversion path.

## 6. Current Standalone Path

The current proven path is Moonquakes to Redis over TCP:

```text
Moonquakes CLI
  -> package.loadlib("./build/raincall/libraincall.so", "raincall_install")
  -> raincall_install()
  -> RAIN.CALL("PING")
  -> hiredis TCP backend
  -> Redis
  -> Lua value
```

This proves that Moonquakes can load the Rain-Call host capability through the
normal `package.loadlib` boundary and call Redis without a C driver bypass.

## 7. Reply Conversion

Current conversion:

```text
Redis simple string -> Lua string
Redis bulk string   -> Lua string or nil
Redis integer       -> Lua integer
Redis array         -> Lua table
Redis error         -> Lua error
RESP3 bool          -> Lua boolean
RESP3 double        -> Lua number
RESP3 map           -> Lua array of { key = ..., value = ... } pairs
RESP3 set/push/attr -> Lua array table
RESP3 verbatim      -> Lua string
RESP3 bignum        -> Lua string
```

RESP3 map is represented as a pair array for now because the current
Moonquakes C API has indexed table writes (`mq_seti`) and field writes
(`mq_setfield`), but not a general `mq_settable`.

## 8. Future Redis Fork Path

The Redis-integrated path starts with the reverse direction:

```text
redis-server
  -> RAIN.FALL command
  -> Moonquakes state
  -> Lua source execution
  -> Redis client reply
```

That proves re-entry and Moonquakes value to Redis reply conversion.

The Redis fork path adds the callback into Redis command execution:

```text
redis-server
  -> RAIN.FALL command
  -> Moonquakes state
  -> Lua source execution
  -> RAIN.CALL(...)
  -> Redis internal command execution
  -> Redis mutates its own state
```

Standalone development uses the TCP backend. The Redis fork path uses an
in-process backend behind `raincall_State`.

`RAIN.CALL` may use the TCP backend from standalone Moonquakes, or from
`RAIN.FALL` when it points at a separate Redis backend. It must not synchronously
call back into the same Redis event loop over TCP, because the current command
handler is already running and the server cannot process the nested request.

```text
TCP proves the protocol boundary.
In-process proves the embedded boundary.
```

The current in-process backend uses an isolated internal client:

```text
RAIN.CALL(cmd, ...)
  -> internal Redis client
  -> lookupCommand / policy checks
  -> call()
  -> capture RESP reply
  -> CallReply
  -> raincall_Reply
  -> Lua value
```

Dangerous or recursive commands, including `RAIN.FALL` through `RAIN.CALL`, are
rejected by policy.

## 9. Removed Or Non-goal Compatibility

Rain-Call does not need to preserve:

```text
redis.call(...)
redis.pcall(...)
Lua 5.1 scripting compatibility
EVAL as Redis-compatible Lua 5.1 scripting
```

Those belong to Redis Lua compatibility.

Rain-Call uses:

```text
RAIN.FALL
RAIN.CALL
Moonquakes Lua 5.4
```

## 10. Naming

Rain-Call core code uses snake_case:

```c
raincall_open
raincall_install
raincall_close
raincall_push_reply
```

Redis command handlers use Redis style:

```c
rainFallCommand
```

Redis protocol commands use uppercase dotted names:

```text
RAIN.FALL
```
