#include "service/finance_management.h"
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

    // ==================== 财务月度报表 API ====================
    
    auto getMonthlyReportFunc = FinanceControllerFactory::instance().create("getMonthlyReport");
    if (getMonthlyReportFunc) {
        CROW_ROUTE(app, "/finance_mng/getMonthlyReport").methods("GET"_method)
        ([getMonthlyReportFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getMonthlyReportFunc(req, *connGuard);
        });
    } else {
        std::cout << "getMonthlyReportFunc not exist" << std::endl;
    }

    auto addExpenseItemFunc = FinanceControllerFactory::instance().create("addExpenseItem");
    if (addExpenseItemFunc) {
        CROW_ROUTE(app, "/finance_mng/addExpenseItem").methods("POST"_method)
        ([addExpenseItemFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addExpenseItemFunc(req, *connGuard);
        });
    } else {
        std::cout << "addExpenseItemFunc not exist" << std::endl;
    }

    auto updateExpenseItemFunc = FinanceControllerFactory::instance().create("updateExpenseItem");
    if (updateExpenseItemFunc) {
        CROW_ROUTE(app, "/finance_mng/updateExpenseItem").methods("POST"_method)
        ([updateExpenseItemFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateExpenseItemFunc(req, *connGuard);
        });
    } else {
        std::cout << "updateExpenseItemFunc not exist" << std::endl;
    }

    auto deleteExpenseItemFunc = FinanceControllerFactory::instance().create("deleteExpenseItem");
    if (deleteExpenseItemFunc) {
        CROW_ROUTE(app, "/finance_mng/deleteExpenseItem").methods("POST"_method)
        ([deleteExpenseItemFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteExpenseItemFunc(req, *connGuard);
        });
    } else {
        std::cout << "deleteExpenseItemFunc not exist" << std::endl;
    }

    auto queryExpenseItemsFunc = FinanceControllerFactory::instance().create("queryExpenseItems");
    if (queryExpenseItemsFunc) {
        CROW_ROUTE(app, "/finance_mng/queryExpenseItems").methods("GET"_method)
        ([queryExpenseItemsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryExpenseItemsFunc(req, *connGuard);
        });
    } else {
        std::cout << "queryExpenseItemsFunc not exist" << std::endl;
    }

    std::cout << "Finance Management Service running on port " << FINANCE_MANAGE_PORT << std::endl;
    app.port(FINANCE_MANAGE_PORT).multithreaded().run();
    
    return 0;
}