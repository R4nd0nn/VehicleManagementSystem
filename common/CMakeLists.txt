cmake_minimum_required(VERSION 3.10)
# 项目名和目录名统一
project(common)

# C++17 标准（Crow 要求）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ 修正：指定可执行文件输出到 user_management/output 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

# ✅ 包含所有头文件目录（解决 #include 报错）
# 1. 根 include 目录（包含子目录，无需单独加 model/service/util）
include_directories(${PROJECT_SOURCE_DIR}/include)
# 2. 公共头文件（上层 common 目录）
include_directories(${PROJECT_SOURCE_DIR}/../common)
include_directories(${PROJECT_SOURCE_DIR}/../config)
include_directories(/home/vehicleManagementSystem/VehicleManagementSystem/common/asio/asio/include)

# ✅ 修正：自动收集所有源文件（递归遍历 src/，并包含根目录的 main.cpp）
file(GLOB_RECURSE SOURCES 
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)
# 如果你的 main.cpp 在根目录，必须手动加进去！
# list(APPEND SOURCES ${PROJECT_SOURCE_DIR}/main.cpp)

# ✅ 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(user_management
    # 或者下面这种传统方式
    pthread
)