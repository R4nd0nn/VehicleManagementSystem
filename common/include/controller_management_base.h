#ifndef BASE_MANAGEMENT_H
#define BASE_MANAGEMENT_H

#include "crow_all.h"
#include "database.h"
#include <memory>
#include <string>
#include <unordered_map>

using ControllerMapFunc = crow::response(*)(const crow::request&, pqxx::connection&);

template<typename Derived>
class BaseControllerFactory {
public:
    static Derived& instance() {
        static Derived factory;
        return factory;
    }

    void registerController(const std::string& name, ControllerMapFunc api) {
        controllerMap[name] = api;
    }

    ControllerMapFunc create(const std::string& name) {
        auto it = controllerMap.find(name);
        return it != controllerMap.end() ? it->second : nullptr;
    }
    
protected:
    BaseControllerFactory() = default;
    std::unordered_map<std::string, ControllerMapFunc> controllerMap;
};

#endif