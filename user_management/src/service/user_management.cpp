#include "service/user_management.h"
#include "service/menu_router_builder.h"
#include "../common/include/jwt/jwt.h"

crow::response getRouterFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 1. 验证token
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["code"] = 401;
        result["msg"] = "Missing token";
        return crow::response(401, result);
    }
    
    try {
        // 验证token
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);
        
        const std::string username = decoded.get_subject();
        
        // 2. 查询当前用户的角色
        pqxx::work txn(conn);
        pqxx::result userRes = txn.exec_params(
            "SELECT role FROM staff WHERE username = $1", username);
        
        if (userRes.empty()) {
            result["code"] = 400;
            result["msg"] = "User not found";
            return crow::response(400, result);
        }
        
        int role = userRes[0]["role"].as<int>();
        
        // 3. 根据角色确定可见的菜单名称列表
        std::vector<std::string> allowedMenuNames;
        
        switch (role) {
            case 0:  // 超级管理员 - 全部可见
            case 1:  // 管理员 - 全部可见
                // 不限制，查询所有
                break;
            case 2:  // 普通用户 - 订单管理，调度管理，集装箱管理，在途监控，异常管理
                allowedMenuNames = {
                    "订单管理", 
                    "调度管理", 
                    "集装箱管理", 
                    "在途监控", 
                    "异常管理"
                };
                break;
            case 3:  // 财务 - 费用管理，财务管理
                allowedMenuNames = {
                    "费用管理", 
                    "财务管理"
                };
                break;
            case 4:  // 访客 - 仅主页
                allowedMenuNames = {
                    "主页"
                };
                break;
            default:
                // 默认只显示主页
                allowedMenuNames = {
                    "主页"
                };
                break;
        }
        
        // 4. 查询菜单
        std::string query = 
            "SELECT DISTINCT "
            "menu_id, parent_id, menu_name, path, component, query, "
            "route_name, visible, status, COALESCE(perms, '') as perms, "
            "menu_type, icon, order_num "
            "FROM menu "
            "WHERE menu_type IN ('M', 'C') AND status = '0' ";
        
        // 根据角色添加过滤条件
        if (role != 0 && role != 1) {
            // 构建允许的菜单名称条件
            std::string menuNameCondition = "AND (";
            for (size_t i = 0; i < allowedMenuNames.size(); i++) {
                if (i > 0) menuNameCondition += " OR ";
                menuNameCondition += "menu_name = '" + allowedMenuNames[i] + "'";
            }
            // 也包含子菜单（父菜单在允许列表中的）
            menuNameCondition += " OR parent_id IN ("
                "SELECT menu_id FROM menu WHERE ";
            for (size_t i = 0; i < allowedMenuNames.size(); i++) {
                if (i > 0) menuNameCondition += " OR ";
                menuNameCondition += "menu_name = '" + allowedMenuNames[i] + "'";
            }
            menuNameCondition += "))";
            query += menuNameCondition;
        }
        
        query += " ORDER BY parent_id, order_num";
        
        pqxx::result res = txn.exec(query);
        
        // 5. 转换为Menu结构体列表
        std::vector<Menu> menus;
        for (const auto& row : res) {
            Menu menu;
            menu.menu_id = row["menu_id"].as<long>();
            menu.parent_id = row["parent_id"].as<long>();
            menu.menu_name = row["menu_name"].as<std::string>();
            menu.path = row["path"].is_null() ? "" : row["path"].as<std::string>();
            menu.component = row["component"].is_null() ? "" : row["component"].as<std::string>();
            menu.query = row["query"].is_null() ? "" : row["query"].as<std::string>();
            menu.route_name = row["route_name"].is_null() ? "" : row["route_name"].as<std::string>();
            menu.visible = row["visible"].as<std::string>();
            menu.status = row["status"].as<std::string>();
            menu.perms = row["perms"].as<std::string>();
            menu.menu_type = row["menu_type"].as<std::string>();
            menu.icon = row["icon"].is_null() ? "" : row["icon"].as<std::string>();
            menu.order_num = row["order_num"].as<int>();
            
            menus.push_back(menu);
        }
        
        // 6. 构建路由树
        crow::json::wvalue routers = MenuRouterBuilder::buildMenus(menus);
        
        // 7. 返回结果
        result["code"] = 200;
        result["msg"] = "操作成功";
        result["data"] = std::move(routers);
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["code"] = 500;
        result["msg"] = "Database error: " + std::string(e.what());
        return crow::response(500, result);
    }
}

