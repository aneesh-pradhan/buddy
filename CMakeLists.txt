cmake_minimum_required(VERSION 3.30)

# Toolchain minimums (enforced below, and repeated in the README):
#   CMake 3.30 — first release that knows to pass -std=c++26 to GCC.
#                CMake 3.28 (Ubuntu 24.04) can do that for Clang 17+, not for GCC.
#   GCC 14 or Clang 18 — GCC 14 is the first GCC with -std=c++26. Clang 17
#   accepts the flag but does not compile libstdc++ from GCC 14 (what Clang
#   selects on current Ubuntu once libgcc 14 is installed). Clang 18 does.
#   Standard library: libstdc++ from GCC 13 or newer, or libc++ from LLVM 20 or
#   newer. libc++ 18 and 19 compile std::jthread only with -fexperimental-library.
project(buddy
  VERSION 0.1.0
  DESCRIPTION "C++26 helper for the CMF Buds Pro 2"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++ modules in this tree. Leaving the scan on makes Ninja require
# clang-scan-deps, which is not part of a plain clang++ install.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
    message(FATAL_ERROR
      "buddy requires GCC 14 or newer for -std=c++26 "
      "(found GCC ${CMAKE_CXX_COMPILER_VERSION}).\n"
      "On Ubuntu 24.04 the default g++ is 13. Install g++-14 and reconfigure "
      "with -DCMAKE_CXX_COMPILER=g++-14 (or CXX=g++-14 cmake --preset default).")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18.0)
    message(FATAL_ERROR
      "buddy requires Clang 18 or newer "
      "(found Clang ${CMAKE_CXX_COMPILER_VERSION}).\n"
      "Clang 17 accepts -std=c++26 but does not compile libstdc++ from GCC 14, "
      "which is the library Clang selects on current Ubuntu.")
  endif()
else()
  message(FATAL_ERROR
    "buddy builds with GCC 14+ or Clang 18+ "
    "(found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}).")
endif()

# A version check on the compiler does not catch an older standard library
# (Clang 19 with libc++ 19 accepts -std=c++26 and still has no std::jthread).
# Invoke the compiler directly so this does not depend on CMake's module scanner.
set(_buddy_probe_src "${CMAKE_BINARY_DIR}/CMakeFiles/buddy_stdlib_probe.cpp")
set(_buddy_probe_bin "${CMAKE_BINARY_DIR}/CMakeFiles/buddy_stdlib_probe")
file(WRITE "${_buddy_probe_src}" [[
#include <format>
#include <stop_token>
#include <string>
#include <thread>
int main() {
  std::jthread worker([](std::stop_token stop) { return stop.stop_requested(); });
  const std::string text = std::format("{}", 1);
  return text.contains("1") ? 0 : 1;
}
]])
separate_arguments(_buddy_cxx_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
separate_arguments(_buddy_link_flags NATIVE_COMMAND "${CMAKE_EXE_LINKER_FLAGS}")
execute_process(
  COMMAND "${CMAKE_CXX_COMPILER}" -std=c++26 -pthread
          ${_buddy_cxx_flags} "${_buddy_probe_src}"
          -o "${_buddy_probe_bin}" -pthread ${_buddy_link_flags}
  RESULT_VARIABLE _buddy_probe_rc
  OUTPUT_VARIABLE _buddy_probe_out
  ERROR_VARIABLE _buddy_probe_err
)
if(NOT _buddy_probe_rc EQUAL 0)
  message(FATAL_ERROR
    "This standard library cannot compile std::jthread, std::format, and "
    "std::string::contains, which buddy uses.\n"
    "Use libstdc++ from GCC 13 or newer (GCC 14+ ships one; Clang on Ubuntu "
    "uses libstdc++ unless you pass -stdlib=libc++), or libc++ from LLVM 20 "
    "or newer.\n"
    "libc++ 18 and 19 hide std::jthread unless -fexperimental-library is set. "
    "buddy does not pass that flag.\n"
    "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
    "(${CMAKE_CXX_COMPILER}).\n"
    "${_buddy_probe_err}${_buddy_probe_out}")
endif()
message(STATUS
  "buddy: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, C++26")

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 6)
FetchContent_MakeAvailable(ftxui)

add_library(cmf src/protocol.cpp src/device.cpp)
target_include_directories(cmf PUBLIC src)
target_compile_options(cmf PRIVATE -Wall -Wextra)

add_executable(buddy src/main.cpp src/logo.cpp src/volume.cpp)
target_link_libraries(buddy PRIVATE cmf ftxui::screen ftxui::dom ftxui::component)
target_compile_options(buddy PRIVATE -Wall -Wextra)

enable_testing()
add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE cmf)
add_test(NAME protocol COMMAND test_protocol)
