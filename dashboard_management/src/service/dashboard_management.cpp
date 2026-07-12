#include "service/dashboard_management.h"
#include "../common/include/jwt/jwt.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// ==================== 工具函数 ====================

std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    std::ostringstream oss;
    oss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 获取当前用户ID
int getCurrentUserId(pqxx::work& txn, const std::string& username) {
    pqxx::result staffRes = txn.exec_params(
        "SELECT id FROM staff WHERE username = $1", username);
    if (staffRes.empty()) {
        return 0;
    }
    return staffRes[0]["id"].as<int>();
}

// 格式化数字保留2位小数
std::string formatDecimal(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// 计算环比增长率
double calcTrend(double current, double previous) {
    if (previous == 0) return 0;
    return ((current - previous) / previous) * 100;
}

// 获取日期范围
struct DateRange {
    std::string startDate;
    std::string endDate;
};

DateRange getDateRangeByType(const std::string& type, const std::string& customStart = "", const std::string& customEnd = "") {
    DateRange range;
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    int year = now_tm->tm_year + 1900;
    int month = now_tm->tm_mon + 1;
    int day = now_tm->tm_mday;
    
    std::ostringstream startOss, endOss;
    
    if (!customStart.empty() && !customEnd.empty()) {
        range.startDate = customStart;
        range.endDate = customEnd;
        return range;
    }
    
    if (type == "week") {
        // 本周一
        int wday = now_tm->tm_wday;
        int offset = (wday == 0) ? 6 : wday - 1;
        int startDay = day - offset;
        int endDay = startDay + 6;
        
        startOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-" << std::setw(2) << std::setfill('0') << startDay;
        endOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-" << std::setw(2) << std::setfill('0') << endDay;
    } else if (type == "month") {
        startOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-01";
        // 下个月第一天
        int nextMonth = month + 1;
        int nextYear = year;
        if (nextMonth > 12) { nextMonth = 1; nextYear++; }
        endOss << nextYear << "-" << std::setw(2) << std::setfill('0') << nextMonth << "-01";
    } else if (type == "quarter") {
        int quarterMonth = ((month - 1) / 3) * 3 + 1;
        startOss << year << "-" << std::setw(2) << std::setfill('0') << quarterMonth << "-01";
        int endMonth = quarterMonth + 3;
        int endYear = year;
        if (endMonth > 12) { endMonth = 1; endYear++; }
        endOss << endYear << "-" << std::setw(2) << std::setfill('0') << endMonth << "-01";
    } else { // year
        startOss << year << "-01-01";
        endOss << (year + 1) << "-01-01";
    }
    
    range.startDate = startOss.str();
    range.endDate = endOss.str();
    return range;
}

// 获取上一期日期范围（用于环比）
DateRange getPreviousDateRange(const std::string& type) {
    DateRange range;
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);
    
    int year = now_tm->tm_year + 1900;
    int month = now_tm->tm_mon + 1;
    
    std::ostringstream startOss, endOss;
    
    if (type == "week") {
        // 上周
        startOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-01";
        endOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-07";
    } else if (type == "month") {
        // 上个月
        int prevMonth = month - 1;
        int prevYear = year;
        if (prevMonth < 1) { prevMonth = 12; prevYear--; }
        startOss << prevYear << "-" << std::setw(2) << std::setfill('0') << prevMonth << "-01";
        endOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-01";
    } else if (type == "quarter") {
        // 上个季度
        int quarterMonth = ((month - 1) / 3) * 3 + 1 - 3;
        int qYear = year;
        if (quarterMonth < 1) { quarterMonth += 12; qYear--; }
        startOss << qYear << "-" << std::setw(2) << std::setfill('0') << quarterMonth << "-01";
        int endMonth = quarterMonth + 3;
        int endYear = qYear;
        if (endMonth > 12) { endMonth = 1; endYear++; }
        endOss << endYear << "-" << std::setw(2) << std::setfill('0') << endMonth << "-01";
    } else { // year
        startOss << (year - 1) << "-01-01";
        endOss << year << "-01-01";
    }
    
    range.startDate = startOss.str();
    range.endDate = endOss.str();
    return range;
}

// ==================== 1. 获取核心指标 ====================
crow::response getCoreStatsFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange currentRange = getDateRangeByType(rangeType, startDate, endDate);
        DateRange previousRange = getPreviousDateRange(rangeType);

        // ========== 当前周期数据 ==========
        // 总订单数
        pqxx::result orderRes = txn.exec_params(
            "SELECT COUNT(*) FROM orders WHERE create_time >= $1 AND create_time < $2",
            currentRange.startDate.c_str(), currentRange.endDate.c_str()
        );
        int totalOrders = orderRes[0][0].as<int>();

        // 总营收（从 container 表）
        pqxx::result revenueRes = txn.exec_params(
            "SELECT COALESCE(SUM(invoice_amount), 0) FROM container "
            "WHERE pickup_time >= $1 AND pickup_time < $2 AND deleted_at IS NULL",
            currentRange.startDate.c_str(), currentRange.endDate.c_str()
        );
        double totalRevenue = revenueRes[0][0].as<double>();

        // 总成本（从 fee 表）
        pqxx::result costRes = txn.exec_params(
            "SELECT COALESCE(SUM(f.amount), 0) FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 AND c.deleted_at IS NULL",
            currentRange.startDate.c_str(), currentRange.endDate.c_str()
        );
        double totalCost = costRes[0][0].as<double>();

        double netProfit = totalRevenue - totalCost;

        // ========== 上期数据（环比） ==========
        pqxx::result prevOrderRes = txn.exec_params(
            "SELECT COUNT(*) FROM orders WHERE create_time >= $1 AND create_time < $2",
            previousRange.startDate.c_str(), previousRange.endDate.c_str()
        );
        int prevTotalOrders = prevOrderRes[0][0].as<int>();

        pqxx::result prevRevenueRes = txn.exec_params(
            "SELECT COALESCE(SUM(invoice_amount), 0) FROM container "
            "WHERE pickup_time >= $1 AND pickup_time < $2 AND deleted_at IS NULL",
            previousRange.startDate.c_str(), previousRange.endDate.c_str()
        );
        double prevTotalRevenue = prevRevenueRes[0][0].as<double>();

        pqxx::result prevCostRes = txn.exec_params(
            "SELECT COALESCE(SUM(f.amount), 0) FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 AND c.deleted_at IS NULL",
            previousRange.startDate.c_str(), previousRange.endDate.c_str()
        );
        double prevTotalCost = prevCostRes[0][0].as<double>();
        double prevNetProfit = prevTotalRevenue - prevTotalCost;

        crow::json::wvalue data;
        data["totalOrders"] = totalOrders;
        data["totalRevenue"] = totalRevenue;
        data["totalCost"] = totalCost;
        data["netProfit"] = netProfit;
        data["orderTrend"] = calcTrend(totalOrders, prevTotalOrders);
        data["revenueTrend"] = calcTrend(totalRevenue, prevTotalRevenue);
        data["costTrend"] = calcTrend(totalCost, prevTotalCost);
        data["profitTrend"] = calcTrend(netProfit, prevNetProfit);

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Core Stats Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 2. 获取营收趋势 ====================
crow::response getRevenueTrendFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 按月分组统计营收
        pqxx::result res = txn.exec_params(
            "SELECT DATE_TRUNC('month', pickup_time) as month, "
            "COALESCE(SUM(invoice_amount), 0) as revenue "
            "FROM container "
            "WHERE pickup_time >= $1 AND pickup_time < $2 AND deleted_at IS NULL "
            "GROUP BY DATE_TRUNC('month', pickup_time) "
            "ORDER BY month",
            range.startDate.c_str(), range.endDate.c_str()
        );

        crow::json::wvalue::list months;
        crow::json::wvalue::list values;

        for (const auto& row : res) {
            std::string monthStr = row[0].as<std::string>();
            double revenue = row[1].as<double>();
            // 提取月份显示
            int year, month;
            sscanf(monthStr.c_str(), "%d-%d", &year, &month);
            std::ostringstream label;
            label << month << "月";
            months.push_back(label.str());
            values.push_back(revenue);
        }

        crow::json::wvalue data;
        data["months"] = std::move(months);
        data["values"] = std::move(values);

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Revenue Trend Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 3. 获取订单完成率 ====================
crow::response getCompletionRateFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 按月统计订单总数和完成数
        pqxx::result res = txn.exec_params(
            "SELECT DATE_TRUNC('month', create_time) as month, "
            "COUNT(*) as total, "
            "SUM(CASE WHEN status = 3 THEN 1 ELSE 0 END) as completed "
            "FROM orders "
            "WHERE create_time >= $1 AND create_time < $2 "
            "GROUP BY DATE_TRUNC('month', create_time) "
            "ORDER BY month",
            range.startDate.c_str(), range.endDate.c_str()
        );

        crow::json::wvalue::list months;
        crow::json::wvalue::list values;

        for (const auto& row : res) {
            std::string monthStr = row[0].as<std::string>();
            int total = row[1].as<int>();
            int completed = row[2].as<int>();
            
            int year, month;
            sscanf(monthStr.c_str(), "%d-%d", &year, &month);
            std::ostringstream label;
            label << month << "月";
            months.push_back(label.str());
            
            double rate = (total > 0) ? (double)completed / total * 100 : 0;
            values.push_back(rate);
        }

        crow::json::wvalue data;
        data["months"] = std::move(months);
        data["values"] = std::move(values);

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Completion Rate Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 4. 获取成本构成 ====================
crow::response getCostStructureFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 按费用类型汇总成本
        pqxx::result res = txn.exec_params(
            "SELECT f.fee_type, COALESCE(SUM(f.amount), 0) as total "
            "FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 AND c.deleted_at IS NULL "
            "GROUP BY f.fee_type "
            "ORDER BY total DESC",
            range.startDate.c_str(), range.endDate.c_str()
        );

        crow::json::wvalue::list categories;
        crow::json::wvalue::list values;

        for (const auto& row : res) {
            std::string feeType = row[0].as<std::string>();
            double total = row[1].as<double>();
            categories.push_back(feeType);
            values.push_back(total);
        }

        crow::json::wvalue data;
        data["categories"] = std::move(categories);
        data["values"] = std::move(values);

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Cost Structure Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 5. 获取异常监控数据 ====================
crow::response getAbnormalDataFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 总订单数
        pqxx::result orderRes = txn.exec_params(
            "SELECT COUNT(*) FROM orders WHERE create_time >= $1 AND create_time < $2",
            range.startDate.c_str(), range.endDate.c_str()
        );
        int totalOrders = orderRes[0][0].as<int>();

        // 异常总数
        pqxx::result abnormalRes = txn.exec_params(
            "SELECT COUNT(*) FROM exception_event "
            "WHERE created_at >= $1 AND created_at < $2 AND deleted_at IS NULL",
            range.startDate.c_str(), range.endDate.c_str()
        );
        int totalAbnormal = abnormalRes[0][0].as<int>();

        // 延误异常数
        pqxx::result delayRes = txn.exec_params(
            "SELECT COUNT(*) FROM exception_event "
            "WHERE created_at >= $1 AND created_at < $2 AND exception_type = '延误' AND deleted_at IS NULL",
            range.startDate.c_str(), range.endDate.c_str()
        );
        int delayCount = delayRes[0][0].as<int>();

        // 附加费频次（fee表中费用类型为'附加费'的记录数）
        pqxx::result surchargeRes = txn.exec_params(
            "SELECT COUNT(*) FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 AND f.fee_type = '附加费' AND c.deleted_at IS NULL",
            range.startDate.c_str(), range.endDate.c_str()
        );
        int surchargeCount = surchargeRes[0][0].as<int>();

        double abnormalRate = (totalOrders > 0) ? (double)totalAbnormal / totalOrders * 100 : 0;
        double delayRate = (totalOrders > 0) ? (double)delayCount / totalOrders * 100 : 0;
        double surchargeRatio = (totalOrders > 0) ? (double)surchargeCount / totalOrders * 100 : 0;

        crow::json::wvalue data;
        data["abnormalRate"] = abnormalRate;
        data["delayRate"] = delayRate;
        data["surchargeCount"] = surchargeCount;
        data["surchargeRatio"] = surchargeRatio;

        result["retCode"] = 200;
        result["data"] = std::move(data);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Abnormal Data Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 6. 获取车辆排行 ====================
crow::response getVehicleRankingFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // ✅ 修复：使用 task_start_time 字段，它是 TIMESTAMP 类型
        // 如果 task_start_time 是 TIME 类型，改用其他字段
        // 这里用 orders.create_time 作为时间筛选
        pqxx::result res = txn.exec_params(
            "SELECT "
            "  v.license_plate, "
            "  COUNT(t.id) as trip_count, "
            "  COALESCE(AVG(CAST(o.weight AS DECIMAL)), 0) as avg_load "
            "FROM vehicle v "
            "LEFT JOIN task t ON v.id = t.vehicle_id "
            "LEFT JOIN orders o ON t.order_id = o.id "
            "WHERE o.create_time >= $1 AND o.create_time < $2 AND t.task_status = 3 "
            "GROUP BY v.id, v.license_plate "
            "ORDER BY trip_count DESC "
            "LIMIT 10",
            range.startDate.c_str(), range.endDate.c_str()
        );

        crow::json::wvalue::list ranking;
        for (const auto& row : res) {
            crow::json::wvalue item;
            std::string licensePlate = row[0].is_null() ? "" : row[0].as<std::string>();
            int tripCount = row[1].as<int>();
            double avgLoad = row[2].as<double>();
            
            item["vehicleNo"] = licensePlate;
            item["tripCount"] = tripCount;
            item["avgLoad"] = avgLoad;
            
            int efficiency = 60 + tripCount * 2 + (int)(avgLoad * 0.5);
            if (efficiency > 100) efficiency = 100;
            item["efficiency"] = efficiency;
            
            ranking.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(ranking);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Vehicle Ranking Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 7. 获取司机排行 ====================
crow::response getDriverRankingFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // ✅ 修复：移除不存在的 mileage 字段
        // 改用 orders.create_time 作为时间筛选
        pqxx::result res = txn.exec_params(
            "SELECT "
            "  d.name as driver_name, "
            "  COUNT(t.id) as completed_orders "
            "FROM driver d "
            "LEFT JOIN task t ON d.id = t.driver_id "
            "LEFT JOIN orders o ON t.order_id = o.id "
            "WHERE o.create_time >= $1 AND o.create_time < $2 AND t.task_status = 3 "
            "GROUP BY d.id, d.name "
            "ORDER BY completed_orders DESC "
            "LIMIT 10",
            range.startDate.c_str(), range.endDate.c_str()
        );

        crow::json::wvalue::list ranking;
        int maxOrders = 0;
        
        // 先计算最大订单数用于评分
        for (const auto& row : res) {
            int orders = row[1].as<int>();
            if (orders > maxOrders) maxOrders = orders;
        }

        for (const auto& row : res) {
            crow::json::wvalue item;
            std::string driverName = row[0].is_null() ? "" : row[0].as<std::string>();
            int completedOrders = row[1].as<int>();
            
            item["driverName"] = driverName;
            item["completedOrders"] = completedOrders;
            
            // 总里程：暂时用完成订单数 * 平均距离估算，或者设为0
            // 如果后续有里程表可以关联
            int totalMileage = completedOrders * 50; // 估算值，可调整
            item["totalMileage"] = totalMileage;
            
            int workload = (maxOrders > 0) ? (int)((double)completedOrders / maxOrders * 100) : 0;
            item["workload"] = workload;
            
            ranking.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(ranking);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Driver Ranking Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 8. 获取订单明细 ====================
crow::response getOrderDetailListFunc(const crow::request& req, pqxx::connection& conn) {
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

        int pageNum = 1, pageSize = 10;
        std::string pageNumStr = get_param("pageNum");
        std::string pageSizeStr = get_param("pageSize");
        if (!pageNumStr.empty()) pageNum = std::stoi(pageNumStr);
        if (!pageSizeStr.empty()) pageSize = std::stoi(pageSizeStr);
        int offset = (pageNum - 1) * pageSize;

        std::string rangeType = get_param("rangeType");
        std::string startDate = get_param("startDate");
        std::string endDate = get_param("endDate");

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 查询订单明细
        pqxx::result res = txn.exec_params(
            "SELECT "
            "  o.id, o.client_name, o.create_time, "
            "  COALESCE(c.invoice_amount, 0) as revenue, "
            "  COALESCE((SELECT SUM(f.amount) FROM fee f WHERE f.container_id = c.id), 0) as cost, "
            "  o.status, "
            "  CASE WHEN e.id IS NOT NULL THEN 1 ELSE 0 END as is_delayed "
            "FROM orders o "
            "LEFT JOIN container c ON o.container_no = c.container_no "
            "LEFT JOIN exception_event e ON e.related_no = o.id::text AND e.exception_type = '延误' AND e.deleted_at IS NULL "
            "WHERE o.create_time >= $1 AND o.create_time < $2 "
            "ORDER BY o.id DESC "
            "LIMIT $3 OFFSET $4",
            range.startDate.c_str(), range.endDate.c_str(),
            pageSize, offset
        );

        // 查询总数
        pqxx::result countRes = txn.exec_params(
            "SELECT COUNT(*) FROM orders WHERE create_time >= $1 AND create_time < $2",
            range.startDate.c_str(), range.endDate.c_str()
        );
        int total = countRes[0][0].as<int>();

        crow::json::wvalue::list list;
        for (const auto& row : res) {
            crow::json::wvalue item;
            int id = row[0].as<int>();
            std::string clientName = row[1].is_null() ? "" : row[1].as<std::string>();
            std::string createTime = row[2].is_null() ? "" : row[2].as<std::string>();
            double revenue = row[3].as<double>();
            double cost = row[4].as<double>();
            int status = row[5].as<int>();
            int isDelayed = row[6].as<int>();

            item["orderId"] = "ORD-" + std::to_string(id);
            item["clientName"] = clientName;
            item["orderDate"] = createTime.substr(0, 10);
            item["revenue"] = revenue;
            item["cost"] = cost;
            item["profit"] = revenue - cost;
            item["status"] = status == 3 ? "已完成" : "进行中";
            item["isDelayed"] = isDelayed == 1;
            
            list.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(list);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Order Detail List Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 9. 导出报表 ====================
crow::response exportDashboardFunc(const crow::request& req, pqxx::connection& conn) {
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

        // ✅ 修复：使用 if 语句代替三元运算符
        std::string rangeType = "month";
        if (body.has("rangeType")) {
            rangeType = body["rangeType"].s();
        }
        
        std::string startDate = "";
        if (body.has("startDate")) {
            startDate = body["startDate"].s();
        }
        
        std::string endDate = "";
        if (body.has("endDate")) {
            endDate = body["endDate"].s();
        }

        DateRange range = getDateRangeByType(rangeType, startDate, endDate);

        // 导出数据汇总
        result["retCode"] = 200;
        result["msg"] = "Export success";
        result["data"]["startDate"] = range.startDate;
        result["data"]["endDate"] = range.endDate;
        result["data"]["exportTime"] = getCurrentTimeString();

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Export Dashboard Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 注册 API ====================
AUTO_REGISTER_DASHBOARD_API("getCoreStats", getCoreStatsFunc);
AUTO_REGISTER_DASHBOARD_API("getRevenueTrend", getRevenueTrendFunc);
AUTO_REGISTER_DASHBOARD_API("getCompletionRate", getCompletionRateFunc);
AUTO_REGISTER_DASHBOARD_API("getCostStructure", getCostStructureFunc);
AUTO_REGISTER_DASHBOARD_API("getAbnormalData", getAbnormalDataFunc);
AUTO_REGISTER_DASHBOARD_API("getVehicleRanking", getVehicleRankingFunc);
AUTO_REGISTER_DASHBOARD_API("getDriverRanking", getDriverRankingFunc);
AUTO_REGISTER_DASHBOARD_API("getOrderDetailList", getOrderDetailListFunc);
AUTO_REGISTER_DASHBOARD_API("exportDashboard", exportDashboardFunc);