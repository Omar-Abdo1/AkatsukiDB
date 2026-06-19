
cmake_minimum_required(VERSION 3.20)
project(AkatsukiDB_Cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -O0 -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")  # only for debugging


#EXTERNAL DEPENDENCIES
include(FetchContent)
FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)


include(FetchContent)
FetchContent_Declare(
    replxx
    GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
    GIT_TAG master
)

FetchContent_MakeAvailable(replxx) # for support history like terminal 


#find_package(SQLite3 REQUIRED) # only for benchmark



file(GLOB_RECURSE AKATSUKI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(AkatsukiDB_lib STATIC ${AKATSUKI_SOURCES})

target_include_directories(AkatsukiDB_lib PUBLIC include)

target_link_libraries(AkatsukiDB_lib PUBLIC nlohmann_json::nlohmann_json)


add_executable(AkatsukiDB_Cpp cli/main.cpp)

#add_executable(benchmark benchmark/benchmark.cpp) # for benchmark

target_link_libraries(AkatsukiDB_Cpp PRIVATE AkatsukiDB_lib replxx)


#target_link_libraries(benchmark PRIVATE AkatsukiDB_lib SQLite::SQLite3) # for benchmark


