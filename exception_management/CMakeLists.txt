cmake_minimum_required(VERSION 3.10)
project(exception_management)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

# 头文件
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/../common
    ${PROJECT_SOURCE_DIR}/../config
)

# 源文件
file(GLOB_RECURSE SOURCES 
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# 可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库（使用完整路径）
target_link_libraries(${PROJECT_NAME}
    pqxx
    pq
    pthread
    crypto
    ssl
    /usr/local/lib/libOpenXLSX.a
    /usr/local/lib/libpugixml.a
    /usr/local/lib/libminiz.a
    dl
    z
)

# 添加头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE
    /usr/local/include
)