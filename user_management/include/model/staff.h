#ifndef COMMON_DEF_H
#define COMMON_DEF_H

#include <string>
#include "VehicleManagementSystem/common/base.h"

struct Staff{
    int id;
    int role;
    std::string name;
    int gender;
    int age;
    std::string birthday;
    std::string position;
    std::string email_address;
    std::string phone_number;
    std::string password;
};

#endif