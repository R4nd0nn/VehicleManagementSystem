#include "service/order_management.h"
#include "../config/port.h"

// 全局连接池
std::shared_ptr<ConnectionPool> g_db_pool;

int main() {
    crow::SimpleApp app;
    
    // 数据库连接字符串
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
    auto addOrderFunc = OrderControllerFactory::instance().create("addOrder");
    if (addOrderFunc) {                        
        CROW_ROUTE(app, "/order_mng/addOrder").methods("POST"_method)
        ([addOrderFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addOrderFunc(req, *connGuard);
        });
    }

    auto queryOrdersFunc = OrderControllerFactory::instance().create("queryOrders");
    if (queryOrdersFunc) {                        
        CROW_ROUTE(app, "/order_mng/queryOrders").methods("GET"_method)
        ([queryOrdersFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryOrdersFunc(req, *connGuard);
        });
    }else{
        std::cout << "queryOrdersFunc not exist " << std::endl;
    }

    std::cout << "Order Management Service running on port " << ORDER_MANAGE_PORT << std::endl;
    app.port(ORDER_MANAGE_PORT).multithreaded().run();
    
    return 0;
}