#include "pch.hpp"

namespace {
    constexpr int kWebSocketRetryCount = 5;
    constexpr int kWebSocketRetryDelaySec = 2;

    void run_main_loop(easywsclient::WebSocket* web_socket) {
        auto start = std::chrono::system_clock::now();
        while (true) {
            const auto now = std::chrono::system_clock::now();
            const auto duration = now - start;
            if (duration >= std::chrono::milliseconds(100)) {
                start = now;
                sdk::update();
                f::run();
                web_socket->send(f::m_data.dump());
            }
            web_socket->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool main()
{
    if (!exc::setup()) {
        LOG_ERROR("Failed to initialize exception handler.");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }
    LOG_INFO("Exception handler initialized.");

    if (!m_memory->setup()) {
        LOG_ERROR("Failed to initialize memory.");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }
    LOG_INFO("Memory initialized.");

    if (!i::setup()) {
        LOG_ERROR("Failed to initialize interfaces.");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }
    LOG_INFO("Interfaces initialized.");

    if (!schema::setup()) {
        LOG_ERROR("Failed to initialize schema.");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }
    LOG_INFO("Schema initialized.");

    WSADATA wsa_data = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        LOG_ERROR("Failed to initialize Winsock.");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }
    LOG_INFO("Winsock initialized.");

    // Use a hardcoded IP address for WebSocket connection
    const std::string ipv4_address = "127.0.0.1";
    const auto formatted_address = std::format("ws://{}:22006/cs2_webradar", ipv4_address);
    easywsclient::WebSocket* web_socket = nullptr;

    // Retry WebSocket connection a few times before giving up
    for (int attempt = 1; attempt <= kWebSocketRetryCount; ++attempt) {
        LOG_INFO("Attempting to connect to WebSocket (attempt %d/%d): '%s'", attempt, kWebSocketRetryCount, formatted_address.c_str());
        web_socket = easywsclient::WebSocket::from_url(formatted_address);
        if (web_socket) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(kWebSocketRetryDelaySec));
    }

    if (!web_socket) {
        LOG_ERROR("Failed to connect to the WebSocket ('%s') after %d attempts.", formatted_address.c_str(), kWebSocketRetryCount);
        return false;
    }
    LOG_INFO("Connected to the WebSocket ('%s').", formatted_address.c_str());

    run_main_loop(web_socket);

    // Optionally, clean up WebSocket (if your library requires it)
    // delete web_socket;

    return true;
}
