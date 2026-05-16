#ifndef TASK_H
#define TASK_H

#include <string>
#include "VehicleManagementSystem/common/base.h"

struct Task{
    int id;
    int order_id;
    int driver_id;
    std::string container_id;
    int task_status;
    std::string task_start_time;
    std::string task_complete_time;
    std::string pick_up_container_time;
    std::string delivered_container_time;
    std::string return_container_time;
    int emergency_status;
    int fee_id;
};

#endif