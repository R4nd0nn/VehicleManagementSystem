#ifndef MENU_H
#define MENU_H

#include <string>
#include "../common/include/base.h"

struct Menu {
    long menu_id;
    long parent_id;
    std::string menu_name;
    std::string path;
    std::string component;
    std::string query;
    std::string route_name;
    std::string menu_type;  // 'M' 菜单, 'C' 按钮
    std::string visible;    // '0' 显示, '1' 隐藏
    std::string status;     // '0' 正常
    std::string perms;
    std::string icon;
    int order_num;
    std::vector<Menu> children;
    
    crow::json::wvalue toJson() const {
        crow::json::wvalue obj;
        obj["menu_id"] = menu_id;
        obj["parent_id"] = parent_id;
        obj["menu_name"] = menu_name;
        obj["path"] = path;
        obj["component"] = component;
        obj["query"] = query;
        obj["route_name"] = route_name;
        obj["menu_type"] = menu_type;
        obj["visible"] = visible;
        obj["status"] = status;
        obj["perms"] = perms;
        obj["icon"] = icon;
        obj["order_num"] = order_num;
        
        // 转换children
        crow::json::wvalue children_array;
        int i = 0;
        for (const auto& child : children) {
            children_array[i++] = child.toJson();
        }
        obj["children"] = std::move(children_array);
        
        return obj;
    }
};

#endif