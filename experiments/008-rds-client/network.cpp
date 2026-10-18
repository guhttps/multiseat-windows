#include "network.h"
#include <stdexcept>

namespace rds_lab {
SocketRuntime::SocketRuntime() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        throw std::runtime_error("Winsock2 startup failed.");
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        WSACleanup();
        throw std::runtime_error("Winsock 2.2 is unavailable.");
    }
}
SocketRuntime::~SocketRuntime() { WSACleanup(); }
bool numeric_address_smoke() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // Both inputs are numeric: this tests the same API the RDP core uses
    // without issuing DNS requests or opening/connecting any socket.
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* addresses = nullptr;
    if (getaddrinfo("10.73.12.2", "3389", &hints, &addresses) != 0) return false;
    bool valid = addresses && addresses->ai_family == AF_INET &&
        addresses->ai_addrlen >= sizeof(sockaddr_in) && addresses->ai_addr;
    if (valid) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
        valid = ntohl(address->sin_addr.s_addr) == 0x0A490C02 && ntohs(address->sin_port) == 3389;
    }
    freeaddrinfo(addresses);
    return valid;
}
}
