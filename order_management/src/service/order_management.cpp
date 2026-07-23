#include "service/order_management.h"

#include <OpenXLSX/OpenXLSX.hpp>
#include <algorithm>
#include <cctype>
#include <regex>
#include <fstream>
#include <sstream>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <chrono>
#include <map>
#include <iconv.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <ctime>
#include <optional>

#include "../common/include/jwt/jwt.h"

std::string base64_decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.c_str(), encoded.length());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    
    char* buffer = new char[encoded.length()];
    int decoded_len = BIO_read(bio, buffer, encoded.length());
    
    std::string result(buffer, decoded_len);
    delete[] buffer;
    BIO_free_all(bio);
    
    return result;
}

// 安全截断
static std::string safeTruncate(const std::string& str, size_t maxLen)
{
    if (str.size() <= maxLen) return str;
    std::string result = str.substr(0, maxLen);
    while (!result.empty() && (result.back() & 0x80) && (result.back() & 0xC0) != 0xC0) result.pop_back();
    return result;
}

// 日期转换 YYYY-MM-DD
static std::string convertDate(const std::string& dateStr)
{
    if (dateStr.empty()) return "";
    int year=0, month=0, day=0;
    if (sscanf(dateStr.c_str(), "%d/%d/%d", &year,&month,&day)==3 && year>1900) { char buf[11]; sprintf(buf,"%04d-%02d-%02d",year,month,day); return buf; }
    if (sscanf(dateStr.c_str(), "%d/%d/%d", &day,&month,&year)==3 && year>1900) { char buf[11]; sprintf(buf,"%04d-%02d-%02d",year,month,day); return buf; }
    return "";
}

// ==================== 工具函数：从订单创建货柜 ====================
void createContainerFromOrder(pqxx::work& txn, const std::string& container_no, 
                               const std::string& customer_note,
                               const std::string& last_free_date,
                               int userId, int orderId) {
    if (container_no.empty()) {
        throw std::runtime_error("container_no is empty, cannot create container");
    }
    
    // 检查 container 是否已存在（通过 container_no）
    pqxx::result checkRes = txn.exec_params(
        "SELECT id FROM container WHERE container_no = $1 AND deleted_at IS NULL",
        container_no.c_str()
    );
    
    if (!checkRes.empty()) {
        // Container 已存在，跳过创建
        std::cout << "[INFO] Container " << container_no << " already exists, skipping creation" << std::endl;
        return;
    }
    
    // 解析 last_free_date 作为 free_expired_time
    int freeExpiredTime = 7;  // 默认 7 天
    if (!last_free_date.empty()) {
        std::tm tm = {};
        std::istringstream ss(last_free_date);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        if (!ss.fail()) {
            auto lastFree = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            auto now = std::chrono::system_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::hours>(lastFree - now).count();
            freeExpiredTime = diff / 24;
            if (freeExpiredTime < 0) freeExpiredTime = 0;
        }
    }
    
    // 生成 waybill_no: "order-" + order_id
    std::string waybill_no = "order-" + std::to_string(orderId);
    
    // 插入 container
    txn.exec_params(
        "INSERT INTO container ("
        "container_no, status, free_days, free_expired_time, "
        "customer_requirement, waybill_no, created_by, updated_by, created_at, updated_at"
        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)",
        container_no.c_str(),
        "空柜",
        7,
        freeExpiredTime,
        customer_note.empty() ? nullptr : customer_note.c_str(),
        waybill_no.c_str(),
        userId,
        userId
    );
}

