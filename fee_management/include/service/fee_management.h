#ifndef FEE_MANAGEMENT_H
#define FEE_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class FeeControllerFactory : public BaseControllerFactory<FeeControllerFactory> {};

#define AUTO_REGISTER_FEE_API(name, func) \
    static auto __reg_##func = [](){ \
        FeeControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif