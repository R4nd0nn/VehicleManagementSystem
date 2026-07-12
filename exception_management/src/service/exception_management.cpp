#include "service/exception_management.h"
#include "../common/include/jwt/jwt.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>

// ==================== 工具函数 ====================

// 获取当前用户ID
int getCurrentUserId(pqxx::work& txn, const std::string& username) {
    pqxx::result staffRes = txn.exec_params(
        "SELECT id FROM staff WHERE username = $1", username);
    if (staffRes.empty()) {
        return 0;
    }
    return staffRes[0]["id"].as<int>();
}

// 生成异常编号：EXC-20260711-001
std::string generateExceptionNo() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    std::ostringstream oss;
    oss << "EXC-"
        << std::setw(4) << std::setfill('0') << (now_tm->tm_year + 1900)
        << std::setw(2) << std::setfill('0') << (now_tm->tm_mon + 1)
        << std::setw(2) << std::setfill('0') << now_tm->tm_mday
        << "-";
    
    // 简单随机数作为序号（实际应该从数据库查询当天最大序号）
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 999);
    oss << std::setw(3) << std::setfill('0') << dis(gen);
    
    return oss.str();
}

// 解析分页参数
int parsePageParam(const std::string& param, int defaultValue = 1) {
    if (param.empty()) return defaultValue;
    try {
        int value = std::stoi(param);
        return value > 0 ? value : defaultValue;
    } catch (...) {
        return defaultValue;
    }
}

