#include "service/fee_management.h"
#include "../common/include/jwt/jwt.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

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

// ==================== 查询货柜列表（分页） ====================
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

        const std::string username = decoded.get_subject();
        int userId = getCurrentUserId(txn, username);
        if (userId == 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }

        // 解析请求参数
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };

        std::string containerNo = get_param("containerNo");
        std::string orderId = get_param("orderId");
        std::string costStatus = get_param("costStatus");

        int pageNum = 1;
        int pageSize = 10;
        std::string pageNumStr = get_param("pageNum");
        if (!pageNumStr.empty()) pageNum = std::stoi(pageNumStr);
        std::string pageSizeStr = get_param("pageSize");
        if (!pageSizeStr.empty()) pageSize = std::stoi(pageSizeStr);
        int offset = (pageNum - 1) * pageSize;

        // 构建查询条件
        std::string whereClause = "WHERE c.deleted_at IS NULL";
        std::vector<std::string> params;
        int paramCounter = 1;

        if (!containerNo.empty()) {
            whereClause += " AND c.container_no LIKE $" + std::to_string(paramCounter);
            params.push_back("%" + containerNo + "%");
            paramCounter++;
        }

        if (!orderId.empty()) {
            // orderId 是 orders.id，但前端传入的是字符串，尝试转为数字
            try {
                int orderIdInt = std::stoi(orderId);
                whereClause += " AND f.order_id = $" + std::to_string(paramCounter);
                params.push_back(std::to_string(orderIdInt));
                paramCounter++;
            } catch (...) {
                // 如果不是数字，尝试通过 waybill_no 匹配
                whereClause += " AND (f.order_id IN (SELECT id FROM orders WHERE waybill_no LIKE $" + 
                               std::to_string(paramCounter) + "))";
                params.push_back("%" + orderId + "%");
                paramCounter++;
            }
        }

        if (!costStatus.empty()) {
            whereClause += " AND c.cost_status = $" + std::to_string(paramCounter);
            params.push_back(costStatus);
            paramCounter++;
        }

        // 查询总数
        std::string countQuery = "SELECT COUNT(DISTINCT c.id) FROM container c "
                                 "LEFT JOIN fee f ON c.id = f.container_id " +
                                 whereClause;
        
        pqxx::result countRes;
        if (params.empty()) {
            countRes = txn.exec(countQuery);
        } else {
            countRes = txn.exec_params(countQuery, pqxx::prepare::make_dynamic_params(params));
        }
        int total = countRes[0][0].as<int>();

        // 查询货柜列表（带费用汇总）
        std::string query = 
            "SELECT "
            "  c.id, "
            "  c.container_no, "
            "  c.waybill_no, "
            "  c.invoice_amount, "
            "  c.cost_status, "
            "  COALESCE(SUM(f.amount), 0) AS total_cost "
            "FROM container c "
            "LEFT JOIN fee f ON c.id = f.container_id " +
            whereClause +
            " GROUP BY c.id, c.container_no, c.waybill_no, c.invoice_amount, c.cost_status "
            "ORDER BY c.id DESC "
            "LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));

        pqxx::result res = txn.exec_params(query, pqxx::prepare::make_dynamic_params(params));

        crow::json::wvalue::list containerList;
        for (const auto& row : res) {
            crow::json::wvalue item;
            int id = row["id"].as<int>();
            double invoiceAmount = row["invoice_amount"].is_null() ? 0 : row["invoice_amount"].as<double>();
            double totalCost = row["total_cost"].as<double>();
            double profit = invoiceAmount - totalCost;

            item["id"] = id;
            item["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].as<std::string>();
            item["orderId"] = row["waybill_no"].is_null() ? "" : row["waybill_no"].as<std::string>();
            item["invoiceAmount"] = invoiceAmount;
            item["totalCost"] = totalCost;
            item["profit"] = profit;
            item["costStatus"] = row["cost_status"].is_null() ? "待核算" : row["cost_status"].as<std::string>();
            
            containerList.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"] = std::move(containerList);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Query Containers Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 获取货柜费用明细 ====================
crow::response getContainerCostFunc(const crow::request& req, pqxx::connection& conn) {
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

        std::string containerIdStr = get_param("containerId");
        if (containerIdStr.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "containerId is required";
            return crow::response(400, result);
        }

        int containerId = std::stoi(containerIdStr);

        // 查询货柜基本信息
        pqxx::result containerRes = txn.exec_params(
            "SELECT id, container_no, waybill_no, invoice_amount, cost_status "
            "FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );

        if (containerRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Container not found";
            return crow::response(404, result);
        }

        const auto& cRow = containerRes[0];
        crow::json::wvalue containerInfo;
        containerInfo["id"] = cRow["id"].as<int>();
        containerInfo["containerNo"] = cRow["container_no"].is_null() ? "" : cRow["container_no"].as<std::string>();
        containerInfo["orderId"] = cRow["waybill_no"].is_null() ? "" : cRow["waybill_no"].as<std::string>();
        containerInfo["invoiceAmount"] = cRow["invoice_amount"].is_null() ? 0 : cRow["invoice_amount"].as<double>();
        containerInfo["costStatus"] = cRow["cost_status"].is_null() ? "待核算" : cRow["cost_status"].as<std::string>();

        // 查询费用明细
        pqxx::result feeRes = txn.exec_params(
            "SELECT id, fee_type, fee_name, amount, fee_note, img_id, created_at "
            "FROM fee WHERE container_id = $1 ORDER BY id ASC",
            containerId
        );

        crow::json::wvalue::list costItems;
        for (const auto& row : feeRes) {
            crow::json::wvalue item;
            item["id"] = row["id"].as<int>();
            item["costType"] = row["fee_type"].is_null() ? "" : row["fee_type"].as<std::string>();
            item["costName"] = row["fee_name"].is_null() ? "" : row["fee_name"].as<std::string>();
            item["amount"] = row["amount"].is_null() ? 0 : row["amount"].as<double>();
            item["remark"] = row["fee_note"].is_null() ? "" : row["fee_note"].as<std::string>();
            item["imgId"] = row["img_id"].is_null() ? 0 : row["img_id"].as<int>();
            item["createdAt"] = row["created_at"].is_null() ? "" : row["created_at"].as<std::string>();
            costItems.push_back(std::move(item));
        }

        result["retCode"] = 200;
        result["data"]["container"] = std::move(containerInfo);
        result["data"]["costItems"] = std::move(costItems);

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Get Container Cost Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 添加费用项 ====================
crow::response addContainerCostFunc(const crow::request& req, pqxx::connection& conn) {
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
        if (!body.has("containerId") || !body.has("orderId") || 
            !body.has("costType") || !body.has("costName") || !body.has("amount")) {
            result["retCode"] = 400;
            result["errorMsg"] = "containerId, orderId, costType, costName and amount are required";
            return crow::response(400, result);
        }

        int containerId = body["containerId"].i();
        int orderId = body["orderId"].i();
        std::string feeType = body["costType"].s();
        std::string feeName = body["costName"].s();
        double amount = body["amount"].d();
        
        // ✅ 修复：使用 if 语句代替三元运算符
        std::string feeNote = "";
        if (body.has("remark")) {
            feeNote = body["remark"].s();
        }
        
        int imgId = 0;
        if (body.has("imgId")) {
            imgId = body["imgId"].i();
        }

        // 校验金额
        if (amount < 0) {
            result["retCode"] = 400;
            result["errorMsg"] = "Amount cannot be negative";
            return crow::response(400, result);
        }

        // 检查货柜是否存在
        pqxx::result containerCheck = txn.exec_params(
            "SELECT id, cost_status FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );
        if (containerCheck.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Container not found";
            return crow::response(404, result);
        }

        // 检查是否已核算
        std::string costStatus = containerCheck[0]["cost_status"].is_null() ? "待核算" : containerCheck[0]["cost_status"].as<std::string>();
        if (costStatus == "已核算") {
            result["retCode"] = 403;
            result["errorMsg"] = "Container has been audited, cannot add cost";
            return crow::response(403, result);
        }

        // 检查订单是否存在
        pqxx::result orderCheck = txn.exec_params(
            "SELECT id FROM orders WHERE id = $1",
            orderId
        );
        if (orderCheck.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Order not found";
            return crow::response(404, result);
        }

        // 插入费用项
        pqxx::result res = txn.exec_params(
            "INSERT INTO fee ("
            "container_id, order_id, fee_type, fee_name, amount, fee_note, img_id, "
            "created_by, updated_by, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP) RETURNING id",
            containerId,
            orderId,
            feeType.c_str(),
            feeName.c_str(),
            amount,
            feeNote.empty() ? nullptr : feeNote.c_str(),
            imgId == 0 ? nullptr : &imgId,
            userId,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to add cost item";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["msg"] = "Cost item added successfully";
        result["data"]["id"] = res[0]["id"].as<int>();

        txn.commit();
        return crow::response(200, result);

    } catch (const pqxx::unique_violation& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Duplicate cost name for this container and order";
        return crow::response(400, result);
    } catch (const std::exception& e) {
        std::cerr << "Add Container Cost Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 更新费用项 ====================
crow::response updateContainerCostFunc(const crow::request& req, pqxx::connection& conn) {
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

        int feeId = body["id"].i();

        // 获取费用项关联的 container_id
        pqxx::result feeCheck = txn.exec_params(
            "SELECT container_id FROM fee WHERE id = $1",
            feeId
        );
        if (feeCheck.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Cost item not found";
            return crow::response(404, result);
        }

        int containerId = feeCheck[0]["container_id"].as<int>();

        // 检查货柜是否已核算
        pqxx::result containerCheck = txn.exec_params(
            "SELECT cost_status FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );
        if (!containerCheck.empty()) {
            std::string costStatus = containerCheck[0]["cost_status"].is_null() ? "待核算" : containerCheck[0]["cost_status"].as<std::string>();
            if (costStatus == "已核算") {
                result["retCode"] = 403;
                result["errorMsg"] = "Container has been audited, cannot update cost";
                return crow::response(403, result);
            }
        }

        // 构建动态更新语句
        std::vector<std::string> updateFields;
        std::vector<std::string> params;
        int paramCounter = 1;

        if (body.has("costType")) {
            std::string feeType = body["costType"].s();
            updateFields.push_back("fee_type = $" + std::to_string(paramCounter));
            params.push_back(feeType);
            paramCounter++;
        }

        if (body.has("costName")) {
            std::string feeName = body["costName"].s();
            updateFields.push_back("fee_name = $" + std::to_string(paramCounter));
            params.push_back(feeName);
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

        // ✅ 修复：使用 if 语句代替三元运算符
        if (body.has("remark")) {
            std::string feeNote = body["remark"].s();
            updateFields.push_back("fee_note = $" + std::to_string(paramCounter));
            params.push_back(feeNote);
            paramCounter++;
        }

        if (body.has("imgId")) {
            int imgId = body["imgId"].i();
            updateFields.push_back("img_id = $" + std::to_string(paramCounter));
            params.push_back(std::to_string(imgId));
            paramCounter++;
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

        std::string updateSql = "UPDATE fee SET ";
        for (size_t i = 0; i < updateFields.size(); i++) {
            if (i > 0) updateSql += ", ";
            updateSql += updateFields[i];
        }
        updateSql += " WHERE id = $" + std::to_string(paramCounter);
        params.push_back(std::to_string(feeId));

        txn.exec_params(updateSql, pqxx::prepare::make_dynamic_params(params));

        result["retCode"] = 200;
        result["msg"] = "Cost item updated successfully";

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Update Container Cost Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 删除费用项 ====================
crow::response deleteContainerCostFunc(const crow::request& req, pqxx::connection& conn) {
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

        int feeId = body["id"].i();

        // 获取费用项关联的 container_id
        pqxx::result feeCheck = txn.exec_params(
            "SELECT container_id FROM fee WHERE id = $1",
            feeId
        );
        if (feeCheck.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Cost item not found";
            return crow::response(404, result);
        }

        int containerId = feeCheck[0]["container_id"].as<int>();

        // 检查货柜是否已核算
        pqxx::result containerCheck = txn.exec_params(
            "SELECT cost_status FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );
        if (!containerCheck.empty()) {
            std::string costStatus = containerCheck[0]["cost_status"].is_null() ? "待核算" : containerCheck[0]["cost_status"].as<std::string>();
            if (costStatus == "已核算") {
                result["retCode"] = 403;
                result["errorMsg"] = "Container has been audited, cannot delete cost";
                return crow::response(403, result);
            }
        }

        txn.exec_params("DELETE FROM fee WHERE id = $1", feeId);

        result["retCode"] = 200;
        result["msg"] = "Cost item deleted successfully";

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Delete Container Cost Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 确认核算 ====================
crow::response auditContainerFunc(const crow::request& req, pqxx::connection& conn) {
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

        if (!body.has("containerId")) {
            result["retCode"] = 400;
            result["errorMsg"] = "containerId is required";
            return crow::response(400, result);
        }

        int containerId = body["containerId"].i();

        // 检查货柜是否存在
        pqxx::result containerCheck = txn.exec_params(
            "SELECT id, cost_status FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );
        if (containerCheck.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Container not found";
            return crow::response(404, result);
        }

        std::string currentStatus = containerCheck[0]["cost_status"].is_null() ? "待核算" : containerCheck[0]["cost_status"].as<std::string>();
        if (currentStatus == "已核算") {
            result["retCode"] = 400;
            result["errorMsg"] = "Container has already been audited";
            return crow::response(400, result);
        }

        // 更新为已核算
        txn.exec_params(
            "UPDATE container SET cost_status = '已核算', updated_at = CURRENT_TIMESTAMP WHERE id = $1",
            containerId
        );

        result["retCode"] = 200;
        result["msg"] = "Container audited successfully";

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Audit Container Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新货柜发票金额 ====================
crow::response updateContainerInvoiceFunc(const crow::request& req, pqxx::connection& conn) {
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

        if (!body.has("containerId") || !body.has("invoiceAmount")) {
            result["retCode"] = 400;
            result["errorMsg"] = "containerId and invoiceAmount are required";
            return crow::response(400, result);
        }

        int containerId = body["containerId"].i();
        double invoiceAmount = body["invoiceAmount"].d();
        
        std::string remark = "";
        if (body.has("remark")) {
            remark = body["remark"].s();
        }

        // 检查货柜是否存在
        pqxx::result checkRes = txn.exec_params(
            "SELECT id FROM container WHERE id = $1 AND deleted_at IS NULL",
            containerId
        );
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Container not found";
            return crow::response(404, result);
        }

        // 更新发票金额
        txn.exec_params(
            "UPDATE container SET invoice_amount = $1, updated_at = CURRENT_TIMESTAMP WHERE id = $2",
            invoiceAmount, containerId
        );

        result["retCode"] = 200;
        result["msg"] = "Invoice amount updated successfully";
        result["data"]["containerId"] = containerId;
        result["data"]["invoiceAmount"] = invoiceAmount;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Update Container Invoice Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 注册 API ====================
AUTO_REGISTER_FEE_API("queryContainers", queryContainersFunc);
AUTO_REGISTER_FEE_API("getContainerCost", getContainerCostFunc);
AUTO_REGISTER_FEE_API("addContainerCost", addContainerCostFunc);
AUTO_REGISTER_FEE_API("updateContainerCost", updateContainerCostFunc);
AUTO_REGISTER_FEE_API("deleteContainerCost", deleteContainerCostFunc);
AUTO_REGISTER_FEE_API("auditContainer", auditContainerFunc);
AUTO_REGISTER_FEE_API("updateContainerInvoice", updateContainerInvoiceFunc);
