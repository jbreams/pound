cmake_minimum_required(VERSION 3.5)
project(pound LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party)

add_executable(pound
    pound.cpp
    piecetable.cpp
    prompt.cpp
    terminal.cpp
    document.cpp
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