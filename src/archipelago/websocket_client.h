#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>

// Forward declarations to avoid platform-specific includes in header
struct WebSocketImpl;

// WebSocket connection states
enum class WSConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Closing,
    Failed
};

// WebSocket message types
enum class WSMessageType
{
    Text,
    Binary,
    Close,
    Ping,
    Pong
};

// WebSocket message structure
struct WSMessage
{
    WSMessageType type;
    std::string data;
    
    WSMessage(WSMessageType t, const std::string& d) : type(t), data(d) {}
    WSMessage(WSMessageType t, std::string&& d) : type(t), data(std::move(d)) {}
};

// Callback function types
using WSConnectedCallback = std::function<void()>;
using WSDisconnectedCallback = std::function<void(int code, const std::string& reason)>;
using WSMessageCallback = std::function<void(const WSMessage& message)>;
using WSErrorCallback = std::function<void(const std::string& error)>;

/**
 * Cross-platform WebSocket client implementation
 * 
 * This class provides a thread-safe WebSocket client that can connect to
 * Archipelago servers. It handles the WebSocket protocol, connection 
 * management, and provides async callbacks for events.
 */
class WebSocketClient
{
public:
    WebSocketClient();
    ~WebSocketClient();
    
    // Delete copy constructor and assignment operator
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;
    
    // Connection management
    bool Connect(const std::string& uri);
    bool Connect(const std::string& host, int port, const std::string& path = "/", bool use_ssl = false);
    void Disconnect(int code = 1000, const std::string& reason = "Normal closure");
    
    // State queries
    WSConnectionState GetState() const { return state_; }
    bool IsConnected() const { return state_ == WSConnectionState::Connected; }
    bool IsConnecting() const { return state_ == WSConnectionState::Connecting; }
    
    // Message sending
    bool SendText(const std::string& message);
    bool SendBinary(const std::string& data);
    bool SendPing(const std::string& data = "");
    
    // Callback registration
    void SetConnectedCallback(WSConnectedCallback callback) { on_connected_ = callback; }
    void SetDisconnectedCallback(WSDisconnectedCallback callback) { on_disconnected_ = callback; }
    void SetMessageCallback(WSMessageCallback callback) { on_message_ = callback; }
    void SetErrorCallback(WSErrorCallback callback) { on_error_ = callback; }
    
    // Configuration
    void SetReconnectEnabled(bool enabled) { auto_reconnect_ = enabled; }
    void SetReconnectDelay(int milliseconds) { reconnect_delay_ms_ = milliseconds; }
    void SetConnectionTimeout(int milliseconds) { connection_timeout_ms_ = milliseconds; }
    void SetPingInterval(int milliseconds) { ping_interval_ms_ = milliseconds; }
    
    // Statistics
    size_t GetMessagesSent() const { return messages_sent_; }
    size_t GetMessagesReceived() const { return messages_received_; }
    size_t GetBytesReceived() const { return bytes_received_; }
    size_t GetBytesSent() const { return bytes_sent_; }
    
private:
    // Internal state management
    void SetState(WSConnectionState new_state);
    void HandleError(const std::string& error);
    void HandleConnected();
    void HandleDisconnected(int code, const std::string& reason);
    void HandleMessage(const WSMessage& message);
    
    // Threading and connection management
    void ConnectionThreadProc();
    void MessageThreadProc();
    void ReconnectThreadProc();
    
    // Platform-specific implementation
    bool ConnectImpl(const std::string& uri);
    void DisconnectImpl();
    bool SendImpl(const std::string& data, WSMessageType type);
    
    // Connection parameters
    std::string uri_;
    std::string host_;
    int port_;
    std::string path_;
    bool use_ssl_;
    
    // State management
    std::atomic<WSConnectionState> state_;
    std::unique_ptr<WebSocketImpl> impl_;
    
    // Threading
    std::unique_ptr<std::thread> connection_thread_;
    std::unique_ptr<std::thread> message_thread_;
    std::unique_ptr<std::thread> reconnect_thread_;
    std::atomic<bool> should_stop_;
    std::atomic<bool> should_reconnect_;
    
    // Message queue for thread-safe sending
    std::queue<WSMessage> outbound_queue_;
    std::queue<WSMessage> inbound_queue_;
    mutable std::mutex outbound_mutex_;
    mutable std::mutex inbound_mutex_;
    std::condition_variable outbound_cv_;
    std::condition_variable inbound_cv_;
    
    // Callbacks
    WSConnectedCallback on_connected_;
    WSDisconnectedCallback on_disconnected_;
    WSMessageCallback on_message_;
    WSErrorCallback on_error_;
    
    // Configuration
    std::atomic<bool> auto_reconnect_;
    std::atomic<int> reconnect_delay_ms_;
    std::atomic<int> connection_timeout_ms_;
    std::atomic<int> ping_interval_ms_;
    
    // Statistics
    std::atomic<size_t> messages_sent_;
    std::atomic<size_t> messages_received_;
    std::atomic<size_t> bytes_sent_;
    std::atomic<size_t> bytes_received_;
    
    // Internal helpers
    void ProcessInboundMessages();
    void ProcessOutboundMessages();
    bool ShouldReconnect() const;
    void ScheduleReconnect();
};

/**
 * Utility functions for WebSocket operations
 */
namespace WSUtils
{
    // URL parsing
    struct ParsedURL
    {
        std::string scheme;
        std::string host;
        int port;
        std::string path;
        bool is_secure;
        
        ParsedURL() : port(0), is_secure(false) {}
    };
    
    bool ParseWebSocketURL(const std::string& url, ParsedURL& result);
    std::string BuildWebSocketURL(const std::string& host, int port, const std::string& path, bool secure);
    
    // Protocol helpers
    std::string GenerateWebSocketKey();
    std::string CalculateWebSocketAccept(const std::string& key);
    bool ValidateWebSocketAccept(const std::string& key, const std::string& accept);
    
    // Error handling
    std::string GetWebSocketErrorString(int error_code);
    
    // Platform detection
    bool IsWindows();
    bool IsLinux();
    bool IsMacOS();
}