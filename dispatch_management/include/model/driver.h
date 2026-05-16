#ifndef DRIVER_H
#define DRIVER_H

#include <string>
#include "VehicleManagementSystem/common/base.h"

struct Driver{
    int id;
    int client_id;
    std::string name;
    int phone_no;
    std::string email;
    std::string driver_license;
    std::string class_of_vehicle;
    std::string license_issue_date;
    std::string license_expire_date;
    std::string license_issue_state;
    int status;
};

#endif