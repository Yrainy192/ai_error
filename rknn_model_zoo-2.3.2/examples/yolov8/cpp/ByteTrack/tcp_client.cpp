#include "tcp_client.h"
#include "task_manager.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
// ================= 构造 =================
TCPClient::TCPClient(const std::string &host, int port, TaskManager *task_manager,
                     MysqlClient *mysql_client)
    : host(host), port(port), task_manager(task_manager), mysql_client(mysql_client), sock_fd(-1),
      running(false), connected(false) {}

// ================= 析构 =================
TCPClient::~TCPClient() { stop(); }

// ================= connect =================
bool TCPClient::connect() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("[TCP] socket failed");
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        // std::cerr << "[TCP] invalid IP: " << host << std::endl;
        spdlog::debug("[TCP] invalid IP: {}", host);
        ::close(sock_fd);
        sock_fd = -1;
        return false;
    }

    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(sock_fd, (sockaddr *)&server_addr, sizeof(server_addr));

    if (ret < 0 && errno != EINPROGRESS) {
        perror("[TCP] connect failed");
        ::close(sock_fd);
        sock_fd = -1;
        return false;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock_fd, &fdset);

    timeval tv{5, 0};

    if (select(sock_fd + 1, nullptr, &fdset, nullptr, &tv) <= 0) {
        // std::cerr << "[TCP] connect timeout" << std::endl;
        spdlog::debug("[TCP] connect timeout");
        ::close(sock_fd);
        sock_fd = -1;
        return false;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err != 0) {
        // std::cerr << "[TCP] connect error: " << strerror(err) << std::endl;
        spdlog::error("[TCP] connect error: {}", strerror(err));
        ::close(sock_fd);
        sock_fd = -1;
        return false;
    }

    fcntl(sock_fd, F_SETFL, flags);

    connected = true;
    // std::cout << "[TCP] connected " << host << ":" << port << std::endl;
    spdlog::info("[TCP] connected {}:{}", host, port);
    return true;
}

// ================= close =================
void TCPClient::close() {
    connected = false;

    if (sock_fd != -1) {
        ::shutdown(sock_fd, SHUT_RDWR);
        ::close(sock_fd);
        sock_fd = -1;
    }

    std::cout << "[TCP] closed" << std::endl;
}

// ================= start =================
void TCPClient::start() {
    if (running)
        return;

    running = true;

    if (!connect()) {
        running = false;
        return;
    }

    std::thread(&TCPClient::receiveLoop, this).detach();
}

// ================= stop =================
void TCPClient::stop() {
    running = false;
    close();
}

// ================= send =================
void TCPClient::sendMessage(const std::map<std::string, std::string> &msg) {
    if (!connected) {
        if (!connect())
            return;
    }

    Json::Value root;
    for (auto &[k, v] : msg) {
        root[k] = v;
    }

    Json::StreamWriterBuilder builder;
    std::string json = Json::writeString(builder, root);
    json += "#";

    send(sock_fd, json.c_str(), json.size(), 0);
}

// ================= recv =================
void TCPClient::receiveLoop() {
    char buffer[2048];
    std::string cache;

    while (running) {
        ssize_t n = recv(sock_fd, buffer, sizeof(buffer), 0);
        cache.append(buffer, n);

        size_t pos;
        while ((pos = cache.find('#')) != std::string::npos) {
            std::string packet = cache.substr(0, pos);
            cache.erase(0, pos + 1);

            if (packet.empty())
                continue;

            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;

            std::istringstream ss(packet);

            if (Json::parseFromStream(builder, ss, &root, &errs)) {
                handleMessage(root);
            }
        }
    }
}

void TCPClient::handleMessage(const Json::Value &msg) {
    if (!msg.isMember("action"))
        return;

    std::string action = msg["action"].asString();
    if (action == "ping")
        return;

    Json::FastWriter writer;
    spdlog::info("[TCP RECEIVE] msg={}", writer.write(msg));

    std::string code = msg.get("code", "").asString();
    std::string grid = msg.get("grid", "").asString();
    std::string car = msg.get("car", "").asString();
    std::string pc = msg.get("pc", "").asString();
    std::string time = msg.get("time", "").asString();

    if (code.empty() || grid.empty())
        return;

    std::map<std::string, std::string> msg_map;
    msg_map["action"] = action;
    msg_map["code"] = code;
    msg_map["grid"] = grid;
    msg_map["car"] = car;
    msg_map["pc"] = pc;

    int cam_id = -1;
    int roi_id = -1;
    // =========================
    // down_car：走正常映射
    // =========================
    if (action == "down_car") {
        int grid_id = std::stoi(grid);

        GridMapItem item;
        if (!task_manager->mapGrid(grid_id, item)) {
            // std::cerr << "[TCP ERROR] unknown grid=" << grid_id << std::endl;
            spdlog::debug("[TCP ERROR] unknown grid={}", grid_id);
            return;
        }

        cam_id = item.cam_id;
        roi_id = item.roi_id;

        msg_map["time"] = time;
    }
    // =========================
    // up_car：固定相机 + 固定ROI
    // =========================
    else if (action == "up_car") {
        cam_id = 0; // 👈 你指定的相机ID（改成你的实际值）
        roi_id = 1; // 👈 只监控一个区域

        msg_map["time"] = time;
    }
    mysql_client->saveRecord(msg_map);
    // =========================
    // 任务逻辑（up/down都执行）
    // =========================
    task_manager->setTask(code, grid, cam_id, roi_id);
}