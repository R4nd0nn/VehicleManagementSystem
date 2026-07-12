#include "service/dispatch_management.h"
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

    auto addDriverFunc = DispatchControllerFactory::instance().create("addDriver");
    if (addDriverFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/addDriver").methods("POST"_method)
        ([addDriverFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addDriverFunc(req, *connGuard);
        });
    }
    
    auto updateDriverFunc = DispatchControllerFactory::instance().create("updateDriver");
    if (updateDriverFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/updateDriver").methods("POST"_method)
        ([updateDriverFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateDriverFunc(req, *connGuard);
        });
    }

    auto deleteDriverFunc = DispatchControllerFactory::instance().create("deleteDriver");
    if (deleteDriverFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/deleteDriver").methods("POST"_method)
        ([deleteDriverFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteDriverFunc(req, *connGuard);
        });
    }

    auto queryDriverFunc = DispatchControllerFactory::instance().create("queryDriver");
    if (queryDriverFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/queryDriver").methods("GET"_method)
        ([queryDriverFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryDriverFunc(req, *connGuard);
        });
    }

    auto addVehicleFunc = DispatchControllerFactory::instance().create("addVehicle");
    if (addVehicleFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/addVehicle").methods("POST"_method)
        ([addVehicleFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return addVehicleFunc(req, *connGuard);
        });
    }

    auto updateVehicleFunc = DispatchControllerFactory::instance().create("updateVehicle");
    if (updateVehicleFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/updateVehicle").methods("POST"_method)
        ([updateVehicleFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateVehicleFunc(req, *connGuard);
        });
    }

    auto deleteVehicleFunc = DispatchControllerFactory::instance().create("deleteVehicle");
    if (deleteVehicleFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/deleteVehicle").methods("POST"_method)
        ([deleteVehicleFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return deleteVehicleFunc(req, *connGuard);
        });
    }

    auto queryVehicleFunc = DispatchControllerFactory::instance().create("queryVehicle");
    if (queryVehicleFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/queryVehicle").methods("GET"_method)
        ([queryVehicleFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return queryVehicleFunc(req, *connGuard);
        });
    }

    auto batchDispatchFunc = DispatchControllerFactory::instance().create("batchDispatch");
    if (batchDispatchFunc) {                        
        CROW_ROUTE(app, "/dispatch_mng/batchDispatch").methods("POST"_method)
        ([batchDispatchFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return batchDispatchFunc(req, *connGuard);
        });
    }

    auto queryScheduleTaskFunc = DispatchControllerFactory::instance().create("queryScheduleTask");
    if (queryScheduleTaskFunc) {
        CROW_ROUTE(app, "/dispatch_mng/queryScheduleTask").methods("GET"_method)
        ([queryScheduleTaskFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return queryScheduleTaskFunc(req, *connGuard);
        });
    }
    
    auto addScheduleTaskFunc = DispatchControllerFactory::instance().create("addScheduleTask");
    if (addScheduleTaskFunc) {
        CROW_ROUTE(app, "/dispatch_mng/addScheduleTask").methods("POST"_method)
        ([addScheduleTaskFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return addScheduleTaskFunc(req, *connGuard);
        });
    }
    
    auto updateScheduleTaskFunc = DispatchControllerFactory::instance().create("updateScheduleTask");
    if (updateScheduleTaskFunc) {
        CROW_ROUTE(app, "/dispatch_mng/updateScheduleTask").methods("POST"_method)
        ([updateScheduleTaskFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return updateScheduleTaskFunc(req, *connGuard);
        });
    }
    
    auto deleteScheduleTaskFunc = DispatchControllerFactory::instance().create("deleteScheduleTask");
    if (deleteScheduleTaskFunc) {
        CROW_ROUTE(app, "/dispatch_mng/deleteScheduleTask").methods("POST"_method)
        ([deleteScheduleTaskFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return deleteScheduleTaskFunc(req, *connGuard);
        });
    }

    auto queryYardSlotsFunc = DispatchControllerFactory::instance().create("queryYardSlots");
    if (queryYardSlotsFunc) {
        CROW_ROUTE(app, "/dispatch_mng/queryYardSlots").methods("GET"_method)
        ([queryYardSlotsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return queryYardSlotsFunc(req, *connGuard);
        });
    }

    auto addYardSlotFunc = DispatchControllerFactory::instance().create("addYardSlot");
    if (addYardSlotFunc) {
        CROW_ROUTE(app, "/dispatch_mng/addYardSlot").methods("POST"_method)
        ([addYardSlotFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return addYardSlotFunc(req, *connGuard);
        });
    }

    auto deleteYardSlotFunc = DispatchControllerFactory::instance().create("deleteYardSlot");
    if (deleteYardSlotFunc) {
        CROW_ROUTE(app, "/dispatch_mng/deleteYardSlot").methods("POST"_method)
        ([deleteYardSlotFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return deleteYardSlotFunc(req, *connGuard);
        });
    }

    auto updateYardSlotStatusFunc = DispatchControllerFactory::instance().create("updateYardSlotStatus");
    if (updateYardSlotStatusFunc) {
        CROW_ROUTE(app, "/dispatch_mng/updateYardSlotStatus").methods("POST"_method)
        ([updateYardSlotStatusFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return updateYardSlotStatusFunc(req, *connGuard);
        });
    }

    auto updateYardSlotFunc = DispatchControllerFactory::instance().create("updateYardSlot");
    if (updateYardSlotFunc) {
        CROW_ROUTE(app, "/dispatch_mng/updateYardSlot").methods("POST"_method)
        ([updateYardSlotFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return updateYardSlotFunc(req, *connGuard);
        });
    }

    auto queryYardLogsFunc = DispatchControllerFactory::instance().create("queryYardLogs");
    if (queryYardLogsFunc) {
        CROW_ROUTE(app, "/dispatch_mng/queryYardLogs").methods("GET"_method)
        ([queryYardLogsFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return queryYardLogsFunc(req, *connGuard);
        });
    }

    auto getYardSlotDetailFunc = DispatchControllerFactory::instance().create("getYardSlotDetail");
    if (getYardSlotDetailFunc) {
        CROW_ROUTE(app, "/dispatch_mng/getYardSlotDetail").methods("GET"_method)
        ([getYardSlotDetailFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            return getYardSlotDetailFunc(req, *connGuard);
        });
    }

    auto getTodayOrdersFunc = DispatchControllerFactory::instance().create("getTodayOrders");
    if (getTodayOrdersFunc) {
        CROW_ROUTE(app, "/dispatch_mng/getTodayOrders").methods("GET"_method)
        ([getTodayOrdersFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return getTodayOrdersFunc(req, *connGuard);
        });
    } else {
        std::cout << "getTodayOrdersFunc not exist" << std::endl;
    }

    auto updateYardSlotVehicleFunc = DispatchControllerFactory::instance().create("updateYardSlotVehicle");
    if (updateYardSlotVehicleFunc) {
        CROW_ROUTE(app, "/dispatch_mng/updateYardSlotVehicle").methods("POST"_method)
        ([updateYardSlotVehicleFunc](const crow::request& req) {
            ConnectionPool::ConnectionGuard connGuard(*g_db_pool);
            if (!connGuard.isValid()) {
                crow::json::wvalue result;
                result["retCode"] = 400;
                result["errorMsg"] = "Database connection failed";
                return crow::response(400, result);
            }
            return updateYardSlotVehicleFunc(req, *connGuard);
        });
    }

    std::cout << "Dispatch Management Service running on port " << DISPATCH_MANAGE_PORT << std::endl;
    app.port(DISPATCH_MANAGE_PORT).multithreaded().run();
    
    return 0;
}