cmake_minimum_required(VERSION 3.12)

# bubble-gpsp is a trimmed fork of Emu4VitaPlus that only ever builds the
# gpsp (GBA) core, so CORE_ROWS keeps just that one entry instead of
# upstream's ~50-core table.
# CORE_DIR;CORE;CORE_SHORT;CONSOLE;TITLE_ID;
set(CORE_ROWS
    "gpsp;gpsp;gpSP;GBA;GPSP4VITA"
)

set(ROW_SIZE 5)

macro(GET_CORES)
    set(CORE_DIRS "" CACHE INTERNAL "CORE_DIRS")
    set(CORES "" CACHE INTERNAL "CORES")
    set(CORE_SHORTS "" CACHE INTERNAL "CORE_SHORTS")
    set(CONSOLES "" CACHE INTERNAL "CONSOLES")
    set(TITLE_IDS "" CACHE INTERNAL "TITLE_IDS")

    list(LENGTH CORE_ROWS length)
    math(EXPR length "${length} / ${ROW_SIZE} - 1")

    foreach(ROW RANGE ${length})
        math(EXPR INDEX "${ROW} * ${ROW_SIZE}")

        list(GET CORE_ROWS ${INDEX} V)
        list(APPEND CORE_DIRS ${V})

        math(EXPR INDEX "${INDEX} + 1")
        list(GET CORE_ROWS ${INDEX} V)
        list(APPEND CORES ${V})

        math(EXPR INDEX "${INDEX} + 1")
        list(GET CORE_ROWS ${INDEX} V)
        list(APPEND CORE_SHORTS ${V})

        math(EXPR INDEX "${INDEX} + 1")
        list(GET CORE_ROWS ${INDEX} V)
        list(APPEND CONSOLES ${V})

        math(EXPR INDEX "${INDEX} + 1")
        list(GET CORE_ROWS ${INDEX} V)
        list(APPEND TITLE_IDS ${V})
    endforeach()
endmacro()