#pragma once
#include <cstdint>
namespace music {
enum class Connection { ConnectingWifi, ConnectingBridge, RetryWifi, RetryBridge, Offline, Waiting, Ready };
inline Connection connection(bool received, bool wifi, bool stale, bool available,
                             uint32_t startupAge, uint32_t stateAge) {
    if (!received) {
        if (startupAge < 10000) return wifi ? Connection::ConnectingBridge : Connection::ConnectingWifi;
        return wifi ? Connection::RetryBridge : Connection::RetryWifi;
    }
    if (!wifi || stateAge > 7000) return Connection::Offline;
    if (stale || !available) return Connection::Waiting;
    return Connection::Ready;
}
inline const char* connectionBadge(Connection value) {
    switch(value) {
        case Connection::ConnectingWifi: case Connection::ConnectingBridge: return "CONNECTING";
        case Connection::RetryWifi: case Connection::RetryBridge: return "RETRYING";
        case Connection::Offline: return "OFFLINE";
        case Connection::Waiting: return "WAITING";
        default: return "";
    }
}
inline const char* connectionTitle(Connection value) {
    switch(value) {
        case Connection::ConnectingWifi: return "正在连接 Wi-Fi";
        case Connection::ConnectingBridge: return "正在连接 Mac";
        case Connection::RetryWifi: return "等待 Wi-Fi 连接";
        case Connection::RetryBridge: return "等待 Mac 连接";
        default: return "";
    }
}
}
