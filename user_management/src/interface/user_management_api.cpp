#include "service/user_management.h"
#include "config/port.h"


int main() {
    crow::SimpleApp app;
    app.port(USER_MANAGE_PORT).run();
    return 0;
}