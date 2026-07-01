cmake_minimum_required(VERSION 3.16)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_library(lob_core STATIC
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/BookPrinter.cpp
)
target_include_directories(lob_core PUBLIC include)

if(MSVC)
    target_compile_options(lob_core PRIVATE /W4)
else()
    target_compile_options(lob_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(lob_demo apps/demo.cpp)
target_link_libraries(lob_demo PRIVATE lob_core)

# Benchmark is only meaningful when the whole project is built Release
# (-DCMAKE_BUILD_TYPE=Release); per-target -O3 wouldn't reach lob_core.
add_executable(lob_bench apps/benchmark.cpp)
target_link_libraries(lob_bench PRIVATE lob_core)

enable_testing()
add_executable(lob_tests tests/test_main.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)
add_test(NAME lob_tests COMMAND lob_tests)
