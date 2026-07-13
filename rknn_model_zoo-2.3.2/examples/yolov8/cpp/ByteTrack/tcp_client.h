#pragma once
#include <json/json.h>
#include <map>
#include <mutex>
#include <mysql_client.h>
#include <string>
#include <thread>
class TaskManager;
class MysqlClient; // 前向声明（可以保留）

class TCPClient {
  public:
    TCPClient(const std::string &host, int port, TaskManager *task_manager,
              MysqlClient *mysql_client);

    ~TCPClient();

    void start();
    void stop();

    void sendMessage(const std::map<std::string, std::string> &msg);

  private:
    void receiveLoop();
    void handleMessage(const Json::Value &msg);
    std::string buildJsonMessage(const std::map<std::string, std::string> &msg);

    bool connect();
    void close();

  private:
    std::string host;
    int port;

    int sock_fd;
    bool running;
    bool connected;

    std::thread recv_thread;
    std::mutex send_mutex;

    TaskManager *task_manager;
    MysqlClient *mysql_client; // ⭐关键
};