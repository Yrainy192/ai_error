#pragma once
#include <json/json.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// =========================
// 独立的报警上报 TCP 客户端（仅发送）
// 用于给【新的上位机】上报格口错分/滞留等AI报警消息
// 与 tcp_client.h（接收任务通知）完全独立，互不影响
// =========================
class AlarmTcpClient {
  public:
    AlarmTcpClient();
    ~AlarmTcpClient();

    void setServer(const std::string &host, int port);

    void start();
    void stop();

    // 发送 JSON 消息（以 # 结尾），失败自动重连
    void sendMessage(const std::map<std::string, std::string> &msg);

  private:
    bool connect();
    void close();
    void reconnectLoop();

  private:
    std::string host_;
    int port_ = 0;

    int sock_fd_ = -1;
    bool running_ = false;
    bool connected_ = false;

    std::thread thread_;
    std::mutex state_mutex_; // 保护 connected_ / sock_fd_
    std::mutex send_mutex_;  // 串行化发送
};
