#include "AlarmTcpClient.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ================= 构造/析构 =================
AlarmTcpClient::AlarmTcpClient() : sock_fd_(-1), running_(false), connected_(false) {}

AlarmTcpClient::~AlarmTcpClient() { stop(); }

void AlarmTcpClient::setServer(const std::string &host, int port) {
    host_ = host;
    port_ = port;
}

// ================= 连接（非阻塞 + select 超时） =================
bool AlarmTcpClient::connect() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        spdlog::error("[AlarmTcp] socket failed: {}", strerror(errno));
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        spdlog::debug("[AlarmTcp] invalid IP: {}", host_);
        ::close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(fd, (sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        spdlog::debug("[AlarmTcp] connect failed: {}", strerror(errno));
        ::close(fd);
        return false;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    timeval tv{5, 0};

    if (select(fd + 1, nullptr, &fdset, nullptr, &tv) <= 0) {
        spdlog::debug("[AlarmTcp] connect timeout");
        ::close(fd);
        return false;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        spdlog::debug("[AlarmTcp] connect error: {}", strerror(err));
        ::close(fd);
        return false;
    }

    fcntl(fd, F_SETFL, flags);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sock_fd_ = fd;
        connected_ = true;
    }
    spdlog::info("[AlarmTcp] connected {}:{}", host_, port_);
    return true;
}

// ================= 关闭 =================
void AlarmTcpClient::close() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    connected_ = false;
    if (sock_fd_ != -1) {
        ::shutdown(sock_fd_, SHUT_RDWR);
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

// ================= 启动/停止 =================
void AlarmTcpClient::start() {
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread(&AlarmTcpClient::reconnectLoop, this);
}

void AlarmTcpClient::stop() {
    running_ = false;
    close();
    if (thread_.joinable())
        thread_.join();
}

// ================= 重连保活线程 =================
void AlarmTcpClient::reconnectLoop() {
    while (running_) {
        bool need_connect = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            need_connect = !connected_ || sock_fd_ == -1;
        }

        if (need_connect) {
            if (!connect()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// ================= 发送 =================
void AlarmTcpClient::sendMessage(const std::map<std::string, std::string> &msg) {
    Json::Value root;
    for (auto &[k, v] : msg) {
        root[k] = v;
    }
    Json::StreamWriterBuilder builder;
    std::string json = Json::writeString(builder, root);
    json += "#";

    std::lock_guard<std::mutex> lock(send_mutex_);

    int fd = -1;
    {
        std::lock_guard<std::mutex> sl(state_mutex_);
        if (!connected_ || sock_fd_ == -1)
            return;
        fd = sock_fd_;
    }

    ssize_t n = ::send(fd, json.c_str(), json.size(), 0);
    if (n < 0) {
        spdlog::warn("[AlarmTcp] send failed: {}", strerror(errno));
        // 标记断开，让重连线程重连
        close();
        return;
    }
    spdlog::info("[AlarmTcp] send: {}", json);
}
