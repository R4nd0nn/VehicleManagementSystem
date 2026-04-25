#ifndef USER_MANAGEMENT_H
#define USER_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class UserControllerFactory : public BaseControllerFactory<UserControllerFactory> {};

#define AUTO_REGISTER_USER_API(name, func) \
    static auto __reg_##func = [](){ \
        UserControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif