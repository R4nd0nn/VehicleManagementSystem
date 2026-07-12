#include "service/finance_management.h"
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

// 获取当前用户ID
int getCurrentUserId(pqxx::work& txn, const std::string& username) {
    pqxx::result staffRes = txn.exec_params(
        "SELECT id FROM staff WHERE username = $1", username);
    if (staffRes.empty()) {
        return 0;
    }
    return staffRes[0]["id"].as<int>();
}

// 格式化月份
std::string formatMonth(int year, int month) {
    std::ostringstream oss;
    oss << year << "-" << std::setw(2) << std::setfill('0') << month;
    return oss.str();
}

// 解析月份
std::pair<int, int> parseMonth(const std::string& monthStr) {
    int year = 0, month = 0;
    sscanf(monthStr.c_str(), "%d-%d", &year, &month);
    return {year, month};
}

// 获取月份的第一天和最后一天
std::pair<std::string, std::string> getMonthDateRange(const std::string& monthStr) {
    auto [year, month] = parseMonth(monthStr);
    
    std::ostringstream startOss;
    startOss << year << "-" << std::setw(2) << std::setfill('0') << month << "-01";
    std::string startDate = startOss.str();
    
    // 计算下个月的第一天，然后减一天
    int nextYear = year;
    int nextMonth = month + 1;
    if (nextMonth > 12) {
        nextMonth = 1;
        nextYear++;
    }
    std::ostringstream endOss;
    endOss << nextYear << "-" << std::setw(2) << std::setfill('0') << nextMonth << "-01";
    std::string endDate = endOss.str();
    
    return {startDate, endDate};
}


// ==================== 获取月度财务报表 ====================
crow::response getMonthlyReportFunc(const crow::request& req, pqxx::connection& conn) {
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
        int userId = getCurrentUserId(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        // 获取月份参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::string monthStr = get_param("month");
        if (monthStr.empty()) {
            // 默认当前月份
            auto now = std::chrono::system_clock::now();
            std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
            std::tm* now_tm = std::localtime(&now_time_t);
            monthStr = formatMonth(now_tm->tm_year + 1900, now_tm->tm_mon + 1);
        }
        
        auto [startDate, endDate] = getMonthDateRange(monthStr);
        
        crow::json::wvalue reportData;
        crow::json::wvalue::list incomeRows;
        crow::json::wvalue::list expenseRows;
        
        // ========== 1. 收入数据 ==========
        // 1.1 本月柜数
        pqxx::result containerCountRes = txn.exec_params(
            "SELECT COUNT(*) FROM container "
            "WHERE pickup_time >= $1 AND pickup_time < $2 AND deleted_at IS NULL",
            startDate.c_str(), endDate.c_str()
        );
        int containerCount = containerCountRes[0][0].as<int>();
        
        // 1.2 本月发票总额
        pqxx::result invoiceRes = txn.exec_params(
            "SELECT COALESCE(SUM(invoice_amount), 0) FROM container "
            "WHERE pickup_time >= $1 AND pickup_time < $2 AND deleted_at IS NULL",
            startDate.c_str(), endDate.c_str()
        );
        double invoiceTotal = invoiceRes[0][0].as<double>();
        
        // 构建收入行
        crow::json::wvalue incomeRow1;
        incomeRow1["label"] = "柜数";
        incomeRow1["amount"] = containerCount;
        incomeRow1["remark"] = monthStr + " 实际操作柜数";
        incomeRows.push_back(std::move(incomeRow1));
        
        crow::json::wvalue incomeRow2;
        incomeRow2["label"] = "发票额";
        incomeRow2["amount"] = invoiceTotal;
        incomeRow2["remark"] = monthStr + " 月发票总额";
        incomeRows.push_back(std::move(incomeRow2));
        
        reportData["incomeRows"] = std::move(incomeRows);
        reportData["totalIncome"] = invoiceTotal;
        
        // ========== 2. 固定支出（从 fee 表获取） ==========
        // 2.1 码头费用（fee_type = '码头费用'）
        pqxx::result wharfFeeRes = txn.exec_params(
            "SELECT COALESCE(SUM(f.amount), 0) FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 "
            "AND f.fee_type = '码头费用' AND c.deleted_at IS NULL",
            startDate.c_str(), endDate.c_str()
        );
        double wharfFee = wharfFeeRes[0][0].as<double>();
        
        // 2.2 工资/人工费用（fee_type = '人工费用'）
        pqxx::result salaryRes = txn.exec_params(
            "SELECT COALESCE(SUM(f.amount), 0) FROM fee f "
            "JOIN container c ON f.container_id = c.id "
            "WHERE c.pickup_time >= $1 AND c.pickup_time < $2 "
            "AND f.fee_type = '人工费用' AND c.deleted_at IS NULL",
            startDate.c_str(), endDate.c_str()
        );
        double salary = salaryRes[0][0].as<double>();
        
        // 构建固定支出行
        crow::json::wvalue expenseRow1;
        expenseRow1["id"] = "fixed_wharf";
        expenseRow1["label"] = "码头费用";
        expenseRow1["amount"] = wharfFee;
        expenseRow1["remark"] = monthStr + " 码头和堆场费用";
        expenseRow1["isFixed"] = true;
        expenseRows.push_back(std::move(expenseRow1));
        
        crow::json::wvalue expenseRow2;
        expenseRow2["id"] = "fixed_salary";
        expenseRow2["label"] = "工资";
        expenseRow2["amount"] = salary;
        expenseRow2["remark"] = monthStr + " 司机+办公室工资";
        expenseRow2["isFixed"] = true;
        expenseRows.push_back(std::move(expenseRow2));
        
        // ========== 3. 自定义支出（从 monthly_expense_detail 读取） ==========
        pqxx::result customExpenseRes = txn.exec_params(
            "SELECT id, label, amount, remark, is_fixed "
            "FROM monthly_expense_detail "
            "WHERE expense_month = $1 AND is_fixed = false "
            "ORDER BY id ASC",
            monthStr.c_str()
        );
        
        for (const auto& row : customExpenseRes) {
            crow::json::wvalue expenseRow;
            expenseRow["id"] = row["id"].as<int>();
            expenseRow["label"] = row["label"].as<std::string>();
            expenseRow["amount"] = row["amount"].as<double>();
            expenseRow["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
            expenseRow["isFixed"] = false;
            expenseRows.push_back(std::move(expenseRow));
        }
        
        reportData["expenseRows"] = std::move(expenseRows);
        
        // 计算总支出
        double totalExpense = wharfFee + salary;
        for (const auto& row : customExpenseRes) {
            totalExpense += row["amount"].as<double>();
        }
        reportData["totalExpense"] = totalExpense;
        
        // ========== 4. 盈亏 ==========
        double profit = invoiceTotal - totalExpense;
        reportData["profit"] = profit;
        
        // ========== 5. 汇总数据（顶部卡片） ==========
        crow::json::wvalue summary;
        summary["totalContainers"] = containerCount;
        summary["totalInvoice"] = invoiceTotal;
        summary["totalCost"] = totalExpense;
        summary["profit"] = profit;
        reportData["summary"] = std::move(summary);
        
        result["retCode"] = 200;
        result["data"] = std::move(reportData);
        result["month"] = monthStr;
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Get Monthly Report Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 添加自定义支出 ====================
crow::response addExpenseItemFunc(const crow::request& req, pqxx::connection& conn) {
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
        if (!body.has("month") || !body.has("label") || !body.has("amount")) {
            result["retCode"] = 400;
            result["errorMsg"] = "month, label and amount are required";
            return crow::response(400, result);
        }

        std::string month = body["month"].s();
        std::string label = body["label"].s();
        double amount = body["amount"].d();
        
        // ✅ 修复：使用 if 语句代替三元运算符
        std::string remark = "";
        if (body.has("remark")) {
            remark = body["remark"].s();
        }
        
        bool isFixed = false;
        if (body.has("isFixed")) {
            isFixed = body["isFixed"].b();
        }

        // 校验金额
        if (amount < 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "Amount cannot be negative";
            return crow::response(400, result);
        }

        // 插入数据
        pqxx::result res = txn.exec_params(
            "INSERT INTO monthly_expense_detail ("
            "expense_month, label, amount, remark, is_fixed, created_by, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) RETURNING id",
            month.c_str(),
            label.c_str(),
            amount,
            remark.empty() ? nullptr : remark.c_str(),
            isFixed,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add expense item";
            return crow::response(400, result);
        }

        int expenseId = res[0]["id"].as<int>();

        result["retCode"] = 200;
        result["msg"] = "Expense item added successfully";
        result["data"]["id"] = expenseId;
        result["data"]["label"] = label;
        result["data"]["amount"] = amount;
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Add Expense Item Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 更新自定义支出 ====================
crow::response updateExpenseItemFunc(const crow::request& req, pqxx::connection& conn) {
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

        int expenseId = body["id"].i();

        // 检查是否存在且不是固定项
        pqxx::result checkRes = txn.exec_params(
            "SELECT id, is_fixed FROM monthly_expense_detail WHERE id = $1",
            expenseId
        );
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Expense item not found";
            return crow::response(404, result);
        }
        
        bool isFixed = checkRes[0]["is_fixed"].as<bool>();
        if (isFixed) {
            result["retCode"] = 403;
            result["errorMsg"] = "Cannot update fixed expense item";
            return crow::response(403, result);
        }

        // 构建动态更新语句
        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        if (body.has("label")) {
            std::string label = body["label"].s();
            updateFields.push_back("label = $" + std::to_string(paramCounter));
            params.push_back(label);
            paramCounter++;
        }
        
        if (body.has("amount")) {
            double amount = body["amount"].d();
            if (amount < 0) {
                result["retCode"] = 400;
                result["errorMsg"] = "Amount cannot be negative";
                return crow::response(400, result);
            }
            updateFields.push_back("amount = $" + std::to_string(paramCounter));
            params.push_back(std::to_string(amount));
            paramCounter++;
        }
        
        if (body.has("remark")) {
            std::string remark = body["remark"].s();
            updateFields.push_back("remark = $" + std::to_string(paramCounter));
            params.push_back(remark.empty() ? "" : remark);
            paramCounter++;
        }

        if (updateFields.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "No fields to update";
            return crow::response(400, result);
        }

        updateFields.push_back("updated_at = CURRENT_TIMESTAMP");

        std::string updateSql = "UPDATE monthly_expense_detail SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(expenseId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Expense item updated successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Update Expense Item Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 删除自定义支出 ====================
crow::response deleteExpenseItemFunc(const crow::request& req, pqxx::connection& conn) {
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

        int expenseId = body["id"].i();

        // 检查是否存在且不是固定项
        pqxx::result checkRes = txn.exec_params(
            "SELECT id, is_fixed FROM monthly_expense_detail WHERE id = $1",
            expenseId
        );
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Expense item not found";
            return crow::response(404, result);
        }
        
        bool isFixed = checkRes[0]["is_fixed"].as<bool>();
        if (isFixed) {
            result["retCode"] = 403;
            result["errorMsg"] = "Cannot delete fixed expense item";
            return crow::response(403, result);
        }

        txn.exec_params("DELETE FROM monthly_expense_detail WHERE id = $1", expenseId);

        result["retCode"] = 200;
        result["msg"] = "Expense item deleted successfully";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        std::cerr << "Delete Expense Item Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 获取支出项列表（分页） ====================
crow::response queryExpenseItemsFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        std::string month = get_param("month");
        if (month.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "month is required";
            return crow::response(400, result);
        }

        int pageNum = 1;
        int pageSize = 20;
        std::string pageNumStr = get_param("pageNum");
        if (!pageNumStr.empty()) pageNum = std::stoi(pageNumStr);
        std::string pageSizeStr = get_param("pageSize");
        if (!pageSizeStr.empty()) pageSize = std::stoi(pageSizeStr);
        int offset = (pageNum - 1) * pageSize;

        // 查询
        pqxx::result res = txn.exec_params(
            "SELECT id, expense_month, label, amount, remark, is_fixed, created_by, created_at, updated_at "
            "FROM monthly_expense_detail "
            "WHERE expense_month = $1 AND is_fixed = false "
            "ORDER BY id DESC "
            "LIMIT $2 OFFSET $3",
            month.c_str(), pageSize, offset
        );

        // 总数
        pqxx::result countRes = txn.exec_params(
            "SELECT COUNT(*) FROM monthly_expense_detail WHERE expense_month = $1 AND is_fixed = false",
            month.c_str()
        );
        int total = countRes[0][0].as<int>();

        crow::json::wvalue::list expenseList;
        for (const auto& row : res) {
            crow::json::wvalue item;
            item["id"] = row["id"].as<int>();
            item["month"] = row["expense_month"].as<std::string>();
            item["label"] = row["label"].as<std::string>();
            item["amount"] = row["amount"].as<double>();
            item["remark"] = row["remark"].is_null() ? "" : row["remark"].as<std::string>();
            item["isFixed"] = row["is_fixed"].as<bool>();
            item["createdAt"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            expenseList.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(expenseList);
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


// ==================== 注册 API ====================
AUTO_REGISTER_FINANCE_API("getMonthlyReport", getMonthlyReportFunc);
AUTO_REGISTER_FINANCE_API("addExpenseItem", addExpenseItemFunc);
AUTO_REGISTER_FINANCE_API("updateExpenseItem", updateExpenseItemFunc);
AUTO_REGISTER_FINANCE_API("deleteExpenseItem", deleteExpenseItemFunc);
AUTO_REGISTER_FINANCE_API("queryExpenseItems", queryExpenseItemsFunc);