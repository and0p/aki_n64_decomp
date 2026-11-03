cmake_minimum_required(VERSION 3.20)
project(aki LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Frame-exact floats: no fused multiply-add, no fast-math, SSE (not x87) on x86.
if(MSVC)
  add_compile_options(/fp:precise /W3)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-unused-parameter)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|AMD64|i.86")
    add_compile_options(-msse2 -mfpmath=sse)
  endif()
  if(NOT EMSCRIPTEN)
    add_compile_options(-fno-exceptions -fno-rtti)
  endif()
endif()

# WWF No Mercy: the faithful decompilation of the match overlay (the oracle for the port).
add_library(nomercy_decomp STATIC
  src/nomercy/nm_ai.cpp
  src/nomercy/nm_aerial.cpp
  src/nomercy/nm_ladder.cpp
  src/nomercy/nm_counter.cpp
  src/nomercy/nm_anim.cpp
  src/nomercy/nm_bounds.cpp
  src/nomercy/nm_down.cpp
  src/nomercy/nm_frame.cpp
  src/nomercy/nm_grapple.cpp
  src/nomercy/nm_hold.cpp
  src/nomercy/nm_host.cpp
  src/nomercy/nm_math.cpp
  src/nomercy/nm_model.cpp
  src/nomercy/nm_object.cpp
  src/nomercy/nm_replay_rec.cpp
  src/nomercy/nm_rng.cpp
  src/nomercy/nm_run.cpp
  src/nomercy/nm_stand.cpp
  src/nomercy/nm_state.cpp
  src/nomercy/nm_strike.cpp
  src/nomercy/nm_tag.cpp
  src/nomercy/nm_states.cpp
  src/nomercy/nm_todo.cpp
  src/nomercy/nm_walk.cpp
  src/nomercy/nm_wrestler.cpp
)
target_include_directories(nomercy_decomp PUBLIC src/nomercy)

add_executable(nm_replay src/tools/nm_replay.cpp)
target_link_libraries(nm_replay PRIVATE nomercy_decomp)

enable_testing()
# The golden traces are ROM-derived and live in _tools/traces (git-ignored); the test is skipped
# when the trace is missing. Record it with tools/trace/record.py.
find_package(Python3 COMPONENTS Interpreter)
# The animations stream from the ROM (never committed): the replay reads it with --rom.
set(NM_ROM "${CMAKE_SOURCE_DIR}/_roms/WWF No Mercy (USA) (Rev A).z64" CACHE FILEPATH "WWF No Mercy USA Rev 1 ROM")
# One test per trace and mode; a missing trace skips its tests (record it with tools/trace/record.py
# from tools/trace/scripts/<name>.json).
set(NM_TRACES nm_walk_run nm_strikes nm_grapples nm_lockup_moves nm_pin_move nm_cover nm_count3 nm_match_end)
if(Python3_FOUND)
  foreach(trace ${NM_TRACES})
    foreach(mode step free)
      add_test(NAME ${trace}_${mode}
        COMMAND ${CMAKE_COMMAND} -DREPLAY=$<TARGET_FILE:nm_replay> -DPYTHON=${Python3_EXECUTABLE}
                -DTRACE=${CMAKE_SOURCE_DIR}/_tools/traces/${trace}.akt -DROM=${NM_ROM} -DMODE=${mode}
                -DOUT=${CMAKE_BINARY_DIR}/${trace}_${mode}.akt
                -DDIFF=${CMAKE_SOURCE_DIR}/tools/trace/diff.py -P ${CMAKE_SOURCE_DIR}/tools/trace/replay_test.cmake)
      set_tests_properties(${trace}_${mode} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
  endforeach()
endif()
