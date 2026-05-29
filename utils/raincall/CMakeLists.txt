cmake_minimum_required(VERSION 3.16)
project(raincall C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MOONQUAKES_ROOT "" CACHE PATH "Path to the Moonquakes repository")

find_path(MOONQUAKES_INCLUDE_DIR
  NAMES moonquakes.h
  HINTS "${MOONQUAKES_ROOT}/include"
  PATHS "/usr/local/include"
)

if(NOT MOONQUAKES_INCLUDE_DIR)
  message(FATAL_ERROR
    "moonquakes.h not found. Pass -DMOONQUAKES_ROOT=/path/to/moonquakes "
    "or install the header under /usr/local/include.")
endif()

add_library(raincall SHARED raincall.c)

target_include_directories(raincall
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${MOONQUAKES_INCLUDE_DIR}"
)

# Do not link libmoonquakes. mq_* symbols stay undefined in libraincall.so
# and are resolved at dlopen time from the host process (Moonquakes CLI,
# Redis with Moonquakes embedded, etc.). Linking libmoonquakes here would
# pull a second runtime instance into the process and split mq_State.
if(APPLE)
  target_link_options(raincall PRIVATE -Wl,-undefined,dynamic_lookup)
endif()

target_compile_options(raincall PRIVATE -Wall -Wextra)
