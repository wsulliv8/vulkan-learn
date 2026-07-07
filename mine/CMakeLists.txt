cmake_minimum_required(VERSION 3.20)
project(Mine LANGUAGES CXX C)

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

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ktx)
set(KTX_SOURCES
  ${KTX_DIR}/lib/texture.c
  ${KTX_DIR}/lib/hashlist.c
  ${KTX_DIR}/lib/checkheader.c
  ${KTX_DIR}/lib/swap.c
  ${KTX_DIR}/lib/memstream.c
  ${KTX_DIR}/lib/filestream.c
  ${KTX_DIR}/lib/vkloader.c)
add_library(ktx STATIC ${KTX_SOURCES})
target_include_directories(ktx SYSTEM PUBLIC
  ${KTX_DIR}/include
  ${KTX_DIR}/other_include)
target_link_libraries(ktx PUBLIC Vulkan::Vulkan)

add_executable(Mine main.cpp)
target_include_directories(Mine SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/external/tinyobj)
target_compile_features(Mine PRIVATE cxx_std_20)
target_link_libraries(Mine PRIVATE SDL3::SDL3 Vulkan::Vulkan ktx)