// 登录接口
crow::response userLoginFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("password")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username and password required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string password = body["password"].s();

    auto token = jwt::create()
        .set_issuer("user_management")
        .set_subject(username) 
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(12))
        .sign(jwt::algorithm::hs256{ "user_management" });

    try {
        pqxx::work txn(conn);
        
        // 查询用户
        pqxx::result res = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1 AND password = $2",
            username, password
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid username or password";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["token"] = token;
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 忘记密码接口
crow::response forgetPasswordFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("phoneNum")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username and phoneNum required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string phoneNum = body["phoneNum"].s();
    
    try {
        pqxx::work txn(conn);
        
        // 验证用户名和手机号
        pqxx::result res = txn.exec_params(
            "SELECT name FROM staff WHERE username = $1 AND phone_no = $2",
            username, phoneNum
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid username or phone number";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["errorMsg"] = "";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 重置密码接口
crow::response resetPasswordFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("newPassword") || !body.has("newPasswordConfirm")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username, newPassword and newPasswordConfirm required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string newPassword = body["newPassword"].s();
    std::string newPasswordConfirm = body["newPasswordConfirm"].s();
    
    // 验证两次密码是否一致
    if (newPassword != newPasswordConfirm) {
        result["retCode"] = 400;
        result["errorMsg"] = "Passwords do not match";
        return crow::response(400, result);
    }
    
    try {
        pqxx::work txn(conn);
        
        // 更新密码
        pqxx::result res = txn.exec_params(
            "UPDATE staff SET password = $1 WHERE username = $2 RETURNING username",
            newPassword, username
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["errorMsg"] = "";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 查询用户信息接口
crow::response queryUserInfoFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    std::string token = req.get_header_value("token");

    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    try {
        pqxx::work txn(conn);
        
        auto decoded = jwt::decode(token);

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded);

        const std::string username = decoded.get_subject();
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, role, username, name, gender, age, birthday, position, email_address, phone_no FROM staff WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数
        std::unordered_map<std::string, std::string> queryParams;
        
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::vector<std::string> paramKeys = {
            "id", "role", "username", "name", "gender", "age", 
            "birthday", "position", "email_address", "phone_no",
            "age_min", "age_max", "birthday_start", "birthday_end",
            "pageNum", "pageSize"
        };
        
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        if (!username.empty()) {
            queryParams["username"] = username;
        }
        
        struct FilterField {
            std::string paramName;
            std::string dbField;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"role", "role", false},
            {"username", "username", false},
            {"name", "name", true},
            {"gender", "gender", false},
            {"age", "age", false},
            {"birthday", "birthday", false},
            {"position", "position", true},
            {"email_address", "email_address", true},
            {"phone_no", "phone_no", true},
            {"age_min", "age", false},
            {"age_max", "age", false},
            {"birthday_start", "birthday", false},
            {"birthday_end", "birthday", false},
        };
        
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName == "age_min") {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "age_max") {
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_start") {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_end") {
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        int pageNum = 1;
        int pageSize = 20;
        
        auto itPage = queryParams.find("pageNum");
        if (itPage != queryParams.end() && !itPage->second.empty()) {
            pageNum = std::stoi(itPage->second);
        }
        
        auto itSize = queryParams.find("pageSize");
        if (itSize != queryParams.end() && !itSize->second.empty()) {
            pageSize = std::stoi(itSize->second);
        }
        
        int offset = (pageNum - 1) * pageSize;
        
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        finalQuery += " ORDER BY id DESC";
        
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        if (res.empty()) { 
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        std::string countQuery = "SELECT COUNT(*) FROM staff WHERE 1=1";
        for (const auto& cond : conditions) {
            countQuery += " AND " + cond;
        }
        
        std::vector<std::string> countParams;
        for (size_t i = 0; i < params.size() - 2; i++) {
            countParams.push_back(params[i]);
        }
        
        pqxx::result countRes;
        if (countParams.empty()) {
            countRes = txn.exec(countQuery);
        } else {
            countRes = txn.exec_params(countQuery, pqxx::prepare::make_dynamic_params(countParams));
        }
        
        int total = countRes[0][0].as<int>();
        
        crow::json::wvalue::list staffList;
        
        for (const auto& row : res) {
            crow::json::wvalue staff;
            staff["id"] = row["id"].as<int>();
            staff["role"] = row["role"].is_null() ? -1 : row["role"].as<int>();
            staff["username"] = row["username"].is_null() ? "" : row["username"].as<std::string>();
            staff["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            staff["gender"] = row["gender"].is_null() ? -1 : row["gender"].as<int>();
            staff["age"] = row["age"].is_null() ? -1 : row["age"].as<int>();
            staff["birthday"] = row["birthday"].is_null() ? "" : row["birthday"].as<std::string>();
            staff["position"] = row["position"].is_null() ? "" : row["position"].as<std::string>();
            staff["email_address"] = row["email_address"].is_null() ? "" : row["email_address"].as<std::string>();
            staff["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>();
            
            staffList.push_back(std::move(staff));
        }
        
        result["retCode"] = 200;
        
        if (staffList.size() == 1) {
            result["staff"] = std::move(staffList[0]);
        } else {
            result["staff"] = std::move(staffList);
        }
        
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

crow::response getUserListFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    std::string token = req.get_header_value("token");

    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    try {
        pqxx::work txn(conn);
        
        auto decoded = jwt::decode(token);

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded);

        const std::string username = decoded.get_subject();
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, role, username, name, gender, age, birthday, position, email_address, phone_no FROM staff WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        std::unordered_map<std::string, std::string> queryParams;
        
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::vector<std::string> paramKeys = {
            "id", "role", "username", "name", "gender", "age", 
            "birthday", "position", "email_address", "phone_no",
            "age_min", "age_max", "birthday_start", "birthday_end",
            "pageNum", "pageSize"
        };
        
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        struct FilterField {
            std::string paramName;
            std::string dbField;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"role", "role", false},
            {"username", "username", false},
            {"name", "name", true},
            {"gender", "gender", false},
            {"age", "age", false},
            {"birthday", "birthday", false},
            {"position", "position", true},
            {"email_address", "email_address", true},
            {"phone_no", "phone_no", true},
            {"age_min", "age", false},
            {"age_max", "age", false},
            {"birthday_start", "birthday", false},
            {"birthday_end", "birthday", false},
        };
        
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName == "age_min") {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "age_max") {
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_start") {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_end") {
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        int pageNum = 1;
        int pageSize = 20;
        
        auto itPage = queryParams.find("pageNum");
        if (itPage != queryParams.end() && !itPage->second.empty()) {
            pageNum = std::stoi(itPage->second);
        }
        
        auto itSize = queryParams.find("pageSize");
        if (itSize != queryParams.end() && !itSize->second.empty()) {
            pageSize = std::stoi(itSize->second);
        }
        
        int offset = (pageNum - 1) * pageSize;
        
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        finalQuery += " ORDER BY id DESC";
        
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        std::string countQuery = "SELECT COUNT(*) FROM staff WHERE 1=1";
        for (const auto& cond : conditions) {
            countQuery += " AND " + cond;
        }
        
        std::vector<std::string> countParams;
        for (size_t i = 0; i < params.size() - 2; i++) {
            countParams.push_back(params[i]);
        }
        
        pqxx::result countRes;
        if (countParams.empty()) {
            countRes = txn.exec(countQuery);
        } else {
            countRes = txn.exec_params(countQuery, pqxx::prepare::make_dynamic_params(countParams));
        }
        
        int total = countRes[0][0].as<int>();
        
        crow::json::wvalue::list staffList; 
        
        for (const auto& row : res) {
            crow::json::wvalue staff;
            staff["id"] = row["id"].as<int>();
            staff["role"] = row["role"].is_null() ? -1 : row["role"].as<int>();
            staff["username"] = row["username"].is_null() ? "" : row["username"].as<std::string>();
            staff["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            staff["gender"] = row["gender"].is_null() ? -1 : row["gender"].as<int>();
            staff["age"] = row["age"].is_null() ? -1 : row["age"].as<int>();
            staff["birthday"] = row["birthday"].is_null() ? "" : row["birthday"].as<std::string>();
            staff["position"] = row["position"].is_null() ? "" : row["position"].as<std::string>();
            staff["email_address"] = row["email_address"].is_null() ? "" : row["email_address"].as<std::string>();
            staff["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>(); 
            
            staffList.push_back(std::move(staff));
        }
        
        result["retCode"] = 200;
        result["data"] = std::move(staffList);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

crow::response addUserFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);
        
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        // ✅ 检查必需字段：username（用户名）和 name（用户名称）
        if (!body.has("username") || !body.has("name") || !body.has("password") || 
            !body.has("phone_no") || !body.has("email") || !body.has("role")) {
             result["retCode"] = 400;
             result["errorMsg"] = "Missing required fields";
             return crow::response(400, result);
        }

        // ✅ username = 用户名（登录用），name = 用户名称（显示用）
        std::string username = body["username"].s();
        std::string name = body["name"].s();
        
        std::string position = "";
        if (body.has("position")) {
            position = body["position"].s();
        }
        
        std::string email_address = body["email"].s();
        std::string phone_no = body["phone_no"].s();
        std::string password = body["password"].s();

        int role = 0;
        if (body["role"].t() == crow::json::type::Number) {
            role = body["role"].i();
        } else if (body["role"].t() == crow::json::type::String) {
            role = std::stoi(body["role"].s());
        }

        int age = -1;
        if (body.has("age") && body["age"].t() == crow::json::type::Number) {
            age = body["age"].i();
        }
        
        std::string birthday = "";
        if (body.has("birthday") && body["birthday"].t() == crow::json::type::String) {
            birthday = body["birthday"].s();
        }

        pqxx::result res = txn.exec_params(
            "INSERT INTO staff (role, username, name, position, email_address, phone_no, password, age, birthday) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) RETURNING id",
            role, 
            username, 
            name, 
            position.empty() ? nullptr : position.c_str(),
            email_address,
            phone_no,
            password,
            (age == -1 ? nullptr : &age),
            birthday.empty() ? nullptr : birthday.c_str()
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add user";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["msg"] = "Success";
        result["userId"] = res[0]["id"].as<int>();
        
        txn.commit(); 
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add User Error: " << e.what() << std::endl;
        result["retCode"] = 400;
        result["errorMsg"] = "Database error or Constraint violation"; 
        return crow::response(400, result);
    }
}

crow::response updateUserFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);
        
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Missing required field: id";
            return crow::response(400, result);
        }

        int userId = body["id"].i();
        
        // ✅ 字段映射：username 是用户名，name 是用户名称
        std::string username = "";
        if (body.has("username")) {
            username = body["username"].s();
        }
        
        std::string name = "";
        if (body.has("name")) {
            name = body["name"].s();
        }
        
        std::string position = "";
        if (body.has("position")) {
            position = body["position"].s();
        }
        
        std::string email_address = "";
        if (body.has("email")) {
            email_address = body["email"].s();
        }
        
        std::string phone_no = "";
        if (body.has("phone_no")) {
            phone_no = body["phone_no"].s();
        }
        
        int role = -1;
        if (body.has("role")) {
            if (body["role"].t() == crow::json::type::Number) {
                role = body["role"].i();
            } else if (body["role"].t() == crow::json::type::String) {
                role = std::stoi(body["role"].s());
            }
        }
        
        int age = -1;
        if (body.has("age") && body["age"].t() == crow::json::type::Number) {
            age = body["age"].i();
        }
        
        std::string birthday = "";
        if (body.has("birthday") && body["birthday"].t() == crow::json::type::String) {
            birthday = body["birthday"].s();
        }
        
        std::string password = "";
        if (body.has("password")) {
            password = body["password"].s();
        }
        
        std::string updateSql = "UPDATE staff SET ";
        std::vector<std::string> setClauses;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // ✅ username 在修改时不可变更，但如果有传则更新（前端已禁用）
        if (!username.empty()) {
            setClauses.push_back("username = $" + std::to_string(paramCounter++));
            params.push_back(username);
        }
        if (!name.empty()) {
            setClauses.push_back("name = $" + std::to_string(paramCounter++));
            params.push_back(name);
        }
        if (!position.empty()) {
            setClauses.push_back("position = $" + std::to_string(paramCounter++));
            params.push_back(position);
        }
        if (!email_address.empty()) {
            setClauses.push_back("email_address = $" + std::to_string(paramCounter++));
            params.push_back(email_address);
        }
        if (!phone_no.empty()) {
            setClauses.push_back("phone_no = $" + std::to_string(paramCounter++));
            params.push_back(phone_no);
        }
        if (role != -1) {
            setClauses.push_back("role = $" + std::to_string(paramCounter++));
            params.push_back(std::to_string(role));
        }
        if (age != -1) {
            setClauses.push_back("age = $" + std::to_string(paramCounter++));
            params.push_back(std::to_string(age));
        }
        if (!birthday.empty()) {
            setClauses.push_back("birthday = $" + std::to_string(paramCounter++));
            params.push_back(birthday);
        }
        if (!password.empty()) {
            setClauses.push_back("password = $" + std::to_string(paramCounter++));
            params.push_back(password);
        }
        
        if (setClauses.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }
        
        for (size_t i = 0; i < setClauses.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += setClauses[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(userId));
        
        pqxx::result res = txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));
        
        if (res.affected_rows() == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found or no changes made";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["msg"] = "Success";
        result["userId"] = userId;
        
        txn.commit(); 
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update User Error: " << e.what() << std::endl;
        result["retCode"] = 400;
        result["errorMsg"] = "Database error or Constraint violation"; 
        return crow::response(400, result);
    }
}

crow::response deleteUserFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        if (!body.has("ids")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Missing required field: ids";
            return crow::response(400, result);
        }

        std::vector<int> ids;
        auto& ids_array = body["ids"];
        
        for (const auto& id_val : ids_array) {
            ids.push_back(id_val.i());
        }

        if (ids.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "ids array cannot be empty";
            return crow::response(400, result);
        }

        pqxx::work txn(conn);

        int deletedCount = 0;
        for (int id : ids) {
            pqxx::result res = txn.exec_params("DELETE FROM staff WHERE id = $1 RETURNING id", id);
            if (!res.empty()) {
                deletedCount++;
            }
        }

        txn.commit();
        
        if (deletedCount > 0) {
            result["retCode"] = 200;
        } else {
            result["retCode"] = 400;
            result["errorMsg"] = "No users were deleted. IDs may not exist.";
            return crow::response(400, result);
        }
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete User Error: " << e.what() << std::endl;
        
        result["retCode"] = 400;
        result["errorMsg"] = "Database error or Constraint violation"; 
        return crow::response(400, result);
    }
}

// 注册接口到工厂
AUTO_REGISTER_USER_API("getRouter", getRouterFunc);
AUTO_REGISTER_USER_API("login", userLoginFunc);
AUTO_REGISTER_USER_API("forgetPassword", forgetPasswordFunc);
AUTO_REGISTER_USER_API("resetPassword", resetPasswordFunc);
AUTO_REGISTER_USER_API("queryUserInfo", queryUserInfoFunc);
AUTO_REGISTER_USER_API("getUserList", getUserListFunc);
AUTO_REGISTER_USER_API("addUser", addUserFunc);
AUTO_REGISTER_USER_API("updateUser", updateUserFunc);
AUTO_REGISTER_USER_API("deleteUser", deleteUserFunc);