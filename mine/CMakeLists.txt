cmake_minimum_required(VERSION 3.20)
project(Mine LANGUAGES CXX)

include(FetchContent)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

if (IS_DIRECTORY "$ENV{VULKAN_SDK}")
  include_directories(SYSTEM $ENV{VULKAN_SDK}/include)
endif()

find_package(Vulkan REQUIRED)

if (LINUX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
endif()

add_executable(Mine main.cpp)
target_compile_features(Mine PRIVATE cxx_std_20)
target_link_libraries(Mine PRIVATE SDL3::SDL3 Vulkan::Vulkan)
