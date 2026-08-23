# transport-cineform-tui: the command line as a transport layer, with a display on it.
#
# Four dependencies, all from this repository's own default.xml, which makes this a `repo`
# manifest ROOT:
#
#   repo init -u https://github.com/weftspun/transport-cineform-tui -m default.xml
#   repo sync
#
# interactor-cineform is in that list for its HEADERS ONLY. `include/cineform/wire.hpp` and
# `progress_bus.hpp` define the four things iceoryx2 compares at connect time -- service name,
# payload type name, size and alignment -- and two copies of those would be the drift that
# `weft/command.hpp` exists to prevent one layer up. Nothing here links the encoder, and this
# build never compiles a line of it.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
cmake_minimum_required(VERSION 3.22)
project(transport_cineform_tui CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HARNESS_DIR    "${CMAKE_SOURCE_DIR}/thirdparty/harness"             CACHE PATH "contract-bus checkout")
set(INTERACTOR_DIR "${CMAKE_SOURCE_DIR}/thirdparty/interactor-cineform" CACHE PATH "interactor-cineform checkout")
set(FTXUI_DIR      "${CMAKE_SOURCE_DIR}/thirdparty/ftxui"               CACHE PATH "FTXUI checkout")

foreach(dep "${HARNESS_DIR}" "${INTERACTOR_DIR}" "${FTXUI_DIR}")
  if(NOT EXISTS "${dep}")
    message(FATAL_ERROR
      "missing dependency at ${dep}.\n"
      "  All come from this repository's own manifest. From the repository root:\n"
      "    repo init -u https://github.com/weftspun/transport-cineform-tui -m default.xml\n"
      "    repo sync")
  endif()
endforeach()

set(WEFT_HARNESS_PROOF OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("${HARNESS_DIR}" harness EXCLUDE_FROM_ALL)
add_subdirectory("${FTXUI_DIR}" ftxui EXCLUDE_FROM_ALL)

# job.cpp is compiled here rather than linked from the interactor, because the interactor is
# an executable and has no library to link against. The FILE is shared, which is what stops
# the two ends disagreeing; building it twice does not reintroduce the drift, because there is
# still exactly one copy of the source.
add_executable(cineform-tui
  src/main.cpp
  src/options.cpp
  "${INTERACTOR_DIR}/src/job.cpp"
)
target_include_directories(cineform-tui PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${INTERACTOR_DIR}/include"
)
target_link_libraries(cineform-tui PRIVATE weft::harness ftxui::dom ftxui::screen)

option(CINEFORM_TUI_PROOF "Build the option parsing proof" ON)
if(CINEFORM_TUI_PROOF)
  enable_testing()
  add_executable(cineform-tui-proof-options proof/options_test.cpp src/options.cpp
    "${INTERACTOR_DIR}/src/job.cpp")
  target_include_directories(cineform-tui-proof-options PRIVATE
    "${CMAKE_SOURCE_DIR}/src" "${INTERACTOR_DIR}/include")
  target_link_libraries(cineform-tui-proof-options PRIVATE weft::harness)
  add_test(NAME options COMMAND cineform-tui-proof-options)
endif()
