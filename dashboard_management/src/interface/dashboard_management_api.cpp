#include "service/dashboard_management.h"
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

    // ==================== 数据驾驶舱 API ====================
    
    // 1. 获取核心指标
    auto getCoreStatsFunc = DashboardControllerFactory::instance().create("getCoreStats");
    if (getCoreStatsFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getCoreStats").methods("GET"_method)
        ([getCoreStatsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getCoreStatsFunc(req, *connGuard);
        });
    } else {
        std::cout << "getCoreStatsFunc not exist" << std::endl;
    }

    // 2. 获取营收趋势
    auto getRevenueTrendFunc = DashboardControllerFactory::instance().create("getRevenueTrend");
    if (getRevenueTrendFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getRevenueTrend").methods("GET"_method)
        ([getRevenueTrendFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getRevenueTrendFunc(req, *connGuard);
        });
    } else {
        std::cout << "getRevenueTrendFunc not exist" << std::endl;
    }

    // 3. 获取订单完成率
    auto getCompletionRateFunc = DashboardControllerFactory::instance().create("getCompletionRate");
    if (getCompletionRateFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getCompletionRate").methods("GET"_method)
        ([getCompletionRateFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getCompletionRateFunc(req, *connGuard);
        });
    } else {
        std::cout << "getCompletionRateFunc not exist" << std::endl;
    }

    // 4. 获取成本构成
    auto getCostStructureFunc = DashboardControllerFactory::instance().create("getCostStructure");
    if (getCostStructureFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getCostStructure").methods("GET"_method)
        ([getCostStructureFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getCostStructureFunc(req, *connGuard);
        });
    } else {
        std::cout << "getCostStructureFunc not exist" << std::endl;
    }

    // 5. 获取异常监控数据
    auto getAbnormalDataFunc = DashboardControllerFactory::instance().create("getAbnormalData");
    if (getAbnormalDataFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getAbnormalData").methods("GET"_method)
        ([getAbnormalDataFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getAbnormalDataFunc(req, *connGuard);
        });
    } else {
        std::cout << "getAbnormalDataFunc not exist" << std::endl;
    }

    // 6. 获取车辆排行
    auto getVehicleRankingFunc = DashboardControllerFactory::instance().create("getVehicleRanking");
    if (getVehicleRankingFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getVehicleRanking").methods("GET"_method)
        ([getVehicleRankingFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getVehicleRankingFunc(req, *connGuard);
        });
    } else {
        std::cout << "getVehicleRankingFunc not exist" << std::endl;
    }

    // 7. 获取司机排行
    auto getDriverRankingFunc = DashboardControllerFactory::instance().create("getDriverRanking");
    if (getDriverRankingFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getDriverRanking").methods("GET"_method)
        ([getDriverRankingFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getDriverRankingFunc(req, *connGuard);
        });
    } else {
        std::cout << "getDriverRankingFunc not exist" << std::endl;
    }

    // 8. 获取订单明细
    auto getOrderDetailListFunc = DashboardControllerFactory::instance().create("getOrderDetailList");
    if (getOrderDetailListFunc) {
        CROW_ROUTE(app, "/dashboard_mng/getOrderDetailList").methods("GET"_method)
        ([getOrderDetailListFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getOrderDetailListFunc(req, *connGuard);
        });
    } else {
        std::cout << "getOrderDetailListFunc not exist" << std::endl;
    }

    // 9. 导出报表
    auto exportDashboardFunc = DashboardControllerFactory::instance().create("exportDashboard");
    if (exportDashboardFunc) {
        CROW_ROUTE(app, "/dashboard_mng/exportDashboard").methods("POST"_method)
        ([exportDashboardFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return exportDashboardFunc(req, *connGuard);
        });
    } else {
        std::cout << "exportDashboardFunc not exist" << std::endl;
    }

    std::cout << "Dashboard Management Service running on port " << DASHBOARD_MANAGE_PORT << std::endl;
    app.port(DASHBOARD_MANAGE_PORT).multithreaded().run();
    
    return 0;
}