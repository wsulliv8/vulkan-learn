#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <string>
#include <vector>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include "slang/slang-com-ptr.h"
#include "slang/slang.h"
#include <ktx.h>
#include <ktxvulkan.h>
#include <tiny_obj_loader.h>
#include <vulkan/vulkan.h>

constexpr uint32_t maxFramesInFlight{2};

struct ShaderDataBuffer {
  VmaAllocation allocation{VK_NULL_HANDLE};
  VmaAllocationInfo allocationInfo{};
  VkBuffer buffer{VK_NULL_HANDLE};
  VkDeviceAddress deviceAddress{};
};
struct ShaderData {
  glm::mat4 projection;
  glm::mat4 view;
  glm::mat4 model[3];
  glm::vec4 lightPos{0.0f, -10.0f, 10.0f, 0.0f};
  uint32_t selected{1};
} shaderData{};

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

struct Texture {
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkImage image{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  VkSampler sampler{VK_NULL_HANDLE};
};
std::array<Texture, 3> textures{};

VkInstance instance{VK_NULL_HANDLE};
VkDevice device{VK_NULL_HANDLE};
VkQueue queue{VK_NULL_HANDLE};
VmaAllocator allocator{VK_NULL_HANDLE};
VkSurfaceKHR surface{VK_NULL_HANDLE};
glm::vec3 camPos{0.0f, 0.0f, -6.0f};
glm::vec3 objectRotations[3]{};
glm::ivec2 windowSize{};
VkSwapchainKHR swapchain{VK_NULL_HANDLE};
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
VkImage depthImage;
VmaAllocation depthImageAllocation;
VkImageView depthImageView;
VkBuffer vBuffer{VK_NULL_HANDLE};
VmaAllocation vBufferAllocation{VK_NULL_HANDLE};
std::array<ShaderDataBuffer, maxFramesInFlight> shaderDataBuffers;
std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
std::array<VkFence, maxFramesInFlight> fences;
std::array<VkSemaphore, maxFramesInFlight> imageAcquiredSemaphores;
std::vector<VkSemaphore> renderCompleteSemaphores;
VkCommandPool commandPool{VK_NULL_HANDLE};
VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
VkDescriptorSetLayout descriptorSetLayoutTex{VK_NULL_HANDLE};
VkDescriptorSet descriptorSetTex{VK_NULL_HANDLE};
VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
VkPipeline pipeline{VK_NULL_HANDLE};
uint32_t frameIndex{0};
bool updateSwapchain{false};
uint32_t imageIndex{0};
static inline void chk(VkResult result) {
  if (result != VK_SUCCESS) {
    std::cerr << "Vulkan call returned an error (" << result << ")\n";
    exit(result);
  }
}

static inline void chk(bool result) {
  if (!result) {
    const char *sdlErr = SDL_GetError();
    if (sdlErr && sdlErr[0] != '\0') {
      std::cerr << "Call returned an error: " << sdlErr << "\n";
    } else {
      std::cerr << "Call returned an error\n";
    }
    exit(result);
  }
}

static inline void chkSwapchain(VkResult result) {
  if (result < VK_SUCCESS) {
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
      return;
    }
    std::cerr << "Vulkan call returned an error (" << result << ")\n";
    exit(result);
  }
}

