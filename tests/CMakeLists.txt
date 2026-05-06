cmake_minimum_required(VERSION 3.16)
project(ungula_ota_lib_tests)

set(CMAKE_CXX_STANDARD 17)

set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_LIST_DIR}/vendor)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Dependencies: UngulaCore (headers only) and EmblogX (headers + sources).
#
# By default we use sibling directories (../../lib, ../../lib_emblogx) which
# is the layout during local development. External consumers who cloned only
# lib_ota can set -DUSE_LOCAL_DEPS=OFF to fetch them from GitHub instead.
# ---------------------------------------------------------------------------
option(USE_LOCAL_DEPS "Use local sibling lib/ and lib_emblogx/ directories" ON)

if(USE_LOCAL_DEPS)
    set(LIB_SRC ${CMAKE_CURRENT_LIST_DIR}/../../lib/src)
    set(LIB_EMBLOGX_SRC ${CMAKE_CURRENT_LIST_DIR}/../../lib_emblogx/src)
else()
    FetchContent_Declare(
        ungula_core
        GIT_REPOSITORY https://github.com/alexconesap/ungula-core.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(ungula_core)
    set(LIB_SRC ${ungula_core_SOURCE_DIR}/src)

    FetchContent_Declare(
        emblogx
        GIT_REPOSITORY https://github.com/alexconesap/emblogx.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(emblogx)
    set(LIB_EMBLOGX_SRC ${emblogx_SOURCE_DIR}/src)
endif()

set(LIB_OTA_SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

include_directories(
    ${LIB_SRC}
    ${LIB_EMBLOGX_SRC}
    ${LIB_OTA_SRC}
    ${CMAKE_CURRENT_LIST_DIR}/ArduinoStub
)

add_compile_definitions(
    EMBLOGX_BACKEND_STDIO
    EMBLOGX_LOG_PREFIX="[TEST] "
    EMBLOGX_ENABLE_SINK_CONSOLE=1
    EMBLOGX_ENABLE_SINK_MEMORY=0
    EMBLOGX_ENABLE_SINK_HTTP=0
    EMBLOGX_ENABLE_SINK_SD=0
)

add_executable(test_ungula_ota
    test_ota_version.cpp
    test_ota_updater.cpp
    ArduinoStub/system_stub.cpp
    ${LIB_OTA_SRC}/ungula/ota/core/ota_updater.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/logger_core.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/async_dispatcher.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/sinks/console_sink.cpp
)
target_link_libraries(test_ungula_ota gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(test_ungula_ota)
