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

        // 查询全量数据
        pqxx::result res = txn.exec("SELECT * FROM orders ORDER BY id DESC");

        // 构建返回的JSON数组
        crow::json::wvalue::list order_list;
        
        for (const auto& row : res) {
            crow::json::wvalue order;
            order["id"] = row["id"].as<int>();
            order["type"] = row["type"].c_str();
            order["from"] = row["start_point"].c_str();
            order["to"] = row["end_point"].c_str();
            order["size"] = row["size"].c_str();
            order["containerNo"] = row["container_no"].c_str();
            order["pin"] = row["pin"].c_str();
            order["customerRequest"] = row["customer_note"].c_str();
            order["vessel"] = row["vessel"].c_str();
            order["shippingLine"] = row["shipping_line"].c_str();
            order["eta"] = row["eta"].c_str();
            order["firstAvailable"] = row["first_available"].c_str();
            order["lastFreeDate"] = row["last_free_date"].c_str();
            order["clientName"] = row["client_name"].c_str();
            order["customerAddress"] = row["customer_address"].c_str();
            order["forwarder"] = row["forwarder"].c_str();
            order["weight"] = row["weight"].c_str();
            order["invoice"] = row["invoice_id"].c_str();
            order["noted"] = row["noted"].c_str();
            
            order_list.push_back(std::move(order));
        }

        txn.commit();

        // 返回数据
        result["retCode"] = 200;
        result["rows"] = std::move(order_list);
        result["total"] = order_list.size();

    } catch (const std::exception& e) {
        std::cerr << "Query Orders Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = "Database error";
        return crow::response(500, result);
    }

    return crow::response(200, result);
}

AUTO_REGISTER_ORDER_API("addOrder", addOrderFunc);
AUTO_REGISTER_ORDER_API("queryOrders", queryOrdersFunc);