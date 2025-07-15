#pragma once

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <optional>

// Archipelago Protocol Version
#define AP_PROTOCOL_VERSION "0.5.1"

// Connection states
enum class APConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Authenticated,
    Failed
};

// Client status values
enum class APClientStatus : int32_t
{
    CLIENT_UNKNOWN = 0,
    CLIENT_CONNECTED = 5,
    CLIENT_READY = 10,
    CLIENT_PLAYING = 20,
    CLIENT_GOAL = 30
};

// Item flags
enum class APItemFlags : int32_t
{
    NONE = 0,
    ADVANCEMENT = 0b001,
    USEFUL = 0b010,
    TRAP = 0b100
};

// Permission flags
enum class APPermission : int32_t
{
    DISABLED = 0b000,
    ENABLED = 0b001,
    GOAL = 0b010,
    AUTO = 0b110,
    AUTO_ENABLED = 0b111
};

// Network structures matching Archipelago protocol
struct APNetworkVersion
{
    int major = 0;
    int minor = 5;
    int build = 1;
    std::string class_name = "Version";
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const;
    bool FromJSON(const rapidjson::Value& value);
};

struct APNetworkItem
{
    int64_t item = 0;
    int64_t location = 0;
    int32_t player = 0;
    int32_t flags = 0;
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const;
    bool FromJSON(const rapidjson::Value& value);
};

struct APNetworkPlayer
{
    int32_t team = 0;
    int32_t slot = 0;
    std::string alias;
    std::string name;
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const;
    bool FromJSON(const rapidjson::Value& value);
};

struct APJSONMessagePart
{
    std::string type;
    std::string text;
    std::string color;
    int32_t flags = 0;
    int32_t player = 0;
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const;
    bool FromJSON(const rapidjson::Value& value);
};

// Protocol packets
struct APPacket
{
    std::string cmd;
    
    virtual ~APPacket() = default;
    virtual rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const = 0;
    virtual bool FromJSON(const rapidjson::Value& value) = 0;
};

// Client -> Server packets
struct APConnectPacket : public APPacket
{
    std::string password;
    std::string game = "Selaco";
    std::string name;
    std::string uuid;
    APNetworkVersion version;
    int32_t items_handling = 0b111; // Get all items
    std::vector<std::string> tags;
    bool slot_data = true;
    
    APConnectPacket() { cmd = "Connect"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APSyncPacket : public APPacket
{
    APSyncPacket() { cmd = "Sync"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APLocationChecksPacket : public APPacket
{
    std::vector<int64_t> locations;
    
    APLocationChecksPacket() { cmd = "LocationChecks"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APStatusUpdatePacket : public APPacket
{
    APClientStatus status = APClientStatus::CLIENT_UNKNOWN;
    
    APStatusUpdatePacket() { cmd = "StatusUpdate"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APSayPacket : public APPacket
{
    std::string text;
    
    APSayPacket() { cmd = "Say"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APGetDataPackagePacket : public APPacket
{
    std::vector<std::string> games;
    
    APGetDataPackagePacket() { cmd = "GetDataPackage"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

// Server -> Client packets
struct APRoomInfoPacket : public APPacket
{
    APNetworkVersion version;
    APNetworkVersion generator_version;
    std::vector<std::string> tags;
    bool password = false;
    std::map<std::string, APPermission> permissions;
    int32_t hint_cost = 0;
    int32_t location_check_points = 0;
    std::vector<std::string> games;
    std::map<std::string, std::string> datapackage_checksums;
    std::string seed_name;
    double time = 0.0;
    
    APRoomInfoPacket() { cmd = "RoomInfo"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APConnectedPacket : public APPacket
{
    int32_t team = 0;
    int32_t slot = 0;
    std::vector<APNetworkPlayer> players;
    std::vector<int64_t> missing_locations;
    std::vector<int64_t> checked_locations;
    std::map<std::string, std::string> slot_data;
    std::map<int32_t, std::string> slot_info; // Simplified
    int32_t hint_points = 0;
    
    APConnectedPacket() { cmd = "Connected"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APConnectionRefusedPacket : public APPacket
{
    std::vector<std::string> errors;
    
    APConnectionRefusedPacket() { cmd = "ConnectionRefused"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APReceivedItemsPacket : public APPacket
{
    int32_t index = 0;
    std::vector<APNetworkItem> items;
    
    APReceivedItemsPacket() { cmd = "ReceivedItems"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

struct APPrintJSONPacket : public APPacket
{
    std::vector<APJSONMessagePart> data;
    std::string type;
    int32_t receiving = 0;
    APNetworkItem item;
    bool found = false;
    int32_t team = 0;
    int32_t slot = 0;
    std::string message;
    std::vector<std::string> tags;
    int32_t countdown = 0;
    
    APPrintJSONPacket() { cmd = "PrintJSON"; }
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

// Location information packet
struct APLocationInfoPacket : public APPacket
{
    std::vector<APNetworkLocation> locations;
    
    APLocationInfoPacket() { cmd = "LocationInfo"; }
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

// Room update packet
struct APRoomUpdatePacket : public APPacket
{
    std::vector<APNetworkPlayer> players;
    std::vector<int64_t> checked_locations;
    std::vector<int64_t> missing_locations;
    
    APRoomUpdatePacket() { cmd = "RoomUpdate"; }
    
    rapidjson::Value ToJSON(rapidjson::Document::AllocatorType& allocator) const override;
    bool FromJSON(const rapidjson::Value& value) override;
};

// Message callbacks
using APMessageCallback = std::function<void(const APPacket& packet)>;
using APConnectedCallback = std::function<void(const APConnectedPacket& packet)>;
using APItemCallback = std::function<void(const APNetworkItem& item)>;
using APLocationCallback = std::function<void(int64_t location_id)>;
using APPrintCallback = std::function<void(const std::string& message)>;

// Utility functions
std::string APPacketToString(const APPacket& packet);
std::unique_ptr<APPacket> APPacketFromString(const std::string& json_str);
std::string GenerateUUID();

// Selaco-specific location and item IDs
namespace SelacoDefs
{
    // Base offsets for Selaco items/locations
    constexpr int64_t ITEM_BASE = 100000;
    constexpr int64_t LOCATION_BASE = 200000;
    
    // Item categories
    enum ItemType
    {
        WEAPON = ITEM_BASE + 1000,
        UPGRADE = ITEM_BASE + 2000,
        KEY = ITEM_BASE + 3000,
        HEALTH = ITEM_BASE + 4000,
        ARMOR = ITEM_BASE + 5000,
        AMMO = ITEM_BASE + 6000
    };
    
    // Location categories  
    enum LocationType
    {
        BOSS_DEFEAT = LOCATION_BASE + 1000,
        ITEM_PICKUP = LOCATION_BASE + 2000,
        LEVEL_COMPLETE = LOCATION_BASE + 3000,
        SECRET_FOUND = LOCATION_BASE + 4000,
        OBJECTIVE = LOCATION_BASE + 5000
    };
}

// Error handling
enum class APError
{
    None,
    ConnectionFailed,
    AuthenticationFailed,
    InvalidPacket,
    NetworkError,
    ProtocolError
};

class APException : public std::exception
{
private:
    std::string message_;
    APError error_code_;
    
public:
    APException(APError code, const std::string& message)
        : error_code_(code), message_(message) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    APError GetErrorCode() const { return error_code_; }
};