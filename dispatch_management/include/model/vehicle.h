#ifndef VEHICLE_H
#define VEHICLE_H

#include <string>
#include "VehicleManagementSystem/common/base.h"

struct Vehicle{
    int id;
    std::string license_plate;
    std::string type;
    int status;
    std::string gps_id;
    int kilometres;
    std::string insurance_expire_date;
};

#endif