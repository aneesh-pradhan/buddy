cmake_minimum_required(VERSION 3.24)
project(buddy
  VERSION 0.1.0
  DESCRIPTION "C++26 helper for the CMF Buds Pro 2"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
