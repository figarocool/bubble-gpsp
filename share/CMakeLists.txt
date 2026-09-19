cmake_minimum_required(VERSION 3.12)

# collect_files() below snapshots pkg/ at configure time, but
# language.zbin/translation.zbin are normally only produced by the
# add_custom_command() further down, which runs at build (make) time -
# on a from-scratch checkout that leaves them out of PUBLIC_PKG_FILES
# entirely (silently: no build error, just a vpk missing them), which
# crashes the app inside LanguageString::Init() the moment it tries to
# open assets/language.zbin from its own package and finds it absent.
# Generate them here too, once, so a single `cmake && make` on a clean
# checkout already has them before collect_files() runs.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/language.zbin OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/translation.zbin)
    execute_process(
        COMMAND python export_share.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../script
        RESULT_VARIABLE _lang_gen_result
    )
    if(NOT _lang_gen_result EQUAL 0)
        message(FATAL_ERROR "export_share.py failed with code ${_lang_gen_result}")
    endif()
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets)
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/../script/language_define.h ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.h)
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/../script/language_define.cpp ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.cpp)
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/../script/language.zbin ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/language.zbin)
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/../script/translation.zbin ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/translation.zbin)
endif()

set(PUBLIC_PKG_FILES "")
collect_files(PUBLIC_PKG_FILES ${CMAKE_CURRENT_SOURCE_DIR}/pkg)
SET(PUBLIC_PKG_FILES "${PUBLIC_PKG_FILES}" CACHE INTERNAL "PUBLIC_PKG_FILES")

add_custom_command(
    OUTPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/language.zbin
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/translation.zbin

    COMMAND python export_share.py
    COMMAND mv language_define.h ${CMAKE_CURRENT_SOURCE_DIR}/source
    COMMAND mv language_define.cpp ${CMAKE_CURRENT_SOURCE_DIR}/source
    COMMAND mv language.zbin ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets
    COMMAND mv translation.zbin ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets

    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../script

    DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../script/language.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../script/translation.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../script/export_share.py
    ${CMAKE_CURRENT_SOURCE_DIR}/../script/language.py
)

add_custom_target(
    LANG_SHARE
    DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/language.zbin
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/assets/translation.zbin
)

file(GLOB SRC source/*.cpp)

if(NOT ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.cpp IN_LIST SRC)
    list(APPEND SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/language_define.cpp)
endif()

include_directories(
    ../deps/libretro-common/include
    ../deps/7-Zip/C
    ../deps/lz4/lib
    ../deps/minizip-ng)

add_library(share STATIC ${SRC})
add_dependencies(share LANG_SHARE)
target_link_libraries(share PUBLIC minizip)