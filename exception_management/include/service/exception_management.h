#ifndef EXCEPTION_MANAGEMENT_H
#define EXCEPTION_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class ExceptionControllerFactory : public BaseControllerFactory<ExceptionControllerFactory> {};

#define AUTO_REGISTER_EXCEPTION_API(name, func) \
    static auto __reg_##func = [](){ \
        ExceptionControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif