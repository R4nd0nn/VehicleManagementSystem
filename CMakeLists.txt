cmake_minimum_required(VERSION 3.10)
project(VehicleManagementSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 公共头文件全局包含
include_directories(${PROJECT_SOURCE_DIR}/common)

# 子模块（后续加其他模块直接加一行）
add_subdirectory(user_management)
# add_subdirectory(vehicle_management)
# add_subdirectory(order_management)