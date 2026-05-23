#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "service/container_management.h"
#include "../common/include/jwt/jwt.h"

// 解析时间字符串（格式：2026-05-20 09:30:00）
std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp) {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        // 尝试只解析日期
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

// 计算时间字符串和当前时间的差值（通过字符串）
int calculateDaysDiffWithNow(const std::string& startStr) {
    return calculateDaysDiff(startStr, getCurrentTimeString());
}

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
                abnormal_status, 
                abnormal_desc, 
                waybill_no, 
                port, 
                free_expired_time, 
                created_at, 
                updated_at
            FROM container 
            WHERE 1=1
        )";
        
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        // 解析 GET 请求参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        // 定义所有支持的参数
        std::vector<std::string> paramKeys = {
            "id", "container_no", "status", "waybill_no", "port", "abnormal_status"
        };
        
        // 获取参数值
        std::unordered_map<std::string, std::string> queryParams;
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        // ========== 支持的筛选字段 ==========
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
        
        // 添加排序
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
            container["abnormal_status"] = row["abnormal_status"].is_null() ? "" : row["abnormal_status"].as<std::string>();
            container["abnormal_desc"] = row["abnormal_desc"].is_null() ? "" : row["abnormal_desc"].as<std::string>();
            container["waybill_no"] = row["waybill_no"].is_null() ? "" : row["waybill_no"].as<std::string>();
            container["port"] = row["port"].is_null() ? "" : row["port"].as<std::string>();
            container["free_expired_time"] = row["free_expired_time"].is_null() ? 0 : row["free_expired_time"].as<int>();
            container["created_at"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            container["updated_at"] = row["updated_at"].is_null() ? "" : row["updated_at"].as<std::string>();
            
            // 计算已用天数
            int usedDays = 0;
            if (!row["pickup_time"].is_null()) {
                std::string pickupTimeStr = row["pickup_time"].as<std::string>();
                
                if (!row["return_time"].is_null()) {
                    // 有还箱时间：还箱时间 - 提箱时间
                    std::string returnTimeStr = row["return_time"].as<std::string>();
                    usedDays = calculateDaysDiff(pickupTimeStr, returnTimeStr);
                } else {
                    // 无还箱时间：当前时间 - 提箱时间
                    usedDays = calculateDaysDiff(pickupTimeStr, getCurrentTimeString());
                }
            }
            container["used_days"] = usedDays;
            
            containerList.push_back(std::move(container));
        }
        
        result["retCode"] = 200;
        result["data"] = std::move(containerList);
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }
}

AUTO_REGISTER_CONTAINER_API("queryContainers", queryContainersFunc);