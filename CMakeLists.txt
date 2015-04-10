cmake_minimum_required(VERSION 2.8)

project(lassort)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

find_package(LibLAS REQUIRED)
find_package(Boost COMPONENTS system thread filesystem program_options regex REQUIRED)

file(GLOB files src/*.cpp include/*.h include/*.hpp )

include_directories(include ${LIBLAS_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})

add_executable(lassort ${files})

target_link_libraries(lassort ${LIBLAS_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_REGEX_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY})