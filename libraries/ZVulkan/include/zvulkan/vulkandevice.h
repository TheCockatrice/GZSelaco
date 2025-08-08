#pragma once

#include "vulkaninstance.h"

#include <functional>
#include <mutex>
#include <vector>
#include <algorithm>
#include <memory>

class VulkanSwapChain;
class VulkanSemaphore;
class VulkanFence;
class VulkanPhysicalDevice;
class VulkanSurface;
class VulkanCompatibleDevice;


struct VulkanUploadSlot {
	VkQueue queue = VK_NULL_HANDLE;
	int queueFamily = -1, queueIndex = -1;
	bool familySupportsGraphics = false;
};


enum VulkanDeviceFlags {
	VK_DEVICE_FLAG_FORCE_EXCLUSIVE_PRESENT = 1
};


class VulkanDevice
{
public:
	VulkanDevice(std::shared_ptr<VulkanInstance> instance, std::shared_ptr<VulkanSurface> surface, const VulkanCompatibleDevice& selectedDevice, int numUploadSlots = 2, int flags = 0);
	~VulkanDevice();

	std::set<std::string> EnabledDeviceExtensions;
	VulkanDeviceFeatures EnabledFeatures;

	VulkanPhysicalDevice PhysicalDevice;

	std::shared_ptr<VulkanInstance> Instance;
	std::shared_ptr<VulkanSurface> Surface;

	VkDevice device = VK_NULL_HANDLE;
	VmaAllocator allocator = VK_NULL_HANDLE;


	VkQueue GraphicsQueue = VK_NULL_HANDLE;
	VkQueue PresentQueue = VK_NULL_HANDLE;
	std::vector<VulkanUploadSlot> uploadQueues;

	int UploadFamily = -1;
	int UploadQueuesSupported = 1;
	bool UploadFamilySupportsGraphics = false;

	int GraphicsFamily = -1;
	int PresentFamily = -1;
	bool GraphicsTimeQueries = false;

	bool isARC = false;

	bool SupportsExtension(const char* ext) const;

	void SetObjectName(const char* name, uint64_t handle, VkObjectType type);

private:
	bool DebugLayerActive = false;

	void CreateDevice(int numUploadSlots = 2);
	void CreateAllocator();
	void ReleaseResources();
};
