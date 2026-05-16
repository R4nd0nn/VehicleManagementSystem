#ifndef DISPATCH_MANAGEMENT_H
#define DISPATCH_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class DispatchControllerFactory : public BaseControllerFactory<DispatchControllerFactory> {};

#define AUTO_REGISTER_DISPATCH_API(name, func) \
    static auto __reg_##func = [](){ \
        DispatchControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif