#include "service/dispatch_management.h"
#include "../common/include/jwt/jwt.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// ==================== 工具函数 ====================

// 获取当前时间字符串
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    std::ostringstream oss;
    oss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 获取当前用户ID和名称
std::pair<int, std::string> getCurrentUser(pqxx::work& txn, const std::string& username) {
    pqxx::result staffRes = txn.exec_params(
        "SELECT id, name FROM staff WHERE username = $1", username);
    if (staffRes.empty()) {
        return {0, ""};
    }
    return {staffRes[0]["id"].as<int>(), staffRes[0]["name"].as<std::string>()};
}

// 记录操作日志
void addYardLog(pqxx::work& txn, int slotId, const std::string& slotName, 
                const std::string& areaType, const std::string& actionType,
                const std::string& containerNo, const std::string& orderId,
                const std::string& driverName, const std::string& remark,
                int operatorId, const std::string& operatorName) {
    txn.exec_params(
        "INSERT INTO yard_log ("
        "slot_id, slot_name, area_type, action_type, container_no, "
        "order_id, driver_name, remark, operator_id, operator_name, created_at"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, CURRENT_TIMESTAMP)",
        slotId,
        slotName.c_str(),
        areaType.c_str(),
        actionType.c_str(),
        containerNo.empty() ? nullptr : containerNo.c_str(),
        orderId.empty() ? nullptr : orderId.c_str(),
        driverName.empty() ? nullptr : driverName.c_str(),
        remark.empty() ? nullptr : remark.c_str(),
        operatorId,
        operatorName.c_str()
    );
}

// ==================== 新增司机 ====================
crow::response addDriverFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 必填字段校验
        if (!body.has("name")) {
            result["retCode"] = 400;
            result["errorMsg"] = "name is required";
            return crow::response(400, result);
        }

        std::string name = body["name"].s();
        
        // 可选字段
        int client_id = 0;
        if (body.has("client_id")) {
            client_id = body["client_id"].i();
        }
        
        std::string phone_no = "";
        if (body.has("phone_no")) {
            phone_no = body["phone_no"].s();
        }
        
        std::string email = "";
        if (body.has("email")) {
            email = body["email"].s();
        }
        
        std::string driver_license = "";
        if (body.has("driver_license")) {
            driver_license = body["driver_license"].s();
        }
        
        std::string license_type = "";
        if (body.has("license_type")) {
            license_type = body["license_type"].s();
        }
        
        std::string license_expire_date = "";
        if (body.has("license_expire_date")) {
            license_expire_date = body["license_expire_date"].s();
        }
        
        int status = 1;
        if (body.has("status")) {
            status = body["status"].i();
        }

        // MSIC 和 DG 字段
        std::string msic_card_no = "";
        if (body.has("msic_card_no")) {
            msic_card_no = body["msic_card_no"].s();
        }
        
        std::string msic_card_expire_date = "";
        if (body.has("msic_card_expire_date")) {
            msic_card_expire_date = body["msic_card_expire_date"].s();
        }
        
        std::string dg_license_no = "";
        if (body.has("dg_license_no")) {
            dg_license_no = body["dg_license_no"].s();
        }
        
        std::string dg_license_expire_date = "";
        if (body.has("dg_license_expire_date")) {
            dg_license_expire_date = body["dg_license_expire_date"].s();
        }

        pqxx::result res = txn.exec_params(
            "INSERT INTO driver ("
            "client_id, name, phone_no, email, driver_license, "
            "license_type, license_expire_date, status, "
            "msic_card_no, msic_card_expire_date, dg_license_no, dg_license_expire_date"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12) RETURNING id",
            client_id,
            name.c_str(),
            phone_no.empty() ? nullptr : phone_no.c_str(),
            email.empty() ? nullptr : email.c_str(),
            driver_license.empty() ? nullptr : driver_license.c_str(),
            license_type.empty() ? nullptr : license_type.c_str(),
            license_expire_date.empty() ? nullptr : license_expire_date.c_str(),
            status,
            msic_card_no.empty() ? nullptr : msic_card_no.c_str(),
            msic_card_expire_date.empty() ? nullptr : msic_card_expire_date.c_str(),
            dg_license_no.empty() ? nullptr : dg_license_no.c_str(),
            dg_license_expire_date.empty() ? nullptr : dg_license_expire_date.c_str()
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add driver";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["msg"] = "Driver added successfully";
        result["id"] = res[0]["id"].as<int>();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Driver Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新司机 ====================
crow::response updateDriverFunc(const crow::request& req, pqxx::connection& conn) {
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
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int driverId = body["id"].i();

        // 检查是否存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM driver WHERE id = $1", driverId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Driver not found";
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
        };

        std::vector<UpdateField> updateableFields = {
            {"client_id", "client_id", false},
            {"name", "name", true},
            {"phone_no", "phone_no", true},
            {"email", "email", true},
            {"driver_license", "driver_license", true},
            {"license_type", "license_type", true},
            {"license_expire_date", "license_expire_date", true},
            {"status", "status", false},
            {"msic_card_no", "msic_card_no", true},
            {"msic_card_expire_date", "msic_card_expire_date", true},
            {"dg_license_no", "dg_license_no", true},
            {"dg_license_expire_date", "dg_license_expire_date", true}
        };

        for (const auto& field : updateableFields) {
            if (body.has(field.paramName)) {
                if (field.isString) {
                    std::string value = body[field.paramName].s();
                    updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                    params.push_back(value);
                } else {
                    int value = body[field.paramName].i();
                    updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                    params.push_back(std::to_string(value));
                }
                paramCounter++;
            }
        }

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        std::string updateSql = "UPDATE driver SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(driverId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Driver updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Driver Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 删除司机 ====================
crow::response deleteDriverFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string deleteSql = "DELETE FROM driver WHERE id IN (";
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) deleteSql += ", ";
            deleteSql += "$" + std::to_string(i + 1);
        }
        deleteSql += ")";

        std::vector<std::string> params;
        for (int id : ids) {
            params.push_back(std::to_string(id));
        }

        pqxx::result res = txn.exec_params(deleteSql, pqxx::prepare::make_dynamic_params(params));
        
        result["retCode"] = 200;
        result["msg"] = "Driver(s) deleted successfully";
        result["affected_rows"] = (int)res.affected_rows();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Driver Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

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

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded);

        const std::string username = decoded.get_subject();
        
        // ========== 动态构建查询条件 ==========
        std::string baseQuery = "SELECT id, client_id, name, phone_no, email, driver_license, "
                                "license_type, license_expire_date, status, "
                                "msic_card_no, msic_card_expire_date, dg_license_no, dg_license_expire_date "
                                "FROM driver WHERE 1=1";
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
            "id", "client_id", "name", "phone_no", "email", "driver_license",
            "license_type", "status",
            "license_expire_date_start", "license_expire_date_end",
            "msic_card_no", "dg_license_no",
            "msic_card_expire_date_start", "msic_card_expire_date_end",
            "dg_license_expire_date_start", "dg_license_expire_date_end",
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
            std::string paramName;
            std::string dbField;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"client_id", "client_id", false},
            {"name", "name", true},
            {"phone_no", "phone_no", false},
            {"email", "email", true},
            {"driver_license", "driver_license", true},
            {"license_type", "license_type", false},
            {"status", "status", false},
            {"license_expire_date_start", "license_expire_date", false},
            {"license_expire_date_end", "license_expire_date", false},
            {"msic_card_no", "msic_card_no", false},
            {"dg_license_no", "dg_license_no", false},
            {"msic_card_expire_date_start", "msic_card_expire_date", false},
            {"msic_card_expire_date_end", "msic_card_expire_date", false},
            {"dg_license_expire_date_start", "dg_license_expire_date", false},
            {"dg_license_expire_date_end", "dg_license_expire_date", false}
        };
        
        // 构建动态条件
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName.find("_start") != std::string::npos) {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_end") != std::string::npos) {
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
        
        finalQuery += " ORDER BY id DESC";
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        // 执行查询
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        // 查询总数
        std::string countQuery = "SELECT COUNT(*) FROM driver WHERE 1=1";
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
            driver["license_expire_date"] = row["license_expire_date"].is_null() ? "" : row["license_expire_date"].as<std::string>(); 
            driver["status"] = row["status"].is_null() ? -1 : row["status"].as<int>();
            
            // MSIC 和 DG 字段
            driver["msic_card_no"] = row["msic_card_no"].is_null() ? "" : row["msic_card_no"].as<std::string>();
            driver["msic_card_expire_date"] = row["msic_card_expire_date"].is_null() ? "" : row["msic_card_expire_date"].as<std::string>();
            driver["dg_license_no"] = row["dg_license_no"].is_null() ? "" : row["dg_license_no"].as<std::string>();
            driver["dg_license_expire_date"] = row["dg_license_expire_date"].is_null() ? "" : row["dg_license_expire_date"].as<std::string>();
            
            driverList.push_back(std::move(driver));
        }
        
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

