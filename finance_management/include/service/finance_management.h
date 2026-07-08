#ifndef FINANCE_MANAGEMENT_H
#define FINANCE_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class FinanceControllerFactory : public BaseControllerFactory<FinanceControllerFactory> {};

#define AUTO_REGISTER_FINANCE_API(name, func) \
    static auto __reg_##func = [](){ \
        FinanceControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif