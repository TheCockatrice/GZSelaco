/*
**  Vulkan backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "volk/volk.h"

#ifdef _WIN32
#undef max
#undef min
#endif

#include <inttypes.h>
#include <vector>
#include <array>
#include <set>
#include <string>
#include <algorithm>

#include "vk_device.h"
#include "vk_swapchain.h"
#include "vk_objects.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "i_system.h"
#include "version.h"
#include "engineerrors.h"
#include "v_text.h"

bool I_GetVulkanPlatformExtensions(unsigned int *count, const char **names);
bool I_CreateVulkanSurface(VkInstance instance, VkSurfaceKHR *surface);

FString JitCaptureStackTrace(int framesToSkip, bool includeNativeFrames, int maxFrames = -1);

// Physical device info
static std::vector<VulkanPhysicalDevice> AvailableDevices;
static std::vector<VulkanCompatibleDevice> SupportedDevices;

CUSTOM_CVAR(Bool, vk_debug, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CVAR(Bool, vk_debug_callstack, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, vk_device, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CUSTOM_CVAR(Int, vk_max_transfer_threads, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0) self = 0;
	else if (self > 4) self = 4;

	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

EXTERN_CVAR(Bool, gl_texture_thread)


CCMD(vk_listdevices)
{
	for (size_t i = 0; i < SupportedDevices.size(); i++)
	{
		Printf("#%d - %s\n", (int)i, SupportedDevices[i].device->Properties.deviceName);
	}
}

VulkanDevice::VulkanDevice()
{
	try
	{
		InitVolk();
		CreateInstance();
		CreateSurface();
		SelectPhysicalDevice();
		SelectFeatures();
		CreateDevice();
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

void VulkanDevice::SelectFeatures()
{
	UsedDeviceFeatures.samplerAnisotropy = PhysicalDevice.Features.samplerAnisotropy;
	UsedDeviceFeatures.fragmentStoresAndAtomics = PhysicalDevice.Features.fragmentStoresAndAtomics;
	UsedDeviceFeatures.depthClamp = PhysicalDevice.Features.depthClamp;
	UsedDeviceFeatures.shaderClipDistance = PhysicalDevice.Features.shaderClipDistance;
}

bool VulkanDevice::CheckRequiredFeatures(const VkPhysicalDeviceFeatures &f)
{
	return
		f.samplerAnisotropy == VK_TRUE &&
		f.fragmentStoresAndAtomics == VK_TRUE;
}


void VulkanDevice::SelectPhysicalDevice()
{
	AvailableDevices = GetPhysicalDevices(instance);
	if (AvailableDevices.empty())
		VulkanError("No Vulkan devices found. Either the graphics card has no vulkan support or the driver is too old.");

	for (size_t idx = 0; idx < AvailableDevices.size(); idx++)
	{
		const auto &info = AvailableDevices[idx];

		if (!CheckRequiredFeatures(info.Features))
			continue;

		std::set<std::string> requiredExtensionSearch(EnabledDeviceExtensions.begin(), EnabledDeviceExtensions.end());
		for (const auto &ext : info.Extensions)
			requiredExtensionSearch.erase(ext.extensionName);
		if (!requiredExtensionSearch.empty())
			continue;

		VulkanCompatibleDevice dev;
		dev.device = &AvailableDevices[idx];

		// @Cockatrice - The old way was broken, using the first graphics queue was a huge problem and only worked because
		// the code that actually requested the queue only ever actually found 1 and reused it for both Graphics and Present
		// So, lets Find our graphics queue family first, this should be the easiest one
		for (int i = 0; i < (int)info.QueueFamilies.size(); i++) {
			const auto &queueFamily = info.QueueFamilies[i];
			if (queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				dev.graphicsFamily = i;
				dev.graphicsTimeQueries = queueFamily.timestampValidBits != 0;
				break;
			}
		}

		// Find a transfer queue family. This can be a graphics family if necessary, but make sure there is enough
		// room. AMD cards do not seem to allow blit ops on a Transfer only queue, to the point where most drivers
		// seem to freeze the entire operating system if you try. We will first try a unique graphics family queue.
		// If we can't get a graphics family queue, mipmaps need to be generated in the main thread which is obviously
		// a bit slower, so avoid this when possible.
		for (int i = 0; i < (int)info.QueueFamilies.size(); i++) {
			const auto &queueFamily = info.QueueFamilies[i];
			if (queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				// Make sure the family has room for another queue
				if (i == dev.graphicsFamily && queueFamily.queueCount < 2) {
					continue;
				}

				// We have LOTS of misalignments between our textures, so we NEED a granularity of 1.
				if (queueFamily.minImageTransferGranularity.width > 1 || queueFamily.minImageTransferGranularity.depth > 1) {
					continue;
				}

				// We are lucky! We found a graphics family for our upload queue, and we can generate mipmaps in
				// the background thread
				dev.uploadFamily = i;
				dev.uploadFamilySupportsGraphics = true;
				break;
			}
		}

		// If we didn't find one, loosen the restrictions
		if(dev.uploadFamily == -1) {
			for (int i = 0; i < (int)info.QueueFamilies.size(); i++) {
				const auto &queueFamily = info.QueueFamilies[i];

				if (i == dev.graphicsFamily && queueFamily.queueCount < 2) {
					continue;
				}

				// We have LOTS of misalignments between our textures, so we NEED a granularity of 1.
				if (queueFamily.minImageTransferGranularity.width > 1 || queueFamily.minImageTransferGranularity.depth > 1) {
					continue;
				}

				// Spec states all families must support Transfer, so we should be able to grab any one that passes the other criteria
				// Note: It turns out that this isn't true. Newer drivers are specifying queues that do not have transfer enabled so we need to check
				if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
					dev.uploadFamily = i;
					dev.uploadFamilySupportsGraphics = false;
					break;
				}
			}
		}

		// Now find a Present family. In the end the Present and Graphics queue CAN be the same queue, but we need to treat that specially
		// The original Vulkan implementation accidentally always ended up with the same queue for graphics and present anyways 
		for (int i = 0; i < (int)info.QueueFamilies.size(); i++) {
			const auto &queueFamily = info.QueueFamilies[i];
			VkBool32 presentSupport = false;
			VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(info.Device, i, surface, &presentSupport);
			if (result == VK_SUCCESS && queueFamily.queueCount > 0 && presentSupport) {
				// Make sure there is enough room in this queue
				uint32_t requiredCount = 1;
				if (i == dev.graphicsFamily) requiredCount++;
				if (i == dev.uploadFamily) requiredCount++;
				if (requiredCount > queueFamily.queueCount) continue;

				dev.presentFamily = i;
				break;
			}
		}

		// If we didn't find a present family with enough room, let's make sure the graphics queue family supports it
		if (dev.presentFamily < 0 && dev.graphicsFamily >= 0) {
			VkBool32 presentSupport = false;
			VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(info.Device, dev.graphicsFamily, surface, &presentSupport);
			if (result == VK_SUCCESS && presentSupport) {
				dev.presentFamily = -2;
			}
		}

		if (dev.graphicsFamily != -1 && dev.uploadFamily != -1 && dev.presentFamily != -1)
		{
			SupportedDevices.push_back(dev);
		}
	}

	if (SupportedDevices.empty())
		VulkanError("No Vulkan device supports the minimum requirements of this application");

	// The device order returned by Vulkan can be anything. Prefer discrete > integrated > virtual gpu > cpu > other
	std::stable_sort(SupportedDevices.begin(), SupportedDevices.end(), [&](const auto &a, const auto b) {

		// Sort by GPU type first. This will ensure the "best" device is most likely to map to vk_device 0
		static const int typeSort[] = { 4, 1, 0, 2, 3 };
		int sortA = a.device->Properties.deviceType < 5 ? typeSort[(int)a.device->Properties.deviceType] : (int)a.device->Properties.deviceType;
		int sortB = b.device->Properties.deviceType < 5 ? typeSort[(int)b.device->Properties.deviceType] : (int)b.device->Properties.deviceType;

		if (sortA != sortB)
			return sortA < sortB;

		// Any driver that is emulating vulkan (i.e. via Direct3D 12) should only be chosen as the last option within each GPU type
		sortA = a.device->LayerProperties.underlyingAPI;
		sortB = b.device->LayerProperties.underlyingAPI;
		if (sortA != sortB)
			return sortA < sortB;

		// Then sort by the device's unique ID so that vk_device uses a consistent order
		int sortUUID = memcmp(a.device->Properties.pipelineCacheUUID, b.device->Properties.pipelineCacheUUID, VK_UUID_SIZE);
		return sortUUID < 0;
	});

	size_t selected = vk_device;
	if (selected >= SupportedDevices.size())
		selected = 0;
		
	// Enable optional extensions we are interested in, if they are available on this device
	for (const auto &ext : SupportedDevices[selected].device->Extensions)
	{
		for (const auto &opt : OptionalDeviceExtensions)
		{
			if (strcmp(ext.extensionName, opt) == 0)
			{
				EnabledDeviceExtensions.push_back(opt);
			}
		}
	}

	PhysicalDevice = *SupportedDevices[selected].device;
	graphicsFamily = SupportedDevices[selected].graphicsFamily;
	presentFamily = SupportedDevices[selected].presentFamily;
	uploadFamily = SupportedDevices[selected].uploadFamily;
	graphicsTimeQueries = SupportedDevices[selected].graphicsTimeQueries;
	uploadFamilySupportsGraphics = SupportedDevices[selected].uploadFamilySupportsGraphics;

	// Test to see if we can fit more upload queues
	int rqt = (uploadFamily == graphicsFamily ? 1 : 0) + (presentFamily == uploadFamily ? 1 : 0);
	uploadQueuesSupported = SupportedDevices[selected].device->QueueFamilies[uploadFamily].queueCount - rqt;

	// Output available devices for debugging
	for (int x = 0; x < SupportedDevices.size(); x++) {
		Printf("Found VK Device #%d: ", x);
		if (x == selected) {
			Printf(TEXTCOLOR_CYAN "%s (Vendor ID: %d  Device ID: %d)\n", SupportedDevices[x].device->Properties.deviceName, SupportedDevices[x].device->Properties.vendorID, SupportedDevices[x].device->Properties.deviceID);
		}
		else {
			Printf(TEXTCOLOR_WHITE "%s (Vendor ID: %d  Device ID: %d)\n", SupportedDevices[x].device->Properties.deviceName, SupportedDevices[x].device->Properties.vendorID, SupportedDevices[x].device->Properties.deviceID);
		}
	}
}

bool VulkanDevice::SupportsDeviceExtension(const char *ext) const
{
	return std::find(EnabledDeviceExtensions.begin(), EnabledDeviceExtensions.end(), ext) != EnabledDeviceExtensions.end();
}

void VulkanDevice::CreateAllocator()
{
	VmaAllocatorCreateInfo allocinfo = {};
	allocinfo.vulkanApiVersion = ApiVersion;
	if (SupportsDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) && SupportsDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
		allocinfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
	if (SupportsDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
		allocinfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocinfo.physicalDevice = PhysicalDevice.Device;
	allocinfo.device = device;
	allocinfo.instance = instance;
	allocinfo.preferredLargeHeapBlockSize = 64 * 1024 * 1024;
	if (vmaCreateAllocator(&allocinfo, &allocator) != VK_SUCCESS)
		VulkanError("Unable to create allocator");
}


static int CreateOrModifyQueueInfo(std::vector<VkDeviceQueueCreateInfo> &infos, uint32_t family, float *priority) {
	for (VkDeviceQueueCreateInfo &info : infos) {
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


void VulkanDevice::CreateDevice()
{
	// TODO: Lower queue priority for upload queues
	float queuePriority[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	int graphicsFamilySlot = CreateOrModifyQueueInfo(queueCreateInfos, graphicsFamily, queuePriority);
	int presentFamilySlot = presentFamily < 0 ? -1 : CreateOrModifyQueueInfo(queueCreateInfos, presentFamily, queuePriority);
	
	// Request as many upload queues as desired and supported
	// Modified to support 0 queues, which will disable background loading
	std::vector<int> uploadFamilySlots;
	int numUploadQueues = vk_max_transfer_threads >= 0 ? vk_max_transfer_threads : 2;
	for (int x = 0; x < numUploadQueues && x < uploadQueuesSupported && gl_texture_thread; x++) {
		uploadFamilySlots.push_back(CreateOrModifyQueueInfo(queueCreateInfos, uploadFamily, queuePriority));
	}

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	VkPhysicalDeviceFeatures2 deviceFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	VkPhysicalDeviceBufferDeviceAddressFeatures deviceAddressFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
	VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };

	deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.enabledExtensionCount = (uint32_t)EnabledDeviceExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = EnabledDeviceExtensions.data();
	deviceCreateInfo.enabledLayerCount = 0;
	deviceFeatures2.features = UsedDeviceFeatures;
	deviceAddressFeatures.bufferDeviceAddress = true;
	deviceAccelFeatures.accelerationStructure = true;
	rayQueryFeatures.rayQuery = true;

	void** next = const_cast<void**>(&deviceCreateInfo.pNext);
	if (SupportsDeviceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
	{
		*next = &deviceFeatures2;
		void** next = &deviceFeatures2.pNext;
	}
	else // vulkan 1.0 specified features in a different way
	{
		deviceCreateInfo.pEnabledFeatures = &deviceFeatures2.features;
	}
	if (SupportsDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
	{
		*next = &deviceAddressFeatures;
		next = &deviceAddressFeatures.pNext;
	}
	if (SupportsDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
	{
		*next = &deviceAccelFeatures;
		next = &deviceAccelFeatures.pNext;
	}
	if (SupportsDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME))
	{
		*next = &rayQueryFeatures;
		next = &rayQueryFeatures.pNext;
	}

	VkResult result = vkCreateDevice(PhysicalDevice.Device, &deviceCreateInfo, nullptr, &device);
	CheckVulkanError(result, "Could not create vulkan device");

	volkLoadDevice(device);

	vkGetDeviceQueue(device, graphicsFamily, graphicsFamilySlot, &graphicsQueue);

	if (presentFamily >= 0 && presentFamilySlot >= 0) {
		vkGetDeviceQueue(device, presentFamily, presentFamilySlot, &presentQueue);
	}
	else {
		vkGetDeviceQueue(device, graphicsFamily, graphicsFamilySlot, &presentQueue);
	}
	
	
	// Upload queues
	if (uploadFamilySlots.size() > 0) {
		VulkanUploadSlot slot = { VK_NULL_HANDLE, uploadFamily, uploadFamilySlots[0], uploadFamilySupportsGraphics };
		vkGetDeviceQueue(device, uploadFamily, uploadFamilySlots[0], &slot.queue);
		uploadQueues.push_back(slot);

		// Push more upload queues if supported
		for (int x = 1; x < (int)uploadFamilySlots.size(); x++) {
			VulkanUploadSlot slot = { VK_NULL_HANDLE, uploadFamily, uploadFamilySlots[x], uploadFamilySupportsGraphics };
			vkGetDeviceQueue(device, uploadFamily, uploadFamilySlots[x], &slot.queue);
			uploadQueues.push_back(slot);

			if (slot.queue == VK_NULL_HANDLE) {
				FString msg;
				msg.Format("Vulkan Error: Failed to create background transfer queue %d!\nCheck vk_max_transfer_threads?", x);
				throw CVulkanError(msg.GetChars());
			}
		}
	}

	Printf(TEXTCOLOR_WHITE "VK Graphics Queue: %p\nVK Present Queue: %p\n", graphicsQueue, presentQueue);
	for (int x = 0; x < (int)uploadQueues.size(); x++) Printf(TEXTCOLOR_WHITE "VK Upload Queue %d: %p\n", x, uploadQueues[x].queue);
	if (uploadQueues.size() == 0) Printf(TEXTCOLOR_RED "No VK Upload Queues Supported or Enabled!\n");
}

void VulkanDevice::CreateSurface()
{
	if (!I_CreateVulkanSurface(instance, &surface))
	{
		VulkanError("Could not create vulkan surface");
	}
}

void VulkanDevice::CreateInstance()
{
	AvailableLayers = GetAvailableLayers();
	Extensions = GetExtensions();
	EnabledExtensions = GetPlatformExtensions();

	std::string debugLayer = "VK_LAYER_KHRONOS_validation";
// @Cockatrice - Force validation layers in debug build
#ifdef NDEBUG
	bool wantDebugLayer = vk_debug;
#else
	bool wantDebugLayer = true;
#endif

	bool debugLayerFound = false;
	if (wantDebugLayer)
	{
		for (const VkLayerProperties& layer : AvailableLayers)
		{
			if (layer.layerName == debugLayer)
			{
				EnabledValidationLayers.push_back(layer.layerName);
				EnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				debugLayerFound = true;
				break;
			}
		}
	}

	if(!debugLayerFound && wantDebugLayer) {
		Printf(TEXTCOLOR_RED "Vulkan Error: Debug layers were requested but not available!");
	}

	// Enable optional instance extensions we are interested in
	for (const auto &ext : Extensions)
	{
		for (const auto &opt : OptionalExtensions)
		{
			if (strcmp(ext.extensionName, opt) == 0)
			{
				EnabledExtensions.push_back(opt);
			}
		}
	}

	// Try get the highest vulkan version we can get
	VkResult result = VK_ERROR_INITIALIZATION_FAILED;
	for (uint32_t apiVersion : { VK_API_VERSION_1_2, VK_API_VERSION_1_1, VK_API_VERSION_1_0 })
	{
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Selaco";
		appInfo.applicationVersion = VK_MAKE_VERSION(VER_MAJOR, VER_MINOR, VER_REVISION);
		appInfo.pEngineName = "GZDoom";
		appInfo.engineVersion = VK_MAKE_VERSION(ENG_MAJOR, ENG_MINOR, ENG_REVISION);
		appInfo.apiVersion = apiVersion;

		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = (uint32_t)EnabledExtensions.size();
		createInfo.enabledLayerCount = (uint32_t)EnabledValidationLayers.size();
		createInfo.ppEnabledLayerNames = EnabledValidationLayers.data();
		createInfo.ppEnabledExtensionNames = EnabledExtensions.data();

		result = vkCreateInstance(&createInfo, nullptr, &instance);
		if (result >= VK_SUCCESS)
		{
			ApiVersion = apiVersion;
			break;
		}
	}
	CheckVulkanError(result, "Could not create vulkan instance");

	volkLoadInstance(instance);

	if (debugLayerFound)
	{
		VkDebugUtilsMessengerCreateInfoEXT dbgCreateInfo = {};
		dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dbgCreateInfo.messageSeverity =
			//VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			//VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dbgCreateInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dbgCreateInfo.pfnUserCallback = DebugCallback;
		dbgCreateInfo.pUserData = this;
		result = vkCreateDebugUtilsMessengerEXT(instance, &dbgCreateInfo, nullptr, &debugMessenger);
		CheckVulkanError(result, "vkCreateDebugUtilsMessengerEXT failed");

		DebugLayerActive = true;
	}
}

VkBool32 VulkanDevice::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
	static std::mutex mtx;
	static std::set<FString> seenMessages;
	static int totalMessages;

	std::unique_lock<std::mutex> lock(mtx);

	FString msg = callbackData->pMessage;

	// Attempt to parse the string because the default formatting is totally unreadable and half of what it writes is totally useless!
	auto parts = msg.Split(" | ");
	if (parts.Size() == 3)
	{
		msg = parts[2];
		auto pos = msg.IndexOf(" The Vulkan spec states:");
		if (pos >= 0)
			msg = msg.Left(pos);

		if (callbackData->objectCount > 0)
		{
			msg += " (";
			for (uint32_t i = 0; i < callbackData->objectCount; i++)
			{
				if (i > 0)
					msg += ", ";
				if (callbackData->pObjects[i].pObjectName)
					msg += callbackData->pObjects[i].pObjectName;
				else
					msg += "<noname>";
			}
			msg += ")";
		}
	}

	bool found = seenMessages.find(msg) != seenMessages.end();
	if (!found)
	{
		if (totalMessages < 20)
		{
			totalMessages++;
			seenMessages.insert(msg);

			const char *typestr;
			bool showcallstack = false;
			if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				typestr = "vulkan error";
				showcallstack = true;
			}
			else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				typestr = "vulkan warning";
			}
			else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
			{
				typestr = "vulkan info";
			}
			else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
			{
				typestr = "vulkan verbose";
			}
			else
			{
				typestr = "vulkan";
			}

			if (showcallstack)
				Printf("\n");
			Printf(TEXTCOLOR_RED "[%s] ", typestr);
			Printf(TEXTCOLOR_WHITE "%s\n", msg.GetChars());

			if (vk_debug_callstack && showcallstack)
			{
				FString callstack = JitCaptureStackTrace(0, true, 5);
				if (!callstack.IsEmpty())
					Printf("%s\n", callstack.GetChars());
			}
		}
	}

	return VK_FALSE;
}

std::vector<VkLayerProperties> VulkanDevice::GetAvailableLayers()
{
	uint32_t layerCount;
	VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
	return availableLayers;
}

std::vector<VkExtensionProperties> VulkanDevice::GetExtensions()
{
	uint32_t extensionCount = 0;
	VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> extensions(extensionCount);
	result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
	return extensions;
}

std::vector<VulkanPhysicalDevice> VulkanDevice::GetPhysicalDevices(VkInstance instance)
{
	uint32_t deviceCount = 0;
	VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (result == VK_ERROR_INITIALIZATION_FAILED) // Some drivers return this when a card does not support vulkan
		return {};
	CheckVulkanError(result, "vkEnumeratePhysicalDevices failed");
	if (deviceCount == 0)
		return {};

	std::vector<VkPhysicalDevice> devices(deviceCount);
	result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	CheckVulkanError(result, "vkEnumeratePhysicalDevices failed (2)");

	std::vector<VulkanPhysicalDevice> devinfo(deviceCount);
	for (size_t i = 0; i < devices.size(); i++)
	{
		auto &dev = devinfo[i];
		dev.Device = devices[i];

		vkGetPhysicalDeviceMemoryProperties(dev.Device, &dev.MemoryProperties);
		vkGetPhysicalDeviceProperties(dev.Device, &dev.Properties);
		vkGetPhysicalDeviceFeatures(dev.Device, &dev.Features);
		
		if (vkGetPhysicalDeviceProperties2) {
			vkGetPhysicalDeviceProperties2(dev.Device, &dev.Properties2);
		}

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(dev.Device, &queueFamilyCount, nullptr);
		dev.QueueFamilies.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(dev.Device, &queueFamilyCount, dev.QueueFamilies.data());

		uint32_t deviceExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(dev.Device, nullptr, &deviceExtensionCount, nullptr);
		dev.Extensions.resize(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(dev.Device, nullptr, &deviceExtensionCount, dev.Extensions.data());
	}
	return devinfo;
}

std::vector<const char *> VulkanDevice::GetPlatformExtensions()
{
	uint32_t extensionCount = 0;
	if (!I_GetVulkanPlatformExtensions(&extensionCount, nullptr))
		VulkanError("Cannot obtain number of Vulkan extensions");

	std::vector<const char *> extensions(extensionCount);
	if (!I_GetVulkanPlatformExtensions(&extensionCount, extensions.data()))
		VulkanError("Cannot obtain list of Vulkan extensions");
	return extensions;
}

void VulkanDevice::InitVolk()
{
	if (volkInitialize() != VK_SUCCESS)
	{
		VulkanError("Unable to find Vulkan");
	}
	auto iver = volkGetInstanceVersion();
	if (iver == 0)
	{
		VulkanError("Vulkan not supported");
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

	if (surface)
		vkDestroySurfaceKHR(instance, surface, nullptr);
	surface = 0;

	if (debugMessenger)
		vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);

	if (instance)
		vkDestroyInstance(instance, nullptr);
	instance = nullptr;
}

uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	for (uint32_t i = 0; i < PhysicalDevice.MemoryProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (PhysicalDevice.MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}

	VulkanError("failed to find suitable memory type!");
	return 0;
}

FString VkResultToString(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return "success";
	case VK_NOT_READY: return "not ready";
	case VK_TIMEOUT: return "timeout";
	case VK_EVENT_SET: return "event set";
	case VK_EVENT_RESET: return "event reset";
	case VK_INCOMPLETE: return "incomplete";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "out of host memory";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "out of device memory";
	case VK_ERROR_INITIALIZATION_FAILED: return "initialization failed";
	case VK_ERROR_DEVICE_LOST: return "device lost";
	case VK_ERROR_MEMORY_MAP_FAILED: return "memory map failed";
	case VK_ERROR_LAYER_NOT_PRESENT: return "layer not present";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "extension not present";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "feature not present";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "incompatible driver";
	case VK_ERROR_TOO_MANY_OBJECTS: return "too many objects";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "format not supported";
	case VK_ERROR_FRAGMENTED_POOL: return "fragmented pool";
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "out of pool memory";
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "invalid external handle";
	case VK_ERROR_SURFACE_LOST_KHR: return "surface lost";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "native window in use";
	case VK_SUBOPTIMAL_KHR: return "suboptimal";
	case VK_ERROR_OUT_OF_DATE_KHR: return "out of date";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "incompatible display";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "validation failed";
	case VK_ERROR_INVALID_SHADER_NV: return "invalid shader";
	case VK_ERROR_FRAGMENTATION_EXT: return "fragmentation";
	case VK_ERROR_NOT_PERMITTED_EXT: return "not permitted";
	default: break;
	}
	FString res;
	res.Format("vkResult %d", (int)result);
	return result;
}
