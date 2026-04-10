#ifndef CROW_H
#define CROW_H

// #include "crow_all.h"
// #include <iostream>
// #include <unordered_map>

// // 1. 定义认证中间件
// struct AuthMiddleware {
//     // 上下文，用于在中间件和路由之间传递数据（例如解析出的用户ID）
//     struct context {
//         std::string user_id;
//     };

//     // 在所有路由处理之前执行
//     void before_handle(crow::request& req, crow::response& res, context& ctx) {
        
        
//         // 1. 从请求头获取 Token
//         std::string auth_header = req.get_header_value("Authorization");
//         if (auth_header.empty() || auth_header.find("Bearer ") != 0) {
//             res.code = 401;
//             res.write("Unauthorized: Missing or invalid Authorization header.");
//             res.end(); // 结束请求，不再往下执行
//             return;
//         }

//         // 2. 提取 Token 并校验
//         std::string token = auth_header.substr(7);
        
//         // 这里替换为你的校验逻辑（如查询数据库/Redis，或解析JWT）
//         // 示例：假设只有 "valid_token_123" 是有效的
//         if (token == "valid_token_123") {
//             // 校验通过，可以将用户信息存入上下文，供后续路由使用
//             ctx.user_id = "user_001"; 
//         } else {
//             res.code = 401;
//             res.write("Unauthorized: Invalid token.");
//             res.end();
//             return;
//         }
//     }

//     // 在所有路由处理之后执行（本例不需要额外操作，可以为空）
//     void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {
//         // 可用于记录日志等
//     }
// };

#endif