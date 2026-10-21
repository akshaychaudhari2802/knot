cmake_minimum_required(VERSION 3.16)
project(knot VERSION 2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)            # RHEL 8 / GCC 8
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)   # a debugger tool should be fast AND debuggable
endif()

option(KNOT_ASAN  "AddressSanitizer + UBSan" OFF)
option(KNOT_TESTS "Build tests and victims"  ON)

# ---- Tier 1: zero third-party dependencies -------------------------------
add_library(knot_tier1 STATIC
    src/proc.cpp
    src/snapshot.cpp
    src/syscall.cpp
    src/source.cpp
    src/symbols.cpp
    src/lock_decoder.cpp
    src/identity.cpp
    src/graph.cpp
    src/fd_resolver.cpp
    src/analysis.cpp
    src/explain.cpp
    src/report.cpp
    src/permission.cpp
    src/stacks.cpp
    src/watch.cpp)
target_include_directories(knot_tier1 PUBLIC include)
target_compile_options(knot_tier1 PRIVATE -Wall -Wextra -Wshadow -pedantic-errors)
if(KNOT_ASAN)
    target_compile_options(knot_tier1 PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(knot_tier1 PUBLIC -fsanitize=address,undefined)
endif()

# ---- Tier 2: stacks via elfutils libdwfl (optional) ------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBDW QUIET libdw)
endif()
if(LIBDW_FOUND)
    add_library(knot_tier2 STATIC tier2/dwfl_session.cpp tier2/attach_safety.cpp)
    target_include_directories(knot_tier2 PRIVATE ${LIBDW_INCLUDE_DIRS})
    target_link_libraries(knot_tier2 PUBLIC knot_tier1 ${LIBDW_LIBRARIES})
    message(STATUS "knot: Tier 2 enabled (stack traces, libdw ${LIBDW_VERSION})")
else()
    message(STATUS "knot: libdw not found - stack traces disabled. Install elfutils-devel (RHEL) "
                   "or libdw-dev (Debian). Every diagnosis feature still works.")
endif()
if(TARGET knot_tier2)
    target_compile_definitions(knot_tier2 PUBLIC KNOT_TIER2=1)
    target_compile_options(knot_tier2 PRIVATE -Wall -Wextra -Wshadow -pedantic-errors)