// 导入 Excel
crow::response importExcelFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;
    
    auto startTotal = std::chrono::steady_clock::now();
    auto logTime = [&startTotal](const std::string& step) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTotal).count();
        std::cout << "[LOG] " << step << " - 耗时: " << elapsed << "ms" << std::endl;
    };

    std::cout << "[LOG] ========== importExcelFunc 开始 ==========" << std::endl;

    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }
    logTime("Token 验证通过");

    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);
        std::string username = decoded.get_subject();
        logTime("JWT 验证完成，用户名: " + username);

        pqxx::work txn(conn);
        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();
        logTime("获取用户ID: " + std::to_string(userId));

        auto body = crow::json::load(req.body);
        if (!body || !body.has("fileData")) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid request, missing fileData";
            return crow::response(400, result);
        }
        logTime("JSON 解析完成");

        std::string base64Data = body["fileData"].s();
        logTime("Base64 数据长度: " + std::to_string(base64Data.length()));
        
        std::string fileContent = base64_decode(base64Data);
        logTime("解码后文件大小: " + std::to_string(fileContent.size()) + " bytes");

        std::string tempFile = "/tmp/excel_import_" + std::to_string(time(nullptr)) + ".xlsx";
        std::ofstream out(tempFile, std::ios::binary);
        out.write(fileContent.c_str(), fileContent.size());
        out.close();
        logTime("临时文件保存成功: " + tempFile);

        std::cout << "[LOG] 开始打开 Excel 文件..." << std::endl;
        OpenXLSX::XLDocument doc;
        doc.open(tempFile);
        logTime("Excel 文件打开成功");

        auto wb = doc.workbook();
        auto sheetNames = wb.worksheetNames();
        logTime("工作表数量: " + std::to_string(sheetNames.size()));
        
        for (const auto& name : sheetNames) {
            std::cout << "[LOG] 发现工作表: " << name << std::endl;
        }

        auto isContinuation = [](unsigned char c) -> bool {
            return (c & 0xC0) == 0x80;
        };

        auto cleanUTF8 = [&isContinuation](const std::string& str) -> std::string {
            if (str.empty()) return "";
            std::string result;
            result.reserve(str.size());
            for (size_t i = 0; i < str.size(); i++) {
                unsigned char c = str[i];
                if (c <= 0x7F) {
                    if (c == 0x09 || c == 0x0A || c == 0x0D || c >= 0x20) {
                        result += c;
                    } else {
                        result += ' ';
                    }
                }
                else if (c >= 0xC2 && c <= 0xDF && i + 1 < str.size() && isContinuation((unsigned char)str[i + 1])) {
                    result += c;
                    result += str[++i];
                }
                else if (c >= 0xE0 && c <= 0xEF && i + 2 < str.size()
                         && isContinuation((unsigned char)str[i + 1])
                         && isContinuation((unsigned char)str[i + 2])) {
                    unsigned char b1 = str[i + 1];
                    if (c == 0xED && b1 >= 0xA0 && b1 <= 0xBF) {
                        i += 2;
                        result += ' ';
                        continue;
                    }
                    result += c;
                    result += str[++i];
                    result += str[++i];
                }
                else if (c >= 0xF0 && c <= 0xF4 && i + 3 < str.size()
                         && isContinuation((unsigned char)str[i + 1])
                         && isContinuation((unsigned char)str[i + 2])
                         && isContinuation((unsigned char)str[i + 3])) {
                    unsigned char b1 = str[i + 1];
                    if (c == 0xF4 && b1 >= 0x90) {
                        i += 3;
                        result += ' ';
                        continue;
                    }
                    result += c;
                    result += str[++i];
                    result += str[++i];
                    result += str[++i];
                }
                else {
                    result += ' ';
                }
            }
            return result;
        };

        auto wordBoundaryMatch = [](const std::string& text, const std::string& pattern) -> bool {
            size_t pos = text.find(pattern);
            while (pos != std::string::npos) {
                bool leftOk = (pos == 0) || (text[pos - 1] == ' ');
                bool rightOk = (pos + pattern.size() == text.size()) || (text[pos + pattern.size()] == ' ');
                if (leftOk && rightOk) return true;
                pos = text.find(pattern, pos + 1);
            }
            return false;
        };

        std::map<std::string, std::vector<std::string>> fieldMapping = {
            {"type",            {"type", "import/export"}},
            {"start_point",     {"from"}},
            {"end_point",       {"to"}},
            {"size",            {"size"}},
            {"container_no",    {"container no", "container"}},
            {"pin",             {"pin"}},
            {"customer_note",   {"客人要求", "customerrequest"}},
            {"vessel",          {"vessel"}},
            {"shipping_line",   {"shipping line"}},
            {"eta",             {"eta"}},
            {"first_available", {"first available", "first avaiable"}},
            {"last_free_date",  {"last free date"}},
            {"client_name",     {"client name"}},
            {"customer_address",{"customer address"}},
            {"forwarder",       {"forwarder", "customer name"}},
            {"weight",          {"weight"}},
            {"invoice_id",      {"invoice"}},
            {"noted",           {"noted"}},
            {"gate_in",         {"gate in"}},
            {"gate_out",        {"gate out"}},
            {"tt",              {"tt"}}
        };

        auto convertType = [](const std::string& typeStr) -> int {
            if (typeStr.empty()) return 1;
            std::string upper = typeStr;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if (upper.find("EXPORT") != std::string::npos) return 2;
            return 1;
        };

        auto isLeapYear = [](int y) -> bool {
            return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        };

        auto isValidDate = [&isLeapYear](int year, int month, int day) -> bool {
            if (year < 1901 || year > 2099 || month < 1 || month > 12 || day < 1) return false;
            static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int maxDay = daysInMonth[month - 1];
            if (month == 2 && isLeapYear(year)) maxDay = 29;
            return day <= maxDay;
        };

        auto excelSerialToDate = [&isValidDate, &isLeapYear](int serial) -> std::string {
            if (serial < 1 || serial > 100000) return "";
            int offset = (serial > 60) ? (serial - 2) : (serial - 1);
            int y = 1900, m = 1, d = 1;
            while (true) {
                int daysInYear = isLeapYear(y) ? 366 : 365;
                if (offset < daysInYear) break;
                offset -= daysInYear;
                y++;
            }
            static const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            for (m = 1; m <= 12; m++) {
                int md = monthDays[m - 1];
                if (m == 2 && isLeapYear(y)) md = 29;
                if (offset < md) break;
                offset -= md;
            }
            d = offset + 1;
            if (!isValidDate(y, m, d)) return "";
            char buf[11];
            sprintf(buf, "%04d-%02d-%02d", y, m, d);
            return std::string(buf);
        };

        auto convertDate = [&isValidDate, &excelSerialToDate](const std::string& dateStr) -> std::string {
            if (dateStr.empty()) return "";
            int year = 0, month = 0, day = 0;

            char* endp = nullptr;
            long serialNum = strtol(dateStr.c_str(), &endp, 10);
            if (*endp == '\0' && serialNum > 0 && serialNum < 100000) {
                return excelSerialToDate((int)serialNum);
            }

            if (sscanf(dateStr.c_str(), "%d/%d/%d", &year, &month, &day) >= 3 && year > 1900) {
                if (isValidDate(year, month, day)) {
                    char buf[11];
                    sprintf(buf, "%04d-%02d-%02d", year, month, day);
                    return std::string(buf);
                }
            }
            if (sscanf(dateStr.c_str(), "%d/%d/%d", &day, &month, &year) == 3 && year > 1900) {
                if (isValidDate(year, month, day)) {
                    char buf[11];
                    sprintf(buf, "%04d-%02d-%02d", year, month, day);
                    return std::string(buf);
                }
            }
            if (sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day) == 3 && year > 1900) {
                if (isValidDate(year, month, day)) {
                    char buf[11];
                    sprintf(buf, "%04d-%02d-%02d", year, month, day);
                    return std::string(buf);
                }
            }
            if (sscanf(dateStr.c_str(), "%d-%d-%d", &day, &month, &year) == 3 && year > 1900) {
                if (isValidDate(year, month, day)) {
                    char buf[11];
                    sprintf(buf, "%04d-%02d-%02d", year, month, day);
                    return std::string(buf);
                }
            }
            return "";
        };

        // ======== 第一阶段：解析表头 ========
        std::vector<std::map<std::string, int>> sheetMaps;
        std::cout << "[LOG] 开始解析表头..." << std::endl;

        for (const auto& sheetName : sheetNames) {
            auto ws = wb.worksheet(sheetName);
            std::map<std::string, int> colMap;
            
            for (int col = 1; col <= 50; col++) {
                try {
                    std::string header = ws.cell(1, col).value().getString();
                    if (header.empty()) continue;
                    
                    header = cleanUTF8(header);
                    std::string lower = header;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    
                    for (const auto& [field, keywords] : fieldMapping) {
                        for (const auto& kw : keywords) {
                            std::string lowerKw = kw;
                            std::transform(lowerKw.begin(), lowerKw.end(), lowerKw.begin(), ::tolower);
                            if (wordBoundaryMatch(lower, lowerKw)) {
                                colMap[field] = col;
                                break;
                            }
                        }
                    }
                } catch (...) {
                    break;
                }
            }
            sheetMaps.push_back(colMap);

            std::cout << "[DIAG] 工作表 " << sheetName << " colMap (" << colMap.size() << " 项):" << std::endl;
            for (const auto& [field, col] : colMap) {
                std::cout << "[DIAG]   " << field << " → Col " << col << std::endl;
            }
        }
        logTime("表头解析完成");

        // ======== 第二阶段：逐行插入 ========
        int successCount = 0;
        int failCount = 0;
        int diagSampleCount = 0;

        auto insertStart = std::chrono::steady_clock::now();

        for (size_t sheetIdx = 0; sheetIdx < sheetNames.size(); sheetIdx++) {
            auto ws = wb.worksheet(sheetNames[sheetIdx]);
            auto colMap = sheetMaps[sheetIdx];
            auto lastRow = ws.rowCount();
            
            std::cout << "[LOG] 处理工作表: " << sheetNames[sheetIdx] << ", 报告总行数: " << lastRow << std::endl;
            
            int sheetSuccess = 0;
            int sheetFail = 0;
            int consecutiveEmpty = 0;
            int maxEmptyRows = 100;
            
            auto sheetStart = std::chrono::steady_clock::now();
            
            for (int rowNum = 2; rowNum <= lastRow && consecutiveEmpty < maxEmptyRows; rowNum++) {
                bool rowFailed = false;
                
                std::string type_raw, eta_raw, first_available_raw, last_free_date_raw;
                std::string weight_raw, gate_in_raw, gate_out_raw, tt_raw;
                
                std::string spName = "sp_" + std::to_string(sheetIdx) + "_" + std::to_string(rowNum);
                txn.exec("SAVEPOINT " + spName);
                
                try {
                    auto getVal = [&](const std::string& field) -> std::string {
                        auto it = colMap.find(field);
                        if (it != colMap.end()) {
                            return cleanUTF8(ws.cell(rowNum, it->second).value().getString());
                        }
                        return "";
                    };
                    
                    auto getValRaw = [&](const std::string& field, std::string& rawOut) -> std::string {
                        auto it = colMap.find(field);
                        if (it != colMap.end()) {
                            rawOut = ws.cell(rowNum, it->second).value().getString();
                            return cleanUTF8(rawOut);
                        }
                        rawOut = "";
                        return "";
                    };
                    
                    int typeVal = convertType(getValRaw("type", type_raw));
                    std::string start_point = getVal("start_point");
                    std::string end_point = getVal("end_point");
                    std::string container_no = getVal("container_no");
                    
                    if (start_point.empty() && end_point.empty() && container_no.empty()) {
                        consecutiveEmpty++;
                        txn.exec("RELEASE SAVEPOINT " + spName);
                        continue;
                    }
                    consecutiveEmpty = 0;

                    // 检查 container_no 是否为空
                    if (container_no.empty()) {
                        throw std::runtime_error("container_no is empty at row " + std::to_string(rowNum));
                    }
                    
                    std::string size = getVal("size");
                    std::string pin = getVal("pin");
                    std::string customer_note = getVal("customer_note");
                    std::string vessel = getVal("vessel");
                    std::string shipping_line = getVal("shipping_line");
                    
                    std::string eta = convertDate(getValRaw("eta", eta_raw));
                    std::string first_available = convertDate(getValRaw("first_available", first_available_raw));
                    std::string last_free_date = convertDate(getValRaw("last_free_date", last_free_date_raw));
                    
                    std::string client_name = getVal("client_name");
                    std::string customer_address = getVal("customer_address");
                    std::string forwarder = getVal("forwarder");
                    std::string invoice_id = getVal("invoice_id");
                    std::string noted = getVal("noted");
                    
                    std::string weight = getValRaw("weight", weight_raw);
                    std::string gate_in = getValRaw("gate_in", gate_in_raw);
                    std::string gate_out = getValRaw("gate_out", gate_out_raw);
                    std::string tt = getValRaw("tt", tt_raw);

                    if (diagSampleCount < 3) {
                        std::cout << "[DIAG] " << sheetNames[sheetIdx] << " Row " << rowNum
                                  << " type_raw=[" << type_raw << "] → " << typeVal
                                  << " | start=[" << start_point << "]"
                                  << " | end=[" << end_point << "]"
                                  << " | container=[" << container_no << "]"
                                  << " | eta_raw=[" << eta_raw << "] → [" << eta << "]"
                                  << " | fa_raw=[" << first_available_raw << "] → [" << first_available << "]"
                                  << " | lfd_raw=[" << last_free_date_raw << "] → [" << last_free_date << "]"
                                  << " | fwd=[" << forwarder << "]"
                                  << " | client=[" << client_name << "]"
                                  << std::endl;
                        diagSampleCount++;
                    }
                    
                    start_point = safeTruncate(start_point, 255);
                    end_point = safeTruncate(end_point, 255);
                    client_name = safeTruncate(client_name, 255);
                    customer_address = safeTruncate(customer_address, 255);
                    forwarder = safeTruncate(forwarder, 255);
                    container_no = safeTruncate(container_no, 255);
                    pin = safeTruncate(pin, 255);
                    invoice_id = safeTruncate(invoice_id, 50);
                    weight = safeTruncate(weight, 50);
                    gate_in = safeTruncate(gate_in, 50);
                    gate_out = safeTruncate(gate_out, 50);
                    tt = safeTruncate(tt, 50);
                    
                    // 插入 orders 并获取 id
                    pqxx::result res = txn.exec_params(
                        "INSERT INTO orders ("
                        "type, start_point, end_point, size, container_no, pin, customer_note, "
                        "vessel, shipping_line, eta, first_available, last_free_date, "
                        "client_name, customer_address, forwarder, weight, noted, invoice_id, "
                        "status, process_client_id, create_user_id, gate_in, gate_out, tt"
                        ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, "
                        "$15, $16, $17, $18, $19, $20, $21, $22, $23, $24) RETURNING id",
                        typeVal,
                        start_point.empty() ? nullptr : start_point.c_str(),
                        end_point.empty() ? nullptr : end_point.c_str(),
                        size.empty() ? nullptr : size.c_str(),
                        container_no.empty() ? nullptr : container_no.c_str(),
                        pin.empty() ? nullptr : pin.c_str(),
                        customer_note.empty() ? nullptr : customer_note.c_str(),
                        vessel.empty() ? nullptr : vessel.c_str(),
                        shipping_line.empty() ? nullptr : shipping_line.c_str(),
                        eta.empty() ? nullptr : eta.c_str(),
                        first_available.empty() ? nullptr : first_available.c_str(),
                        last_free_date.empty() ? nullptr : last_free_date.c_str(),
                        client_name.empty() ? nullptr : client_name.c_str(),
                        customer_address.empty() ? nullptr : customer_address.c_str(),
                        forwarder.empty() ? nullptr : forwarder.c_str(),
                        weight.empty() ? nullptr : weight.c_str(),
                        noted.empty() ? nullptr : noted.c_str(),
                        invoice_id.empty() ? nullptr : invoice_id.c_str(),
                        1,
                        userId,
                        userId,
                        gate_in.empty() ? nullptr : gate_in.c_str(),
                        gate_out.empty() ? nullptr : gate_out.c_str(),
                        tt.empty() ? nullptr : tt.c_str()
                    );

                    if (res.empty()) {
                        throw std::runtime_error("Failed to insert order at row " + std::to_string(rowNum));
                    }

                    int orderId = res[0]["id"].as<int>();

                    // ✅ 创建对应的 container
                    createContainerFromOrder(txn, container_no, customer_note, 
                                              last_free_date, userId, orderId);

                    successCount++;
                    sheetSuccess++;
                    
                    txn.exec("RELEASE SAVEPOINT " + spName);
                    
                    if (successCount % 500 == 0) {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - insertStart).count();
                        std::cout << "[LOG] 已插入 " << successCount << " 行数据, 耗时: " << elapsed << "ms" << std::endl;
                    }
                    
                } catch (const pqxx::sql_error& e) {
                    rowFailed = true;
                    failCount++;
                    sheetFail++;
                    try { txn.exec("ROLLBACK TO SAVEPOINT " + spName); } catch (...) {}
                    if (sheetFail <= 5) {
                        std::cerr << "[ERROR] Sheet " << sheetNames[sheetIdx] << " Row " << rowNum << ": " << e.what() << std::endl;
                        std::cerr << "  >> TYPE=" << type_raw << " ETA=" << eta_raw
                                  << " FA=" << first_available_raw << " LFD=" << last_free_date_raw
                                  << " WT=" << weight_raw << " GI=" << gate_in_raw
                                  << " GO=" << gate_out_raw << " TT=" << tt_raw << std::endl;
                    }
                } catch (const std::exception& e) {
                    rowFailed = true;
                    failCount++;
                    sheetFail++;
                    try { txn.exec("ROLLBACK TO SAVEPOINT " + spName); } catch (...) {}
                    if (sheetFail <= 5) {
                        std::cerr << "[ERROR] Sheet " << sheetNames[sheetIdx] << " Row " << rowNum << ": " << e.what() << std::endl;
                        std::cerr << "  >> " << e.what() << std::endl;
                    }
                }
                
                if (rowFailed && sheetFail > 0 && sheetFail % 200 == 0) {
                    std::cerr << "[ERROR] 已连续失败 " << sheetFail << " 行，终止工作表 " << sheetNames[sheetIdx] << std::endl;
                    consecutiveEmpty = maxEmptyRows;
                }
            }
            
            auto sheetEnd = std::chrono::steady_clock::now();
            auto sheetElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(sheetEnd - sheetStart).count();
            std::cout << "[LOG] 工作表 " << sheetNames[sheetIdx] << " 完成, 成功: " << sheetSuccess 
                      << ", 失败: " << sheetFail << ", 耗时: " << sheetElapsed << "ms" << std::endl;
        }

        auto insertEnd = std::chrono::steady_clock::now();
        auto insertElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(insertEnd - insertStart).count();
        std::cout << "[LOG] 数据插入完成, 成功: " << successCount << ", 失败: " << failCount << ", 总耗时: " << insertElapsed << "ms" << std::endl;

        txn.commit();
        logTime("事务提交完成");

        doc.close();
        logTime("Excel 文件关闭");

        std::remove(tempFile.c_str());
        logTime("临时文件删除");

        auto totalEnd = std::chrono::steady_clock::now();
        auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - startTotal).count();
        std::cout << "[LOG] ========== importExcelFunc 完成, 总耗时: " << totalElapsed << "ms ==========" << std::endl;

        result["retCode"] = 200;
        result["msg"] = "导入完成，成功: " + std::to_string(successCount) + " 条，失败: " + std::to_string(failCount) + " 条";
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] importExcelFunc 异常: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = "导入失败: " + std::string(e.what());
        return crow::response(500, result);
    }
}