int main(int argc, char *argv[]) {
  if (!std::filesystem::is_directory("assets")) {
    std::cerr << "Could not locate assets folder from current working directory\n";
    return EXIT_FAILURE;
  }

  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "Mine",
                               .apiVersion = VK_API_VERSION_1_3};
  chk(SDL_Init(SDL_INIT_VIDEO));
  const char *vulkanLoader = nullptr;
  if (const char *sdk = std::getenv("VULKAN_SDK")) {
    static std::string vulkanLoaderPath =
        std::string(sdk) + "/lib/VulkanLoader/lib/libvulkan.so.1";
    vulkanLoader = vulkanLoaderPath.c_str();
  }
  chk(SDL_Vulkan_LoadLibrary(vulkanLoader));
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

  uint32_t queueFamily{UINT32_MAX};
  for (uint32_t i = 0; i < queueFamilies.size(); i++) {
    if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], i)) {
      queueFamily = i;
      break;
    }
  }
  if (queueFamily == UINT32_MAX) {
    std::cerr << "No queue family supports graphics and presentation\n";
    exit(EXIT_FAILURE);
  }

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
  swapchainImageViews.resize(imageCount);
  chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount,
                              swapchainImages.data()));
  for (uint32_t i = 0; i < imageCount; i++) {
    VkImageViewCreateInfo viewCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapchainImages[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = imageFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1},
    };
    chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
  }

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
  std::vector<tinyobj::material_t> materials;
  chk(tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr,
                       "assets/suzanne.obj"));

  const VkDeviceSize indexCount{shapes[0].mesh.indices.size()};
  std::vector<Vertex> vertices{};
  std::vector<uint16_t> indices{};
  for (auto &index : shapes[0].mesh.indices) {
    Vertex v{
        .pos = {attrib.vertices[3 * index.vertex_index + 0],
                -attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]},
        .normal = {attrib.normals[3 * index.normal_index + 0],
                   -attrib.normals[3 * index.normal_index + 1],
                   attrib.normals[3 * index.normal_index + 2]},
        .uv = {attrib.texcoords[2 * index.texcoord_index + 0],
               1.0 - attrib.texcoords[2 * index.texcoord_index + 1]},
    };
    vertices.push_back(v);
    indices.push_back(indices.size());
  }

  VkDeviceSize vBufSize{sizeof(Vertex) * vertices.size()};
  VkDeviceSize iBufSize{sizeof(uint16_t) * indices.size()};
  VkBufferCreateInfo bufferCI{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = vBufSize + iBufSize,
      .usage =
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
  };
  VmaAllocationCreateInfo vBufferAllocCI{
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };
  VmaAllocationInfo vBufferAllocInfo{};
  chk(vmaCreateBuffer(allocator, &bufferCI, &vBufferAllocCI, &vBuffer,
                      &vBufferAllocation, &vBufferAllocInfo));

  memcpy(vBufferAllocInfo.pMappedData, vertices.data(), vBufSize);
  memcpy((char *)vBufferAllocInfo.pMappedData + vBufSize, indices.data(),
         iBufSize);

  for (auto i = 0; i < maxFramesInFlight; i++) {
    VkBufferCreateInfo uBufferCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(ShaderData),
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    VmaAllocationCreateInfo uBufferAllocCI{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    chk(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI,
                        &shaderDataBuffers[i].buffer,
                        &shaderDataBuffers[i].allocation,
                        &shaderDataBuffers[i].allocationInfo));
    VkBufferDeviceAddressInfo uBufferBdaInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = shaderDataBuffers[i].buffer,
    };
    shaderDataBuffers[i].deviceAddress =
        vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
  }

  VkSemaphoreCreateInfo semaphoreCI{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  VkFenceCreateInfo fenceCI{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };
  for (auto i = 0; i < maxFramesInFlight; i++) {
    chk(vkCreateSemaphore(device, &semaphoreCI, nullptr,
                          &imageAcquiredSemaphores[i]));
    chk(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
  }
  renderCompleteSemaphores.resize(swapchainImages.size());
  for (auto &semaphore : renderCompleteSemaphores) {
    chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
  }

  VkCommandPoolCreateInfo commandPoolCI{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queueFamily,
  };
  chk(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

  VkCommandBufferAllocateInfo cbAllocCI{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = commandPool,
      .commandBufferCount = maxFramesInFlight,
  };
  chk(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers.data()));
  std::vector<VkDescriptorImageInfo> textureDescriptors{};
  for (auto i = 0; i < textures.size(); i++) {
    ktxTexture *ktxTexture{nullptr};
    std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
    ktxTexture_CreateFromNamedFile(
        filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);

    VkImageCreateInfo texImgCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = ktxTexture_GetVkFormat(ktxTexture),
        .extent = {.width = ktxTexture->baseWidth,
                   .height = ktxTexture->baseHeight,
                   .depth = 1},
        .mipLevels = ktxTexture->numLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo texImgAllocCI{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    chk(vmaCreateImage(allocator, &texImgCI, &texImgAllocCI, &textures[i].image,
                       &textures[i].allocation, nullptr));
    VkImageViewCreateInfo texVewCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = textures[i].image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = texImgCI.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = ktxTexture->numLevels,
                             .layerCount = 1},
    };
    chk(vkCreateImageView(device, &texVewCI, nullptr, &textures[i].view));

    VkBuffer imgSrcBuffer{};
    VmaAllocation imgSrcAllocation{};
    VkBufferCreateInfo imgSrcBufferCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (uint32_t)ktxTexture->dataSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo imgSrcAllocCI{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo imgSrcAllocInfo{};
    chk(vmaCreateBuffer(allocator, &imgSrcBufferCI, &imgSrcAllocCI,
                        &imgSrcBuffer, &imgSrcAllocation, &imgSrcAllocInfo));
    memcpy(imgSrcAllocInfo.pMappedData, ktxTexture->pData,
           ktxTexture->dataSize);

    VkFenceCreateInfo fenceOneTimeCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fenceOneTime{};
    chk(vkCreateFence(device, &fenceOneTimeCI, nullptr, &fenceOneTime));
    VkCommandBuffer cbOneTime{};
    VkCommandBufferAllocateInfo cbOneTimeAI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .commandBufferCount = 1,
    };
    chk(vkAllocateCommandBuffers(device, &cbOneTimeAI, &cbOneTime));
    VkCommandBufferBeginInfo cbOneTimeBI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    chk(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));
    VkImageMemoryBarrier2 barrierTexImage{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = textures[i].image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = ktxTexture->numLevels,
                             .layerCount = 1},
    };
    VkDependencyInfo barrierTexInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierTexImage,
    };
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    std::vector<VkBufferImageCopy> copyRegions{};
    for (auto j = 0; j < ktxTexture->numLevels; j++) {
      ktx_size_t mipOffset{0};
      KTX_error_code ret =
          ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
      copyRegions.push_back({
          .bufferOffset = mipOffset,
          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .mipLevel = (uint32_t)j,
                               .layerCount = 1},
          .imageExtent{.width = ktxTexture->baseWidth >> j,
                       .height = ktxTexture->baseHeight >> j,
                       .depth = 1},
      });
    }
    vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, textures[i].image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(copyRegions.size()),
                           copyRegions.data());
    VkImageMemoryBarrier2 barrierTexRead{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = textures[i].image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = ktxTexture->numLevels,
                             .layerCount = 1},
    };
    barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    chk(vkEndCommandBuffer(cbOneTime));
    VkSubmitInfo oneTimeSI{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cbOneTime,
    };
    chk(vkQueueSubmit(queue, 1, &oneTimeSI, fenceOneTime));
    chk(vkWaitForFences(device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fenceOneTime, nullptr);
    vmaDestroyBuffer(allocator, imgSrcBuffer, imgSrcAllocation);

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .maxLod = (float)ktxTexture->numLevels,
    };
    chk(vkCreateSampler(device, &samplerCI, nullptr, &textures[i].sampler));
    ktxTexture_Destroy(ktxTexture);
    textureDescriptors.push_back({
        .sampler = textures[i].sampler,
        .imageView = textures[i].view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    });
  }

  VkDescriptorBindingFlags descVariableFlag{
      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT};
  VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 1,
      .pBindingFlags = &descVariableFlag,
  };
  VkDescriptorSetLayoutBinding descLayoutBindingTex{
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = static_cast<uint32_t>(textures.size()),
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  VkDescriptorSetLayoutCreateInfo descLayoutTexCI{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &descBindingFlags,
      .bindingCount = 1,
      .pBindings = &descLayoutBindingTex,
  };
  chk(vkCreateDescriptorSetLayout(device, &descLayoutTexCI, nullptr,
                                  &descriptorSetLayoutTex));

  VkDescriptorPoolSize poolSize{
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = static_cast<uint32_t>(textures.size()),
  };
  VkDescriptorPoolCreateInfo descPoolCI{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &poolSize,
  };
  chk(vkCreateDescriptorPool(device, &descPoolCI, nullptr, &descriptorPool));

  uint32_t variableDescCount{static_cast<uint32_t>(textures.size())};
  VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
      .descriptorSetCount = 1,
      .pDescriptorCounts = &variableDescCount,
  };

  VkDescriptorSetAllocateInfo texDescSetAlloc{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = &variableDescCountAI,
      .descriptorPool = descriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSetLayoutTex,
  };
  chk(vkAllocateDescriptorSets(device, &texDescSetAlloc, &descriptorSetTex));
  VkWriteDescriptorSet writeDescSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptorSetTex,
      .dstBinding = 0,
      .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = textureDescriptors.data(),
  };
  vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);

  slang::createGlobalSession(slangGlobalSession.writeRef());

  auto slangTargets{std::to_array<slang::TargetDesc>(
      {{.format = SLANG_SPIRV,
        .profile = slangGlobalSession->findProfile("spirv_1_4")}})};
  auto slangOptions{std::to_array<slang::CompilerOptionEntry>(
      {{slang::CompilerOptionName::EmitSpirvDirectly,
        {slang::CompilerOptionValueKind::Int, 1}}})};
  slang::SessionDesc slangSessionDesc{
      .targets = slangTargets.data(),
      .targetCount = SlangInt(slangTargets.size()),
      .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
      .compilerOptionEntries = slangOptions.data(),
      .compilerOptionEntryCount = uint32_t(slangOptions.size()),
  };
  Slang::ComPtr<slang::ISession> slangSession;
  slangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());

  Slang::ComPtr<slang::IModule> slangModule{slangSession->loadModuleFromSource(
      "triangle", "assets/shader.slang", nullptr, nullptr)};
  Slang::ComPtr<ISlangBlob> spirv;
  slangModule->getTargetCode(0, spirv.writeRef());

  VkShaderModuleCreateInfo shaderModuleCI{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv->getBufferSize(),
      .pCode = (uint32_t *)spirv->getBufferPointer(),
  };

  VkShaderModule shaderModule{};
  chk(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));

  VkPushConstantRange pushConstantRange{.stageFlags =
                                            VK_SHADER_STAGE_VERTEX_BIT,
                                        .size = sizeof(VkDeviceAddress)};
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptorSetLayoutTex,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstantRange,
  };
  chk(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr,
                             &pipelineLayout));

  VkVertexInputBindingDescription vertexBinding{
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
  };
  std::vector<VkVertexInputAttributeDescription> vertexAttributes{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
      {.location = 1,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32_SFLOAT,
       .offset = offsetof(Vertex, normal)},
      {.location = 2,
       .binding = 0,
       .format = VK_FORMAT_R32G32_SFLOAT,
       .offset = offsetof(Vertex, uv)},
  };

  VkPipelineVertexInputStateCreateInfo vertexInputState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount =
          static_cast<uint32_t>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,

  };

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = shaderModule,
          .pName = "main",
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = shaderModule,
          .pName = "main",
      },
  };

  VkPipelineViewportStateCreateInfo viewportState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };
  std::vector<VkDynamicState> dynamicStates{
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo dynamicState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data(),
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
  };
  VkPipelineRenderingCreateInfo renderingCI{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &imageFormat,
      .depthAttachmentFormat = depthFormat,
  };
  VkPipelineColorBlendAttachmentState blendAttachment{.colorWriteMask = 0xF};
  VkPipelineColorBlendStateCreateInfo colorBlendState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blendAttachment};
  VkPipelineRasterizationStateCreateInfo rasterizationState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f};
  VkPipelineMultisampleStateCreateInfo multisampleState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  VkGraphicsPipelineCreateInfo pipelineCI{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingCI,
      .stageCount = 2,
      .pStages = shaderStages.data(),
      .pVertexInputState = &vertexInputState,
      .pInputAssemblyState = &inputAssemblyState,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizationState,
      .pMultisampleState = &multisampleState,
      .pDepthStencilState = &depthStencilState,
      .pColorBlendState = &colorBlendState,
      .pDynamicState = &dynamicState,
      .layout = pipelineLayout,
  };
  chk(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr,
                                &pipeline));

  uint64_t lastTime{SDL_GetTicks()};
  bool quit{false};
  while (!quit) {
    chk(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
    chk(vkResetFences(device, 1, &fences[frameIndex]));

    chkSwapchain(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                       imageAcquiredSemaphores[frameIndex],
                                       VK_NULL_HANDLE, &imageIndex));

    shaderData.projection = glm::perspective(
        glm::radians(45.0f), (float)windowSize.x / (float)windowSize.y, 0.1f,
        32.0f);
    shaderData.view = glm::translate(glm::mat4(1.0f), camPos);
    for (auto i = 0; i < 3; i++) {
      auto instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
      shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) *
                            glm::mat4_cast(glm::quat(objectRotations[i]));
    }
    memcpy(shaderDataBuffers[frameIndex].allocationInfo.pMappedData,
           &shaderData, sizeof(shaderData));

    auto cb = commandBuffers[frameIndex];
    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbBI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    chk(vkBeginCommandBuffer(cb, &cbBI));
    std::array<VkImageMemoryBarrier2, 2> outputBarriers{
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = swapchainImages[imageIndex],
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1},
        },
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .image = depthImage,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                               VK_IMAGE_ASPECT_STENCIL_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1},
        }};
    VkDependencyInfo barrierDependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = outputBarriers.data(),
    };
    vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

    VkRenderingAttachmentInfo colorAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{.color{.float32{0.0f, 0.0f, 0.2f, 1.0f}}}

    };
    VkRenderingAttachmentInfo depthAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue{.depthStencil{.depth = 1.0f, .stencil = 0}}};

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{.extent{.width = static_cast<uint32_t>(windowSize.x),
                            .height = static_cast<uint32_t>(windowSize.y)}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };
    vkCmdBeginRendering(cb, &renderingInfo);

    VkViewport vp{
        .width = static_cast<float>(windowSize.x),
        .height = static_cast<float>(windowSize.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{.extent{.width = static_cast<uint32_t>(windowSize.x),
                             .height = static_cast<uint32_t>(windowSize.y)}};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize vOffset{0};
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSetTex, 0, nullptr);
    vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
    vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(VkDeviceAddress),
                       &shaderDataBuffers[frameIndex].deviceAddress);
    vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0);
    vkCmdEndRendering(cb);

    VkImageMemoryBarrier2 barrierPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages[imageIndex],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1},
    };
    VkDependencyInfo barrierPresentDependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierPresent,
    };
    vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

    chk(vkEndCommandBuffer(cb));

    VkPipelineStageFlags waitStages =
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAcquiredSemaphores[frameIndex],
        .pWaitDstStageMask = &waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderCompleteSemaphores[imageIndex],
    };
    chk(vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]));

    frameIndex = (frameIndex + 1) % maxFramesInFlight;

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };
    chk(vkQueuePresentKHR(queue, &presentInfo));

    float elapsedTime{(float)(SDL_GetTicks() - lastTime) / 1000.0f};
    lastTime = SDL_GetTicks();
    for (SDL_Event event; SDL_PollEvent(&event);) {
      if (event.type == SDL_EVENT_QUIT) {
        quit = true;
        break;
      }

      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          objectRotations[shaderData.selected].x -=
              (float)event.motion.yrel * elapsedTime;
          objectRotations[shaderData.selected].y +=
              (float)event.motion.xrel * elapsedTime;
        }
      }

      if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        camPos.z += (float)event.wheel.y * elapsedTime * 10.0f;
      }

      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
          shaderData.selected =
              (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
        }
        if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
          shaderData.selected =
              (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
        }
      }

      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        updateSwapchain = true;
      }
    }

    if (updateSwapchain) {
      updateSwapchain = false;
      vkDeviceWaitIdle(device);
      chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex],
                                                    surface, &surfaceCaps));
      swapchainCI.oldSwapchain = swapchain;
      swapchainCI.imageExtent = {.width = static_cast<uint32_t>(windowSize.x),
                                 .height = static_cast<uint32_t>(windowSize.y)};
      chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain));
      for (auto i = 0; i < imageCount; i++) {
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
      }
      chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
      swapchainImages.resize(imageCount);
      chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount,
                                  swapchainImages.data()));
      swapchainImageViews.resize(imageCount);
      for (auto i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewCI{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = imageFormat,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1},
        };
        chk(vkCreateImageView(device, &viewCI, nullptr,
                              &swapchainImageViews[i]));
      }
      for (auto &semaphore : renderCompleteSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
      }
      renderCompleteSemaphores.resize(imageCount);
      for (auto &semaphore : renderCompleteSemaphores) {
        chk(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
      }
      vkDestroySwapchainKHR(device, swapchainCI.oldSwapchain, nullptr);
      vmaDestroyImage(allocator, depthImage, depthImageAllocation);
      vkDestroyImageView(device, depthImageView, nullptr);
      depthImageCI.extent = {.width = static_cast<uint32_t>(windowSize.x),
                             .height = static_cast<uint32_t>(windowSize.y)};
      VmaAllocationCreateInfo allocCI{
          .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
          .usage = VMA_MEMORY_USAGE_AUTO,
      };
      chk(vmaCreateImage(allocator, &depthImageCI, &allocCI, &depthImage,
                         &depthImageAllocation, nullptr));
      VkImageViewCreateInfo viewCI{
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = depthImage,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = depthFormat,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                               .levelCount = 1,
                               .layerCount = 1},
      };
      chk(vkCreateImageView(device, &viewCI, nullptr, &depthImageView));
    }
  }
  // Tear down
  chk(vkDeviceWaitIdle(device));
  for (auto i = 0; i < maxFramesInFlight; i++) {
    vkDestroyFence(device, fences[i], nullptr);
    vkDestroySemaphore(device, imageAcquiredSemaphores[i], nullptr);
    vmaDestroyBuffer(allocator, shaderDataBuffers[i].buffer,
                     shaderDataBuffers[i].allocation);
  }
  for (auto i = 0; i < renderCompleteSemaphores.size(); i++) {
    vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
  }
  vmaDestroyImage(allocator, depthImage, depthImageAllocation);
  vkDestroyImageView(device, depthImageView, nullptr);
  for (auto i = 0; i < swapchainImageViews.size(); i++) {
    vkDestroyImageView(device, swapchainImageViews[i], nullptr);
  }
  vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
  for (auto i = 0; i < textures.size(); i++) {
    vkDestroyImageView(device, textures[i].view, nullptr);
    vkDestroySampler(device, textures[i].sampler, nullptr);
    vmaDestroyImage(allocator, textures[i].image, textures[i].allocation);
  }
  vkDestroyDescriptorSetLayout(device, descriptorSetLayoutTex, nullptr);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyCommandPool(device, commandPool, nullptr);
  vkDestroyShaderModule(device, shaderModule, nullptr);
  vmaDestroyAllocator(allocator);
  SDL_DestroyWindow(window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_Quit();
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);
};
