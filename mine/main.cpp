#include <cstdint>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

int main() {
  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "Mine",
                               .apiVersion = VK_API_VERSION_1_3};
  uint32_t instanceExtensionCount{0};
  char const *const *instanceExtensions{
      SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount)};

  return 0;
}