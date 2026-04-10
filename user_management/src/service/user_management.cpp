#include "user_management.h"


crow::response getUserInfo(const crow::request& req) {
    crow::json::wvalue result;
    return crow::response(200, result);
}

crow::response userLogin(const crow::request& req) {
    crow::json::wvalue result;
    return crow::response(200, result);
}

crow::response userLogout(const crow::request& req) {
    crow::json::wvalue result;
    return crow::response(200, result);
}

AUTO_REGISTER_USER_API("getUserInfo", getUserInfo);
AUTO_REGISTER_USER_API("login", userLogin);
AUTO_REGISTER_USER_API("logout", userLogout);