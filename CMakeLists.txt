
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



# Bundle all your engine code into one static library.
add_library(AkatsukiDB_lib STATIC
        src/Storage/Page.cpp
        src/Storage/PageManager.cpp
        src/Storage/LruCache.cpp
        src/Storage/BufferPool.cpp
        src/Storage/StorageLayout.cpp
        src/Table/TableManager.cpp
        src/Table/RowSerializer.cpp
        src/Table/TableRegistry.cpp
        src/Index/IndexKey.cpp
        include/AkatsukiDB/Index/IndexKey.hpp
        src/Index/BPlusTreeNode.cpp
        include/AkatsukiDB/Index/BPlusTreeNode.hpp
        src/Index/BPlusTree.cpp
        include/AkatsukiDB/Index/BPlusTree.hpp
)

# Tell the library where to find its own header files.
target_include_directories(AkatsukiDB_lib PUBLIC include)

# Link the JSON library to your core database library!
target_link_libraries(AkatsukiDB_lib PUBLIC nlohmann_json::nlohmann_json)


# Build the actual program you can run from the terminal.
add_executable(AkatsukiDB_Cpp cli/main.cpp)


# through AkatsukiDB_lib straight into your executable.
target_link_libraries(AkatsukiDB_Cpp PRIVATE AkatsukiDB_lib)