#pragma once 
#include <vk_types.h>

namespace vkutil {
    bool LoadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);

};