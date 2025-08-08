
#include "vulkandevice.h"
#include "vulkanobjects.h"
#include "vulkancompatibledevice.h"
#include <algorithm>
#include <set>
#include <string>


VulkanDevice::VulkanDevice(std::shared_ptr<VulkanInstance> instance, std::shared_ptr<VulkanSurface> surface, const VulkanCompatibleDevice& selectedDevice, int numUploadSlots, int flags) : Instance(instance), Surface(surface)
{
	PhysicalDevice = *selectedDevice.Device;
	EnabledDeviceExtensions = selectedDevice.EnabledDeviceExtensions;
	EnabledFeatures = selectedDevice.EnabledFeatures;

	GraphicsFamily = selectedDevice.GraphicsFamily;
	PresentFamily = selectedDevice.PresentFamily;
	UploadFamily = selectedDevice.UploadFamily;
	UploadFamilySupportsGraphics = selectedDevice.UploadFamilySupportsGraphics;
	GraphicsTimeQueries = selectedDevice.GraphicsTimeQueries;
	DebugLayerActive = instance->DebugLayerActive;

	// Detect ARC since it requires special handling due to immature drivers
	isARC = PhysicalDevice.Properties.Properties.vendorID == 0x8086 && strstr(PhysicalDevice.Properties.Properties.deviceName, "Arc");
	
	// @Cockatrice - Special case for ARC GPU, try not to use concurrent mode swapchain
	if (isARC || flags & VK_DEVICE_FLAG_FORCE_EXCLUSIVE_PRESENT) {
		PresentFamily = -2;
	}

	// Test to see if we can fit more upload queues
	int rqt = (UploadFamily == GraphicsFamily ? 1 : 0) + (PresentFamily == UploadFamily ? 1 : 0);
	UploadQueuesSupported = selectedDevice.Device->QueueFamilies[UploadFamily].queueCount - rqt;

	try
	{
		CreateDevice(numUploadSlots);
		CreateAllocator();
	}
	catch (...)
	{
		ReleaseResources();
		throw;
	}
}

VulkanDevice::~VulkanDevice()
{
	ReleaseResources();
}

bool VulkanDevice::SupportsExtension(const char* ext) const
{
	return
		EnabledDeviceExtensions.find(ext) != EnabledDeviceExtensions.end() ||
		Instance->EnabledExtensions.find(ext) != Instance->EnabledExtensions.end();
}

void VulkanDevice::CreateAllocator()
{
	VmaAllocatorCreateInfo allocinfo = {};
	allocinfo.vulkanApiVersion = Instance->ApiVersion;
	if (SupportsExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) && SupportsExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
		allocinfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
	if (SupportsExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
		allocinfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocinfo.physicalDevice = PhysicalDevice.Device;
	allocinfo.device = device;
	allocinfo.instance = Instance->Instance;
	allocinfo.preferredLargeHeapBlockSize = 64 * 1024 * 1024;
	if (vmaCreateAllocator(&allocinfo, &allocator) != VK_SUCCESS)
		VulkanError("Unable to create allocator");
}


static int CreateOrModifyQueueInfo(std::vector<VkDeviceQueueCreateInfo>& infos, uint32_t family, float* priority) {
	for (VkDeviceQueueCreateInfo& info : infos) {
		if (info.queueFamilyIndex == family) {
			info.queueCount++;
			return info.queueCount - 1;
		}
	}

	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = family;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = priority;
	infos.push_back(queueCreateInfo);

	return 0;
}


void VulkanDevice::CreateDevice(int numUploadSlots)
{
	// TODO: Lower queue priority for upload queues
	float queuePriority[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	int graphicsFamilySlot = CreateOrModifyQueueInfo(queueCreateInfos, GraphicsFamily, queuePriority);
	int presentFamilySlot = PresentFamily < 0 ? -1 : CreateOrModifyQueueInfo(queueCreateInfos, PresentFamily, queuePriority);

	// Request as many upload queues as desired and supported. Minimum 1
	std::vector<int> uploadFamilySlots;
	int numUploadQueues = numUploadSlots >= 0 ? numUploadSlots : 2;

	for (int x = 0; x < numUploadQueues && x < UploadQueuesSupported; x++) {
		uploadFamilySlots.push_back(CreateOrModifyQueueInfo(queueCreateInfos, UploadFamily, queuePriority));
	}


	// @Cockatrice - Temporary debug info!
	VulkanPrintLog("debug", "Vulkan Queue Create Layout:\n");
	for (auto& q : queueCreateInfos) {
		std::string output = "\tQueue Family: " + std::to_string(q.queueFamilyIndex) + "  # of queues: " + std::to_string(q.queueCount);
		VulkanPrintLog("debug", output);
	}
	VulkanPrintLog("debug", "Graphics Family: " + std::to_string(GraphicsFamily));
	VulkanPrintLog("debug", "Present Family: " + std::to_string(PresentFamily));
	VulkanPrintLog("debug", "Upload Family: " + std::to_string(UploadFamily));

	std::vector<const char*> extensionNames;
	extensionNames.reserve(EnabledDeviceExtensions.size());
	for (const auto& name : EnabledDeviceExtensions)
		extensionNames.push_back(name.c_str());

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.enabledExtensionCount = (uint32_t)extensionNames.size();
	deviceCreateInfo.ppEnabledExtensionNames = extensionNames.data();
	deviceCreateInfo.enabledLayerCount = 0;

	VkPhysicalDeviceFeatures2 deviceFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	deviceFeatures2.features = EnabledFeatures.Features;

	void** next = const_cast<void**>(&deviceCreateInfo.pNext);
	if (SupportsExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
	{
		*next = &deviceFeatures2;
		next = &deviceFeatures2.pNext;
	}
	else // vulkan 1.0 specified features in a different way
	{
		deviceCreateInfo.pEnabledFeatures = &deviceFeatures2.features;
	}

	if (SupportsExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
	{
		*next = &EnabledFeatures.BufferDeviceAddress;
		next = &EnabledFeatures.BufferDeviceAddress.pNext;
	}
	if (SupportsExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
	{
		*next = &EnabledFeatures.AccelerationStructure;
		next = &EnabledFeatures.AccelerationStructure.pNext;
	}
	if (SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME))
	{
		*next = &EnabledFeatures.RayQuery;
		next = &EnabledFeatures.RayQuery.pNext;
	}
	if (SupportsExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
	{
		*next = &EnabledFeatures.DescriptorIndexing;
		next = &EnabledFeatures.DescriptorIndexing.pNext;
	}

	VulkanPrintLog("debug", "Creating Vulkan device on " + std::string(PhysicalDevice.Properties.Properties.deviceName));

	VkResult result = vkCreateDevice(PhysicalDevice.Device, &deviceCreateInfo, nullptr, &device);
	CheckVulkanError(result, "Could not create vulkan device");

	if (isARC) {
		VulkanPrintLog("debug", "Detected Intel ARC!");
	}

	volkLoadDevice(device);

	if (GraphicsFamily != -1)
		vkGetDeviceQueue(device, GraphicsFamily, graphicsFamilySlot, &GraphicsQueue);
	if (PresentFamily >= 0 && presentFamilySlot >= 0)
		vkGetDeviceQueue(device, PresentFamily, presentFamilySlot, &PresentQueue);
	else if(PresentFamily == -2)
		vkGetDeviceQueue(device, GraphicsFamily, graphicsFamilySlot, &PresentQueue);
		//PresentQueue = GraphicsQueue;	// I think we still need a reference to a queue even if there is no present family
	else
		VulkanError("No valid combination of queues gave us a Graphics and Present queue. \nCockatrice must have done something wrong or your hardware does not support it.");

	// Upload queues
	if (uploadFamilySlots.size() > 0) {
		VulkanUploadSlot slot = { VK_NULL_HANDLE, UploadFamily, uploadFamilySlots[0], UploadFamilySupportsGraphics };
		vkGetDeviceQueue(device, UploadFamily, uploadFamilySlots[0], &slot.queue);
		uploadQueues.push_back(slot);

		// Push more upload queues if supported
		for (int x = 1; x < (int)uploadFamilySlots.size(); x++) {
			VulkanUploadSlot slot = { VK_NULL_HANDLE, UploadFamily, uploadFamilySlots[x], UploadFamilySupportsGraphics };
			vkGetDeviceQueue(device, UploadFamily, uploadFamilySlots[x], &slot.queue);
			uploadQueues.push_back(slot);

			if (slot.queue == VK_NULL_HANDLE) {
				VulkanError(("Vulkan Error : Failed to create background transfer queue [" + std::to_string(x) + "] !\n\tCheck vk_max_transfer_threads ?").c_str());
			}
		}
	}
}

void VulkanDevice::ReleaseResources()
{
	if (device)
		vkDeviceWaitIdle(device);

	if (allocator)
		vmaDestroyAllocator(allocator);

	if (device)
		vkDestroyDevice(device, nullptr);
	device = nullptr;
}

void VulkanDevice::SetObjectName(const char* name, uint64_t handle, VkObjectType type)
{
	if (!DebugLayerActive) return;

	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectHandle = handle;
	info.objectType = type;
	info.pObjectName = name;
	vkSetDebugUtilsObjectNameEXT(device, &info);
}
