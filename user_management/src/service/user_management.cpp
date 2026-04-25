#include "service/user_management.h"
#include "../common/include/jwt/jwt.h"

// 登录接口
crow::response userLoginFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("password")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username and password required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string password = body["password"].s();

    auto token = jwt::create()
        .set_issuer("user_management")
        .set_subject(username) 
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(12))
        .sign(jwt::algorithm::hs256{ "user_management" });

    try {
        pqxx::work txn(conn);
        
        // 查询用户
        pqxx::result res = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1 AND password = $2",
            username, password
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid username or password";
            return crow::response(400, result);
        }

        result["retCode"] = 200;
        result["token"] = token;
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 忘记密码接口
crow::response forgetPasswordFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("phoneNum")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username and phoneNum required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string phoneNum = body["phoneNum"].s();
    
    try {
        pqxx::work txn(conn);
        
        // 验证用户名和手机号
        pqxx::result res = txn.exec_params(
            "SELECT name FROM staff WHERE name = $1 AND phone_no = $2",
            username, phoneNum
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid username or phone number";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["errorMsg"] = "";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 重置密码接口
crow::response resetPasswordFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("newPassword") || !body.has("newPasswordConfirm")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username, newPassword and newPasswordConfirm required";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string newPassword = body["newPassword"].s();
    std::string newPasswordConfirm = body["newPasswordConfirm"].s();
    
    // 验证两次密码是否一致
    if (newPassword != newPasswordConfirm) {
        result["retCode"] = 400;
        result["errorMsg"] = "Passwords do not match";
        return crow::response(400, result);
    }
    
    try {
        pqxx::work txn(conn);
        
        // 更新密码
        pqxx::result res = txn.exec_params(
            "UPDATE staff SET password = $1 WHERE name = $2 RETURNING name",
            newPassword, username
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        result["retCode"] = 200;
        result["errorMsg"] = "";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 查询用户信息接口
crow::response queryUserInfoFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        pqxx::result res = txn.exec_params(
        "SELECT id, role, name, gender, age, birthday, position, email_address, phone_no FROM staff WHERE username = $1", username);

        if (res.empty()) { 
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        const auto& row = res[0];
        
        // 🔑 关键：创建一个 staff 对象
        crow::json::wvalue staff;
        staff["id"] = row["id"].as<int>();
        staff["role"] = row["role"].as<int>();
        staff["name"] = row["name"].as<std::string>();
        staff["gender"] = row["gender"].as<int>();
        staff["age"] = row["age"].as<int>();
        staff["birthday"] = row["birthday"].as<std::string>();
        staff["position"] = row["position"].as<std::string>();
        staff["email_address"] = row["email_address"].as<std::string>();
        staff["phone_no"] = row["phone_no"].as<std::string>(); 
        
        // 将 staff 对象放入 result
        result["retCode"] = 200;
        result["staff"] = std::move(staff);  // 或者直接 result["staff"] = staff;
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

crow::response getUserListFunc(const crow::request& req, pqxx::connection& conn) {
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
        
        pqxx::result res = txn.exec_params(
        "SELECT id, role, name, gender, age, birthday, position, email_address, phone_no FROM staff");

        crow::json::wvalue::list staffList; 
        
        for (const auto& row : res) {
            crow::json::wvalue staff;
            staff["id"] = row["id"].as<int>();
            staff["role"] = row["role"].is_null() ? -1 : row["role"].as<int>();
            staff["name"] = row["name"].is_null() ? "" : row["name"].as<std::string>();
            staff["gender"] = row["gender"].is_null() ? -1 : row["gender"].as<int>();
            staff["age"] = row["age"].is_null() ? -1 : row["age"].as<int>();
            
            staff["birthday"] = row["birthday"].is_null() ? "" : row["birthday"].as<std::string>();
            staff["position"] = row["position"].is_null() ? "" : row["position"].as<std::string>();
            staff["email_address"] = row["email_address"].is_null() ? "" : row["email_address"].as<std::string>();
            staff["phone_no"] = row["phone_no"].is_null() ? "" : row["phone_no"].as<std::string>(); 
            
            // 将单个 staff 对象加入列表
            staffList.push_back(std::move(staff));
        }
        
        // 5. 设置返回结果
        result["retCode"] = 200;
        result["data"] = std::move(staffList);
        
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        return crow::response(400, result);
    }
}

// 注册接口到工厂
AUTO_REGISTER_USER_API("login", userLoginFunc);
AUTO_REGISTER_USER_API("forgetPassword", forgetPasswordFunc);
AUTO_REGISTER_USER_API("resetPassword", resetPasswordFunc);
AUTO_REGISTER_USER_API("queryUserInfo", queryUserInfoFunc);
AUTO_REGISTER_USER_API("getUserList", getUserListFunc);