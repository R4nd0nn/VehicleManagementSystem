cmake_minimum_required(VERSION 3.10)
project(order_management)

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

# 链接库（直接使用 -l 标志）
target_link_libraries(${PROJECT_NAME}
    pqxx
    pq
    pthread
    crypto  
)