#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <vma/vk_mem_alloc.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <vulkan/vulkan.h>

VkInstance instance{VK_NULL_HANDLE};
VkDevice device{VK_NULL_HANDLE};
VkQueue queue{VK_NULL_HANDLE};
VmaAllocator allocator{VK_NULL_HANDLE};
VkSurfaceKHR surface{VK_NULL_HANDLE};
glm::ivec2 windowSize{};
VkSwapchainKHR swapchain{VK_NULL_HANDLE};
std::vector<VkImage> swapchainImages;
VkImage depthImage;
VmaAllocation depthImageAllocation;
VkImageView depthImageView;

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

static inline void chk(VkResult result) {
  if (result != VK_SUCCESS) {
    std::cerr << "Vulkan call returned an error (" << result << ")\n";
    exit(result);
  }
}

static inline void chk(bool result) {
  if (!result) {
    std::cerr << "Call returned an error\n";
    exit(result);
  }
}

int main(int argc, char *argv[]) {
  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "Mine",
                               .apiVersion = VK_API_VERSION_1_3};
  uint32_t instanceExtensionCount{0};
  char const *const *instanceExtensions{
      SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount)};

  VkInstanceCreateInfo instanceCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledExtensionCount = instanceExtensionCount,
      .ppEnabledExtensionNames = instanceExtensions};
  chk(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

  uint32_t deviceCount{0};
  chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
  std::vector<VkPhysicalDevice> devices(deviceCount);
  chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

  uint32_t deviceIndex{0};
  if (argc > 1) {
    deviceIndex = std::stoi(argv[1]);
    assert(deviceIndex < deviceCount);
  }

  VkPhysicalDeviceProperties2 deviceProperties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
  std::cout << "Device: " << deviceProperties.properties.deviceName
            << std::endl;

  uint32_t queueFamilyCount{0};
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex],
                                           &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(
      devices[deviceIndex], &queueFamilyCount, queueFamilies.data());

  uint32_t queueFamily{0};
  for (uint32_t i = 0; i < queueFamilies.size(); i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamily = i;
      break;
    }
  }
  chk(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex],
                                        queueFamily));

  const float qfpriorities{1.0f};
  VkDeviceQueueCreateInfo queueCI{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queueFamily,
      .queueCount = 1,
      .pQueuePriorities = &qfpriorities,
  };

  const std::vector<const char *> deviceExtensions{
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceVulkan12Features enabledVk12Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .bufferDeviceAddress = true,
  };
  VkPhysicalDeviceVulkan13Features enabledVk13Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &enabledVk12Features,
      .synchronization2 = true,
      .dynamicRendering = true,
  };
  VkPhysicalDeviceFeatures enabledVk10Features{
      .samplerAnisotropy = VK_TRUE,
  };

  VkDeviceCreateInfo deviceCI{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &enabledVk13Features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCI,
      .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
      .ppEnabledExtensionNames = deviceExtensions.data(),
      .pEnabledFeatures = &enabledVk10Features,
  };
  chk(vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device));
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  VmaVulkanFunctions vkFunctions{
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
      .vkCreateImage = vkCreateImage,
  };
  VmaAllocatorCreateInfo allocatorCI{
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = devices[deviceIndex],
      .device = device,
      .pVulkanFunctions = &vkFunctions,
      .instance = instance,
  };
  chk(vmaCreateAllocator(&allocatorCI, &allocator));

  SDL_Window *window = SDL_CreateWindow(
      "Mine", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  chk(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
  chk(SDL_GetWindowSize(window, &windowSize.x, &windowSize.y));
  VkSurfaceCapabilitiesKHR surfaceCaps{};
  chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface,
                                                &surfaceCaps));
  VkExtent2D swapchainExtent{surfaceCaps.currentExtent};
  if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {
    swapchainExtent = {.width = static_cast<uint32_t>(windowSize.x),
                       .height = static_cast<uint32_t>(windowSize.y)};
  }

  const VkFormat imageFormat{VK_FORMAT_B8G8R8A8_SRGB};
  VkSwapchainCreateInfoKHR swapchainCI{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = surfaceCaps.minImageCount,
      .imageFormat = imageFormat,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent{.width = swapchainExtent.width,
                   .height = swapchainExtent.height},
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
  };
  chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));

  uint32_t imageCount{0};
  chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
  swapchainImages.resize(imageCount);
  chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount,
                              swapchainImages.data()));

  std::vector<VkFormat> depthFormatList{VK_FORMAT_D32_SFLOAT_S8_UINT,
                                        VK_FORMAT_D24_UNORM_S8_UINT};
  VkFormat depthFormat{VK_FORMAT_UNDEFINED};
  for (VkFormat format : depthFormatList) {
    VkFormatProperties2 formatProperties{
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
    vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format,
                                         &formatProperties);
    if (formatProperties.formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      depthFormat = format;
      break;
    }
  }

  VkImageCreateInfo depthImageCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = depthFormat,
      .extent = {.width = static_cast<uint32_t>(windowSize.x),
                 .height = static_cast<uint32_t>(windowSize.y),
                 .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo allocCI{
      .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};
  chk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage,
                     &depthImageAllocation, nullptr));

  VkImageViewCreateInfo depthViewCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = depthImage,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = depthFormat,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .levelCount = 1,
                           .layerCount = 1},
  };
  chk(vkCreateImageView(device, &depthViewCI, nullptr, &depthImageView));

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;

  return 0;
}