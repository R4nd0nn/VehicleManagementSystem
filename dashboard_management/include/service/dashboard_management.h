#ifndef DASHBOARD_MANAGEMENT_H
#define DASHBOARD_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class DashboardControllerFactory : public BaseControllerFactory<DashboardControllerFactory> {};

#define AUTO_REGISTER_DASHBOARD_API(name, func) \
    static auto __reg_##func = [](){ \
        DashboardControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif