#include "mysql_client.h"
#include <iomanip>
#include <iostream>
#include <spdlog/spdlog.h>
#include <sstream>

MysqlClient::MysqlClient(const std::string &host, const std::string &user,
                         const std::string &password, const std::string &database, int port)
    : database(database) {

    tables = {"package", "AI_error"};

    conn = mysql_init(nullptr);
    if (!conn) {
        spdlog::error("mysql_init() failed");
        throw std::runtime_error("mysql_init() failed");
    }

    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), database.c_str(),
                            port, nullptr, 0)) {
        std::string error = mysql_error(conn);

        spdlog::error("MySQL连接失败: host={}, port={}, user={}, database={}, error={}", host, port,
                      user, database, error);

        mysql_close(conn);
        throw std::runtime_error("Connect failed: " + error);
    }

    if (mysql_set_character_set(conn, "utf8mb4") != 0) {
        spdlog::warn("设置MySQL字符集失败: {}", mysql_error(conn));
    }

    spdlog::info("MySQL连接成功: {}:{} / {}", host, port, database);
    initTables();
    if (!initTables()) {
        spdlog::error("数据库表初始化失败");
        throw std::runtime_error("init tables failed");
    }
}

bool MysqlClient::initTables() {

    std::string sql;

    sql = "CREATE TABLE IF NOT EXISTS package("
          "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
          "code VARCHAR(255) UNIQUE,"
          "car VARCHAR(64),"
          "pc VARCHAR(64),"
          "grid VARCHAR(64),"
          "up_time DATETIME,"
          "down_time DATETIME,"
          "up_video VARCHAR(255),"
          "down_video VARCHAR(255),"
          "up_error VARCHAR(255),"
          "down_error VARCHAR(255)"
          ")";

    if (!executeQuery(sql)) {
        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS AI_error("
          "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
          "grid VARCHAR(64),"
          "create_time DATETIME,"
          "beep_state TINYINT(1) DEFAULT 1,"
          "video VARCHAR(255),"
          "package_id VARCHAR(64)"
          ")";

    if (!executeQuery(sql)) {
        return false;
    }

    return true;
}

MysqlClient::~MysqlClient() {
    if (conn) {
        mysql_close(conn);
    }
}

std::time_t MysqlClient::parseTime(const std::string &timeStr) {
    // 解析格式: YYYYMMDDHHMMSSFFFFFF
    // 例如: 20260103153045123456
    struct tm tm = {};
    int microseconds;

    if (timeStr.length() >= 14) {
        tm.tm_year = std::stoi(timeStr.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(timeStr.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(timeStr.substr(6, 2));
        tm.tm_hour = std::stoi(timeStr.substr(8, 2));
        tm.tm_min = std::stoi(timeStr.substr(10, 2));
        tm.tm_sec = std::stoi(timeStr.substr(12, 2));
    }

    return mktime(&tm);
}

bool MysqlClient::executeQuery(const std::string &sql) {

    std::lock_guard<std::mutex> lock(mysql_mutex);
    if (mysql_query(conn, sql.c_str())) {
        std::cerr << "❌ SQL执行失败: " << mysql_error(conn) << std::endl;
        std::cerr << "SQL: " << sql << std::endl;
        return false;
    }
    return true;
}

bool MysqlClient::saveRecord(const std::map<std::string, std::string> &msg) {
    auto it = msg.find("action");
    if (it == msg.end()) {
        std::cerr << "❌ 缺少action字段" << std::endl;
        return false;
    }

    std::string action = it->second;
    std::string code = msg.at("code");
    std::string grid = msg.at("grid");
    std::string car = msg.at("car");
    std::string pc = msg.at("pc");
    std::string timeStr = msg.at("time");

    // 转义特殊字符防止SQL注入
    char escapedCar[256], escapedPc[256], escapedCode[256], escapedGrid[256];
    mysql_real_escape_string(conn, escapedCar, car.c_str(), car.length());
    mysql_real_escape_string(conn, escapedPc, pc.c_str(), pc.length());
    mysql_real_escape_string(conn, escapedCode, code.c_str(), code.length());
    mysql_real_escape_string(conn, escapedGrid, grid.c_str(), grid.length());

    std::time_t eventTime = parseTime(timeStr);
    char timeBuffer[32];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&eventTime));

    std::string sql;

    if (action == "up_car") {
        sql = "INSERT INTO package(car, pc, code, grid, up_time) "
              "VALUES('" +
              std::string(escapedCar) +
              "', "
              "'" +
              std::string(escapedPc) +
              "', "
              "'" +
              std::string(escapedCode) +
              "', "
              "'" +
              std::string(escapedGrid) +
              "', "
              "'" +
              std::string(timeBuffer) +
              "') "
              "ON DUPLICATE KEY UPDATE "
              "car=VALUES(car), pc=VALUES(pc), grid=VALUES(grid), up_time=VALUES(up_time)";
    } else if (action == "down_car") {
        sql = "UPDATE package "
              "SET down_time='" +
              std::string(timeBuffer) +
              "', "
              "grid='" +
              std::string(escapedGrid) +
              "', "
              "car='" +
              std::string(escapedCar) +
              "', "
              "pc='" +
              std::string(escapedPc) +
              "' "
              "WHERE code='" +
              std::string(escapedCode) + "'";
    } else {
        std::cerr << "❌ 未知action: " << action << std::endl;
        return false;
    }

    return executeQuery(sql);
}

bool MysqlClient::updateVideo(const std::string &code, const std::string &video, VideoType type) {

    std::string field;

    switch (type) {

    case VideoType::UP:
        field = "up_video";
        break;

    case VideoType::DOWN:
        field = "down_video";
        break;

    case VideoType::UP_ERROR:
        field = "up_error";
        break;

    case VideoType::DOWN_ERROR:
        field = "down_error";
        break;

    default:
        return false;
    }

    char escCode[128];
    char escVideo[512];

    mysql_real_escape_string(conn, escCode, code.c_str(), code.length());

    mysql_real_escape_string(conn, escVideo, video.c_str(), video.length());

    std::string sql = "UPDATE package SET " + field + "='" + std::string(escVideo) +
                      "' WHERE code='" + std::string(escCode) + "'";

    return executeQuery(sql);
}

bool MysqlClient::upAIError(const std::string &grid, const std::string &time,
                            const std::string &video, const std::string &package_id) {

    char escapedGrid[128] = {0};
    char escapedTime[128] = {0};
    char escapedVideo[512] = {0};
    char escapedPackageId[256] = {0};

    mysql_real_escape_string(conn, escapedGrid, grid.c_str(), grid.length());

    mysql_real_escape_string(conn, escapedTime, time.c_str(), time.length());

    mysql_real_escape_string(conn, escapedVideo, video.c_str(), video.length());

    mysql_real_escape_string(conn, escapedPackageId, package_id.c_str(), package_id.length());

    std::string sql = "INSERT INTO ai_error "
                      "(grid, create_time, beep_state, video, package_id) VALUES ('" +
                      std::string(escapedGrid) + "', '" + std::string(escapedTime) + "', 1, '" +
                      std::string(escapedVideo) + "', '" + std::string(escapedPackageId) + "')";

    return executeQuery(sql);
}

bool MysqlClient::clearAIError(const std::string &grid) {

    char escapedGrid[128];

    mysql_real_escape_string(conn, escapedGrid, grid.c_str(), grid.length());

    std::string sql = "UPDATE ai_error "
                      "SET beep_state=0 "
                      "WHERE grid='" +
                      std::string(escapedGrid) +
                      "' "
                      "AND beep_state=1";

    return executeQuery(sql);
}