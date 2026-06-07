cmake_minimum_required(VERSION 4.2)
project(AkatsukiDB_Cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AkatsukiDB_lib STATIC
        src/Storage/Page.cpp
        src/Storage/PageManager.cpp
        src/Storage/LruCache.cpp
        src/Storage/BufferPool.cpp
        src/Storage/StorageLayout.cpp)
#create library

target_include_directories(AkatsukiDB_lib PUBLIC include)
#where the headers are in /include

add_executable(AkatsukiDB_Cpp cli/main.cpp)

target_link_libraries(AkatsukiDB_Cpp PRIVATE AkatsukiDB_lib)
#link the console to the library
