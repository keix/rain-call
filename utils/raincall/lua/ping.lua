local install, err, where = package.loadlib(
  "./build/raincall/libraincall.so",
  "raincall_install"
)

assert(install, err)
install()

local pong = RAIN.CALL("PING")
assert(pong == "PONG")

print("RAIN.CALL('PING') = " .. pong)

assert(RAIN.CALL("SET", "moon", "quake") == "OK")
assert(RAIN.CALL("GET", "moon") == "quake")

print("RAIN.CALL('GET', 'moon') = " .. RAIN.CALL("GET", "moon"))
