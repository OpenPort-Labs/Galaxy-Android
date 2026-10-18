#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GalaxyAndroid";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PetariVK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    std::vector<const char*> layers;
    const char* validation = vkDebugValue("PETARI_VK_VALIDATION", "debug.petari.vkvalidation");
    bool wantValidation = validation && validation[0] == '1';
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if (wantValidation && layerCount > 0) {
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for (const auto& layer : availableLayers) {
            if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                LOGW("Enabling Vulkan Validation Layer %s (spec %u) - expect a large frame-time cost", layer.layerName,
                     layer.specVersion);
                break;
            }
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = (uint32_t)layers.size();
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult res = vkCreateInstance(&createInfo, nullptr, &mInstance);
    if (res != VK_SUCCESS) {
        LOGE("vkCreateInstance failed with result %d", (int)res);
        if (!layers.empty()) {
            LOGW("Retrying vkCreateInstance without validation layers...");
            createInfo.enabledLayerCount = 0;
            createInfo.ppEnabledLayerNames = nullptr;
            res = vkCreateInstance(&createInfo, nullptr, &mInstance);
        }
    }
    return res == VK_SUCCESS;
}

bool VkRenderer::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    return vkCreateAndroidSurfaceKHR(mInstance, &sci, nullptr, &mSurface) == VK_SUCCESS;
}

bool VkRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());

    for (const auto& dev : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &presentSupport);
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
                mPhysicalDevice = dev;
                mQueueFamily = i;
                vkGetPhysicalDeviceProperties(dev, &mDeviceProperties);
                vkGetPhysicalDeviceFeatures(dev, &mDeviceFeatures);
                LOGI("Selected Vulkan GPU: %s (Driver version: 0x%x, API: %d.%d)",
                     mDeviceProperties.deviceName, mDeviceProperties.driverVersion,
                     VK_VERSION_MAJOR(mDeviceProperties.apiVersion),
                     VK_VERSION_MINOR(mDeviceProperties.apiVersion));
                return true;
            }
        }
    }
    return false;
}

bool VkRenderer::createLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = mQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    uint32_t availExtCount = 0;
    vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &availExtCount, nullptr);
    if (availExtCount > 0) {
        std::vector<VkExtensionProperties> avail(availExtCount);
        vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &availExtCount, avail.data());
#if defined(VK_EXT_device_fault) && defined(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT)
        for (const VkExtensionProperties& ext : avail) {
            if (strcmp(ext.extensionName, "VK_EXT_device_fault") == 0) {
                deviceExtensions.push_back("VK_EXT_device_fault");
                LOGI("VK_EXT_device_fault available; device losses will report a reason");
                break;
            }
        }
#endif
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = mDeviceFeatures.samplerAnisotropy;
    deviceFeatures.depthClamp = VK_FALSE;
    if (mDeviceFeatures.robustBufferAccess) {
        deviceFeatures.robustBufferAccess = VK_TRUE;
    }
    createInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice) != VK_SUCCESS) {
        return false;
    }

    ++sVkDeviceGeneration;
    vkGetDeviceQueue(mDevice, mQueueFamily, 0, &mQueue);
    return true;
}

}  // namespace gpu
