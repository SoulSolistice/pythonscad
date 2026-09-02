cmake_minimum_required(VERSION 3.5...3.20)

project(winconsole VERSION 1.0 LANGUAGES C)

set(CMAKE_EXECUTABLE_SUFFIX_C .com)

add_executable(winconsole winconsole.c)

# The wrapper starts its sibling by a bare "pythonscad.exe", which CreateProcess
# resolves against the directory of the running image, so the .com has to sit
# next to the .exe rather than in this subdirectory's own build folder. It is
# also where the test suite looks for it (tests/CMakeLists.txt sets
# OPENSCAD_BINPATH to ${CMAKE_BINARY_DIR}/pythonscad.com on Windows).
set_target_properties(winconsole PROPERTIES
  OUTPUT_NAME ${EXECUTABLE_NAME}
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Multi configuration generators append the configuration name to
# RUNTIME_OUTPUT_DIRECTORY; pin those too so the file always lands beside the
# executable.
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
  set_target_properties(winconsole PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}")
endforeach()

install(TARGETS winconsole RUNTIME DESTINATION ".")