// ==================== 新增车辆 ====================
crow::response addVehicleFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 必填字段
        if (!body.has("license_plate")) {
            result["retCode"] = 400;
            result["errorMsg"] = "license_plate is required";
            return crow::response(400, result);
        }

        std::string license_plate = body["license_plate"].s();
        
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM vehicle WHERE license_plate = $1", license_plate);
        if (!checkRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "License plate already exists";
            return crow::response(400, result);
        }

        // 可选字段
        std::string type = "";
        if (body.has("type")) {
            type = body["type"].s();
        }
        
        int status = 1;
        if (body.has("status")) {
            status = body["status"].i();
        }
        
        std::string gps_id = "";
        if (body.has("gps_id")) {
            gps_id = body["gps_id"].s();
        }
        
        int kilometres = 0;
        if (body.has("kilometres")) {
            kilometres = body["kilometres"].i();
        }
        
        std::string rego_expire_date = "";
        if (body.has("rego_expire_date")) {
            rego_expire_date = body["rego_expire_date"].s();
        }
        
        std::string tag_number = "";
        if (body.has("tag_number")) {
            tag_number = body["tag_number"].s();
        }
        
        std::string fuel_card_number = "";
        if (body.has("fuel_card_number")) {
            fuel_card_number = body["fuel_card_number"].s();
        }
        
        int driver_id = 0;
        if (body.has("driver_id")) {
            driver_id = body["driver_id"].i();
        }

        pqxx::result res = txn.exec_params(
            "INSERT INTO vehicle ("
            "license_plate, type, status, gps_id, kilometres, "
            "rego_expire_date, tag_number, fuel_card_number, driver_id"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) RETURNING id",
            license_plate.c_str(),
            type.empty() ? "" : type.c_str(),
            status,
            gps_id.empty() ? "" : gps_id.c_str(),
            kilometres,
            rego_expire_date.empty() ? nullptr : rego_expire_date.c_str(),
            tag_number.empty() ? nullptr : tag_number.c_str(),
            fuel_card_number.empty() ? nullptr : fuel_card_number.c_str(),
            driver_id == 0 ? nullptr : &driver_id
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add vehicle";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["msg"] = "Vehicle added successfully";
        result["id"] = res[0]["id"].as<int>();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Vehicle Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新车辆 ====================
