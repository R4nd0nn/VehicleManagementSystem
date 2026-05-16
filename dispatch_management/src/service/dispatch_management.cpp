#include "service/dispatch_management.h"
#include "../common/include/jwt/jwt.h"

crow::response queryDriverFunc(const crow::request& req, pqxx::connection& conn) {
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
        std::string baseQuery = "SELECT id, client_id, name, phone_no, email, driver_license, license_type, class_of_vehicle, license_issue_date, license_expire_date, license_issue_state, status FROM driver WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数 - 使用 Crow 的 url_params 方法
        std::unordered_map<std::string, std::string> queryParams;
        
        // 获取单个参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        // 定义所有支持的参数
        std::vector<std::string> paramKeys = {
            "id", "client_id", "name", "phone_no", "email", "driver_license",
            "license_type", "class_of_vehicle", "license_issue_state", "status",
            "license_issue_date_start", "license_issue_date_end",
            "license_expire_date_start", "license_expire_date_end",
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
            {"client_id", "client_id", false},
            {"name", "name", true},
            {"phone_no", "phone_no", false},
            {"email", "email", true},
            {"driver_license", "driver_license", true},
            {"license_type", "license_type", false},
            {"class_of_vehicle", "class_of_vehicle", false},
            {"license_issue_state", "license_issue_state", false},
            {"status", "status", false},
            {"license_issue_date_start", "license_issue_date", false},
            {"license_issue_date_end", "license_issue_date", false},
            {"license_expire_date_start", "license_expire_date", false},
            {"license_expire_date_end", "license_expire_date", false},
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
                } else if (filter.paramName.find("_start") != std::string::npos) {
                    // 日期范围查询 - 起始
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_end") != std::string::npos) {
                    // 日期范围查询 - 结束
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
        std::string countQuery = "SELECT COUNT(*) FROM driver WHERE 1=1";
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
        
        // 构建返回数据
        crow::json::wvalue::list driverList; 
        
        for (const auto& row : res) {
            crow::json::wvalue driver;
            driver["id"] = row["id"].as<int>();
            driver["client_id"] = row["client_id"].is_null() ? -1 : row["client_id"].as<int>();
            driver["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            driver["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>();
            driver["email"] = row["email"].is_null() ? "" : row["email"].as<std::string>();
            driver["driver_license"] = row["driver_license"].is_null() ? "" : row["driver_license"].as<std::string>();
            driver["license_type"] = row["license_type"].is_null() ? "" : row["license_type"].as<std::string>();
            driver["class_of_vehicle"] = row["class_of_vehicle"].is_null() ? "" : row["class_of_vehicle"].as<std::string>();
            driver["license_issue_date"] = row["license_issue_date"].is_null() ? "" : row["license_issue_date"].as<std::string>();
            driver["license_expire_date"] = row["license_expire_date"].is_null() ? "" : row["license_expire_date"].as<std::string>(); 
            driver["license_issue_state"] = row["license_issue_state"].is_null() ? "" : row["license_issue_state"].as<std::string>();
            driver["status"] = row["status"].is_null() ? -1 : row["status"].as<int>();
            
            driverList.push_back(std::move(driver));
        }
        
        // 设置返回结果
        result["retCode"] = 200;
        result["data"] = std::move(driverList);
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

crow::response queryVehicleFunc(const crow::request& req, pqxx::connection& conn) {
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
        std::string baseQuery = "SELECT id, license_plate, type, status, gps_id, kilometres, insurance_expire_date FROM vehicle WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数 - 使用 Crow 的 url_params 方法
        std::unordered_map<std::string, std::string> queryParams;
        
        // 获取单个参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        // 定义所有支持的参数
        std::vector<std::string> paramKeys = {
            "id", "license_plate", "type", "status", "gps_id",
            "kilometres_min", "kilometres_max",
            "insurance_expire_date_start", "insurance_expire_date_end",
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
            {"license_plate", "license_plate", true},
            {"type", "type", false},
            {"status", "status", false},
            {"gps_id", "gps_id", false},
            {"kilometres_min", "kilometres", false},
            {"kilometres_max", "kilometres", false},
            {"insurance_expire_date_start", "insurance_expire_date", false},
            {"insurance_expire_date_end", "insurance_expire_date", false},
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
                } else if (filter.paramName == "kilometres_min") {
                    // 公里数范围查询 - 最小值
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "kilometres_max") {
                    // 公里数范围查询 - 最大值
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_start") != std::string::npos) {
                    // 日期范围查询 - 起始
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_end") != std::string::npos) {
                    // 日期范围查询 - 结束
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
        std::string countQuery = "SELECT COUNT(*) FROM vehicle WHERE 1=1";
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
        
        // 构建返回数据
        crow::json::wvalue::list vehicleList; 
        
        for (const auto& row : res) {
            crow::json::wvalue vehicle;
            vehicle["id"] = row["id"].as<int>();
            vehicle["license_plate"] = row["license_plate"].is_null() ? "" : row["license_plate"].as<std::string>();
            vehicle["type"] = row["type"].is_null() ? "" : row["type"].as<std::string>();
            vehicle["status"] = row["status"].is_null() ? -1 : row["status"].as<int>();
            vehicle["gps_id"] = row["gps_id"].is_null() ? "" : row["gps_id"].as<std::string>();
            vehicle["kilometres"] = row["kilometres"].is_null() ? -1 : row["kilometres"].as<int>();
            vehicle["insurance_expire_date"] = row["insurance_expire_date"].is_null() ? "" : row["insurance_expire_date"].as<std::string>();
            vehicleList.push_back(std::move(vehicle));
        }
        
        // 设置返回结果
        result["retCode"] = 200;
        result["data"] = std::move(vehicleList);
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

crow::response batchDispatchFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // Token 校验
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    try {
        pqxx::work txn(conn);
        
        // JWT 验证
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);
        
        // 解析请求体
        auto body = crow::json::load(req.body);
        if (!body) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid JSON body";
            return crow::response(400, result);
        }
        
        auto tasks_array =  body;
        
        for (const auto& task_data : tasks_array) {
            // 获取前端数据
            std::string orderId = task_data["orderId"].s();
            std::string selectedVehicle = task_data["selectedVehicle"].s();
            int selectedDriver = task_data["selectedDriver"].i();
            
            if (orderId.empty()) {
                result["retCode"] = 400;
                result["errorMsg"] = "orderId is required";
                return crow::response(400, result);
            }
            
            // 方法1: 如果前端传的是 "ORD-3" 格式
            size_t dashPos = orderId.find('-');
            orderId = orderId.substr(dashPos + 1);

            // ========== 1. 从 orderId 找到 orders 表中的 id ==========
            // 注意：orderId 是字符串如 "ORD-3"，需要根据你的实际字段名查询
            // 假设 orders 表有一个 order_no 字段存储订单号
            pqxx::result order_res = txn.exec_params(
                "SELECT id, container_no FROM orders WHERE id = $1",
                std::stoi(orderId)
            );
            
            if (order_res.empty()) {
                result["retCode"] = 400;
                result["errorMsg"] = "Order not found: " + orderId;
                return crow::response(400, result);
            }
            
            int order_id = order_res[0]["id"].as<int>();
            std::string container_no = order_res[0]["container_no"].is_null() ? "" : order_res[0]["container_no"].as<std::string>();
            
            // ========== 2. 从 container_no 找到 container 表中的 id ==========
            int container_id = -1;
            if (!container_no.empty()) {
                pqxx::result container_res = txn.exec_params(
                    "SELECT id FROM container WHERE container_no = $1",
                    container_no
                );
                if (!container_res.empty()) {
                    container_id = container_res[0]["id"].as<int>();
                }
            }
            
            // ========== 3. 从 selectedVehicle 找到 vehicle 表中的 id ==========
            int vehicle_id = -1;
            if (!selectedVehicle.empty()) {
                pqxx::result vehicle_res = txn.exec_params(
                    "SELECT id FROM vehicle WHERE license_plate = $1",
                    selectedVehicle
                );
                if (!vehicle_res.empty()) {
                    vehicle_id = vehicle_res[0]["id"].as<int>();
                } else {
                    result["retCode"] = 404;
                    result["errorMsg"] = "Vehicle not found: " + selectedVehicle;
                    return crow::response(404, result);
                }
            }
            
            // ========== 4. task status 初始设为1 ==========
            int task_status = 1;
            
            auto now = std::chrono::system_clock::now();
            auto now_time = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&now_time), "%H:%M:%S");
            std::string task_start_time = ss.str();

            // ========== 6. 插入 task 表 ==========
            pqxx::result insert_res = txn.exec_params(
                "INSERT INTO task (order_id, driver_id, vehicle_id, container_id, task_status, task_start_time, emergency_status) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                order_id,
                selectedDriver,
                vehicle_id,
                container_id,
                task_status,
                task_start_time,
                0
            );
    
        }
        
        txn.commit();
        
        // 返回成功结果
        result["retCode"] = 200;
        
    } catch (const std::exception& e) {
        std::cerr << "Create Task Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
    
    return crow::response(200, result);
}

AUTO_REGISTER_DISPATCH_API("queryDriver", queryDriverFunc);
AUTO_REGISTER_DISPATCH_API("queryVehicle", queryVehicleFunc);
AUTO_REGISTER_DISPATCH_API("batchDispatch", batchDispatchFunc);