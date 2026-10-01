cmake_minimum_required(VERSION 3.16)
project(random_picker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(roster STATIC src/roster.cpp)
target_include_directories(roster PUBLIC src)

add_executable(picker_cli src/cli.cpp)
target_link_libraries(picker_cli roster)

if(WIN32)
  add_executable(picker WIN32 src/picker_gui.cpp)
  target_link_libraries(picker roster gdi32 user32 comdlg32 bcrypt)
  target_link_options(picker PRIVATE -static -static-libgcc -static-libstdc++)
endif()
