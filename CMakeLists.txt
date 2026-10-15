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

