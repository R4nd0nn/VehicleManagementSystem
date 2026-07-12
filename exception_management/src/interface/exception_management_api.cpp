#include "service/exception_management.h"
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

    // ==================== 异常管理 API ====================
    
    // 1. 查询异常列表
    auto queryExceptionsFunc = ExceptionControllerFactory::instance().create("queryExceptions");
    if (queryExceptionsFunc) {
        CROW_ROUTE(app, "/exception_mng/queryExceptions").methods("GET"_method)
        ([queryExceptionsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryExceptionsFunc(req, *connGuard);
        });
    } else {
        std::cout << "queryExceptionsFunc not exist" << std::endl;
    }

    // 2. 获取异常详情
    auto getExceptionDetailFunc = ExceptionControllerFactory::instance().create("getExceptionDetail");
    if (getExceptionDetailFunc) {
        CROW_ROUTE(app, "/exception_mng/getExceptionDetail").methods("GET"_method)
        ([getExceptionDetailFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getExceptionDetailFunc(req, *connGuard);
        });
    } else {
        std::cout << "getExceptionDetailFunc not exist" << std::endl;
    }

    // 3. 上报异常
    auto addExceptionFunc = ExceptionControllerFactory::instance().create("addException");
    if (addExceptionFunc) {
        CROW_ROUTE(app, "/exception_mng/addException").methods("POST"_method)
        ([addExceptionFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addExceptionFunc(req, *connGuard);
        });
    } else {
        std::cout << "addExceptionFunc not exist" << std::endl;
    }

    // 4. 处理异常（更新）
    auto updateExceptionFunc = ExceptionControllerFactory::instance().create("updateException");
    if (updateExceptionFunc) {
        CROW_ROUTE(app, "/exception_mng/updateException").methods("POST"_method)
        ([updateExceptionFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateExceptionFunc(req, *connGuard);
        });
    } else {
        std::cout << "updateExceptionFunc not exist" << std::endl;
    }

    // 5. 删除异常（软删除）
    auto deleteExceptionFunc = ExceptionControllerFactory::instance().create("deleteException");
    if (deleteExceptionFunc) {
        CROW_ROUTE(app, "/exception_mng/deleteException").methods("POST"_method)
        ([deleteExceptionFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteExceptionFunc(req, *connGuard);
        });
    } else {
        std::cout << "deleteExceptionFunc not exist" << std::endl;
    }

    // 6. 批量删除异常
    auto batchDeleteExceptionFunc = ExceptionControllerFactory::instance().create("batchDeleteException");
    if (batchDeleteExceptionFunc) {
        CROW_ROUTE(app, "/exception_mng/batchDeleteException").methods("POST"_method)
        ([batchDeleteExceptionFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return batchDeleteExceptionFunc(req, *connGuard);
        });
    } else {
        std::cout << "batchDeleteExceptionFunc not exist" << std::endl;
    }

    // 7. 获取异常统计数据
    auto getExceptionStatsFunc = ExceptionControllerFactory::instance().create("getExceptionStats");
    if (getExceptionStatsFunc) {
        CROW_ROUTE(app, "/exception_mng/getExceptionStats").methods("GET"_method)
        ([getExceptionStatsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getExceptionStatsFunc(req, *connGuard);
        });
    } else {
        std::cout << "getExceptionStatsFunc not exist" << std::endl;
    }

    std::cout << "Exception Management Service running on port " << EXCEPTION_MANAGE_PORT << std::endl;
    app.port(EXCEPTION_MANAGE_PORT).multithreaded().run();
    
    return 0;
}