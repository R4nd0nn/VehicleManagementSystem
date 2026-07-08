#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "service/container_management.h"
#include "../common/include/jwt/jwt.h"

// 解析时间字符串
std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp) {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        ss.clear();
        ss.str(timestamp);
        ss >> std::get_time(&tm, "%Y-%m-%d");
    }
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return tp;
}

// 获取当前时间字符串
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    std::ostringstream oss;
    oss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 计算两个时间字符串的天数差
int calculateDaysDiff(const std::string& startStr, const std::string& endStr) {
    auto start = parseTimestamp(startStr);
    auto end = parseTimestamp(endStr);
    auto diff = end - start;
    return std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24;
}

// ==================== 新增集装箱 ====================
crow::response addContainerFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 获取当前用户ID
        int currentUserId = 0;
        try {
            currentUserId = std::stoi(decoded.get_payload_claim("user_id").as_string());
        } catch (...) {
            currentUserId = 0;
        }

        // 必填字段校验
        if (!body.has("container_no")) {
            result["retCode"] = 400;
            result["errorMsg"] = "container_no is required";
            return crow::response(400, result);
        }

        std::string container_no = body["container_no"].s();
        
        // 检查货柜号是否已存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM container WHERE container_no = $1 AND deleted_at IS NULL", container_no);
        if (!checkRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Container number already exists";
            return crow::response(400, result);
        }

        // 处理字段
        std::string status = "空柜";
        if (body.has("status")) {
            status = body["status"].s();
        }
        
        std::string pickup_time = "";
        if (body.has("pickup_time")) {
            pickup_time = body["pickup_time"].s();
        }
        
        std::string return_time = "";
        if (body.has("return_time")) {
            return_time = body["return_time"].s();
        }
        
        int free_days = 7;
        if (body.has("free_days")) {
            free_days = body["free_days"].i();
        }
        
        int free_expired_time = 7;
        if (body.has("free_expired_time")) {
            free_expired_time = body["free_expired_time"].i();
        }
        
        std::string abnormal_status = "";
        if (body.has("abnormal_status")) {
            abnormal_status = body["abnormal_status"].s();
        }
        
        std::string abnormal_desc = "";
        if (body.has("abnormal_desc")) {
            abnormal_desc = body["abnormal_desc"].s();
        }
        
        std::string waybill_no = "";
        if (body.has("waybill_no")) {
            waybill_no = body["waybill_no"].s();
        }
        
        std::string port = "";
        if (body.has("port")) {
            port = body["port"].s();
        }
        
        std::string customer_requirement = "";
        if (body.has("customer_requirement")) {
            customer_requirement = body["customer_requirement"].s();
        }

        // 插入数据库
        pqxx::result res = txn.exec_params(
            "INSERT INTO container ("
            "container_no, status, pickup_time, return_time, free_days, free_expired_time, "
            "abnormal_status, abnormal_desc, waybill_no, port, customer_requirement, "
            "created_by, updated_by, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) RETURNING id",
            container_no.c_str(),
            status.c_str(),
            pickup_time.empty() ? nullptr : pickup_time.c_str(),
            return_time.empty() ? nullptr : return_time.c_str(),
            free_days,
            free_expired_time,
            abnormal_status.empty() ? nullptr : abnormal_status.c_str(),
            abnormal_desc.empty() ? nullptr : abnormal_desc.c_str(),
            waybill_no.empty() ? nullptr : waybill_no.c_str(),
            port.empty() ? nullptr : port.c_str(),
            customer_requirement.empty() ? nullptr : customer_requirement.c_str(),
            currentUserId,
            currentUserId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add container";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["msg"] = "Container added successfully";
        result["id"] = res[0]["id"].as<int>();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Container Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新集装箱 ====================
crow::response updateContainerFunc(const crow::request& req, pqxx::connection& conn) {
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

        int currentUserId = 0;
        try {
            currentUserId = std::stoi(decoded.get_payload_claim("user_id").as_string());
        } catch (...) {
            currentUserId = 0;
        }

        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int containerId = body["id"].i();

        // 检查是否存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM container WHERE id = $1 AND deleted_at IS NULL", containerId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Container not found";
            return crow::response(404, result);
        }

        // 构建动态更新语句
        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        struct UpdateField {
            std::string dbField;
            std::string paramName;
            bool isString;
            bool isTimestamp;  // 新增：标记是否为时间字段
        };

        std::vector<UpdateField> updateableFields = {
            {"container_no", "container_no", true, false},
            {"status", "status", true, false},
            {"pickup_time", "pickup_time", true, true},   // 时间字段
            {"return_time", "return_time", true, true},   // 时间字段
            {"free_days", "free_days", false, false},
            {"free_expired_time", "free_expired_time", false, false},
            {"abnormal_status", "abnormal_status", true, false},
            {"abnormal_desc", "abnormal_desc", true, false},
            {"waybill_no", "waybill_no", true, false},
            {"port", "port", true, false},
            {"customer_requirement", "customer_requirement", true, false}
        };

        for (const auto& field : updateableFields) {
            if (body.has(field.paramName)) {
                if (field.isString) {
                    std::string value = body[field.paramName].s();
                    
                    // 时间字段：空字符串转为 "null"（表示 NULL）
                    if (field.isTimestamp && value.empty()) {
                        updateFields.push_back(field.dbField + " = NULL");
                        // 不需要添加参数
                    } else {
                        updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                        params.push_back(value);
                        paramCounter++;
                    }
                } else {
                    int value = body[field.paramName].i();
                    updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                    params.push_back(std::to_string(value));
                    paramCounter++;
                }
            }
        }

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        updateFields.push_back("updated_by = $" + std::to_string(paramCounter));
        params.push_back(std::to_string(currentUserId));
        paramCounter++;
        
        updateFields.push_back("updated_at = CURRENT_TIMESTAMP");

        std::string updateSql = "UPDATE container SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(containerId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Container updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Container Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 删除集装箱（软删除，仅管理员） ====================
crow::response deleteContainerFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 检查用户是否是管理员
        std::string role = decoded.get_payload_claim("role").as_string();
        if (role != "admin") {
            result["retCode"] = 403;
            result["errorMsg"] = "Only admin can delete containers";
            return crow::response(403, result);
        }

        int currentUserId = 0;
        try {
            currentUserId = std::stoi(decoded.get_payload_claim("user_id").as_string());
        } catch (...) {
            currentUserId = 0;
        }

        // 获取要删除的ID列表
        std::vector<int> ids;
        
        if (body.has("ids")) {
            try {
                for (const auto& item : body["ids"]) {
                    ids.push_back(item.i());
                }
            } catch (...) {
                ids.push_back(body["ids"].i());
            }
        } else if (body.has("id")) {
            ids.push_back(body["id"].i());
        } else {
            result["retCode"] = 400;
            result["errorMsg"] = "id or ids is required";
            return crow::response(400, result);
        }

        if (ids.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No valid ids provided";
            return crow::response(400, result);
        }

        // 软删除
        std::string deleteSql = "UPDATE container SET deleted_at = CURRENT_TIMESTAMP, deleted_by = $1 WHERE id IN (";
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) deleteSql += ", ";
            deleteSql += "$" + std::to_string(i + 2);
        }
        deleteSql += ") AND deleted_at IS NULL";

        std::vector<std::string> params;
        params.push_back(std::to_string(currentUserId));
        for (int id : ids) {
            params.push_back(std::to_string(id));
        }

        pqxx::result res = txn.exec_params(deleteSql, pqxx::prepare::make_dynamic_params(params));
        
        result["retCode"] = 200;
        result["msg"] = "Container(s) deleted successfully";
        result["affected_rows"] = (int)res.affected_rows();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Container Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 查询集装箱列表 ====================
