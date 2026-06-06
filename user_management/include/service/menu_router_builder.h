#ifndef MENU_ROUTER_BUILDER_H
#define MENU_ROUTER_BUILDER_H

#include <string>
#include <vector>
#include <algorithm>

#include "../model/menu.h"
#include "../common/include/controller_management_base.h"

class MenuRouterBuilder {
private:
    static constexpr long MENU_ROOT_ID = 0;
    
    // 递归构建菜单树
    static std::vector<Menu> buildMenuTree(std::vector<Menu>& menus, long parentId) {
        std::vector<Menu> tree;
        for (auto& menu : menus) {
            if (menu.parent_id == parentId) {
                // 递归查找子菜单
                menu.children = buildMenuTree(menus, menu.menu_id);
                tree.push_back(std::move(menu));
            }
        }
        // 按 order_num 排序
        std::sort(tree.begin(), tree.end(), [](const Menu& a, const Menu& b) {
            return a.order_num < b.order_num;
        });
        return tree;
    }
    
    // 获取路由路径
    static std::string getRouterPath(const Menu& menu) {
        std::string routerPath = menu.path;
        
        // 如果是顶级菜单（parent_id == 0）
        if (menu.parent_id == MENU_ROOT_ID) {
            // 顶级菜单路径以 '/' 开头
            if (!routerPath.empty() && routerPath[0] != '/') {
                routerPath = "/" + routerPath;
            }
            return routerPath;
        }
        
        // 子菜单：保持原路径，前端filterChildren会处理拼接
        return routerPath;
    }
    
    // 判断是否为外链
    static bool isLink(const Menu& menu) {
        return !menu.component.empty() && menu.component.find("http") == 0;
    }
    
    // 获取组件路径
    static std::string getComponent(const Menu& menu) {
        // 外链使用 InnerLink
        if (isLink(menu)) {
            return "InnerLink";
        }
        
        if (menu.component.empty()) {
            return "ParentView";
        }
        
        if (menu.component == "Layout" || 
            menu.component == "ParentView" || 
            menu.component == "InnerLink") {
            return menu.component;
        }
        
        return menu.component;
    }
    
    // 构建单个路由VO
    static crow::json::wvalue buildRouter(const Menu& menu) {
        crow::json::wvalue router;
        
        // 1. name: 优先使用 route_name，否则使用 path
        std::string name = menu.route_name.empty() ? menu.path : menu.route_name;
        router["name"] = name;
        
        // 2. path
        router["path"] = getRouterPath(menu);
        
        // 3. hidden: visible == '1' 时隐藏
        router["hidden"] = (menu.visible == "1");
        
        // 4. component
        router["component"] = getComponent(menu);
        
        // 5. meta
        crow::json::wvalue meta;
        meta["title"] = menu.menu_name;
        meta["icon"] = menu.icon;
        meta["noCache"] = false;
        router["meta"] = std::move(meta);
        
        // 6. 处理子菜单
        if (!menu.children.empty() && menu.menu_type == "M") {
            // 只有当有子菜单时才设置 redirect
            router["redirect"] = "noRedirect";
            
            // 递归构建子路由
            crow::json::wvalue children_array;
            int i = 0;
            for (const auto& child : menu.children) {
                // 只处理菜单类型('M')，按钮类型('C')不生成路由
                if (child.menu_type == "M") {
                    children_array[i++] = buildRouter(child);
                }
            }
            
            if (i > 0) {
                router["children"] = std::move(children_array);
                // 有子菜单时设置 alwaysShow
                router["alwaysShow"] = true;
            }
        }
        
        // 7. query: 只有非空时才设置
        if (!menu.query.empty()) {
            router["query"] = menu.query;
        }
        
        return router;
    }
    
public:
    // 主构建方法
    static crow::json::wvalue buildMenus(const std::vector<Menu>& menus) {
        // 1. 先建立父子关系映射，构建树形结构
        std::unordered_map<long, std::vector<Menu>> childrenMap;
        std::vector<Menu> rootMenus;
        
        for (const auto& menu : menus) {
        if (menu.parent_id == 0) {
            rootMenus.push_back(menu);
        } else {
            childrenMap[menu.parent_id].push_back(menu);
        }
        }
        
        // 2. 递归构建 Router（完全按照 Java 逻辑）
        std::function<std::vector<crow::json::wvalue>(const std::vector<Menu>&)> buildRouters = 
        [&](const std::vector<Menu>& menuList) -> std::vector<crow::json::wvalue> {
        std::vector<crow::json::wvalue> routers;
        
        for (const auto& menu : menuList) {
            crow::json::wvalue router;
            
            // 设置 hidden
            router["hidden"] = (menu.visible == "1");
            
            // 设置 name
            router["name"] = menu.route_name.empty() ? menu.menu_name : menu.route_name;
            
            // 设置 path（按照 Java 的 getRouterPath 逻辑）
            std::string routerPath = menu.path;
            auto it = childrenMap.find(menu.menu_id);
            if (it != childrenMap.end() && !it->second.empty()) {
                // 有子菜单，path 前面加 /
                if (routerPath.empty()) {
                    routerPath = menu.menu_name;
                }
                if (routerPath[0] != '/') {
                    routerPath = "/" + routerPath;
                }
            }
            router["path"] = routerPath;
            
            // 设置 component
            router["component"] = menu.component.empty() ? "Layout" : menu.component;
            
            // 设置 query
            router["query"] = menu.query;
            
            // 设置 meta
            crow::json::wvalue meta;
            meta["title"] = menu.menu_name;
            meta["icon"] = menu.icon.empty() ? "system" : menu.icon;
            meta["noCache"] = false;
            meta["link"] = nullptr;
            router["meta"] = std::move(meta);
            
            // 获取子菜单
            std::vector<Menu> childMenus;
            auto childIt = childrenMap.find(menu.menu_id);
            if (childIt != childrenMap.end()) {
                childMenus = childIt->second;
            }
            
            // Java 逻辑：如果菜单类型是 'M' 且有子菜单，才设置 alwaysShow 和 redirect
            if (menu.menu_type == "M" && !childMenus.empty()) {
                router["alwaysShow"] = true;
                router["redirect"] = "noRedirect";
                router["children"] = buildRouters(childMenus);  // 递归
            } else {
                router["children"] = crow::json::wvalue::list();
            }
            
            routers.push_back(std::move(router));
        }
        
        return routers;
        };
        
        // 3. 从根菜单开始构建
        std::vector<crow::json::wvalue> resultRouters = buildRouters(rootMenus);
        
        crow::json::wvalue result;
        result = std::move(resultRouters);
        return result;
    }
};

#endif