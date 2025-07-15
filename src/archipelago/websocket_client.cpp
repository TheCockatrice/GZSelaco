#include "websocket_client.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif

// Platform-specific WebSocket implementation
struct WebSocketImpl
{
    int socket_fd = -1;
    bool connected = false;
    std::string host;
    int port;
    std::string path;
    bool use_ssl;
    
    // Simple HTTP upgrade to WebSocket
    bool SendHandshake(const std::string& key)
    {
        std::string handshake = 
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + ":" + std::to_string(port) + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        
        int bytes_sent = send(socket_fd, handshake.c_str(), handshake.length(), 0);
        return bytes_sent == handshake.length();
    }
    
    bool ReceiveHandshakeResponse()
    {
        char buffer[4096];
        int bytes_received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) return false;
        
        buffer[bytes_received] = '\0';
        std::string response(buffer);
        
        // Basic validation of WebSocket handshake response
        return response.find("HTTP/1.1 101") != std::string::npos &&
               response.find("Upgrade: websocket") != std::string::npos;
    }
};

WebSocketClient::WebSocketClient()
    : state_(WSConnectionState::Disconnected)
    , impl_(std::make_unique<WebSocketImpl>())
    , should_stop_(false)
    , should_reconnect_(false)
    , auto_reconnect_(false)
    , reconnect_delay_ms_(3000)
    , connection_timeout_ms_(10000)
    , ping_interval_ms_(30000)
    , messages_sent_(0)
    , messages_received_(0)
    , bytes_sent_(0)
    , bytes_received_(0)
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebSocketClient::~WebSocketClient()
{
    Disconnect();
    
    if (connection_thread_ && connection_thread_->joinable()) {
        connection_thread_->join();
    }
    if (message_thread_ && message_thread_->joinable()) {
        message_thread_->join();
    }
    if (reconnect_thread_ && reconnect_thread_->joinable()) {
        reconnect_thread_->join();
    }
    
#ifdef _WIN32
    WSACleanup();
#endif
}

bool WebSocketClient::Connect(const std::string& uri)
{
    WSUtils::ParsedURL parsed;
    if (!WSUtils::ParseWebSocketURL(uri, parsed)) {
        HandleError("Invalid WebSocket URL: " + uri);
        return false;
    }
    
    return Connect(parsed.host, parsed.port, parsed.path, parsed.is_secure);
}

bool WebSocketClient::Connect(const std::string& host, int port, const std::string& path, bool use_ssl)
{
    if (state_ == WSConnectionState::Connected || state_ == WSConnectionState::Connecting) {
        return false;
    }
    
    host_ = host;
    port_ = port;
    path_ = path;
    use_ssl_ = use_ssl;
    
    SetState(WSConnectionState::Connecting);
    
    // Start connection in background thread
    should_stop_ = false;
    connection_thread_ = std::make_unique<std::thread>(&WebSocketClient::ConnectionThreadProc, this);
    
    return true;
}

void WebSocketClient::Disconnect(int code, const std::string& reason)
{
    should_stop_ = true;
    should_reconnect_ = false;
    
    if (impl_->connected) {
        // Send close frame (simplified)
        uint8_t close_frame[] = { 0x88, 0x00 }; // Close frame, no payload
        send(impl_->socket_fd, reinterpret_cast<char*>(close_frame), 2, 0);
        
#ifdef _WIN32
        closesocket(impl_->socket_fd);
#else
        close(impl_->socket_fd);
#endif
        impl_->connected = false;
    }
    
    SetState(WSConnectionState::Disconnected);
}

bool WebSocketClient::SendText(const std::string& message)
{
    if (!IsConnected()) return false;
    
    // Queue message for sending
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_queue_.emplace(WSMessageType::Text, message);
    outbound_cv_.notify_one();
    
    return true;
}

bool WebSocketClient::SendBinary(const std::string& data)
{
    if (!IsConnected()) return false;
    
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_queue_.emplace(WSMessageType::Binary, data);
    outbound_cv_.notify_one();
    
    return true;
}

bool WebSocketClient::SendPing(const std::string& data)
{
    if (!IsConnected()) return false;
    
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_queue_.emplace(WSMessageType::Ping, data);
    outbound_cv_.notify_one();
    
    return true;
}

void WebSocketClient::SetState(WSConnectionState new_state)
{
    state_ = new_state;
}

void WebSocketClient::HandleError(const std::string& error)
{
    if (on_error_) {
        on_error_(error);
    }
    SetState(WSConnectionState::Failed);
}

void WebSocketClient::HandleConnected()
{
    SetState(WSConnectionState::Connected);
    if (on_connected_) {
        on_connected_();
    }
    
    // Start message processing thread
    message_thread_ = std::make_unique<std::thread>(&WebSocketClient::MessageThreadProc, this);
}

void WebSocketClient::HandleDisconnected(int code, const std::string& reason)
{
    SetState(WSConnectionState::Disconnected);
    if (on_disconnected_) {
        on_disconnected_(code, reason);
    }
    
    if (ShouldReconnect()) {
        ScheduleReconnect();
    }
}

void WebSocketClient::HandleMessage(const WSMessage& message)
{
    messages_received_++;
    bytes_received_ += message.data.size();
    
    if (on_message_) {
        on_message_(message);
    }
}

