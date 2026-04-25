#ifndef ORDER_MANAGEMENT_H
#define ORDER_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class OrderControllerFactory : public BaseControllerFactory<OrderControllerFactory> {};

#define AUTO_REGISTER_ORDER_API(name, func) \
    static auto __reg_##func = [](){ \
        OrderControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif