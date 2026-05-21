# Keyspace Notification Test Coverage Analysis

This document analyzes the test coverage for keyspace notifications that affect
modules like RediSearch, which use `SetKeyMeta` in notification callbacks.

## RediSearch Notification Events

RediSearch's `HashNotificationCallback` handles the following events (from
`modules/redisearch/src/src/notifications.c`):

```c
typedef enum {
  _null_cmd,
  hset_cmd,
  hmset_cmd,
  hsetnx_cmd,
  hincrby_cmd,
  hincrbyfloat_cmd,
  hdel_cmd,
  del_cmd,
  set_cmd,
  rename_from_cmd,
  rename_to_cmd,
  trimmed_cmd,
  restore_cmd,
  expire_cmd,
  persist_cmd,
  expired_cmd,
  hexpire_cmd,
  hpersist_cmd,
  hexpired_cmd,
  evicted_cmd,
  change_cmd,
  loaded_cmd,
  copy_to_cmd,
} RedisCmd;
```

## Test Coverage Matrix

Legend:
- ✅ = Already covered (before this PR)
- 🆕 = Added in this PR
- ❌ = Not covered

### Hash Events (NOTIFY_HASH)

| Event | Commands That Trigger | Coverage | Notes |
|-------|----------------------|----------|-------|
| `hset` | HSET, HMSET, HSETNX | ✅ Already covered | HSET, HMSET, HSETNX tests |
| `hset` | HSETEX | 🆕 **This PR** | HSETEX test |
| `hdel` | HDEL | 🆕 **This PR** | HDEL test |
| `hdel` | HGETDEL | 🆕 **This PR** | HGETDEL test |
| `hdel` | HGETEX (past timestamp), HSETEX (past timestamp), HEXPIRE (past timestamp) | 🆕 **This PR** | Covered via HGETEX/HSETEX/HEXPIRE tests |
| `hincrby` | HINCRBY | ✅ Already covered | HINCRBY test |
| `hincrbyfloat` | HINCRBYFLOAT | ✅ Already covered | HINCRBYFLOAT test |
| `hexpire` | HEXPIRE, HPEXPIRE, HEXPIREAT, HPEXPIREAT | 🆕 **This PR** | HEXPIRE test |
| `hexpire` | HGETEX (with EX/PX), HSETEX (with EX/PX) | 🆕 **This PR** | HGETEX/HSETEX tests |
| `hpersist` | HPERSIST | 🆕 **This PR** | HPERSIST test |
| `hpersist` | HGETEX (with PERSIST) | 🆕 **This PR** | HGETEX test |
| `hexpired` | Lazy field expiration, Active field expiration | 🆕 **This PR** | Hash field expiry test |

### Generic Events (NOTIFY_GENERIC)

| Event | Commands That Trigger | Coverage | Notes |
|-------|----------------------|----------|-------|
| `del` | DEL, UNLINK | ✅ Already covered | DEL test |
| `del` | Hash becomes empty | 🆕 **This PR** | Via HDEL/HGETDEL tests |
| `rename_from` | RENAME, RENAMENX | ✅ Already covered | RENAME test |
| `rename_to` | RENAME, RENAMENX | ✅ Already covered | RENAME test |
| `restore` | RESTORE | ✅ Already covered | RESTORE test |
| `expire` | EXPIRE, PEXPIRE, EXPIREAT, PEXPIREAT | ✅ Already covered | EXPIRE test |
| `expire` | SET (with EX/PX), GETEX (with EX/PX), SETEX, PSETEX | ⚠️ Partial | SET test covers some |
| `persist` | PERSIST | 🆕 **This PR** | PERSIST test |
| `copy_to` | COPY | 🆕 **This PR** | COPY test |
| `loaded` | RDB load (DEBUG RELOAD, server restart) | ✅ Already covered | DEBUG RELOAD test |

### String Events (NOTIFY_STRING)

