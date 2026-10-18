#pragma once
// Include this before Windows.h: the native RDP core needs Winsock2, and the
// frontend owns its process-level startup/cleanup lifetime.
#include <winsock2.h>
#include <ws2tcpip.h>

namespace rds_lab {
class SocketRuntime {
public:
    SocketRuntime();
    ~SocketRuntime();
    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;
};
bool numeric_address_smoke();
}
