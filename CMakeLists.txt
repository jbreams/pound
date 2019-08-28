cmake_minimum_required(VERSION 3.5)
project(pound LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

add_subdirectory(third_party)

add_executable(pound
    pound.cpp
    piecetable.cpp
    terminal.cpp
)

target_link_libraries(pound
    fmt
    string-view-lite
    optional
)

add_executable(piecetable_test
    piecetable_test.cpp
    piecetable.cpp
)

target_link_libraries(piecetable_test
    fmt
    string-view-lite
    optional
    Catch2::Catch2
)