| Event | Commands That Trigger | Coverage | Notes |
|-------|----------------------|----------|-------|
| `set` | SET, SETEX, PSETEX, SETNX, SETRANGE, etc. | ✅ Already covered | SET test |

### Expired/Evicted Events

| Event | Commands That Trigger | Coverage | Notes |
|-------|----------------------|----------|-------|
| `expired` | Key expiration (lazy or active) | ✅ Already covered | EXPIRE test waits for expiry |
| `evicted` | Memory eviction | ❌ Not tested | Requires maxmemory config |

## Summary: What This PR Adds

### New Tests Added in This PR

| Test | Events Covered | Status Without Fix |
|------|----------------|-------------------|
| HSETEX | `hset`, `hexpire`, `hdel` | ❌ **CRASHES** |
| HGETDEL | `hdel`, `hexpired` | ❌ **CRASHES** |
| HGETEX | `hexpire`, `hpersist`, `hdel` | ❌ **CRASHES** |
| HDEL | `hdel` | ❌ **CRASHES** |
| HEXPIRE | `hexpire`, `hdel` | ❌ **CRASHES** |
| HPERSIST | `hpersist` | ✅ Passes |
| (field expiry) | `hexpired` | ✅ Passes |
| PERSIST | `persist` | ✅ Passes |
| COPY | `copy_to` | ✅ Passes |

### Bug Fixes Required

Commands that need fixing for use-after-reallocation when `SetKeyMeta` is called:
- `hsetexCommand` - accesses `o` after `notifyKeyspaceEvent`
- `hgetdelCommand` - accesses `o` after `notifyKeyspaceEvent`
- `hgetexCommand` - accesses `o` after `notifyKeyspaceEvent`
- `hdelCommand` - accesses `o` after `notifyKeyspaceEvent`
- `hexpireGenericCommand` - accesses `hashObj` after `notifyKeyspaceEvent`

## Still Not Covered (Future Work)

| Event | Command/Trigger | Reason |
|-------|-----------------|--------|
| `evicted` | Memory eviction | Requires maxmemory configuration |

## Command to Event Mapping

### Commands Already Covered (Before This PR)

| Command | Event(s) Triggered |
|---------|-------------------|
| HSETNX | `hset` |
| HSET | `hset` |
| HMSET | `hset` |
| HINCRBY | `hincrby` |
| HINCRBYFLOAT | `hincrbyfloat` |
| SET | `set` |
| APPEND | `append` |
| INCR | `incrby` |
| INCRBY | `incrby` |
| INCRBYFLOAT | `incrbyfloat` |
| GETSET | `set` |
| SETRANGE | `setrange` |
| DEL | `del` |
| RENAME | `rename_from`, `rename_to` |
| RESTORE | `restore` |
| EXPIRE/PEXPIRE | `expire` |
| DEBUG RELOAD | `loaded` |

### Commands Added in This PR

| Command | Event(s) Triggered |
|---------|-------------------|
| HSETEX | `hset`, `hexpire`, `hdel` |
| HGETDEL | `hdel`, `hexpired` |
| HGETEX | `hexpire`, `hpersist`, `hdel` |
| HDEL | `hdel` |
| HEXPIRE/HPEXPIRE | `hexpire`, `hdel` |
| HPERSIST | `hpersist` |

### Commands NOT Covered

| Command | Event(s) Triggered |
|---------|-------------------|
| PERSIST | `persist` |
| COPY | `copy_to` |
| (field expiry wait) | `hexpired` |

## Source Files Reference

- Notification events: `src/notify.c`
- Hash notifications: `src/t_hash.c`
- String notifications: `src/t_string.c`
- Generic notifications: `src/db.c`, `src/expire.c`
- RediSearch handler: `modules/redisearch/src/src/notifications.c`
- Test module: `tests/modules/keymeta_notify.c`
- Test file: `tests/unit/moduleapi/ksn_notify_side_effect.tcl`
