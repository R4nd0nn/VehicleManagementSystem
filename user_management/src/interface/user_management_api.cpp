#include "service/user_management.h"
#include "../config/port.h"

// 全局连接池
std::shared_ptr<ConnectionPool> g_db_pool;

int main() {
    crow::SimpleApp app;
    
    // 数据库连接字符串
    std::string conn_str = "host=localhost "
                           "port=5432 "
                           "dbname=vehicleManageDB "
                           "user=admin "
                           "password=admin123";
    
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
                result["role"] = "";
                result["route"] = "";
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
    
    std::cout << "User Management Service running on port " << USER_MANAGE_PORT << std::endl;
    app.port(USER_MANAGE_PORT).multithreaded().run();
    
    return 0;
}