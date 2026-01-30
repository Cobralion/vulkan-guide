// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <DeletionQueue.h>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "DescriptorAllocatorGrowable.h"


#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

struct AllocatedBuffer
{
    VkBuffer buffer;
	VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;
};

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection; // w for sun power
    glm::vec4 sunlightColor;
};

struct Vertex
{
	glm::vec3 position; // positon
	float uv_x;         // uv x coord -> x cord of the texture
    glm::vec3 normal;   // normals
    float uv_y;         // uv y coord -> y cord of the texture
    glm::vec4 color;    // color
};

struct GPUMeshBuffers
{
	AllocatedBuffer indexBuffer;        // buffer for the indices
	AllocatedBuffer vertexBuffer;       // buffer for the vertices
    VkDeviceAddress vertexBufferAddress;
};

struct GPUDrawPushConstants
{
	glm::mat4 worldMatrix;          // world matrix
	VkDeviceAddress vertexBuffer;   // address of the vertex buffer for access in shader
};
