#include "service/fee_management.h"
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

    // ==================== 货柜列表查询 ====================
    auto queryContainersFunc = FeeControllerFactory::instance().create("queryContainers");
    if (queryContainersFunc) {
        CROW_ROUTE(app, "/fee_mng/queryContainers").methods("GET"_method)
        ([queryContainersFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryContainersFunc(req, *connGuard);
        });
    } else {
        std::cout << "queryContainersFunc not exist" << std::endl;
    }

    // ==================== 获取货柜费用明细 ====================
    auto getContainerCostFunc = FeeControllerFactory::instance().create("getContainerCost");
    if (getContainerCostFunc) {
        CROW_ROUTE(app, "/fee_mng/getContainerCost").methods("GET"_method)
        ([getContainerCostFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getContainerCostFunc(req, *connGuard);
        });
    } else {
        std::cout << "getContainerCostFunc not exist" << std::endl;
    }

    // ==================== 添加费用项 ====================
    auto addContainerCostFunc = FeeControllerFactory::instance().create("addContainerCost");
    if (addContainerCostFunc) {
        CROW_ROUTE(app, "/fee_mng/addContainerCost").methods("POST"_method)
        ([addContainerCostFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addContainerCostFunc(req, *connGuard);
        });
    } else {
        std::cout << "addContainerCostFunc not exist" << std::endl;
    }

    // ==================== 更新费用项 ====================
    auto updateContainerCostFunc = FeeControllerFactory::instance().create("updateContainerCost");
    if (updateContainerCostFunc) {
        CROW_ROUTE(app, "/fee_mng/updateContainerCost").methods("POST"_method)
        ([updateContainerCostFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateContainerCostFunc(req, *connGuard);
        });
    } else {
        std::cout << "updateContainerCostFunc not exist" << std::endl;
    }

    // ==================== 删除费用项 ====================
    auto deleteContainerCostFunc = FeeControllerFactory::instance().create("deleteContainerCost");
    if (deleteContainerCostFunc) {
        CROW_ROUTE(app, "/fee_mng/deleteContainerCost").methods("POST"_method)
        ([deleteContainerCostFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteContainerCostFunc(req, *connGuard);
        });
    } else {
        std::cout << "deleteContainerCostFunc not exist" << std::endl;
    }

    // ==================== 确认核算 ====================
    auto auditContainerFunc = FeeControllerFactory::instance().create("auditContainer");
    if (auditContainerFunc) {
        CROW_ROUTE(app, "/fee_mng/auditContainer").methods("POST"_method)
        ([auditContainerFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return auditContainerFunc(req, *connGuard);
        });
    } else {
        std::cout << "auditContainerFunc not exist" << std::endl;
    }

    auto updateContainerInvoiceFunc = FeeControllerFactory::instance().create("updateContainerInvoice");
    if (updateContainerInvoiceFunc) {
        CROW_ROUTE(app, "/fee_mng/updateContainerInvoice").methods("POST"_method)
        ([updateContainerInvoiceFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateContainerInvoiceFunc(req, *connGuard);
        });
    } else {
        std::cout << "updateContainerInvoiceFunc not exist" << std::endl;
    }

    // ========== 批量保存费用明细 ==========
    auto saveContainerCostFunc = FeeControllerFactory::instance().create("saveContainerCost");
    if (saveContainerCostFunc) {
        CROW_ROUTE(app, "/fee_mng/saveContainerCost").methods("POST"_method)
        ([saveContainerCostFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return saveContainerCostFunc(req, *connGuard);
        });
    }

    std::cout << "Fee Management Service running on port " << FEE_MANAGE_PORT << std::endl;
    app.port(FEE_MANAGE_PORT).multithreaded().run();
    
    return 0;
}