crow::response addOrderFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    std::string token = req.get_header_value("token");
    if (token == "") {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);

        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        const std::string username = decoded.get_subject();

        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        
        int userId = staffRes[0]["id"].as<int>();

        // ===================== 解析参数 =====================
        // 1. Jobs -> type
        int type = 1;
        if (body.has("type")) {
            std::string typeStr = body["type"].s();
            std::string upperType = typeStr;
            std::transform(upperType.begin(), upperType.end(), upperType.begin(), ::toupper);
            if (upperType == "EXPORT") {
                type = 2;
            } else if (upperType == "IMPORT") {
                type = 1;
            } else {
                try { type = std::stoi(typeStr); } catch (...) { type = 1; }
            }
        }

        // 2. Container Size -> size
        std::string size = "";
        if (body.has("size")) {
            size = body["size"].s();
        }

        // 3. Container Number -> container_no
        std::string container_no = "";
        if (body.has("containerNo")) {
            container_no = body["containerNo"].s();
        }

        if (container_no.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "container_no is required";
            return crow::response(400, result);
        }

        // 4. EIDO Pin -> pin
        std::string pin = "";
        if (body.has("pin")) {
            pin = body["pin"].s();
        }

        // 5. Weight -> weight
        std::string weight = "";
        if (body.has("weight")) {
            weight = body["weight"].s();
        }

        // 6. Shipping Line -> shipping_line
        std::string shipping_line = "";
        if (body.has("shippingLine")) {
            shipping_line = body["shippingLine"].s();
        }

        // 7. Free Detention Date -> last_free_date
        std::string last_free_date = "0001-01-01";
        if (body.has("lastFreeDate")) {
            last_free_date = body["lastFreeDate"].s();
        }

        // ✅ 8. Pick up Location -> start_point（起点）
        std::string start_point = "";
        if (body.has("port")) {
            start_point = body["port"].s();
        }

        // ✅ 9. Empty Dehire Depot -> end_point（终点）
        std::string end_point = "";
        if (body.has("emptyDehireDepot")) {
            end_point = body["emptyDehireDepot"].s();
        }

        // 10. Vessel Name -> vessel
        std::string vessel = "";
        if (body.has("vessel")) {
            vessel = body["vessel"].s();
        }

        // 11. ETA -> eta
        std::string eta = "0001-01-01";
        if (body.has("eta")) {
            eta = body["eta"].s();
        }

        // 12. ETD -> etd
        std::string etd = "0001-01-01";
        if (body.has("etd")) {
            etd = body["etd"].s();
        }

        // 13. First Free Date -> first_available
        std::string first_available = "0001-01-01";
        if (body.has("firstAvailable")) {
            first_available = body["firstAvailable"].s();
        }

        // 14. Last Free Date (LFD) -> last_free_date_lfd
        std::string last_free_date_lfd = "0001-01-01";
        if (body.has("lastFreeDateLfd")) {
            last_free_date_lfd = body["lastFreeDateLfd"].s();
        }

        // 15. Delivery Type -> delivery_type
        std::string delivery_type = "";
        if (body.has("deliveryType")) {
            delivery_type = body["deliveryType"].s();
        }

        // 16. Door Direction -> door_direction
        std::string door_direction = "";
        if (body.has("doorDirection")) {
            door_direction = body["doorDirection"].s();
        }

        // 17. Client Names -> client_name
        std::string client_name = "";
        if (body.has("clientName")) {
            client_name = body["clientName"].s();
        }

        // 18. Delivery Address -> customer_address
        std::string customer_address = "";
        if (body.has("customerAddress")) {
            customer_address = body["customerAddress"].s();
        }

        // ✅ 19. Instructions -> customer_note
        std::string customer_note = "";
        if (body.has("customerRequest")) {
            customer_note = body["customerRequest"].s();
        }

        // 20. Forwarder Name -> forwarder
        std::string forwarder = "";
        if (body.has("forwarder")) {
            forwarder = body["forwarder"].s();
        }

        // 21. Booking Person -> booking_person
        std::string booking_person = "";
        if (body.has("bookingPerson")) {
            booking_person = body["bookingPerson"].s();
        }

        // 22. Extra Surcharge -> extra_surcharge
        std::string extra_surcharge = "";
        if (body.has("extraSurcharge")) {
            extra_surcharge = body["extraSurcharge"].s();
        }

        int status = 1;
        int process_client_id = userId;
        
        // ===================== 插入 orders =====================
        pqxx::result res = txn.exec_params(
            "INSERT INTO orders ("
            "type, size, container_no, pin, weight, shipping_line, last_free_date, "
            "start_point, end_point, vessel, eta, etd, first_available, last_free_date_lfd, "
            "delivery_type, door_direction, client_name, customer_address, customer_note, "
            "forwarder, booking_person, extra_surcharge, "
            "status, process_client_id, create_time, create_user_id"
            ") VALUES ("
            "$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22,$23,$24,CURRENT_TIMESTAMP,$25"
            ") RETURNING id",
            type,
            size.empty() ? nullptr : size.c_str(),
            container_no.empty() ? nullptr : container_no.c_str(),
            pin.empty() ? nullptr : pin.c_str(),
            weight.empty() ? nullptr : weight.c_str(),
            shipping_line.empty() ? nullptr : shipping_line.c_str(),
            last_free_date == "0001-01-01" ? nullptr : last_free_date.c_str(),
            start_point.empty() ? nullptr : start_point.c_str(),      // ✅ Pick up Location
            end_point.empty() ? nullptr : end_point.c_str(),          // ✅ Empty Dehire Depot
            vessel.empty() ? nullptr : vessel.c_str(),
            eta == "0001-01-01" ? nullptr : eta.c_str(),
            etd == "0001-01-01" ? nullptr : etd.c_str(),
            first_available == "0001-01-01" ? nullptr : first_available.c_str(),
            last_free_date_lfd == "0001-01-01" ? nullptr : last_free_date_lfd.c_str(),
            delivery_type.empty() ? nullptr : delivery_type.c_str(),
            door_direction.empty() ? nullptr : door_direction.c_str(),
            client_name.empty() ? nullptr : client_name.c_str(),
            customer_address.empty() ? nullptr : customer_address.c_str(),
            customer_note.empty() ? nullptr : customer_note.c_str(),  // ✅ Instructions
            forwarder.empty() ? nullptr : forwarder.c_str(),
            booking_person.empty() ? nullptr : booking_person.c_str(),
            extra_surcharge.empty() ? nullptr : extra_surcharge.c_str(),
            status,
            process_client_id,
            userId
        );

        if (res.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "Failed to insert order";
            return crow::response(400, result);
        }

        int orderId = res[0]["id"].as<int>();

        createContainerFromOrder(txn, container_no, customer_note, 
                                  last_free_date, userId, orderId);

        result["retCode"] = 200;
        result["msg"] = "Order and container created successfully";
        result["orderId"] = orderId;
        txn.commit();

    } catch (const std::exception& e) {
        std::cerr << "Add Order Error: " << e.what() << std::endl;
        result["retCode"] = 400;
        result["errorMsg"] = e.what();
        return crow::response(400, result);
    }

    return crow::response(200, result);
}

crow::response queryOrdersFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    std::string token = req.get_header_value("token");
    if (token == "") {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    try {
        pqxx::work txn(conn);

        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        // ✅ 添加新字段到 SELECT
        std::string baseQuery = "SELECT id, type, start_point, end_point, size, container_no, pin, customer_note, "
                                 "vessel, shipping_line, eta, first_available, last_free_date, "
                                 "client_name, customer_address, forwarder, weight, invoice_id, noted, "
                                 "status, process_client_id, create_time, create_user_id, "
                                 "port, empty_dehire_depot, etd, last_free_date_lfd, "
                                 "delivery_type, door_direction, booking_person, extra_surcharge "
                                 "FROM orders WHERE 1=1";
        
        std::vector<std::string> conditions;
        std::vector<std::string> params;
        int paramCounter = 1;
        
        auto get_param = [&req](const std::string& key) -> std::string {
            char* value = req.url_params.get(key);
            return value ? std::string(value) : "";
        };
        
        std::vector<std::string> paramKeys = {
            "id", "type", "from", "to", "size", "containerNo", "pin", 
            "customerRequest", "vessel", "shippingLine", "eta", 
            "firstAvailable", "lastFreeDate", "clientName", 
            "customerAddress", "forwarder", "weight", "invoice", "noted",
            "status", "process_client_id", "create_user_id",
            "eta_start", "eta_end", "first_available_start", "first_available_end",
            "last_free_date_start", "last_free_date_end",
            "port", "etd", "delivery_type", "door_direction",
            "pageNum", "pageSize"
        };
        
        std::unordered_map<std::string, std::string> queryParams;
        for (const auto& key : paramKeys) {
            std::string value = get_param(key);
            if (!value.empty()) {
                queryParams[key] = value;
            }
        }
        
        struct FilterField {
            std::string paramName;
            std::string dbField;
            bool isLike;
        };
        
        std::vector<FilterField> filters = {
            {"id", "id", false},
            {"type", "type", true},
            {"from", "start_point", true},
            {"to", "end_point", true},
            {"size", "size", false},
            {"containerNo", "container_no", true},
            {"pin", "pin", true},
            {"customerRequest", "customer_note", true},
            {"vessel", "vessel", true},
            {"shippingLine", "shipping_line", true},
            {"eta", "eta", false},
            {"firstAvailable", "first_available", false},
            {"lastFreeDate", "last_free_date", false},
            {"clientName", "client_name", true},
            {"customerAddress", "customer_address", true},
            {"forwarder", "forwarder", true},
            {"weight", "weight", false},
            {"invoice", "invoice_id", true},
            {"noted", "noted", true},
            {"status", "status", false},
            {"process_client_id", "process_client_id", false},
            {"create_user_id", "create_user_id", false},
            {"eta_start", "eta", false},
            {"eta_end", "eta", false},
            {"first_available_start", "first_available", false},
            {"first_available_end", "first_available", false},
            {"last_free_date_start", "last_free_date", false},
            {"last_free_date_end", "last_free_date", false},
            {"port", "port", true},
            {"etd", "etd", false},
            {"delivery_type", "delivery_type", false},
            {"door_direction", "door_direction", false}
        };
        
        for (const auto& filter : filters) {
            auto it = queryParams.find(filter.paramName);
            if (it != queryParams.end() && !it->second.empty()) {
                std::string condition;
                
                if (filter.isLike) {
                    condition = filter.dbField + " LIKE $" + std::to_string(paramCounter);
                    params.push_back("%" + it->second + "%");
                } else if (filter.paramName.find("_start") != std::string::npos) {
                    condition = filter.dbField + " >= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else if (filter.paramName.find("_end") != std::string::npos) {
                    condition = filter.dbField + " <= $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                } else {
                    condition = filter.dbField + " = $" + std::to_string(paramCounter);
                    params.push_back(it->second);
                }
                
                conditions.push_back(condition);
                paramCounter++;
            }
        }
        
        int pageNum = 1;
        int pageSize = 20;
        
        auto itPage = queryParams.find("pageNum");
        if (itPage != queryParams.end() && !itPage->second.empty()) {
            pageNum = std::stoi(itPage->second);
        }
        
        auto itSize = queryParams.find("pageSize");
        if (itSize != queryParams.end() && !itSize->second.empty()) {
            pageSize = std::stoi(itSize->second);
        }
        
        int offset = (pageNum - 1) * pageSize;
        
        std::string finalQuery = baseQuery;
        for (const auto& cond : conditions) {
            finalQuery += " AND " + cond;
        }
        
        finalQuery += " ORDER BY id DESC";
        finalQuery += " LIMIT $" + std::to_string(paramCounter) + " OFFSET $" + std::to_string(paramCounter + 1);
        params.push_back(std::to_string(pageSize));
        params.push_back(std::to_string(offset));
        
        pqxx::result res = txn.exec_params(finalQuery, pqxx::prepare::make_dynamic_params(params));
        
        std::string countQuery = "SELECT COUNT(*) FROM orders WHERE 1=1";
        for (const auto& cond : conditions) {
            countQuery += " AND " + cond;
        }
        
        std::vector<std::string> countParams;
        for (size_t i = 0; i < params.size() - 2; i++) {
            countParams.push_back(params[i]);
        }
        
        pqxx::result countRes;
        if (countParams.empty()) {
            countRes = txn.exec(countQuery);
        } else {
            countRes = txn.exec_params(countQuery, pqxx::prepare::make_dynamic_params(countParams));
        }
        
        int total = countRes[0][0].as<int>();

        crow::json::wvalue::list order_list;
        
        for (const auto& row : res) {
            crow::json::wvalue order;
            order["id"] = row["id"].as<int>();
            order["type"] = row["type"].is_null() ? 0 : row["type"].as<int>();
            order["from"] = row["start_point"].is_null() ? "" : row["start_point"].c_str();
            order["to"] = row["end_point"].is_null() ? "" : row["end_point"].c_str();
            order["size"] = row["size"].is_null() ? "" : row["size"].c_str();
            order["containerNo"] = row["container_no"].is_null() ? "" : row["container_no"].c_str();
            order["pin"] = row["pin"].is_null() ? "" : row["pin"].c_str();
            order["customerRequest"] = row["customer_note"].is_null() ? "" : row["customer_note"].c_str();
            order["vessel"] = row["vessel"].is_null() ? "" : row["vessel"].c_str();
            order["shippingLine"] = row["shipping_line"].is_null() ? "" : row["shipping_line"].c_str();
            order["eta"] = row["eta"].is_null() ? "" : row["eta"].c_str();
            order["firstAvailable"] = row["first_available"].is_null() ? "" : row["first_available"].c_str();
            order["lastFreeDate"] = row["last_free_date"].is_null() ? "" : row["last_free_date"].c_str();
            order["clientName"] = row["client_name"].is_null() ? "" : row["client_name"].c_str();
            order["customerAddress"] = row["customer_address"].is_null() ? "" : row["customer_address"].c_str();
            order["forwarder"] = row["forwarder"].is_null() ? "" : row["forwarder"].c_str();
            order["weight"] = row["weight"].is_null() ? "" : row["weight"].c_str();
            order["invoice"] = row["invoice_id"].is_null() ? "" : row["invoice_id"].c_str();
            order["noted"] = row["noted"].is_null() ? "" : row["noted"].c_str();
            
            // ✅ 返回状态值（前端根据状态映射显示对应标签和颜色）
            order["status"] = row["status"].is_null() ? 1 : row["status"].as<int>();
            
            order["process_client_id"] = row["process_client_id"].is_null() ? 0 : row["process_client_id"].as<int>();
            order["create_time"] = row["create_time"].is_null() ? "" : row["create_time"].c_str();
            order["create_user_id"] = row["create_user_id"].is_null() ? 0 : row["create_user_id"].as<int>();
            
            // 新增字段
            order["port"] = row["port"].is_null() ? "" : row["port"].c_str();
            order["emptyDehireDepot"] = row["empty_dehire_depot"].is_null() ? "" : row["empty_dehire_depot"].c_str();
            order["etd"] = row["etd"].is_null() ? "" : row["etd"].c_str();
            order["lastFreeDateLfd"] = row["last_free_date_lfd"].is_null() ? "" : row["last_free_date_lfd"].c_str();
            order["deliveryType"] = row["delivery_type"].is_null() ? "" : row["delivery_type"].c_str();
            order["doorDirection"] = row["door_direction"].is_null() ? "" : row["door_direction"].c_str();
            order["bookingPerson"] = row["booking_person"].is_null() ? "" : row["booking_person"].c_str();
            order["extraSurcharge"] = row["extra_surcharge"].is_null() ? "" : row["extra_surcharge"].c_str();
            
            order_list.push_back(std::move(order));
        }

        txn.commit();

        result["retCode"] = 200;
        result["rows"] = std::move(order_list);
        result["total"] = total;
        result["pageNum"] = pageNum;
        result["pageSize"] = pageSize;

    } catch (const std::exception& e) {
        std::cerr << "Query Orders Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }

    return crow::response(200, result);
}

// ==================== 审核通过 ====================
crow::response approveOrderFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    // Token 校验
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);

        // JWT 验证
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        const std::string username = decoded.get_subject();

        // 获取当前用户ID
        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();

        // 必填字段校验
        if (!body.has("orderId")) {
            result["retCode"] = 400;
            result["errorMsg"] = "orderId is required";
            return crow::response(400, result);
        }

        // 解析 orderId（支持 "ORD-123" 或纯数字 "123"）
        std::string orderIdStr = body["orderId"].s();
        int orderId = 0;
        
        size_t dashPos = orderIdStr.find('-');
        if (dashPos != std::string::npos) {
            orderId = std::stoi(orderIdStr.substr(dashPos + 1));
        } else {
            orderId = std::stoi(orderIdStr);
        }

        // 检查订单是否存在且状态为待审核(3)
        pqxx::result checkRes = txn.exec_params(
            "SELECT id, status FROM orders WHERE id = $1", orderId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Order not found";
            return crow::response(404, result);
        }

        int currentStatus = checkRes[0]["status"].as<int>();
        if (currentStatus != 3) {
            result["retCode"] = 400;
            result["errorMsg"] = "Order is not in pending review status";
            return crow::response(400, result);
        }

        // 审核通过：状态变为 4（已完成）
        txn.exec_params(
            "UPDATE orders SET status = 4, process_client_id = $1, update_time = CURRENT_TIMESTAMP WHERE id = $2",
            userId, orderId
        );

        // 获取订单的 container_no
        pqxx::result orderRes = txn.exec_params(
            "SELECT container_no FROM orders WHERE id = $1", orderId);
        std::string containerNo = orderRes[0]["container_no"].is_null() ? "" : orderRes[0]["container_no"].as<std::string>();

        // 如果有关联的 container，更新其状态
        if (!containerNo.empty()) {
            txn.exec_params(
                "UPDATE container SET status = '满柜', updated_at = CURRENT_TIMESTAMP WHERE container_no = $1",
                containerNo.c_str()
            );
        }

        result["retCode"] = 200;
        result["msg"] = "Order approved successfully";
        result["data"]["orderId"] = orderId;
        result["data"]["newStatus"] = 4;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Approve Order Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}