void WebSocketClient::ConnectionThreadProc()
{
    // Create socket
    impl_->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->socket_fd < 0) {
        HandleError("Failed to create socket");
        return;
    }
    
    // Resolve hostname
    struct hostent* host_entry = gethostbyname(host_.c_str());
    if (!host_entry) {
        HandleError("Failed to resolve hostname: " + host_);
        return;
    }
    
    // Setup address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    memcpy(&server_addr.sin_addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
    
    // Connect to server
    if (connect(impl_->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        HandleError("Failed to connect to server");
        return;
    }
    
    // Perform WebSocket handshake
    std::string ws_key = WSUtils::GenerateWebSocketKey();
    if (!impl_->SendHandshake(ws_key)) {
        HandleError("Failed to send WebSocket handshake");
        return;
    }
    
    if (!impl_->ReceiveHandshakeResponse()) {
        HandleError("Invalid WebSocket handshake response");
        return;
    }
    
    impl_->connected = true;
    HandleConnected();
}

void WebSocketClient::MessageThreadProc()
{
    char buffer[4096];
    
    while (!should_stop_ && impl_->connected) {
        // Simple WebSocket frame parsing (basic implementation)
        int bytes_received = recv(impl_->socket_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            HandleDisconnected(1006, "Connection lost");
            break;
        }
        
        // Parse WebSocket frame (simplified)
        if (bytes_received >= 2) {
            uint8_t* frame = reinterpret_cast<uint8_t*>(buffer);
            bool fin = (frame[0] & 0x80) != 0;
            uint8_t opcode = frame[0] & 0x0F;
            bool masked = (frame[1] & 0x80) != 0;
            uint8_t payload_len = frame[1] & 0x7F;
            
            if (fin && opcode == 0x01) { // Text frame
                int header_size = 2;
                if (masked) header_size += 4; // Mask key
                
                if (bytes_received > header_size && payload_len <= bytes_received - header_size) {
                    std::string message(buffer + header_size, payload_len);
                    HandleMessage(WSMessage(WSMessageType::Text, std::move(message)));
                }
            }
        }
        
        // Process outbound messages
        ProcessOutboundMessages();
    }
}

void WebSocketClient::ProcessOutboundMessages()
{
    std::unique_lock<std::mutex> lock(outbound_mutex_);
    while (!outbound_queue_.empty() && impl_->connected) {
        WSMessage msg = outbound_queue_.front();
        outbound_queue_.pop();
        lock.unlock();
        
        // Send WebSocket frame (simplified)
        if (msg.type == WSMessageType::Text) {
            std::vector<uint8_t> frame;
            frame.push_back(0x81); // FIN + Text frame
            
            if (msg.data.size() < 126) {
                frame.push_back(static_cast<uint8_t>(msg.data.size()));
            } else {
                // Extended payload length (not implemented for simplicity)
                frame.push_back(126);
                frame.push_back((msg.data.size() >> 8) & 0xFF);
                frame.push_back(msg.data.size() & 0xFF);
            }
            
            // Add payload
            frame.insert(frame.end(), msg.data.begin(), msg.data.end());
            
            int bytes_sent = send(impl_->socket_fd, reinterpret_cast<char*>(frame.data()), frame.size(), 0);
            if (bytes_sent > 0) {
                messages_sent_++;
                bytes_sent_ += bytes_sent;
            }
        }
        
        lock.lock();
    }
}

bool WebSocketClient::ShouldReconnect() const
{
    return auto_reconnect_ && !should_stop_;
}

void WebSocketClient::ScheduleReconnect()
{
    should_reconnect_ = true;
    reconnect_thread_ = std::make_unique<std::thread>(&WebSocketClient::ReconnectThreadProc, this);
}

void WebSocketClient::ReconnectThreadProc()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));
    
    if (should_reconnect_ && !should_stop_) {
        Connect(host_, port_, path_, use_ssl_);
    }
}

// Utility functions
namespace WSUtils
{
    bool ParseWebSocketURL(const std::string& url, ParsedURL& result)
    {
        // Simple URL parsing for ws:// and wss://
        std::string lower_url = url;
        std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
        
        if (lower_url.find("ws://") == 0) {
            result.scheme = "ws";
            result.is_secure = false;
            result.port = 80;
        } else if (lower_url.find("wss://") == 0) {
            result.scheme = "wss";
            result.is_secure = true;
            result.port = 443;
        } else {
            return false;
        }
        
        // Extract host and path
        size_t start = url.find("://") + 3;
        size_t path_pos = url.find('/', start);
        size_t port_pos = url.find(':', start);
        
        if (path_pos == std::string::npos) {
            result.path = "/";
            path_pos = url.length();
        } else {
            result.path = url.substr(path_pos);
        }
        
        if (port_pos != std::string::npos && port_pos < path_pos) {
            result.host = url.substr(start, port_pos - start);
            result.port = std::stoi(url.substr(port_pos + 1, path_pos - port_pos - 1));
        } else {
            result.host = url.substr(start, path_pos - start);
        }
        
        return true;
    }
    
    std::string GenerateWebSocketKey()
    {
        // Simple key generation (not cryptographically secure)
        std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; // Base64 encoded sample
        return key;
    }
    
    bool IsWindows() { 
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }
    
    bool IsLinux() {
#ifdef __linux__
        return true;
#else
        return false;
#endif
    }
}