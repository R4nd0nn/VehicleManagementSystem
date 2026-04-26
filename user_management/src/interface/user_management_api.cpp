#include "service/user_management.h"
#include "../config/port.h"

// 全局连接池
std::shared_ptr<ConnectionPool> g_db_pool;

int main() {
    crow::SimpleApp app;
    
    const char* db_host = std::getenv("DB_HOST");
    const char* db_port = std::getenv("DB_PORT");
    const char* db_name = std::getenv("DB_NAME");
    const char* db_user = std::getenv("DB_USER");
    const char* db_pass = std::getenv("DB_PASSWORD");
    
    std::string host = db_host ? db_host : "localhost";
    std::string port = db_port ? db_port : "5432";
    std::string name = db_name ? db_name : "vehicleManageDB";
    std::string user = db_user ? db_user : "admin";
    std::string pass = db_pass ? db_pass : "admin123";
    
    std::string conn_str = "host=" + host + 
                           " port=" + port + 
                           " dbname=" + name + 
                           " user=" + user + 
                           " password=" + pass;
    
    // 初始化连接池
    g_db_pool = std::make_shared<ConnectionPool>(conn_str, 10);

    // ========== 登录接口 ==========
    auto loginFunc = UserControllerFactory::instance().create("login");
    if (loginFunc) {                        
        CROW_ROUTE(app, "/user_mng/login").methods("POST"_method)
        ([loginFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return loginFunc(req, *connGuard);
        });
    }
    
    // ========== 忘记密码接口 ==========
    auto forgetPasswordFunc = UserControllerFactory::instance().create("forgetPassword");
    if (forgetPasswordFunc) {
        CROW_ROUTE(app, "/user_mng/forgetPassword").methods("POST"_method)
        ([forgetPasswordFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return forgetPasswordFunc(req, *connGuard);
        });
    }
    
    // ========== 重置密码接口 ==========
    auto resetPasswordFunc = UserControllerFactory::instance().create("resetPassword");
    if (resetPasswordFunc) {
        CROW_ROUTE(app, "/user_mng/resetPassword").methods("POST"_method)
        ([resetPasswordFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return resetPasswordFunc(req, *connGuard);
        });
    }

    auto queryUserInfoFunc = UserControllerFactory::instance().create("queryUserInfo");
    if (queryUserInfoFunc) {                        
        CROW_ROUTE(app, "/user_mng/queryUserInfo").methods("GET"_method)
        ([queryUserInfoFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryUserInfoFunc(req, *connGuard);
        });
    }

    auto getUserListFunc = UserControllerFactory::instance().create("getUserList");
    if (getUserListFunc) {                        
        CROW_ROUTE(app, "/user_mng/getUserList").methods("GET"_method)
        ([getUserListFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getUserListFunc(req, *connGuard);
        });
    }

    auto addUserFunc = UserControllerFactory::instance().create("addUser");
    if (addUserFunc) {                        
        CROW_ROUTE(app, "/user_mng/addUser").methods("POST"_method)
        ([addUserFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addUserFunc(req, *connGuard);
        });
    }
    
    auto deleteUserFunc = UserControllerFactory::instance().create("deleteUser");
    if (deleteUserFunc) {                        
        CROW_ROUTE(app, "/user_mng/deleteUser").methods("POST"_method)
        ([deleteUserFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteUserFunc(req, *connGuard);
        });
    }
    std::cout << "User Management Service running on port " << USER_MANAGE_PORT << std::endl;
    app.port(USER_MANAGE_PORT).multithreaded().run();
    
    return 0;
}