// ==================== 驳回订单 ====================
crow::response rejectOrderFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    // Token 校验
    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    // 解析请求体
    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);

        // JWT 验证
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        const std::string username = decoded.get_subject();

        // 获取当前用户ID
        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();

        // 必填字段校验
        if (!body.has("orderId")) {
            result["retCode"] = 400;
            result["errorMsg"] = "orderId is required";
            return crow::response(400, result);
        }

        // 解析 orderId
        std::string orderIdStr = body["orderId"].s();
        int orderId = 0;
        
        size_t dashPos = orderIdStr.find('-');
        if (dashPos != std::string::npos) {
            orderId = std::stoi(orderIdStr.substr(dashPos + 1));
        } else {
            orderId = std::stoi(orderIdStr);
        }

        // 检查订单是否存在且状态为待审核(3)
        pqxx::result checkRes = txn.exec_params(
            "SELECT id, status FROM orders WHERE id = $1", orderId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Order not found";
            return crow::response(404, result);
        }

        int currentStatus = checkRes[0]["status"].as<int>();
        if (currentStatus != 3) {
            result["retCode"] = 400;
            result["errorMsg"] = "Order is not in pending review status";
            return crow::response(400, result);
        }

        // 获取驳回原因（可选）
        std::string rejectReason = "";
        if (body.has("rejectReason")) {
            rejectReason = body["rejectReason"].s();
        }

        // 驳回：状态变为 6（订单异常）
        txn.exec_params(
            "UPDATE orders SET status = 6, process_client_id = $1, noted = $2, update_time = CURRENT_TIMESTAMP WHERE id = $3",
            userId,
            rejectReason.empty() ? nullptr : rejectReason.c_str(),
            orderId
        );

        result["retCode"] = 200;
        result["msg"] = "Order rejected successfully";
        result["data"]["orderId"] = orderId;
        result["data"]["newStatus"] = 6;
        result["data"]["rejectReason"] = rejectReason;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Reject Order Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

