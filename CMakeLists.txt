
cmake_minimum_required(VERSION 3.20)
project(AkatsukiDB_Cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


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




file(GLOB_RECURSE AKATSUKI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Bundle all your engine code into one static library.
add_library(AkatsukiDB_lib STATIC ${AKATSUKI_SOURCES})

# Tell the library where to find its own header files.
target_include_directories(AkatsukiDB_lib PUBLIC include)

# Link the JSON library to your core database library!
target_link_libraries(AkatsukiDB_lib PUBLIC nlohmann_json::nlohmann_json)


# Build the actual program you can run from the terminal.
add_executable(AkatsukiDB_Cpp cli/main.cpp)


# through AkatsukiDB_lib straight into your executable. 
target_link_libraries(AkatsukiDB_Cpp PRIVATE AkatsukiDB_lib replxx)
