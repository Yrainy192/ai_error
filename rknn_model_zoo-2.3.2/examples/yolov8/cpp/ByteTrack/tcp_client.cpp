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
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("[TCP] socket failed");
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        // std::cerr << "[TCP] invalid IP: " << host << std::endl;
        spdlog::debug("[TCP] invalid IP: {}", host);
        ::close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = ::connect(fd, (sockaddr *)&server_addr, sizeof(server_addr));

    if (ret < 0 && errno != EINPROGRESS) {
        perror("[TCP] connect failed");
        ::close(fd);
        return false;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);

    timeval tv{5, 0};

    if (select(fd + 1, nullptr, &fdset, nullptr, &tv) <= 0) {
        // std::cerr << "[TCP] connect timeout" << std::endl;
        spdlog::debug("[TCP] connect timeout");
        ::close(fd);
        return false;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);

    if (err != 0) {
        // std::cerr << "[TCP] connect error: " << strerror(err) << std::endl;
        spdlog::error("[TCP] connect error: {}", strerror(err));
        ::close(fd);
        return false;
    }

    fcntl(fd, F_SETFL, flags);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sock_fd = fd;
        connected = true;
    }
    // std::cout << "[TCP] connected " << host << ":" << port << std::endl;
    spdlog::info("[TCP] connected {}:{}", host, port);
    return true;
}

// ================= close =================
void TCPClient::close() {
    std::lock_guard<std::mutex> lock(state_mutex_);
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
    thread_ = std::thread(&TCPClient::reconnectLoop, this);
}

// ================= stop =================
void TCPClient::stop() {
    running = false;
    close();

    if (thread_.joinable())
        thread_.join();
}

// ================= send =================
void TCPClient::sendMessage(const std::map<std::string, std::string> &msg) {
    std::lock_guard<std::mutex> lock(send_mutex);

    int fd = -1;
    {
        std::lock_guard<std::mutex> sl(state_mutex_);
        if (!connected || sock_fd == -1)
            return; // 未连接：交给重连线程自动重连
        fd = sock_fd;
    }

    Json::Value root;
    for (auto &[k, v] : msg) {
        root[k] = v;
    }

    Json::StreamWriterBuilder builder;
    std::string json = Json::writeString(builder, root);
    json += "#";

    ssize_t n = ::send(fd, json.c_str(), json.size(), 0);
    if (n < 0) {
        spdlog::warn("[TCP] send failed: {}", strerror(errno));
        close(); // 标记断开，让重连线程重连
    }
}

// ================= 重连保活线程 =================
void TCPClient::reconnectLoop() {
    while (running) {
        bool need_connect = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            need_connect = !connected || sock_fd == -1;
        }

        if (need_connect) {
            if (!connect()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            // 连接成功后启动接收线程
            std::thread(&TCPClient::receiveLoop, this).detach();
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// ================= recv =================
void TCPClient::receiveLoop() {
    char buffer[2048];
    std::string cache;

    while (running) {
        int fd = -1;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            fd = sock_fd;
        }
        if (fd == -1)
            break;

        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            // 断开：标记断开，交给重连线程重连
            spdlog::info("[TCP] receive closed, will reconnect");
            close();
            break;
        }
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

    std::map<std::string, std::string> msg_map;
    msg_map["action"] = action;
    msg_map["code"] = code;
    msg_map["grid"] = grid;
    msg_map["car"] = car;
    msg_map["pc"] = pc;

    int cam_id = -1;
    int roi_id = -1;
    // =========================
    // up_car：固定相机 + 固定ROI
    // 上车时 code/grid 可能为空（还没扫码、没分配格口），属正常，直接存库
    // =========================
    if (action == "up_car") {
        cam_id = 0; // 指定的相机ID
        roi_id = 1; // 只监控一个区域

        msg_map["time"] = time;
    }
    // =========================
    // down_car：必须有 code 和 grid，走正常映射
    // =========================
    else {
        if (code.empty() || grid.empty()) {
            spdlog::warn("[TCP DROP] code 或 grid 为空: code={}, grid={}, action={}", code, grid,
                         action);
            return;
        }
        // 格口限制：只处理1~40号格口
        int grid_num = std::stoi(grid);

        if (grid_num > 40 || grid_num <= 0) {
            spdlog::warn("[TCP IGNORE] invalid grid={}, code={}", grid_num, code);
            return;
        }

        if (action == "down_car") {
            int grid_id = std::stoi(grid);
            GridMapItem item;
            if (!task_manager->mapGrid(grid_id, item)) {
                spdlog::warn("[TCP ERROR] unknown grid={}, code={}", grid_id, code);
                return;
            }

            cam_id = item.cam_id;
            roi_id = item.roi_id;

            msg_map["time"] = time;
        }
    }

    mysql_client->saveRecord(msg_map);
    // =========================
    // 任务逻辑（up/down都执行）
    // =========================
    task_manager->setTask(code, grid, cam_id, roi_id);
}