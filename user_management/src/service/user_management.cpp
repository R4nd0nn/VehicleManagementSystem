#include "service/user_management.h"

// 登录接口
crow::response userLoginFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body || !body.has("username") || !body.has("password")) {
        result["retCode"] = 400;
        result["errorMsg"] = "username and password required";
        result["role"] = "";
        result["route"] = "";
        return crow::response(400, result);
    }
    
    std::string username = body["username"].s();
    std::string password = body["password"].s();
    
    try {
        pqxx::work txn(conn);
        
        // 查询用户
        pqxx::result res = txn.exec_params(
            "SELECT role FROM staff WHERE name = $1 AND password = $2",
            username, password
        );
        
        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid username or password";
            result["role"] = "";
            result["route"] = "";
            return crow::response(400, result);
        }
        
        std::string role = res[0]["role"].as<std::string>();
        
        result["retCode"] = 200;
        result["errorMsg"] = "";
        result["role"] = role;
        result["route"] = "";
        
        txn.commit();
        return crow::response(200, result);
        
    } catch (const std::exception& e) {
        result["retCode"] = 400;
        result["errorMsg"] = "Database error";
        result["role"] = "";
        result["route"] = "";
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

// 注册接口到工厂
AUTO_REGISTER_USER_API("login", userLoginFunc);
AUTO_REGISTER_USER_API("forgetPassword", forgetPasswordFunc);
AUTO_REGISTER_USER_API("resetPassword", resetPasswordFunc);