#pragma once
#include <windows.h>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rds_lab {
// A test controller acknowledges each validated guest preflight through its
// child's stdin pipe. No arbitrary text, device identifiers or host input.
class FixtureControlBuffer {
public:
    static constexpr size_t limit = 32;
    size_t size() const { return data_.size(); }
    void feed(std::span<const char> bytes) {
        if (bytes.empty()) return;
        if (bytes.size() > limit - data_.size()) throw std::runtime_error("Fixture acknowledgement exceeds its bound.");
        for (char value : bytes)
            if ((value < 'a' || value > 'z') && value != '\r' && value != '\n')
                throw std::runtime_error("Invalid fixture acknowledgement character.");
        data_.append(bytes.data(), bytes.size());
    }
    bool consume(std::string_view expected) {
        const size_t newline = data_.find('\n');
        if (newline == std::string::npos) return false;
        size_t length = newline;
        if (length && data_[length - 1] == '\r') --length;
        if (newline + 1 != data_.size() || std::string_view(data_.data(), length) != expected)
            throw std::runtime_error("Out-of-order or pipelined fixture acknowledgement.");
        data_.clear(); return true;
    }
private:
    std::string data_;
};
class FixtureControl {
public:
    FixtureControl() : pipe_(GetStdHandle(STD_INPUT_HANDLE)) {
        if (!pipe_ || pipe_ == INVALID_HANDLE_VALUE || GetFileType(pipe_) != FILE_TYPE_PIPE)
            throw std::runtime_error("Router hotplug requires a controller stdin pipe.");
    }
    bool poll(std::string_view expected) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr))
            throw std::runtime_error("Fixture controller pipe closed.");
        if (!available) return false;
        if (available > FixtureControlBuffer::limit - buffer_.size())
            throw std::runtime_error("Too much fixture controller input.");
        std::array<char, FixtureControlBuffer::limit> bytes{};
        DWORD read = 0;
        if (!ReadFile(pipe_, bytes.data(), available, &read, nullptr) || !read)
            throw std::runtime_error("Fixture controller input failed.");
        buffer_.feed(std::span<const char>(bytes.data(), read));
        return buffer_.consume(expected);
    }
private:
    HANDLE pipe_;
    FixtureControlBuffer buffer_;
};
}
