#ifndef MYSQL_CLIENT_HPP
#define MYSQL_CLIENT_HPP

#include "VideoType.h"
#include <ctime>
#include <map>
#include <mutex>
#include <mysql/mysql.h>
#include <vector>
class MysqlClient {
  public:
    MysqlClient(const std::string &host, const std::string &user, const std::string &password,
                const std::string &database, int port = 3306);

    bool initTables();

    ~MysqlClient();

    bool saveRecord(const std::map<std::string, std::string> &msg);

    bool updateVideo(const std::string &code, const std::string &video, VideoType type);

    bool upAIError(const std::string &grid, const std::string &time, const std::string &video,
                   const std::string &package_id);

    bool clearAIError(const std::string &grid);

  private:
    MYSQL *conn;
    std::string database;

    std::mutex mysql_mutex;

    bool executeQuery(const std::string &sql);
    std::time_t parseTime(const std::string &timeStr);

    std::vector<std::string> tables;
};

#endif // MYSQL_CLIENT_HPP