// ==================== 1. 查询异常列表 ====================
crow::response queryExceptionsFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 解析请求参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };

        std::string exceptionType = get_param("exceptionType");
        std::string status = get_param("status");
        std::string relatedNo = get_param("relatedNo");

        int pageNum = parsePageParam(get_param("pageNum"), 1);
        int pageSize = parsePageParam(get_param("pageSize"), 10);
        int offset = (pageNum - 1) * pageSize;

        // 构建查询条件
        std::string whereClause = "WHERE deleted_at IS NULL";
        std::vector<std::string> params;
        int paramCounter = 1;

        if (!exceptionType.empty()) {
            whereClause += " AND exception_type = $" + std::to_string(paramCounter);
            params.push_back(exceptionType);
            paramCounter++;
        }

        if (!status.empty()) {
            whereClause += " AND status = $" + std::to_string(paramCounter);
            params.push_back(status);
            paramCounter++;
        }

        if (!relatedNo.empty()) {
            whereClause += " AND related_no LIKE $" + std::to_string(paramCounter);
            params.push_back("%" + relatedNo + "%");
            paramCounter++;
        }

        // 查询总数
        std::string countQuery = "SELECT COUNT(*) FROM exception_event " + whereClause;
        
        pqxx::result countRes;
        if (params.empty()) {
            countRes = txn.exec(countQuery);
        } else {
            countRes = txn.exec_params(countQuery, pqxx::prepare::make_dynamic_params(params));
        }
        int total = countRes[0][0].as<int>();

        // 查询数据
        std::string query = 
            "SELECT id, exception_no, exception_type, related_type, related_no, "
            "reporter, report_time, description, handler, action, process_remark, "
            "status, complete_time, created_at, updated_at "
            "FROM exception_event " + whereClause +
            " ORDER BY id DESC "
            "LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));

        pqxx::result res = txn.exec_params(query, pqxx::prepare::make_dynamic_params(params));

        crow::json::wvalue::list exceptionList;
        for (const auto& row : res) {
            crow::json::wvalue item;
            item["id"] = row["id"].as<int>();
            item["exceptionNo"] = row["exception_no"].is_null() ? "" : row["exception_no"].as<std::string>();
            item["exceptionType"] = row["exception_type"].is_null() ? "" : row["exception_type"].as<std::string>();
            item["relatedType"] = row["related_type"].is_null() ? "" : row["related_type"].as<std::string>();
            item["relatedNo"] = row["related_no"].is_null() ? "" : row["related_no"].as<std::string>();
            item["reporter"] = row["reporter"].is_null() ? "" : row["reporter"].as<std::string>();
            item["reportTime"] = row["report_time"].is_null() ? "" : row["report_time"].as<std::string>();
            item["description"] = row["description"].is_null() ? "" : row["description"].as<std::string>();
            item["handler"] = row["handler"].is_null() ? "" : row["handler"].as<std::string>();
            item["action"] = row["action"].is_null() ? "" : row["action"].as<std::string>();
            item["processRemark"] = row["process_remark"].is_null() ? "" : row["process_remark"].as<std::string>();
            item["status"] = row["status"].is_null() ? "pending" : row["status"].as<std::string>();
            item["completeTime"] = row["complete_time"].is_null() ? "" : row["complete_time"].as<std::string>();
            item["createdAt"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            exceptionList.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(exceptionList);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Query Exceptions Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 2. 获取异常详情 ====================
crow::response getExceptionDetailFunc(const crow::request& req, pqxx::connection& conn) {
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

        // 解析参数
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

        int exceptionId = std::stoi(idStr);

        pqxx::result res = txn.exec_params(
            "SELECT id, exception_no, exception_type, related_type, related_no, "
            "reporter, report_time, description, handler, action, process_remark, "
            "status, complete_time, created_at, updated_at "
            "FROM exception_event "
            "WHERE id = $1 AND deleted_at IS NULL",
            exceptionId
        );

        if (res.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Exception not found";
            return crow::response(404, result);
        }

        const auto& row = res[0];
        crow::json::wvalue data;
        data["id"] = row["id"].as<int>();
        data["exceptionNo"] = row["exception_no"].is_null() ? "" : row["exception_no"].as<std::string>();
        data["exceptionType"] = row["exception_type"].is_null() ? "" : row["exception_type"].as<std::string>();
        data["relatedType"] = row["related_type"].is_null() ? "" : row["related_type"].as<std::string>();
        data["relatedNo"] = row["related_no"].is_null() ? "" : row["related_no"].as<std::string>();
        data["reporter"] = row["reporter"].is_null() ? "" : row["reporter"].as<std::string>();
        data["reportTime"] = row["report_time"].is_null() ? "" : row["report_time"].as<std::string>();
        data["description"] = row["description"].is_null() ? "" : row["description"].as<std::string>();
        data["handler"] = row["handler"].is_null() ? "" : row["handler"].as<std::string>();
        data["action"] = row["action"].is_null() ? "" : row["action"].as<std::string>();
        data["processRemark"] = row["process_remark"].is_null() ? "" : row["process_remark"].as<std::string>();
        data["status"] = row["status"].is_null() ? "pending" : row["status"].as<std::string>();
        data["completeTime"] = row["complete_time"].is_null() ? "" : row["complete_time"].as<std::string>();

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Exception Detail Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 3. 上报异常 ====================
crow::response addExceptionFunc(const crow::request& req, pqxx::connection& conn) {
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
        int userId = getCurrentUserId(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        // 必填字段校验
        if (!body.has("exceptionType") || !body.has("relatedType") || 
            !body.has("relatedNo") || !body.has("reporter") || !body.has("description")) {
            result["retCode"] = 400;
            result["errorMsg"] = "exceptionType, relatedType, relatedNo, reporter and description are required";
            return crow::response(400, result);
        }

        std::string exceptionNo = generateExceptionNo();
        std::string exceptionType = body["exceptionType"].s();
        std::string relatedType = body["relatedType"].s();
        std::string relatedNo = body["relatedNo"].s();
        std::string reporter = body["reporter"].s();
        std::string description = body["description"].s();

        pqxx::result res = txn.exec_params(
            "INSERT INTO exception_event ("
            "exception_no, exception_type, related_type, related_no, "
            "reporter, description, status, created_by, updated_by, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, 'pending', $7, $8, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) RETURNING id",
            exceptionNo.c_str(),
            exceptionType.c_str(),
            relatedType.c_str(),
            relatedNo.c_str(),
            reporter.c_str(),
            description.c_str(),
            userId,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add exception";
            return crow::response(400, result);
        }

        int exceptionId = res[0]["id"].as<int>();

        result["retCode"] = 200;
        result["msg"] = "Exception reported successfully";
        result["data"]["id"] = exceptionId;
        result["data"]["exceptionNo"] = exceptionNo;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Add Exception Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 4. 处理异常（更新） ====================
crow::response updateExceptionFunc(const crow::request& req, pqxx::connection& conn) {
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
        int userId = getCurrentUserId(txn, username);
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

        int exceptionId = body["id"].i();

        // 检查是否存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM exception_event WHERE id = $1 AND deleted_at IS NULL",
            exceptionId
        );
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Exception not found";
            return crow::response(404, result);
        }

        // 构建动态更新语句
        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        if (body.has("handler")) {
            std::string handler = body["handler"].s();
            updateFields.push_back("handler = $" + std::to_string(paramCounter));
            params.push_back(handler);
            paramCounter++;
        }

        if (body.has("action")) {
            std::string action = body["action"].s();
            updateFields.push_back("action = $" + std::to_string(paramCounter));
            params.push_back(action);
            paramCounter++;
        }

        if (body.has("processRemark")) {
            std::string processRemark = body["processRemark"].s();
            updateFields.push_back("process_remark = $" + std::to_string(paramCounter));
            params.push_back(processRemark);
            paramCounter++;
        }

        if (body.has("status")) {
            std::string status = body["status"].s();
            updateFields.push_back("status = $" + std::to_string(paramCounter));
            params.push_back(status);
            paramCounter++;
            
            // ✅ 修复：如果状态是 completed，单独添加 complete_time 字段
            if (status == "completed") {
                updateFields.push_back("complete_time = CURRENT_TIMESTAMP");
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

        std::string updateSql = "UPDATE exception_event SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(exceptionId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Exception updated successfully";

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Update Exception Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 5. 删除异常（软删除） ====================
crow::response deleteExceptionFunc(const crow::request& req, pqxx::connection& conn) {
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
        int userId = getCurrentUserId(txn, username);
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

        int exceptionId = body["id"].i();

        // 检查是否存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM exception_event WHERE id = $1 AND deleted_at IS NULL",
            exceptionId
        );
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Exception not found";
            return crow::response(404, result);
        }

        // 软删除
        txn.exec_params(
            "UPDATE exception_event SET deleted_at = CURRENT_TIMESTAMP, deleted_by = $1 WHERE id = $2",
            userId, exceptionId
        );

        result["retCode"] = 200;
        result["msg"] = "Exception deleted successfully";

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Delete Exception Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 6. 批量删除异常 ====================
crow::response batchDeleteExceptionFunc(const crow::request& req, pqxx::connection& conn) {
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
        int userId = getCurrentUserId(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        if (!body.has("ids")) {
            result["retCode"] = 400;
            result["errorMsg"] = "ids is required";
            return crow::response(400, result);
        }

        std::vector<int> ids;
        try {
            for (const auto& item : body["ids"]) {
                ids.push_back(item.i());
            }
        } catch (...) {
            // 如果是单个数字，尝试作为单个ID处理
            ids.push_back(body["ids"].i());
        }

        if (ids.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No valid ids provided";
            return crow::response(400, result);
        }

        // 构建批量删除SQL
        std::string deleteSql = "UPDATE exception_event SET deleted_at = CURRENT_TIMESTAMP, deleted_by = $1 WHERE id IN (";
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) deleteSql += ", ";
            deleteSql += "$" + std::to_string(i + 2);
        }
        deleteSql += ") AND deleted_at IS NULL";

        std::vector<std::string> params;
        params.push_back(std::to_string(userId));
        for (int id : ids) {
            params.push_back(std::to_string(id));
        }

        pqxx::result res = txn.exec_params(deleteSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Exceptions deleted successfully";
        result["affectedRows"] = (int)res.affected_rows();

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Batch Delete Exception Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 7. 获取异常统计数据 ====================
crow::response getExceptionStatsFunc(const crow::request& req, pqxx::connection& conn) {
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

        // ========== 1. 状态统计 ==========
        pqxx::result statusRes = txn.exec(
            "SELECT status, COUNT(*) FROM exception_event "
            "WHERE deleted_at IS NULL "
            "GROUP BY status"
        );

        int pending = 0, processing = 0, completed = 0;
        for (const auto& row : statusRes) {
            std::string status = row[0].as<std::string>();
            int count = row[1].as<int>();
            if (status == "pending") pending = count;
            else if (status == "processing") processing = count;
            else if (status == "completed") completed = count;
        }
        int total = pending + processing + completed;

        // ========== 2. 异常率 ==========
        double exceptionRate = 0.0;
        if (total > 0) {
            // ✅ 修复：orders 表没有 deleted_at 字段，直接查询
            pqxx::result orderRes = txn.exec("SELECT COUNT(*) FROM orders");
            int totalOrders = orderRes[0][0].as<int>();
            if (totalOrders > 0) {
                exceptionRate = (double)total / totalOrders * 100;
            }
        }

        // ========== 3. 高频异常类型 Top5 ==========
        pqxx::result typeRes = txn.exec_params(
            "SELECT exception_type, COUNT(*) as cnt FROM exception_event "
            "WHERE deleted_at IS NULL "
            "GROUP BY exception_type "
            "ORDER BY cnt DESC "
            "LIMIT 5"
        );

        crow::json::wvalue::list topTypes;
        for (const auto& row : typeRes) {
            crow::json::wvalue item;
            std::string type = row[0].as<std::string>();
            int count = row[1].as<int>();
            item["type"] = type;
            item["count"] = count;
            item["percent"] = total > 0 ? (double)count / total * 100 : 0;
            topTypes.push_back(std::move(item));
        }

        // ========== 4. 近6个月趋势 ==========
        auto now = std::chrono::system_clock::now();
        std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time_t);
        
        int currentYear = now_tm->tm_year + 1900;
        int currentMonth = now_tm->tm_mon + 1;

        crow::json::wvalue::list months;
        crow::json::wvalue::list values;

        for (int i = 5; i >= 0; i--) {
            int targetMonth = currentMonth - i;
            int targetYear = currentYear;
            if (targetMonth <= 0) {
                targetMonth += 12;
                targetYear--;
            }
            
            std::ostringstream monthLabel;
            monthLabel << targetMonth << "月";
            months.push_back(monthLabel.str());

            std::ostringstream startOss, endOss;
            startOss << targetYear << "-" << std::setw(2) << std::setfill('0') << targetMonth << "-01";
            
            int nextMonth = targetMonth + 1;
            int nextYear = targetYear;
            if (nextMonth > 12) {
                nextMonth = 1;
                nextYear++;
            }
            endOss << nextYear << "-" << std::setw(2) << std::setfill('0') << nextMonth << "-01";

            pqxx::result monthRes = txn.exec_params(
                "SELECT COUNT(*) FROM exception_event "
                "WHERE deleted_at IS NULL "
                "AND created_at >= $1 AND created_at < $2",
                startOss.str().c_str(), endOss.str().c_str()
            );
            int count = monthRes[0][0].as<int>();
            values.push_back(count);
        }

        crow::json::wvalue stats;
        stats["pending"] = pending;
        stats["processing"] = processing;
        stats["completed"] = completed;
        stats["total"] = total;
        stats["exceptionRate"] = exceptionRate;

        crow::json::wvalue trendData;
        trendData["months"] = std::move(months);
        trendData["values"] = std::move(values);

        result["retCode"] = 200;
        result["data"]["stats"] = std::move(stats);
        result["data"]["topExceptionTypes"] = std::move(topTypes);
        result["data"]["trendData"] = std::move(trendData);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Exception Stats Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 注册 API ====================
AUTO_REGISTER_EXCEPTION_API("queryExceptions", queryExceptionsFunc);
AUTO_REGISTER_EXCEPTION_API("getExceptionDetail", getExceptionDetailFunc);
AUTO_REGISTER_EXCEPTION_API("addException", addExceptionFunc);
AUTO_REGISTER_EXCEPTION_API("updateException", updateExceptionFunc);
AUTO_REGISTER_EXCEPTION_API("deleteException", deleteExceptionFunc);
AUTO_REGISTER_EXCEPTION_API("batchDeleteException", batchDeleteExceptionFunc);
AUTO_REGISTER_EXCEPTION_API("getExceptionStats", getExceptionStatsFunc);