crow::response updateVehicleFunc(const crow::request& req, pqxx::connection& conn) {
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
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int vehicleId = body["id"].i();

        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM vehicle WHERE id = $1", vehicleId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Vehicle not found";
            return crow::response(404, result);
        }

        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        struct UpdateField {
            std::string dbField;
            std::string paramName;
            bool isString;
        };

        std::vector<UpdateField> updateableFields = {
            {"license_plate", "license_plate", true},
            {"type", "type", true},
            {"status", "status", false},
            {"gps_id", "gps_id", true},
            {"kilometres", "kilometres", false},
            {"rego_expire_date", "rego_expire_date", true},
            {"tag_number", "tag_number", true},
            {"fuel_card_number", "fuel_card_number", true},
            {"driver_id", "driver_id", false}
        };

        for (const auto& field : updateableFields) {
            if (body.has(field.paramName)) {
                if (field.isString) {
                    std::string value = body[field.paramName].s();
                    updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                    params.push_back(value);
                } else {
                    int value = body[field.paramName].i();
                    updateFields.push_back(field.dbField + " = $" + std::to_string(paramCounter));
                    params.push_back(std::to_string(value));
                }
                paramCounter++;
            }
        }

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        std::string updateSql = "UPDATE vehicle SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(vehicleId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Vehicle updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Vehicle Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 删除车辆 ====================
crow::response deleteVehicleFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string deleteSql = "DELETE FROM vehicle WHERE id IN (";
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) deleteSql += ", ";
            deleteSql += "$" + std::to_string(i + 1);
        }
        deleteSql += ")";

        std::vector<std::string> params;
        for (int id : ids) {
            params.push_back(std::to_string(id));
        }

        pqxx::result res = txn.exec_params(deleteSql, pqxx::prepare::make_dynamic_params(params));
        
        result["retCode"] = 200;
        result["msg"] = "Vehicle(s) deleted successfully";
        result["affected_rows"] = (int)res.affected_rows();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Vehicle Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
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

        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");

        verifier.verify(decoded);

        const std::string username = decoded.get_subject();
        
        std::string baseQuery = "SELECT v.id, v.license_plate, v.type, v.status, v.gps_id, v.kilometres, "
                                "v.rego_expire_date, v.tag_number, v.fuel_card_number, v.driver_id, "
                                "d.name AS driver_name "
                                "FROM vehicle v "
                                "LEFT JOIN driver d ON v.driver_id = d.id "
                                "WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        std::unordered_map<std::string, std::string> queryParams;
        
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::vector<std::string> paramKeys = {
            "id", "license_plate", "type", "status", "gps_id",
            "kilometres_min", "kilometres_max",
            "rego_expire_date_start", "rego_expire_date_end",
            "driver_id",
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
            std::string tablePrefix;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", "v.", false},
            {"license_plate", "license_plate", "v.", true},
            {"type", "type", "v.", false},
            {"status", "status", "v.", false},
            {"gps_id", "gps_id", "v.", false},
            {"kilometres_min", "kilometres", "v.", false},
            {"kilometres_max", "kilometres", "v.", false},
            {"rego_expire_date_start", "rego_expire_date", "v.", false},
            {"rego_expire_date_end", "rego_expire_date", "v.", false},
            {"driver_id", "driver_id", "v.", false}
        };
        
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                std::string fullDbField = filter.tablePrefix + filter.dbField;
                
                if (filter.isLike) {
                    condition = fullDbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName == "kilometres_min") {
                    condition = fullDbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName == "kilometres_max") {
                    condition = fullDbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_start") != std::string::npos) {
                    condition = fullDbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_end") != std::string::npos) {
                    condition = fullDbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    condition = fullDbField + " = $" + std::to_string(paramCounter);
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
        
        finalQuery += " ORDER BY v.id DESC";
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        std::string countQuery = "SELECT COUNT(*) FROM vehicle v WHERE 1=1";
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
        
        crow::json::wvalue::list vehicleList; 
        
        for (const auto& row : res) {
            crow::json::wvalue vehicle;
            vehicle["id"] = row["id"].as<int>();
            vehicle["license_plate"] = row["license_plate"].is_null() ? "" : row["license_plate"].as<std::string>();
            vehicle["type"] = row["type"].is_null() ? "" : row["type"].as<std::string>();
            vehicle["status"] = row["status"].is_null() ? -1 : row["status"].as<int>();
            vehicle["gps_id"] = row["gps_id"].is_null() ? "" : row["gps_id"].as<std::string>();
            vehicle["kilometres"] = row["kilometres"].is_null() ? -1 : row["kilometres"].as<int>();
            vehicle["rego_expire_date"] = row["rego_expire_date"].is_null() ? "" : row["rego_expire_date"].as<std::string>();
            vehicle["tag_number"] = row["tag_number"].is_null() ? "" : row["tag_number"].as<std::string>();
            vehicle["fuel_card_number"] = row["fuel_card_number"].is_null() ? "" : row["fuel_card_number"].as<std::string>();
            vehicle["driver_id"] = row["driver_id"].is_null() ? 0 : row["driver_id"].as<int>();
            vehicle["driver_name"] = row["driver_name"].is_null() ? "" : row["driver_name"].as<std::string>();
            vehicleList.push_back(std::move(vehicle));
        }
        
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
        
        auto body = crow::json::load(req.body);
        if (!body) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid JSON body";
            return crow::response(400, result);
        }
        
        auto tasks_array = body;
        int successCount = 0;
        
        for (const auto& task_data : tasks_array) {
            std::string orderId = task_data["orderId"].s();
            std::string selectedVehicle = task_data["selectedVehicle"].s();
            int selectedDriver = task_data["selectedDriver"].i();
            
            if (orderId.empty()) {
                result["retCode"] = 400;
                result["errorMsg"] = "orderId is required";
                return crow::response(400, result);
            }
            
            size_t dashPos = orderId.find('-');
            int orderIdInt = (dashPos != std::string::npos) ? 
                std::stoi(orderId.substr(dashPos + 1)) : std::stoi(orderId);

            pqxx::result order_res = txn.exec_params(
                "SELECT id, container_no FROM orders WHERE id = $1",
                orderIdInt
            );
            
            if (order_res.empty()) {
                result["retCode"] = 400;
                result["errorMsg"] = "Order not found: " + orderId;
                return crow::response(400, result);
            }
            
            int order_id = order_res[0]["id"].as<int>();
            std::string container_no = order_res[0]["container_no"].is_null() ? "" : order_res[0]["container_no"].as<std::string>();
            
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
            
            pqxx::result driver_res = txn.exec_params(
                "SELECT id FROM driver WHERE id = $1",
                selectedDriver
            );
            if (driver_res.empty()) {
                result["retCode"] = 404;
                result["errorMsg"] = "Driver not found: " + std::to_string(selectedDriver);
                return crow::response(404, result);
            }
            
            int task_status = 1;
            
            auto now = std::chrono::system_clock::now();
            auto now_time = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&now_time), "%H:%M:%S");
            std::string task_start_time = ss.str();

            pqxx::result insert_res;
            if (container_id == -1) {
                insert_res = txn.exec_params(
                    "INSERT INTO task (order_id, driver_id, vehicle_id, container_id, task_status, task_start_time, emergency_status) "
                    "VALUES ($1, $2, $3, NULL, $4, $5, $6) RETURNING id",
                    order_id,
                    selectedDriver,
                    vehicle_id,
                    task_status,
                    task_start_time,
                    0
                );
            } else {
                insert_res = txn.exec_params(
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
            
            txn.exec_params(
                "UPDATE orders SET status = 2 WHERE id = $1",
                order_id
            );
            
            txn.exec_params(
                "UPDATE vehicle SET status = 2, driver_id = $1 WHERE id = $2",
                selectedDriver, vehicle_id
            );
            
            txn.exec_params(
                "UPDATE driver SET status = 2 WHERE id = $1",
                selectedDriver
            );
            
            successCount++;
        }
        
        txn.commit();
        
        result["retCode"] = 200;
        result["message"] = "Batch dispatch completed";
        result["successCount"] = successCount;
        
    } catch (const std::exception& e) {
        std::cerr << "Create Task Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
    
    return crow::response(200, result);
}

