#pragma once

#include "volk/volk.h"
#include "vk_mem_alloc/vk_mem_alloc.h"
#include "engineerrors.h"
#include <mutex>
#include <vector>
#include <algorithm>
#include <memory>
#include "zstring.h"

class VulkanSwapChain;
class VulkanSemaphore;
class VulkanFence;


// Provided by VK_MSFT_layered_driver
typedef enum VkLayeredDriverUnderlyingApiMSFT {
	VK_LAYERED_DRIVER_UNDERLYING_API_NONE_MSFT = 0,
	VK_LAYERED_DRIVER_UNDERLYING_API_D3D12_MSFT = 1
} VkLayeredDriverUnderlyingApiMSFT;

// Provided by VK_MSFT_layered_driver
typedef struct VkPhysicalDeviceLayeredDriverPropertiesMSFT {
	VkStructureType                     sType;
	void* pNext;
	VkLayeredDriverUnderlyingApiMSFT    underlyingAPI;
} VkPhysicalDeviceLayeredDriverPropertiesMSFT;


class VulkanPhysicalDevice
{
public:
	VkPhysicalDevice Device = VK_NULL_HANDLE;

	std::vector<VkExtensionProperties> Extensions;
	std::vector<VkQueueFamilyProperties> QueueFamilies;
	VkPhysicalDeviceProperties Properties = {};
	VkPhysicalDeviceFeatures Features = {};
	VkPhysicalDeviceMemoryProperties MemoryProperties = {};

	// Layer properties used to filter out layered drivers (D3D12 for instance)
	VkPhysicalDeviceLayeredDriverPropertiesMSFT LayerProperties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_DRIVER_PROPERTIES_MSFT,
		NULL,
		VK_LAYERED_DRIVER_UNDERLYING_API_NONE_MSFT
	};

	VkPhysicalDeviceProperties2 Properties2 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
		&LayerProperties,
		{}
	};
};

class VulkanCompatibleDevice
{
public:
	VulkanPhysicalDevice *device = nullptr;
	int graphicsFamily = -1;
	int presentFamily = -1;
	int uploadFamily = -1;
	bool graphicsTimeQueries = false;
	bool uploadFamilySupportsGraphics = false;
};

struct VulkanUploadSlot {
	VkQueue queue;
	int queueFamily, queueIndex;
	bool familySupportsGraphics;
};


class VulkanDevice
{
public:
	VulkanDevice();
	~VulkanDevice();

	void SetDebugObjectName(const char *name, uint64_t handle, VkObjectType type)
	{
		if (!DebugLayerActive) return;

		VkDebugUtilsObjectNameInfoEXT info = {};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		info.objectHandle = handle;
		info.objectType = type;
		info.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(device, &info);
	}

	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

	// Instance setup
	std::vector<VkLayerProperties> AvailableLayers;
	std::vector<VkExtensionProperties> Extensions;
	std::vector<const char *> EnabledExtensions;
	std::vector<const char *> OptionalExtensions = { VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME };
	std::vector<const char*> EnabledValidationLayers;
	uint32_t ApiVersion = {};

	// Device setup
	VkPhysicalDeviceFeatures UsedDeviceFeatures = {};
	std::vector<const char *> EnabledDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	std::vector<const char *> OptionalDeviceExtensions =
	{
		VK_EXT_HDR_METADATA_EXTENSION_NAME,
		VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME
	};
	VulkanPhysicalDevice PhysicalDevice;
	bool DebugLayerActive = false;

	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VmaAllocator allocator = VK_NULL_HANDLE;

	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue presentQueue = VK_NULL_HANDLE;
	//VkQueue uploadQueue = VK_NULL_HANDLE;
	std::vector<VulkanUploadSlot> uploadQueues;

	int graphicsFamily = -1;
	int presentFamily = -1;
	int uploadFamily = -1;
	int uploadQueuesSupported = 1;
	bool graphicsTimeQueries = false;
	bool uploadFamilySupportsGraphics = false;

	bool SupportsDeviceExtension(const char* ext) const;

private:
	void CreateInstance();
	void CreateSurface();
	void SelectPhysicalDevice();
	void SelectFeatures();
	void CreateDevice();
	void CreateAllocator();
	void ReleaseResources();

	static bool CheckRequiredFeatures(const VkPhysicalDeviceFeatures &f);

	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

	static void InitVolk();
	static std::vector<VkLayerProperties> GetAvailableLayers();
	static std::vector<VkExtensionProperties> GetExtensions();
	static std::vector<const char *> GetPlatformExtensions();
	static std::vector<VulkanPhysicalDevice> GetPhysicalDevices(VkInstance instance);
};

FString VkResultToString(VkResult result);

class CVulkanError : public CEngineError
{
public:
	CVulkanError() : CEngineError() {}
	CVulkanError(const char* message) : CEngineError(message) {}
};


inline void VulkanError(const char *text)
{
	throw CVulkanError(text);
}

inline void CheckVulkanError(VkResult result, const char *text)
{
	if (result >= VK_SUCCESS)
		return;

	FString msg;
	msg.Format("%s: %s", text, VkResultToString(result).GetChars());
	throw CVulkanError(msg.GetChars());
}
