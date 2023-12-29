cmake_minimum_required(VERSION 3.14)

project(bench_libfork LANGUAGES CXX)

include(../../cmake/project-is-top-level.cmake)
include(../../cmake/folders.cmake)
include(../../cmake/CPM.cmake)

# ---- Dependencies ----
CPMAddPackage("gh:martinus/nanobench#v4.1.0")
CPMAddPackage(
  NAME TooManyCooks
  GIT_REPOSITORY https://github.com/tzcnt/TooManyCooks.git
  GIT_TAG 7990de1c6e1cde6a0191dbd4617c4f646d638b9b
  DOWNLOAD_ONLY
)

find_package(libhwloc)
add_definitions(
  "-DTMC_WORK_ITEM=CORO"
  "-DTMC_PRIORITY_COUNT=1"
  "-DTMC_USE_HWLOC"
)
link_libraries(${LIBHWLOC_LIBRARY})

find_package(libjemalloc)
link_libraries(${LIBJEMALLOC_LIBRARY})

include_directories(
  ${TooManyCooks_SOURCE_DIR}/include
)

# ---- Benchmarks ----
add_executable(tmc_fib.bench fib.cpp)
target_link_libraries(tmc_fib.bench PRIVATE nanobench)

add_executable(tmc_reduce.bench reduce.cpp)
target_link_libraries(tmc_reduce.bench PRIVATE nanobench)

add_executable(tmc_matmul.bench matmul.cpp)
target_link_libraries(tmc_matmul.bench PRIVATE nanobench)

add_executable(tmc_dfs.bench dfs.cpp)
target_link_libraries(tmc_dfs.bench PRIVATE nanobench)

# ---- End-of-file commands ----
add_folders(benchmarks)