crow::response queryContainersFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = R"(
            SELECT 
                id, 
                container_no, 
                status, 
                pickup_time, 
                return_time, 
                free_days, 
                free_expired_time,
                abnormal_status, 
                abnormal_desc, 
                waybill_no, 
                port, 
                customer_requirement,
                created_at, 
                updated_at
            FROM container 
            WHERE deleted_at IS NULL
        )";
        
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::unordered_map<std::string, std::string> queryParams;
        std::vector<std::string> paramKeys = {
            "id", "container_no", "status", "waybill_no", "port", "abnormal_status"
        };
        
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        // 支持的筛选字段
        struct FilterField {
            std::string paramName;
            std::string dbField;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"container_no", "container_no", true},
            {"status", "status", false},
            {"waybill_no", "waybill_no", true},
            {"port", "port", true},
            {"abnormal_status", "abnormal_status", true}
        };
        
        // 构建动态条件
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else {
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        // 组装完整查询
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        finalQuery += " ORDER BY id DESC";
        
        // 执行查询
        pqxx::result res;
        if (params.empty()) {
            res = txn.exec(finalQuery);
        } else {
            res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        }
        
        // 构建返回数据
        crow::json::wvalue::list containerList;
        
        for (const auto& row : res) {
            crow::json::wvalue container;
            container["id"] = row["id"].as<int>();
            container["container_no"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
            container["status"] = row["status"].is_null() ? "" : row["status"].as<std::string>();
            container["pickup_time"] = row["pickup_time"].is_null() ? "" : row["pickup_time"].as<std::string>();
            container["return_time"] = row["return_time"].is_null() ? "" : row["return_time"].as<std::string>();
            container["free_days"] = row["free_days"].is_null() ? 7 : row["free_days"].as<int>();
            container["free_expired_time"] = row["free_expired_time"].is_null() ? 7 : row["free_expired_time"].as<int>();
            container["abnormal_status"] = row["abnormal_status"].is_null() ? "" : row["abnormal_status"].as<std::string>();
            container["abnormal_desc"] = row["abnormal_desc"].is_null() ? "" : row["abnormal_desc"].as<std::string>();
            container["waybill_no"] = row["waybill_no"].is_null() ? "" : row["waybill_no"].as<std::string>();
            container["port"] = row["port"].is_null() ? "" : row["port"].as<std::string>();
            container["customer_requirement"] = row["customer_requirement"].is_null() ? "" : row["customer_requirement"].as<std::string>();
            container["created_at"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            container["updated_at"] = row["updated_at"].is_null() ? "" : row["updated_at"].as<std::string>();
            
            // 计算已用天数
            int usedDays = 0;
            if (!row["pickup_time"].is_null()) {
                std::string pickupTimeStr = row["pickup_time"].as<std::string>();
                
                if (!row["return_time"].is_null()) {
                    std::string returnTimeStr = row["return_time"].as<std::string>();
                    usedDays = calculateDaysDiff(pickupTimeStr, returnTimeStr);
                } else {
                    usedDays = calculateDaysDiff(pickupTimeStr, getCurrentTimeString());
                }
            }
            container["used_days"] = usedDays;
            
            containerList.push_back(std::move(container));
        }
        
        result["retCode"] = 200;
        result["data"] = std::move(containerList);
        result["total"] = (int)containerList.size();
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

AUTO_REGISTER_CONTAINER_API("addContainer", addContainerFunc);
AUTO_REGISTER_CONTAINER_API("deleteContainer", deleteContainerFunc);
AUTO_REGISTER_CONTAINER_API("updateContainer", updateContainerFunc);
AUTO_REGISTER_CONTAINER_API("queryContainers", queryContainersFunc);