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
        
        // 2. 查询所有启用的菜单和按钮
        pqxx::work txn(conn);
        pqxx::result res = txn.exec(
            "SELECT DISTINCT "
            "menu_id, parent_id, menu_name, path, component, query, "
            "route_name, visible, status, COALESCE(perms, '') as perms, "
            "menu_type, icon, order_num "
            "FROM menu "
            "WHERE menu_type IN ('M', 'C') AND status = '0' "
            "ORDER BY parent_id, order_num"
        );
        
        // 3. 转换为Menu结构体列表
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
        
        // 4. 构建路由树
        crow::json::wvalue routers = MenuRouterBuilder::buildMenus(menus);
        
        // 5. 返回结果（修改字段名以匹配前端期望）
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
            "SELECT name FROM staff WHERE name = $1 AND phone_no = $2",
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
            "UPDATE staff SET password = $1 WHERE name = $2 RETURNING name",
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

        // 2. 验证 Token 的签名和有效期
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded); // 验证失败会抛出异常

        // 3. ✅ 验证通过，从 "sub" 字段（Subject）中取出用户名
        const std::string username = decoded.get_subject();
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, role, name, gender, age, birthday, position, email_address, phone_no FROM staff WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数
        std::unordered_map<std::string, std::string> queryParams;
        
        // 获取单个参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        // 定义所有支持的参数（包括 username）
        std::vector<std::string> paramKeys = {
            "id", "role", "name", "gender", "age", 
            "birthday", "position", "email_address", "phone_no", "username",
            "age_min", "age_max", "birthday_start", "birthday_end",
            "pageNum", "pageSize"
        };
        
        // 获取参数值
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        // 默认添加 username 条件（从 token 中获取）
        if (!username.empty()) {
            queryParams["username"] = username;
        }
        
        // ========== 支持的所有筛选字段 ==========
        struct FilterField {
            std::string paramName;   // 请求参数名
            std::string dbField;     // 数据库字段名
            bool isLike;             // 是否是模糊查询
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"role", "role", false},
            {"name", "name", true},
            {"gender", "gender", false},
            {"age", "age", false},
            {"birthday", "birthday", false},
            {"position", "position", true},
            {"email_address", "email_address", true},
            {"phone_no", "phone_no", true},
            {"username", "username", false},
            {"age_min", "age", false},
            {"age_max", "age", false},
            {"birthday_start", "birthday", false},
            {"birthday_end", "birthday", false},
        };
        
        // 构建动态条件
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    // 模糊查询
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName == "age_min") {
                    // 年龄范围查询 - 最小值
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "age_max") {
                    // 年龄范围查询 - 最大值
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_start") {
                    // 生日范围查询 - 起始
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_end") {
                    // 生日范围查询 - 结束
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    // 精确查询
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        // 添加分页支持
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
        
        // 组装完整查询
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        // 添加排序
        finalQuery += " ORDER BY id DESC";
        
        // 添加分页
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        // 执行查询
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        if (res.empty()) { 
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        // 查询总数（不带分页）
        std::string countQuery = "SELECT COUNT(*) FROM staff WHERE 1=1";
        for (const auto& cond : conditions) {
            countQuery += " AND " + cond;
        }
        
        // 准备总数查询的参数（去掉分页的两个参数）
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
        
        // 构建返回的JSON数组
        crow::json::wvalue::list staffList;
        
        for (const auto& row : res) {
            crow::json::wvalue staff;
            staff["id"] = row["id"].as<int>();
            staff["role"] = row["role"].is_null() ? -1 : row["role"].as<int>();
            staff["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            staff["gender"] = row["gender"].is_null() ? -1 : row["gender"].as<int>();
            staff["age"] = row["age"].is_null() ? -1 : row["age"].as<int>();
            staff["birthday"] = row["birthday"].is_null() ? "" : row["birthday"].as<std::string>();
            staff["position"] = row["position"].is_null() ? "" : row["position"].as<std::string>();
            staff["email_address"] = row["email_address"].is_null() ? "" : row["email_address"].as<std::string>();
            staff["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>();
            
            staffList.push_back(std::move(staff));
        }
        
        // 设置返回结果
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

        // 2. 验证 Token 的签名和有效期
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded); // 验证失败会抛出异常

        // 3. ✅ 验证通过，从 "sub" 字段（Subject）中取出用户名
        const std::string username = decoded.get_subject();
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, role, name, gender, age, birthday, position, email_address, phone_no FROM staff WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数
        std::unordered_map<std::string, std::string> queryParams;
        
        // 获取单个参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        // 定义所有支持的参数
        std::vector<std::string> paramKeys = {
            "id", "role", "name", "gender", "age", 
            "birthday", "position", "email_address", "phone_no",
            "age_min", "age_max", "birthday_start", "birthday_end",
            "pageNum", "pageSize"
        };
        
        // 获取参数值
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        // ========== 支持的所有筛选字段 ==========
        struct FilterField {
            std::string paramName;   // 请求参数名
            std::string dbField;     // 数据库字段名
            bool isLike;             // 是否是模糊查询
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"role", "role", false},
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
        
        // 构建动态条件
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    // 模糊查询
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName == "age_min") {
                    // 年龄范围查询 - 最小值
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "age_max") {
                    // 年龄范围查询 - 最大值
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_start") {
                    // 生日范围查询 - 起始
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "birthday_end") {
                    // 生日范围查询 - 结束
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    // 精确查询
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        // 添加分页支持
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
        
        // 组装完整查询
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        // 添加排序
        finalQuery += " ORDER BY id DESC";
        
        // 添加分页
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        // 执行查询
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        // 查询总数（不带分页）
        std::string countQuery = "SELECT COUNT(*) FROM staff WHERE 1=1";
        for (const auto& cond : conditions) {
            countQuery += " AND " + cond;
        }
        
        // 准备总数查询的参数（去掉分页的两个参数）
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
        
        // 构建返回的JSON数组
        crow::json::wvalue::list staffList; 
        
        for (const auto& row : res) {
            crow::json::wvalue staff;
            staff["id"] = row["id"].as<int>();
            staff["role"] = row["role"].is_null() ? -1 : row["role"].as<int>();
            staff["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            staff["gender"] = row["gender"].is_null() ? -1 : row["gender"].as<int>();
            staff["age"] = row["age"].is_null() ? -1 : row["age"].as<int>();
            staff["birthday"] = row["birthday"].is_null() ? "" : row["birthday"].as<std::string>();
            staff["position"] = row["position"].is_null() ? "" : row["position"].as<std::string>();
            staff["email_address"] = row["email_address"].is_null() ? "" : row["email_address"].as<std::string>();
            staff["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>(); 
            
            staffList.push_back(std::move(staff));
        }
        
        // 5. 设置返回结果
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

        // 检查必需字段
        if (!body.has("name") || !body.has("nickName") || !body.has("password") || 
            !body.has("phone_no") || !body.has("email") || !body.has("role")) {
             result["retCode"] = 400;
             result["errorMsg"] = "Missing required fields";
             return crow::response(400, result);
        }

        std::string username = body["name"].s();
        std::string name = body["nickName"].s();
        
        // ✅ 修复：分开处理 position 字段
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

        // 处理 age 字段
        int age = -1;
        if (body.has("age") && body["age"].t() == crow::json::type::Number) {
            age = body["age"].i();
        }
        
        // 处理 birthday 字段
        std::string birthday = "";
        if (body.has("birthday") && body["birthday"].t() == crow::json::type::String) {
            birthday = body["birthday"].s();
        }

        // 插入数据
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

        // 检查必需字段（id 必须存在）
        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Missing required field: id";
            return crow::response(400, result);
        }

        int userId = body["id"].i();
        
        // 获取可选字段
        std::string username = "";
        if (body.has("name")) {
            username = body["name"].s();
        }
        
        std::string name = "";
        if (body.has("nickName")) {
            name = body["nickName"].s();
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
        
        // 处理 role 字段
        int role = -1;
        if (body.has("role")) {
            if (body["role"].t() == crow::json::type::Number) {
                role = body["role"].i();
            } else if (body["role"].t() == crow::json::type::String) {
                role = std::stoi(body["role"].s());
            }
        }
        
        // 处理 age 字段
        int age = -1;
        if (body.has("age") && body["age"].t() == crow::json::type::Number) {
            age = body["age"].i();
        }
        
        // 处理 birthday 字段
        std::string birthday = "";
        if (body.has("birthday") && body["birthday"].t() == crow::json::type::String) {
            birthday = body["birthday"].s();
        }
        
        // 处理 password 字段（可选，如果提供则更新）
        std::string password = "";
        if (body.has("password")) {
            password = body["password"].s();
        }
        
        // 动态构建 UPDATE 语句
        std::string updateSql = "UPDATE staff SET ";
        std::vector<std::string> setClauses;
        std::vector<std::string> params;
        int paramCounter = 1;
        
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
        
        // 组装 SQL
        for (size_t i = 0; i < setClauses.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += setClauses[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(userId));
        
        // 执行更新
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
        // JWT 验证
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        // 检查是否有 ids 字段
        if (!body.has("ids")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Missing required field: ids";
            return crow::response(400, result);
        }

        // 获取 ID 列表
        std::vector<int> ids;
        auto& ids_array = body["ids"];
        
        // 提取所有 ID
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
        // ✅ 打印具体错误，方便调试
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