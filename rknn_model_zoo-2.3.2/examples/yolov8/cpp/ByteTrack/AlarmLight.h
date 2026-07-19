#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class AlarmLight {

  public:
    AlarmLight(const std::string &ip, int port);

    ~AlarmLight();

    /*
        TCP连接
    */
    bool connect();

    /*
        开灯

        slave:
        1-32
    */
    bool on(uint8_t slave_id);

    /*
        关灯
    */
    bool off(uint8_t slave_id);

    /*
        查询状态

        返回:

        0x0011:
            报警


        0x0000:
            空闲

    */
    bool readStatus(uint8_t slave_id, uint16_t &status);

    /*
        长报警等待解除

    */
    bool waitAlarmClear(uint8_t slave_id);

  private:
    /*
        写寄存器

    */
    bool writeRegister(uint8_t slave_id, uint16_t addr, uint16_t value);

    /*
        Modbus完整事务

        send + recv必须一起锁

    */
    bool transact(const std::vector<uint8_t> &request, std::vector<uint8_t> &response);

    bool sendFrame(const std::vector<uint8_t> &frame);

    bool recvFrame(std::vector<uint8_t> &data);

    uint16_t crc16(const uint8_t *data, int len);

    void closeSocket();

  private:
    std::string ip_;
    int port_;
    int sock_fd_;

    /*
        保护一次完整Modbus通信
        send + recv + parse

    */

    std::mutex mutex_;
};