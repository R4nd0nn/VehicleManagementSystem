#include "service/order_management.h"
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


    
    std::cout << "Order Management Service running on port " << ORDER_MANAGE_PORT << std::endl;
    app.port(ORDER_MANAGE_PORT).multithreaded().run();
    
    return 0;
}