crow::response queryScheduleTaskFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string baseQuery = "SELECT id, vehicle_id, task_date, task_type, "
                                "start_point, end_point, task_time, customer, description, status, sort_order, "
                                "created_by, created_at, updated_by, updated_at "
                                "FROM schedule_task WHERE 1=1";
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::vector<std::string> paramKeys = {
            "id", "vehicle_id", "task_date", "task_type", "status", 
            "start_point", "end_point", "customer"
        };
        
        std::unordered_map<std::string, std::string> queryParams;
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
            {"vehicle_id", "vehicle_id", false},
            {"task_date", "task_date", false},
            {"task_type", "task_type", false},
            {"status", "status", false},
            {"start_point", "start_point", true},
            {"end_point", "end_point", true},
            {"customer", "customer", true}
        };
        
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
        finalQuery += " ORDER BY task_date ASC, sort_order ASC, task_time ASC";
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        std::string countQuery = "SELECT COUNT(*) FROM schedule_task WHERE 1=1";
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
        
        crow::json::wvalue::list taskList;
        for (const auto& row : res) {
            crow::json::wvalue task;
            task["id"] = row["id"].as<int>();
            task["vehicle_id"] = row["vehicle_id"].as<int>();
            task["task_date"] = row["task_date"].is_null() ? "" : row["task_date"].as<std::string>();
            task["task_type"] = row["task_type"].is_null() ? "" : row["task_type"].as<std::string>();
            task["start_point"] = row["start_point"].is_null() ? "" : row["start_point"].as<std::string>();
            task["end_point"] = row["end_point"].is_null() ? "" : row["end_point"].as<std::string>();
            task["task_time"] = row["task_time"].is_null() ? "" : row["task_time"].as<std::string>();
            task["customer"] = row["customer"].is_null() ? "" : row["customer"].as<std::string>();
            task["description"] = row["description"].is_null() ? "" : row["description"].as<std::string>();
            task["status"] = row["status"].is_null() ? "pending" : row["status"].as<std::string>();
            task["sort_order"] = row["sort_order"].is_null() ? 0 : row["sort_order"].as<int>();
            task["created_by"] = row["created_by"].is_null() ? 0 : row["created_by"].as<int>();
            task["created_at"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            task["updated_by"] = row["updated_by"].is_null() ? 0 : row["updated_by"].as<int>();
            task["updated_at"] = row["updated_at"].is_null() ? "" : row["updated_at"].as<std::string>();
            taskList.push_back(std::move(task));
        }
        
        result["retCode"] = 200;
        result["data"] = std::move(taskList);
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

crow::response addScheduleTaskFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        const std::string username = decoded.get_subject();
        
        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();

        if (!body.has("vehicle_id") || !body.has("task_date") || !body.has("task_type")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Missing required fields: vehicle_id, task_date, task_type";
            return crow::response(400, result);
        }

        int vehicle_id = body["vehicle_id"].i();
        std::string task_date = body["task_date"].s();
        std::string task_type = body["task_type"].s();
        
        std::string start_point = "";
        if (body.has("start_point")) {
            start_point = body["start_point"].s();
        }
        
        std::string end_point = "";
        if (body.has("end_point")) {
            end_point = body["end_point"].s();
        }
        
        std::string task_time = "";
        if (body.has("task_time")) {
            task_time = body["task_time"].s();
        }
        
        std::string customer = "";
        if (body.has("customer")) {
            customer = body["customer"].s();
        }
        
        std::string description = "";
        if (body.has("description")) {
            description = body["description"].s();
        }
        
        std::string status = "pending";
        if (body.has("status")) {
            status = body["status"].s();
        }
        
        int sort_order = 0;
        if (body.has("sort_order")) {
            sort_order = body["sort_order"].i();
        }

        pqxx::result res = txn.exec_params(
            "INSERT INTO schedule_task ("
            "vehicle_id, task_date, task_type, start_point, end_point, task_time, "
            "customer, description, status, sort_order, "
            "created_by, created_at, updated_by, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, CURRENT_TIMESTAMP, $12, CURRENT_TIMESTAMP) RETURNING id",
            vehicle_id,
            task_date.c_str(),
            task_type.c_str(),
            start_point.empty() ? nullptr : start_point.c_str(),
            end_point.empty() ? nullptr : end_point.c_str(),
            task_time.empty() ? nullptr : task_time.c_str(),
            customer.empty() ? nullptr : customer.c_str(),
            description.empty() ? nullptr : description.c_str(),
            status.c_str(),
            sort_order,
            userId,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add schedule task";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["msg"] = "Schedule task added successfully";
        result["id"] = res[0]["id"].as<int>();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Schedule Task Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

crow::response updateScheduleTaskFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        const std::string username = decoded.get_subject();
        
        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();

        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int taskId = body["id"].i();

        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM schedule_task WHERE id = $1", taskId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Schedule task not found";
            return crow::response(404, result);
        }

        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        auto addStringField = [&](const std::string& dbField, const std::string& paramName) {
            if (body.has(paramName)) {
                std::string value = body[paramName].s();
                updateFields.push_back(dbField + " = $" + std::to_string(paramCounter));
                params.push_back(value.empty() ? "" : value);
                paramCounter++;
            }
        };
        
        auto addIntField = [&](const std::string& dbField, const std::string& paramName) {
            if (body.has(paramName)) {
                int value = body[paramName].i();
                updateFields.push_back(dbField + " = $" + std::to_string(paramCounter));
                params.push_back(std::to_string(value));
                paramCounter++;
            }
        };

        addIntField("vehicle_id", "vehicle_id");
        addStringField("task_date", "task_date");
        addStringField("task_type", "task_type");
        addStringField("start_point", "start_point");
        addStringField("end_point", "end_point");
        addStringField("task_time", "task_time");
        addStringField("customer", "customer");
        addStringField("description", "description");
        addStringField("status", "status");
        addIntField("sort_order", "sort_order");

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        updateFields.push_back("updated_by = $" + std::to_string(paramCounter));
        updateFields.push_back("updated_at = CURRENT_TIMESTAMP");
        params.push_back(std::to_string(userId));
        paramCounter++;

        std::string updateSql = "UPDATE schedule_task SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(taskId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Schedule task updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Schedule Task Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

crow::response deleteScheduleTaskFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string deleteSql = "DELETE FROM schedule_task WHERE id IN (";
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) deleteSql += ", ";
            deleteSql += "$" + std::to_string(i + 1);
        }
        deleteSql += ")";

        std::vector<std::string> params;
        for (int id : ids) {
            params.push_back(std::to_string(id));
        }

        pqxx::result res = txn.exec_params(deleteSql, pqxx::prepare::make_dynamic_params(params));
        
        result["retCode"] = 200;
        result["msg"] = "Schedule task(s) deleted successfully";
        result["affected_rows"] = (int)res.affected_rows();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Schedule Task Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 获取所有场地位置 ====================