// ==================== 更新订单状态 ====================
crow::response updateOrderStatusFunc(const crow::request& req, pqxx::connection& conn) {
    crow::json::wvalue result;

    std::string token = req.get_header_value("token");
    if (token.empty()) {
        result["retCode"] = 401;
        result["errorMsg"] = "Missing token";
        return crow::response(401, result);
    }

    auto body = crow::json::load(req.body);
    if (!body) {
        result["retCode"] = 400;
        result["errorMsg"] = "Request body error";
        return crow::response(400, result);
    }

    try {
        pqxx::work txn(conn);

        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"user_management"})
            .with_issuer("user_management");
        verifier.verify(decoded);

        const std::string username = decoded.get_subject();

        pqxx::result staffRes = txn.exec_params(
            "SELECT id FROM staff WHERE username = $1", username);
        if (staffRes.empty()) {
            result["retCode"] = 400;
            result["errorMsg"] = "User not found";
            return crow::response(400, result);
        }
        int userId = staffRes[0]["id"].as<int>();

        if (!body.has("id") || !body.has("status")) {
            result["retCode"] = 400;
            result["errorMsg"] = "id and status are required";
            return crow::response(400, result);
        }

        int orderId = body["id"].i();
        int newStatus = body["status"].i();

        if (newStatus < 1 || newStatus > 10) {
            result["retCode"] = 400;
            result["errorMsg"] = "Invalid status value, must be between 1 and 10";
            return crow::response(400, result);
        }

        pqxx::result checkRes = txn.exec_params(
            "SELECT id, status FROM orders WHERE id = $1", orderId);
        if (checkRes.empty()) {
            result["retCode"] = 404;
            result["errorMsg"] = "Order not found";
            return crow::response(404, result);
        }

        int oldStatus = checkRes[0]["status"].as<int>();

        // ✅ 修复：使用 updated_at（如果表中有该字段）
        // 如果没有 updated_at，使用下面注释掉的版本
        txn.exec_params(
            "UPDATE orders SET status = $1, process_client_id = $2, updated_at = CURRENT_TIMESTAMP WHERE id = $3",
            newStatus, userId, orderId
        );

        result["retCode"] = 200;
        result["msg"] = "Order status updated successfully";
        result["data"]["orderId"] = orderId;
        result["data"]["oldStatus"] = oldStatus;
        result["data"]["newStatus"] = newStatus;

        txn.commit();
        return crow::response(200, result);

    } catch (const std::exception& e) {
        std::cerr << "Update Order Status Error: " << e.what() << std::endl;
        result["retCode"] = 500;
        result["errorMsg"] = e.what();
        return crow::response(500, result);
    }
}

AUTO_REGISTER_ORDER_API("addOrder", addOrderFunc);
AUTO_REGISTER_ORDER_API("importExcel", importExcelFunc);
AUTO_REGISTER_ORDER_API("queryOrders", queryOrdersFunc);
AUTO_REGISTER_ORDER_API("approveOrder", approveOrderFunc);
AUTO_REGISTER_ORDER_API("rejectOrder", rejectOrderFunc);
AUTO_REGISTER_ORDER_API("updateOrderStatus", updateOrderStatusFunc);