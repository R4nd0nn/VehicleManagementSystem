#ifndef USER_MANAGEMENT_H
#define USER_MANAGEMENT_H

#include "../common/include/crow_all.h"
#include <memory>
#include <string>
#include <unordered_map>

using UserControllerMapFunc = crow::response(*)(const crow::request& req);

class UserControllerFactory{
public:
    static UserControllerFactory& instance() {
        static UserControllerFactory factory;
        return factory;
    }

    void registerController(const std::string& name, UserControllerMapFunc userApi) {
        userControllerMap[name] = userApi;
    }

    UserControllerMapFunc create(const std::string& name) {
        auto it = userControllerMap.find(name);
        if (it != userControllerMap.end()) {
            return it->second;
        }
        return nullptr;
    }
private:
    UserControllerFactory() = default;
    UserControllerFactory(const UserControllerFactory&) = delete;
    UserControllerFactory& operator=(const UserControllerFactory&) = delete;
    std::unordered_map<std::string, UserControllerMapFunc> userControllerMap;    
};

#define AUTO_REGISTER_USER_API(name, func) \
    static auto __reg_##func = [](){ \
        UserControllerFactory::instance().registerController(name, func); \
        return nullptr; \
    }();


#endif