crow::response queryYardSlotsFunc(const crow::request& req, pqxx::connection& conn) {
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

        // ========== 查询 Container Area ==========
        pqxx::result containerRes = txn.exec(
            "SELECT id, slot_name, area_type, status, container_no, "
            "order_id, in_time, remark, created_at, updated_at "
            "FROM yard_slot "
            "WHERE area_type = 'container' AND deleted_at IS NULL "
            "ORDER BY id"
        );

        crow::json::wvalue::list containerSlots;
        for (const auto& row : containerRes) {
            crow::json::wvalue slot;
            slot["id"] = row["id"].as<int>();
            slot["name"] = row["slot_name"].as<std::string>();
            slot["areaType"] = row["area_type"].as<std::string>();
            slot["status"] = row["status"].is_null() ? "empty" : row["status"].as<std::string>();
            slot["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
            slot["orderId"] = row["order_id"].is_null() ? "" : row["order_id"].as<std::string>();
            slot["driverName"] = "";
            slot["inTime"] = row["in_time"].is_null() ? "" : row["in_time"].as<std::string>();
            slot["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
            slot["vehicleId"] = 0;
            slot["vehicleNo"] = "";
            containerSlots.push_back(std::move(slot));
        }

        // ========== 查询 Parking Area（关联车辆和货柜信息） ==========
        // ✅ 移除 ys.vehicle_no（表中已不存在），改用 v.license_plate
        pqxx::result parkingRes = txn.exec(
            "SELECT DISTINCT ON (ys.id) "
            "  ys.id, ys.slot_name, ys.area_type, ys.status, ys.remark, "
            "  ys.vehicle_id, ys.in_time, "
            "  v.license_plate, v.type AS vehicle_type, v.driver_id, "
            "  d.name AS driver_name, "
            "  t.container_id, "
            "  c.container_no "
            "FROM yard_slot ys "
            "LEFT JOIN vehicle v ON ys.vehicle_id = v.id "
            "LEFT JOIN driver d ON v.driver_id = d.id "
            "LEFT JOIN task t ON t.vehicle_id = v.id AND t.task_status = 2 "
            "LEFT JOIN container c ON t.container_id = c.id AND c.deleted_at IS NULL "
            "WHERE ys.area_type = 'parking' AND ys.deleted_at IS NULL "
            "ORDER BY ys.id, t.id DESC"
        );

        crow::json::wvalue::list parkingSlots;
        for (const auto& row : parkingRes) {
            crow::json::wvalue slot;
            slot["id"] = row["id"].as<int>();
            slot["name"] = row["slot_name"].as<std::string>();
            slot["areaType"] = row["area_type"].as<std::string>();
            slot["status"] = row["status"].is_null() ? "empty" : row["status"].as<std::string>();
            slot["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
            
            // 车辆信息
            int vehicleId = row["vehicle_id"].is_null() ? 0 : row["vehicle_id"].as<int>();
            slot["vehicleId"] = vehicleId;
            
            // ✅ 车牌号从 vehicle 表获取
            std::string vehicleNo = row["license_plate"].is_null() ? "" : row["license_plate"].as<std::string>();
            slot["vehicleNo"] = vehicleNo;
            
            slot["vehicleType"] = row["vehicle_type"].is_null() ? "" : row["vehicle_type"].as<std::string>();
            slot["driverName"] = row["driver_name"].is_null() ? "" : row["driver_name"].as<std::string>();
            slot["driverId"] = row["driver_id"].is_null() ? 0 : row["driver_id"].as<int>();
            slot["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
            slot["containerId"] = row["container_id"].is_null() ? 0 : row["container_id"].as<int>();
            slot["inTime"] = row["in_time"].is_null() ? "" : row["in_time"].as<std::string>();
            slot["orderId"] = "";
            
            parkingSlots.push_back(std::move(slot));
        }

        result["retCode"] = 200;
        result["data"]["containerSlots"] = std::move(containerSlots);
        result["data"]["parkingSlots"] = std::move(parkingSlots);
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

// ==================== 添加位置 ====================
crow::response addYardSlotFunc(const crow::request& req, pqxx::connection& conn) {
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

        const std::string username = decoded.get_subject();
        auto [userId, userName] = getCurrentUser(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("name") || !body.has("area")) {
            result["retCode"] = 400;
            result["errorMsg"] = "name and area are required";
            return crow::response(400, result);
        }

        std::string slotName = body["name"].s();
        std::string areaType = body["area"].s();
        
        if (areaType != "container" && areaType != "parking") {
            result["retCode"] = 400;
            result["errorMsg"] = "area must be 'container' or 'parking'";
            return crow::response(400, result);
        }

        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM yard_slot WHERE slot_name = $1 AND area_type = $2 AND deleted_at IS NULL",
            slotName.c_str(), areaType.c_str()
        );
        if (!checkRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Slot name already exists in this area";
            return crow::response(400, result);
        }

        std::string status = "empty";
        if (body.has("status")) {
            status = body["status"].s();
        }

        std::string remark = "";
        if (body.has("remark")) {
            remark = body["remark"].s();
        }

        pqxx::result res = txn.exec_params(
            "INSERT INTO yard_slot ("
            "slot_name, area_type, status, remark, created_by, updated_by, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) RETURNING id, slot_name, status",
            slotName.c_str(),
            areaType.c_str(),
            status.c_str(),
            remark.empty() ? nullptr : remark.c_str(),
            userId,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add slot";
            return crow::response(400, result);
        }

        int slotId = res[0]["id"].as<int>();

        addYardLog(txn, slotId, slotName, areaType, "add", "", "", "", remark, userId, userName);

        result["retCode"] = 200;
        result["msg"] = "Slot added successfully";
        result["data"]["id"] = slotId;
        result["data"]["name"] = res[0]["slot_name"].as<std::string>();
        result["data"]["status"] = res[0]["status"].as<std::string>();
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Yard Slot Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 删除位置（软删除，仅管理员） ====================
crow::response deleteYardSlotFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string role = decoded.get_payload_claim("role").as_string();
        if (role != "admin") {
            result["retCode"] = 403;
            result["errorMsg"] = "Only admin can delete slots";
            return crow::response(403, result);
        }

        const std::string username = decoded.get_subject();
        auto [userId, userName] = getCurrentUser(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int slotId = body["id"].i();

        pqxx::result slotRes = txn.exec_params(
            "SELECT slot_name, area_type, status, vehicle_id FROM yard_slot WHERE id = $1 AND deleted_at IS NULL",
            slotId
        );
        if (slotRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Slot not found";
            return crow::response(404, result);
        }

        std::string slotName = slotRes[0]["slot_name"].as<std::string>();
        std::string areaType = slotRes[0]["area_type"].as<std::string>();
        std::string status = slotRes[0]["status"].as<std::string>();
        int vehicleId = slotRes[0]["vehicle_id"].is_null() ? 0 : slotRes[0]["vehicle_id"].as<int>();

        if (areaType == "parking" && status == "occupied" && vehicleId > 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "Cannot delete parking slot with vehicle parked in it";
            return crow::response(400, result);
        }

        txn.exec_params(
            "UPDATE yard_slot SET deleted_at = CURRENT_TIMESTAMP, deleted_by = $1 WHERE id = $2",
            userId, slotId
        );

        addYardLog(txn, slotId, slotName, areaType, "delete", "", "", "", "", userId, userName);

        result["retCode"] = 200;
        result["msg"] = "Slot deleted successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Yard Slot Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新位置状态 ====================
crow::response updateYardSlotStatusFunc(const crow::request& req, pqxx::connection& conn) {
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

        const std::string username = decoded.get_subject();
        auto [userId, userName] = getCurrentUser(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("id") || !body.has("action")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id and action are required";
            return crow::response(400, result);
        }

        int slotId = body["id"].i();
        std::string action = body["action"].s();

        // ✅ 移除 vehicle_no, driver_name
        pqxx::result slotRes = txn.exec_params(
            "SELECT slot_name, area_type, status, container_no, vehicle_id, order_id, in_time "
            "FROM yard_slot WHERE id = $1 AND deleted_at IS NULL",
            slotId
        );
        if (slotRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Slot not found";
            return crow::response(404, result);
        }

        std::string slotName = slotRes[0]["slot_name"].as<std::string>();
        std::string areaType = slotRes[0]["area_type"].as<std::string>();
        std::string currentStatus = slotRes[0]["status"].as<std::string>();
        std::string currentContainerNo = slotRes[0]["container_no"].is_null() ? "" : slotRes[0]["container_no"].as<std::string>();

        std::string newStatus;
        std::string containerNo = "";
        std::string orderId = "";
        std::string remark = "";
        std::string logAction = action;

        if (action == "in") {
            if (areaType != "container") {
                result["retCode"] = 400;
                result["errorMsg"] = "'in' action is only for Container Area";
                return crow::response(400, result);
            }
            
            if (!body.has("containerNo")) {
                result["retCode"] = 400;
                result["errorMsg"] = "containerNo is required for 'in' action";
                return crow::response(400, result);
            }
            if (currentStatus != "empty") {
                result["retCode"] = 400;
                result["errorMsg"] = "Slot is not empty, cannot mark as in";
                return crow::response(400, result);
            }
            
            newStatus = "occupied";
            containerNo = body["containerNo"].s();
            if (body.has("orderId")) orderId = body["orderId"].s();
            if (body.has("remark")) remark = body["remark"].s();
            
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = $1, container_no = $2, order_id = $3, "
                "in_time = CURRENT_TIMESTAMP, remark = $4, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $5",
                newStatus.c_str(),
                containerNo.c_str(),
                orderId.empty() ? nullptr : orderId.c_str(),
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );
            
        } else if (action == "out") {
            if (areaType != "container") {
                result["retCode"] = 400;
                result["errorMsg"] = "'out' action is only for Container Area";
                return crow::response(400, result);
            }
            
            if (currentStatus != "occupied") {
                result["retCode"] = 400;
                result["errorMsg"] = "Slot is not occupied, cannot mark as out";
                return crow::response(400, result);
            }
            
            newStatus = "empty";
            containerNo = currentContainerNo;
            if (body.has("remark")) remark = body["remark"].s();
            
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = $1, container_no = NULL, order_id = NULL, "
                "in_time = NULL, remark = $2, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $3",
                newStatus.c_str(),
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );
            
        } else if (action == "reserve") {
            if (currentStatus != "empty") {
                result["retCode"] = 400;
                result["errorMsg"] = "Slot is not empty, cannot reserve";
                return crow::response(400, result);
            }
            
            newStatus = "reserved";
            if (body.has("remark")) remark = body["remark"].s();
            
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = $1, remark = $2, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $3",
                newStatus.c_str(),
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );
            
        } else if (action == "cancel_reserve") {
            if (currentStatus != "reserved") {
                result["retCode"] = 400;
                result["errorMsg"] = "Slot is not reserved, cannot cancel reserve";
                return crow::response(400, result);
            }
            
            newStatus = "empty";
            if (body.has("remark")) remark = body["remark"].s();
            
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = $1, remark = $2, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $3",
                newStatus.c_str(),
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );
            
        } else {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid action. Supported: in, out, reserve, cancel_reserve";
            return crow::response(400, result);
        }

        addYardLog(txn, slotId, slotName, areaType, logAction, 
                   containerNo, orderId, "", remark, userId, userName);

        result["retCode"] = 200;
        result["msg"] = "Status updated successfully";
        result["data"]["status"] = newStatus;
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Yard Slot Status Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新位置信息（编辑） ====================
crow::response updateYardSlotFunc(const crow::request& req, pqxx::connection& conn) {
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

        const std::string username = decoded.get_subject();
        auto [userId, userName] = getCurrentUser(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("id")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }

        int slotId = body["id"].i();

        // 获取位置信息
        pqxx::result slotRes = txn.exec_params(
            "SELECT slot_name, area_type FROM yard_slot WHERE id = $1 AND deleted_at IS NULL",
            slotId
        );
        if (slotRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Slot not found";
            return crow::response(404, result);
        }

        std::string slotName = slotRes[0]["slot_name"].as<std::string>();
        std::string areaType = slotRes[0]["area_type"].as<std::string>();

        // 构建动态更新语句
        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        auto addStringField = [&](const std::string& dbField, const std::string& paramName) {
            if (body.has(paramName)) {
                std::string value = body[paramName].s();
                updateFields.push_back(dbField + " = $" + std::to_string(paramCounter));
                params.push_back(value);
                paramCounter++;
            }
        };

        // Container Area 编辑字段（移除 driver_name）
        addStringField("container_no", "containerNo");
        addStringField("order_id", "orderId");
        addStringField("remark", "remark");

        // 如果更新了货柜号且状态是 empty，自动变为 occupied
        if (body.has("containerNo")) {
            std::string containerNo = body["containerNo"].s();
            if (!containerNo.empty()) {
                // 检查当前状态
                pqxx::result statusRes = txn.exec_params(
                    "SELECT status FROM yard_slot WHERE id = $1", slotId
                );
                if (!statusRes.empty() && statusRes[0]["status"].as<std::string>() == "empty") {
                    updateFields.push_back("status = 'occupied'");
                    updateFields.push_back("in_time = CURRENT_TIMESTAMP");
                }
            } else {
                // 清空了货柜号，状态变为 empty
                updateFields.push_back("status = 'empty'");
                updateFields.push_back("in_time = NULL");
            }
        }

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        updateFields.push_back("updated_by = $" + std::to_string(paramCounter));
        params.push_back(std::to_string(userId));
        paramCounter++;
        
        updateFields.push_back("updated_at = CURRENT_TIMESTAMP");

        std::string updateSql = "UPDATE yard_slot SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(slotId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        // 记录日志
        std::string remark = "";
        std::string containerNo = "";
        std::string orderId = "";
        
        if (body.has("remark")) {
            remark = body["remark"].s();
        }
        if (body.has("containerNo")) {
            containerNo = body["containerNo"].s();
        }
        if (body.has("orderId")) {
            orderId = body["orderId"].s();
        }
        
        addYardLog(txn, slotId, slotName, areaType, "edit", 
                   containerNo, orderId, "", remark, userId, userName);

        result["retCode"] = 200;
        result["msg"] = "Slot updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Yard Slot Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 获取操作日志 ====================
crow::response queryYardLogsFunc(const crow::request& req, pqxx::connection& conn) {
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

        int pageNum = 1;
        int pageSize = 20;
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::string pageNumStr = get_param("pageNum");
        if (!pageNumStr.empty()) pageNum = std::stoi(pageNumStr);
        std::string pageSizeStr = get_param("pageSize");
        if (!pageSizeStr.empty()) pageSize = std::stoi(pageSizeStr);
        
        int offset = (pageNum - 1) * pageSize;

        pqxx::result res = txn.exec_params(
            "SELECT id, slot_id, slot_name, area_type, action_type, "
            "container_no, order_id, driver_name, remark, operator_name, created_at "
            "FROM yard_log ORDER BY created_at DESC LIMIT $1 OFFSET $2",
            pageSize, offset
        );

        pqxx::result countRes = txn.exec("SELECT COUNT(*) FROM yard_log");
        int total = countRes[0][0].as<int>();

        crow::json::wvalue::list logList;
        for (const auto& row : res) {
            crow::json::wvalue log;
            log["id"] = row["id"].as<int>();
            log["slotId"] = row["slot_id"].as<int>();
            log["slotName"] = row["slot_name"].as<std::string>();
            log["areaType"] = row["area_type"].as<std::string>();
            log["actionType"] = row["action_type"].as<std::string>();
            log["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
            log["orderId"] = row["order_id"].is_null() ? "" : row["order_id"].as<std::string>();
            log["driverName"] = row["driver_name"].is_null() ? "" : row["driver_name"].as<std::string>();
            log["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
            log["operatorName"] = row["operator_name"].is_null() ? "" : row["operator_name"].as<std::string>();
            log["createdAt"] = row["created_at"].as<std::string>();
            logList.push_back(std::move(log));
        }

        result["retCode"] = 200;
        result["data"] = std::move(logList);
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

// ==================== 获取单个位置详情 ====================
crow::response getYardSlotDetailFunc(const crow::request& req, pqxx::connection& conn) {
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

        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::string idStr = get_param("id");
        if (idStr.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "id is required";
            return crow::response(400, result);
        }
        
        int slotId = std::stoi(idStr);

        // ✅ 移除 vehicle_no，改为从 vehicle 表关联查询
        pqxx::result slotRes = txn.exec_params(
            "SELECT ys.id, ys.slot_name, ys.area_type, ys.status, ys.container_no, "
            "ys.vehicle_id, ys.order_id, ys.in_time, ys.remark, ys.created_at, ys.updated_at, "
            "v.license_plate AS vehicle_no, v.type AS vehicle_type, v.driver_id, "
            "d.name AS driver_name "
            "FROM yard_slot ys "
            "LEFT JOIN vehicle v ON ys.vehicle_id = v.id "
            "LEFT JOIN driver d ON v.driver_id = d.id "
            "WHERE ys.id = $1 AND ys.deleted_at IS NULL",
            slotId
        );

        if (slotRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Slot not found";
            return crow::response(404, result);
        }

        const auto& row = slotRes[0];
        crow::json::wvalue slot;
        slot["id"] = row["id"].as<int>();
        slot["name"] = row["slot_name"].as<std::string>();
        slot["areaType"] = row["area_type"].as<std::string>();
        slot["status"] = row["status"].as<std::string>();
        slot["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
        slot["vehicleId"] = row["vehicle_id"].is_null() ? 0 : row["vehicle_id"].as<int>();
        slot["vehicleNo"] = row["vehicle_no"].is_null() ? "" : row["vehicle_no"].as<std::string>();
        slot["vehicleType"] = row["vehicle_type"].is_null() ? "" : row["vehicle_type"].as<std::string>();
        slot["driverName"] = row["driver_name"].is_null() ? "" : row["driver_name"].as<std::string>();
        slot["driverId"] = row["driver_id"].is_null() ? 0 : row["driver_id"].as<int>();
        slot["orderId"] = row["order_id"].is_null() ? "" : row["order_id"].as<std::string>();
        slot["inTime"] = row["in_time"].is_null() ? "" : row["in_time"].as<std::string>();
        slot["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
        slot["createdAt"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
        slot["updatedAt"] = row["updated_at"].is_null() ? "" : row["updated_at"].as<std::string>();

        result["retCode"] = 200;
        result["data"] = std::move(slot);
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

// ==================== 获取今日订单 ====================
crow::response getTodayOrdersFunc(const crow::request& req, pqxx::connection& conn) {
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

        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };

        int pageNum = 1;
        int pageSize = 10;
        std::string pageNumStr = get_param("pageNum");
        if (!pageNumStr.empty()) pageNum = std::stoi(pageNumStr);
        std::string pageSizeStr = get_param("pageSize");
        if (!pageSizeStr.empty()) pageSize = std::stoi(pageSizeStr);
        int offset = (pageNum - 1) * pageSize;

        auto now = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time_t);
        std::ostringstream todayOss;
        todayOss << (now_tm->tm_year + 1900) << "-" 
                 << std::setw(2) << std::setfill('0') << (now_tm->tm_mon + 1) << "-" 
                 << std::setw(2) << std::setfill('0') << now_tm->tm_mday;
        std::string todayStr = todayOss.str();

        std::string query = 
            "SELECT o.id, o.start_point, o.end_point, o.status, "
            "v.license_plate AS vehicle_no, "
            "d.name AS driver_name "
            "FROM orders o "
            "LEFT JOIN task t ON o.id = t.order_id "
            "LEFT JOIN vehicle v ON t.vehicle_id = v.id "
            "LEFT JOIN driver d ON t.driver_id = d.id "
            "WHERE DATE(o.create_time) = $1 "
            "ORDER BY o.id DESC "
            "LIMIT $2 OFFSET $3";

        pqxx::result res = txn.exec_params(
            query,
            todayStr.c_str(),
            pageSize,
            offset
        );

        pqxx::result countRes = txn.exec_params(
            "SELECT COUNT(*) FROM orders WHERE DATE(create_time) = $1",
            todayStr.c_str()
        );
        int total = countRes[0][0].as<int>();

        crow::json::wvalue::list orderList;
        for (const auto& row : res) {
            crow::json::wvalue order;
            order["id"] = row[0].as<int>();
            order["start_point"] = row[1].is_null() ? "" : row[1].as<std::string>();
            order["end_point"] = row[2].is_null() ? "" : row[2].as<std::string>();
            order["status"] = row[3].is_null() ? 1 : row[3].as<int>();
            order["vehicle_no"] = row[4].is_null() ? "" : row[4].as<std::string>();
            order["driver_name"] = row[5].is_null() ? "" : row[5].as<std::string>();
            orderList.push_back(std::move(order));
        }

        result["retCode"] = 200;
        result["data"] = std::move(orderList);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Today Orders Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新 Parking Area 车辆（车辆停入/驶出） ====================
crow::response updateYardSlotVehicleFunc(const crow::request& req, pqxx::connection& conn) {
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

        const std::string username = decoded.get_subject();
        auto [userId, userName] = getCurrentUser(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("slotId") || !body.has("action")) {
            result["retCode"] = 400;
            result["errorMsg"] = "slotId and action are required";
            return crow::response(400, result);
        }

        int slotId = body["slotId"].i();
        std::string action = body["action"].s();

        // ✅ 移除 vehicle_no
        pqxx::result slotRes = txn.exec_params(
            "SELECT slot_name, area_type, status, vehicle_id, remark "
            "FROM yard_slot WHERE id = $1 AND area_type = 'parking' AND deleted_at IS NULL",
            slotId
        );
        if (slotRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Parking slot not found";
            return crow::response(404, result);
        }

        std::string slotName = slotRes[0]["slot_name"].as<std::string>();
        std::string currentStatus = slotRes[0]["status"].as<std::string>();
        int currentVehicleId = slotRes[0]["vehicle_id"].is_null() ? 0 : slotRes[0]["vehicle_id"].as<int>();

        if (action == "park_in") {
            if (currentStatus == "reserved") {
                result["retCode"] = 400;
                result["errorMsg"] = "This parking slot is reserved, cannot park in";
                return crow::response(400, result);
            }
            
            if (currentStatus == "occupied") {
                result["retCode"] = 400;
                result["errorMsg"] = "This parking slot is already occupied";
                return crow::response(400, result);
            }

            if (!body.has("vehicleId")) {
                result["retCode"] = 400;
                result["errorMsg"] = "vehicleId is required for park_in action";
                return crow::response(400, result);
            }

            int vehicleId = body["vehicleId"].i();
            
            pqxx::result vehicleRes = txn.exec_params(
                "SELECT license_plate FROM vehicle WHERE id = $1",
                vehicleId
            );
            if (vehicleRes.empty()) {
                result["retCode"] = 404;
                result["errorMsg"] = "Vehicle not found";
                return crow::response(404, result);
            }
            std::string licensePlate = vehicleRes[0]["license_plate"].as<std::string>();

            pqxx::result checkRes = txn.exec_params(
                "SELECT id FROM yard_slot WHERE vehicle_id = $1 AND area_type = 'parking' AND status = 'occupied' AND deleted_at IS NULL",
                vehicleId
            );
            if (!checkRes.empty()) {
                result["retCode"] = 400;
                result["errorMsg"] = "Vehicle is already parked in another slot";
                return crow::response(400, result);
            }

            std::string remark = "";
            if (body.has("remark")) {
                remark = body["remark"].s();
            }
            
            // ✅ 移除 vehicle_no 字段
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = 'occupied', vehicle_id = $1, "
                "in_time = CURRENT_TIMESTAMP, remark = $2, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $3",
                vehicleId,
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );

            addYardLog(txn, slotId, slotName, "parking", "park_in", "", "", "", 
                       "车辆 " + licensePlate + " 停入", userId, userName);

            result["retCode"] = 200;
            result["msg"] = "Vehicle parked in successfully";
            result["data"]["vehicleId"] = vehicleId;
            result["data"]["vehicleNo"] = licensePlate;
            
        } else if (action == "park_out") {
            if (currentStatus != "occupied") {
                result["retCode"] = 400;
                result["errorMsg"] = "This parking slot is not occupied";
                return crow::response(400, result);
            }

            if (currentVehicleId == 0) {
                result["retCode"] = 400;
                result["errorMsg"] = "No vehicle in this parking slot";
                return crow::response(400, result);
            }

            std::string remark = "";
            if (body.has("remark")) {
                remark = body["remark"].s();
            }

            // ✅ 移除 vehicle_no 字段
            txn.exec_params(
                "UPDATE yard_slot SET "
                "status = 'empty', vehicle_id = NULL, "
                "in_time = NULL, remark = $1, updated_at = CURRENT_TIMESTAMP "
                "WHERE id = $2",
                remark.empty() ? nullptr : remark.c_str(),
                slotId
            );

            addYardLog(txn, slotId, slotName, "parking", "park_out", "", "", "", 
                       "车辆驶出", userId, userName);

            result["retCode"] = 200;
            result["msg"] = "Vehicle parked out successfully";
            
        } else {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid action. Supported: park_in, park_out";
            return crow::response(400, result);
        }

        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Yard Slot Vehicle Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 注册 API ====================
AUTO_REGISTER_DISPATCH_API("addDriver", addDriverFunc);
AUTO_REGISTER_DISPATCH_API("updateDriver", updateDriverFunc);
AUTO_REGISTER_DISPATCH_API("deleteDriver", deleteDriverFunc);
AUTO_REGISTER_DISPATCH_API("queryDriver", queryDriverFunc);
AUTO_REGISTER_DISPATCH_API("addVehicle", addVehicleFunc);
AUTO_REGISTER_DISPATCH_API("updateVehicle", updateVehicleFunc);
AUTO_REGISTER_DISPATCH_API("deleteVehicle", deleteVehicleFunc);
AUTO_REGISTER_DISPATCH_API("queryVehicle", queryVehicleFunc);
AUTO_REGISTER_DISPATCH_API("batchDispatch", batchDispatchFunc);
AUTO_REGISTER_DISPATCH_API("queryScheduleTask", queryScheduleTaskFunc);
AUTO_REGISTER_DISPATCH_API("addScheduleTask", addScheduleTaskFunc);
AUTO_REGISTER_DISPATCH_API("updateScheduleTask", updateScheduleTaskFunc);
AUTO_REGISTER_DISPATCH_API("deleteScheduleTask", deleteScheduleTaskFunc);
AUTO_REGISTER_DISPATCH_API("queryYardSlots", queryYardSlotsFunc);
AUTO_REGISTER_DISPATCH_API("addYardSlot", addYardSlotFunc);
AUTO_REGISTER_DISPATCH_API("deleteYardSlot", deleteYardSlotFunc);
AUTO_REGISTER_DISPATCH_API("updateYardSlotStatus", updateYardSlotStatusFunc);
AUTO_REGISTER_DISPATCH_API("updateYardSlot", updateYardSlotFunc);
AUTO_REGISTER_DISPATCH_API("queryYardLogs", queryYardLogsFunc);
AUTO_REGISTER_DISPATCH_API("getYardSlotDetail", getYardSlotDetailFunc);
AUTO_REGISTER_DISPATCH_API("getTodayOrders", getTodayOrdersFunc);
AUTO_REGISTER_DISPATCH_API("updateYardSlotVehicle", updateYardSlotVehicleFunc);