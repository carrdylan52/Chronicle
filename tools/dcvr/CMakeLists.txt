include(FetchContent)
# Khronos OpenXR SDK release-1.1.63, peeled release commit. Bundled JSON and generated sources.
set(BUILD_API_LAYERS OFF)
set(BUILD_TESTS OFF)
set(BUILD_CONFORMANCE_TESTS OFF)
set(BUILD_SDK_TESTS OFF)
set(BUILD_WITH_SYSTEM_JSONCPP OFF)
set(DYNAMIC_LOADER OFF)
FetchContent_Declare(dc_openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG f2448a8797c85814aa892efc1ab8707900fbcc78)
FetchContent_MakeAvailable(dc_openxr)

add_executable(dcvr_probe main.cpp ${CMAKE_SOURCE_DIR}/port/src/gfx/requirements.cpp)
set_target_properties(dcvr_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(dcvr_probe PRIVATE ${CMAKE_SOURCE_DIR}/port/src)
target_link_libraries(dcvr_probe PRIVATE openxr_loader Vulkan::Vulkan SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_definitions(dcvr_probe PRIVATE XR_USE_GRAPHICS_API_VULKAN)
target_compile_options(dcvr_probe PRIVATE -Wall)
if(WIN32)
    target_link_libraries(dcvr_probe PRIVATE advapi32 ole32)
endif()
