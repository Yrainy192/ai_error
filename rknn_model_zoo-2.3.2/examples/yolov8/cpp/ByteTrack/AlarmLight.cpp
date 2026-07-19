#include "AlarmLight.h"

#include <cstring>
#include <iostream>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

AlarmLight::AlarmLight(const std::string &ip, int port) : ip_(ip), port_(port), sock_fd_(-1) {}

AlarmLight::~AlarmLight() { closeSocket(); }

void AlarmLight::closeSocket() {

    if (sock_fd_ >= 0) {

        close(sock_fd_);

        sock_fd_ = -1;
    }
}

bool AlarmLight::connect() {

    if (sock_fd_ >= 0)
        return true;

    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd_ < 0)
        return false;

    sockaddr_in addr{};

    addr.sin_family = AF_INET;

    addr.sin_port = htons(port_);

    inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr);

    if (::connect(sock_fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
        closeSocket();
        return false;
    }
    return true;
}

uint16_t AlarmLight::crc16(const uint8_t *data, int len) {
    uint16_t crc = 0xffff;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc >>= 1;

                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool AlarmLight::transact(const std::vector<uint8_t> &req, std::vector<uint8_t> &resp) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!sendFrame(req)) {
        return false;
    }

    uint8_t slave_id = req[0];

    uint8_t func = req[1];

    while (true) {

        std::vector<uint8_t> frame;

        if (!recvFrame(frame)) {
            return false;
        }

        for (auto b : frame) {
            printf("%02X ", b);
        }
        printf("\n");
        if (frame.size() < 2)
            continue;
        if (frame[0] != slave_id) {
            printf("[AlarmLight] ignore slave %d\n", frame[0]);
            continue;
        }

        if (frame[1] != func) {
            printf("[AlarmLight] ignore func %02X\n", frame[1]);
            continue;
        }
        resp = frame;
        return true;
    }
}

bool AlarmLight::sendFrame(const std::vector<uint8_t> &frame) {

    int ret = send(sock_fd_, frame.data(), frame.size(), 0);
    if (ret != (int)frame.size()) {

        closeSocket();
        return false;
    }
    return true;
}
bool AlarmLight::recvFrame(std::vector<uint8_t> &resp) {
    resp.clear();

    uint8_t b;

    while (true) {
        int n = recv(sock_fd_, &b, 1, 0);

        if (n <= 0)
            return false;

        if (b >= 1 && b <= 32) {
            resp.push_back(b);
            break;
        }
    }

    int n = recv(sock_fd_, &b, 1, 0);

    if (n <= 0)
        return false;

    resp.push_back(b);

    uint8_t func = resp[1];

    if (func != 0x03) {
        /*
            这里收到06或者其他数据
            不处理
        */

        printf("[AlarmLight] ignore func %02X\n", func);

        resp.clear();

        return false;
    }

    // 03
    n = recv(sock_fd_, &b, 1, 0);

    if (n <= 0)
        return false;

    resp.push_back(b);

    uint8_t len = resp[2];

    int frame_len = 3 + len + 2;

    while (resp.size() < frame_len) {
        n = recv(sock_fd_, &b, 1, 0);

        if (n <= 0)
            return false;

        resp.push_back(b);
    }

    return true;
}

bool AlarmLight::writeRegister(uint8_t slave_id, uint16_t addr, uint16_t value) {
    std::vector<uint8_t> frame;

    frame.push_back(slave_id);
    frame.push_back(0x06);

    frame.push_back(addr >> 8);
    frame.push_back(addr & 0xff);

    frame.push_back(value >> 8);
    frame.push_back(value & 0xff);

    uint16_t crc = crc16(frame.data(), frame.size());

    frame.push_back(crc & 0xff);
    frame.push_back(crc >> 8);

    std::lock_guard<std::mutex> lock(mutex_);

    if (!sendFrame(frame)) {
        return false;
    }

    for (auto b : frame) {
        printf("%02X ", b);
    }

    printf("\n");

    return true;
}

bool AlarmLight::on(uint8_t slave_id) { return writeRegister(slave_id, 0x0020, 0x0011); }

bool AlarmLight::off(uint8_t slave_id) { return writeRegister(slave_id, 0x0020, 0x0000); }

bool AlarmLight::readStatus(uint8_t slave_id, uint16_t &status) {
    std::vector<uint8_t> frame;
    frame.push_back(slave_id);
    frame.push_back(0x03);
    frame.push_back(0x00);
    frame.push_back(0x20);
    frame.push_back(0x00);
    frame.push_back(0x01);
    uint16_t crc = crc16(frame.data(), frame.size());
    frame.push_back(crc & 0xff);
    frame.push_back(crc >> 8);
    std::vector<uint8_t> resp;
    if (!transact(frame, resp))
        return false;
    // 打印收到的TCP数据
    std::cout << "[AlarmLight RX] ";
    for (auto b : resp) {
        printf("%02X ", b);
    }
    std::cout << std::endl;
    if (resp.size() != 7)
        return false;
    status = (resp[3] << 8) | resp[4];
    return true;
}

// bool AlarmLight::waitAlarmClear(uint8_t slave_id) {
//     while (true) {
//         uint16_t status = 0;
//         if (!readStatus(slave_id, status)) {
//             return false;
//         }
//         // std::cout << "报警灯 " << (uint16_t)status << " 已处理" << std::endl;
//         if (status == 0x0000) {

//             std::cout << "报警灯 " << (int)slave_id << " 已处理" << std::endl;
//             return true;
//         }
//         std::cout << "报警灯 " << (int)slave_id << " 报警中" << std::endl;
//         usleep(500000);
//     }
// }
bool AlarmLight::waitAlarmClear(uint8_t slave_id) {
    bool alarm_started = false;

    while (true) {
        uint16_t status = 0;

        if (!readStatus(slave_id, status)) {
            std::cout << "读取报警灯状态失败 slave=" << (int)slave_id << std::endl;

            // 不要认为报警结束
            usleep(500000);
            continue;
        }

        std::cout << "alarm slave=" << (int)slave_id << " status=" << status << std::endl;

        if (status != 0) {
            alarm_started = true;
        }

        if (alarm_started && status == 0) {
            std::cout << "报警灯 " << (int)slave_id << " 已处理" << std::endl;

            return true;
        }

        usleep(500000);
    }
}