# Regression test for SetKeyMeta during keyspace notification.
#
# Bug: hsetnxCommand fired notifyKeyspaceEvent BEFORE accessing the kvobj
# pointer for hashTypeLength/updateKeysizesHist/updateSlotAllocSize.
# If a module's notification callback called SetKeyMeta (which requires
# REDISMODULE_WRITE and triggers keyMetaSetMetadata), the kvobj could be
# reallocated, leaving hsetnxCommand with a stale pointer. This caused
# "Guru Meditation: Unknown hash encoding" crash.
#
# Fix: Move notifyKeyspaceEvent to AFTER all kvobj accesses in hsetnxCommand,
# matching the safe pattern already used by hsetCommand.

set testmodule [file normalize tests/modules/keymeta_notify.so]

start_server {tags {"modules" "external:skip"}} {
    r module load $testmodule

    test {HSETNX with SetKeyMeta in notification does not crash} {
        # This is the primary regression test.
        # Before the fix, this would crash with:
        #   "Guru Meditation: Unknown hash encoding #t_hash.c:1335"
        r HSETNX mykey field1 value1

        # Verify the hash is valid and accessible
        assert_equal [r HGET mykey field1] "value1"

        # Verify metadata was set by the notification callback
        assert_equal [r keymetanotify.get mykey] "notified"

        # Second HSETNX on same field (no-op, field exists)
        r HSETNX mykey field1 value2
        assert_equal [r HGET mykey field1] "value1"

        # HSETNX on a new field in the same hash
        r HSETNX mykey field2 value2
        assert_equal [r HGET mykey field2] "value2"
        assert_equal [r HLEN mykey] 2

        # Verify the hash is still fully functional
        assert_equal [r keymetanotify.get mykey] "notified"
    }

    test {HSET with SetKeyMeta in notification works correctly} {
        r DEL mykey2
        r HSET mykey2 f1 v1
        assert_equal [r HGET mykey2 f1] "v1"
        assert_equal [r keymetanotify.get mykey2] "notified"

        # Multiple fields
        r HSET mykey2 f2 v2 f3 v3
        assert_equal [r HLEN mykey2] 3
        assert_equal [r keymetanotify.get mykey2] "notified"
    }

    test {HMSET with SetKeyMeta in notification works correctly} {
        r DEL mykey3
        r HMSET mykey3 f1 v1 f2 v2
        assert_equal [r HGET mykey3 f1] "v1"
        assert_equal [r HGET mykey3 f2] "v2"
        assert_equal [r keymetanotify.get mykey3] "notified"
    }

    test {HINCRBY with SetKeyMeta in notification works correctly} {
        r DEL mykey4
        r HSET mykey4 counter 10
        r HINCRBY mykey4 counter 5
        assert_equal [r HGET mykey4 counter] "15"
        assert_equal [r keymetanotify.get mykey4] "notified"
    }

    test {HINCRBYFLOAT with SetKeyMeta in notification works correctly} {
        r DEL mykey5
        r HSET mykey5 value 10.5
        r HINCRBYFLOAT mykey5 value 1.5
        assert_equal [r HGET mykey5 value] "12"
        assert_equal [r keymetanotify.get mykey5] "notified"
    }

    test {Multiple HSETNX on new keys with SetKeyMeta does not crash} {
        # Stress test: create many keys via HSETNX
        for {set i 0} {$i < 100} {incr i} {
            r HSETNX "stresskey:$i" field "value$i"
        }

        # Verify all keys are valid
        for {set i 0} {$i < 100} {incr i} {
            assert_equal [r HGET "stresskey:$i" field] "value$i"
            assert_equal [r keymetanotify.get "stresskey:$i"] "notified"
        }
    }

    test {SetKeyMeta notification count is tracked} {
        # The setcount should be > 0 since we've been setting metadata
        set count [r keymetanotify.setcount]
        assert {$count > 0}
    }
}
