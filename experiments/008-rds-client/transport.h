#pragma once
#include "config.h"
#include "input-router.h"
#include <memory>

namespace rds_lab {
enum class TransportState { Idle, Connecting, Connected, Closed, Failed, CertificateRejected };
class Transport {
public:
    Transport(const Config& config, unsigned seat_index, HWND notify_window);
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    void start();
    void stop();
    bool enqueue(const InputEvent& event);
    TransportState state() const;
    unsigned error_code() const;
    unsigned frame_count() const;
    std::string diagnostics() const;
    bool draw(HDC dc, const RECT& target);
    static bool library_smoke();
    static bool enable_safe_ssl_diagnostics();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
inline constexpr UINT frame_message = WM_APP + 18;
inline constexpr UINT state_message = WM_APP + 19;
}
