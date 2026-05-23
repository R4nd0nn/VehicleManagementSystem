#ifndef CONTAINER_MANAGEMENT_H
#define CONTAINER_MANAGEMENT_H

#include "../common/include/controller_management_base.h"

class ContainerControllerFactory : public BaseControllerFactory<ContainerControllerFactory> {};

#define AUTO_REGISTER_CONTAINER_API(name, func) \
    static auto __reg_##func = [](){ \
        ContainerControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();

#endif