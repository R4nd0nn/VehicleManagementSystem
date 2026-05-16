#include "service/order_management.h"
#include "../common/include/jwt/jwt.h"

crow::response addOrderFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    // Token 校验
    std::string token = req.get_header_value("token");
    if (token == "") {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);

        // JWT 验证
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        // ===================== 数字类型：没有就用 0 =====================
        int type = 0;
        if (body.has("type")) {
            type = std::stoi(body["type"].s());
        }

        int weight = 0;
        if (body.has("weight")) {
            weight = std::stoi(body["weight"].s());
        }

        int invoice_id = 0;
        if (body.has("invoice")) {
            invoice_id = std::stoi(body["invoice"].s());
        }

        // ===================== 字符串类型：没有就用 "" =====================
        std::string start_point = "";
        if (body.has("from")) {
            start_point = body["from"].s();
        }

        std::string end_point = "";
        if (body.has("to")) {
            end_point = body["to"].s();
        }

        std::string size = "";
        if (body.has("size")) {
            size = body["size"].s();
        }

        std::string container_no = "";
        if (body.has("containerNo")) {
            container_no = body["containerNo"].s();
        }

        std::string pin = "";
        if (body.has("pin")) {
            pin = body["pin"].s();
        }

        std::string customer_note = "";
        if (body.has("customerRequest")) {
            customer_note = body["customerRequest"].s();
        }

        std::string vessel = "";
        if (body.has("vessel")) {
            vessel = body["vessel"].s();
        }

        std::string shipping_line = "";
        if (body.has("shippingLine")) {
            shipping_line = body["shippingLine"].s();
        }

        std::string client_name = "";
        if (body.has("clientName")) {
            client_name = body["clientName"].s();
        }

        std::string customer_address = "";
        if (body.has("customerAddress")) {
            customer_address = body["customerAddress"].s();
        }

        std::string forwarder = "";
        if (body.has("forwarder")) {
            forwarder = body["forwarder"].s();
        }

        std::string noted = "";
        if (body.has("noted")) {
            noted = body["noted"].s();
        }

        // ===================== 日期类型：没有就用 0001-01-01 =====================
        std::string eta = "0001-01-01";
        if (body.has("eta")) {
            eta = body["eta"].s();
        }

        std::string first_available = "0001-01-01";
        if (body.has("firstAvailable")) {
            first_available = body["firstAvailable"].s();
        }

        std::string last_free_date = "0001-01-01";
        if (body.has("lastFreeDate")) {
            last_free_date = body["lastFreeDate"].s();
        }

        // 默认字段
        int status = 0;
        int process_client_id = 0;

        // 插入数据库
        txn.exec_params(
            "INSERT INTO orders ("
            "type, start_point, end_point, size, container_no, pin, customer_note, "
            "vessel, shipping_line, eta, first_available, last_free_date, "
            "client_name, customer_address, forwarder, weight, invoice_id, noted, "
            "status, process_client_id"
            ") VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20)",
            type, start_point, end_point, size, container_no, pin, customer_note,
            vessel, shipping_line, eta, first_available, last_free_date,
            client_name, customer_address, forwarder, weight, invoice_id, noted,
            status, process_client_id
        );

        result["retCode"] = 200;
        txn.commit();

    } catch (const std::exception& e) {
        std::cerr << "Add Order Error: " << e.what() << std::endl;
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }

    return crow::response(200, result);
}

crow::response queryOrdersFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    // Token 校验
    std::string token = req.get_header_value("token");
    if (token == "") {
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

        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, type, start_point, end_point, size, container_no, pin, customer_note, vessel, shipping_line, eta, first_available, last_free_date, client_name, customer_address, forwarder, weight, invoice_id, noted FROM orders WHERE 1=1";
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
            "id", "type", "from", "to", "size", "containerNo", "pin", 
            "customerRequest", "vessel", "shippingLine", "eta", 
            "firstAvailable", "lastFreeDate", "clientName", 
            "customerAddress", "forwarder", "weight", "invoice", "noted",
            "eta_start", "eta_end", "first_available_start", "first_available_end",
            "last_free_date_start", "last_free_date_end",
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
            {"type", "type", true},
            {"from", "start_point", true},
            {"to", "end_point", true},
            {"size", "size", false},
            {"containerNo", "container_no", true},
            {"pin", "pin", true},
            {"customerRequest", "customer_note", true},
            {"vessel", "vessel", true},
            {"shippingLine", "shipping_line", true},
            {"eta", "eta", false},
            {"firstAvailable", "first_available", false},
            {"lastFreeDate", "last_free_date", false},
            {"clientName", "client_name", true},
            {"customerAddress", "customer_address", true},
            {"forwarder", "forwarder", true},
            {"weight", "weight", false},
            {"invoice", "invoice_id", true},
            {"noted", "noted", true},
            {"eta_start", "eta", false},
            {"eta_end", "eta", false},
            {"first_available_start", "first_available", false},
            {"first_available_end", "first_available", false},
            {"last_free_date_start", "last_free_date", false},
            {"last_free_date_end", "last_free_date", false},
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
        std::string countQuery = "SELECT COUNT(*) FROM orders WHERE 1=1";
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
        crow::json::wvalue::list order_list;
        
        for (const auto& row : res) {
            crow::json::wvalue order;
            order["id"] = row["id"].as<int>();
            order["type"] = row["type"].is_null() ? "" : row["type"].c_str();
            order["from"] = row["start_point"].is_null() ? "" : row["start_point"].c_str();
            order["to"] = row["end_point"].is_null() ? "" : row["end_point"].c_str();
            order["size"] = row["size"].is_null() ? "" : row["size"].c_str();
            order["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].c_str();
            order["pin"] = row["pin"].is_null() ? "" : row["pin"].c_str();
            order["customerRequest"] = row["customer_note"].is_null() ? "" : row["customer_note"].c_str();
            order["vessel"] = row["vessel"].is_null() ? "" : row["vessel"].c_str();
            order["shippingLine"] = row["shipping_line"].is_null() ? "" : row["shipping_line"].c_str();
            order["eta"] = row["eta"].is_null() ? "" : row["eta"].c_str();
            order["firstAvailable"] = row["first_available"].is_null() ? "" : row["first_available"].c_str();
            order["lastFreeDate"] = row["last_free_date"].is_null() ? "" : row["last_free_date"].c_str();
            order["clientName"] = row["client_name"].is_null() ? "" : row["client_name"].c_str();
            order["customerAddress"] = row["customer_address"].is_null() ? "" : row["customer_address"].c_str();
            order["forwarder"] = row["forwarder"].is_null() ? "" : row["forwarder"].c_str();
            order["weight"] = row["weight"].is_null() ? "" : row["weight"].c_str();
            order["invoice"] = row["invoice_id"].is_null() ? "" : row["invoice_id"].c_str();
            order["noted"] = row["noted"].is_null() ? "" : row["noted"].c_str();
            
            order_list.push_back(std::move(order));
        }

        txn.commit();

        // 返回数据
        result["retCode"] = 200;
        result["rows"] = std::move(order_list);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

    } catch (const std::exception& e) {
        std::cerr << "Query Orders Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }

    return crow::response(200, result);
}

AUTO_REGISTER_ORDER_API("addOrder", addOrderFunc);
AUTO_REGISTER_ORDER_API("queryOrders", queryOrdersFunc);