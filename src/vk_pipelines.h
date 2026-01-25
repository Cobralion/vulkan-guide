#pragma once 
#include <vk_types.h>

namespace vkutil {
    bool LoadShaderModuleSPV(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
	bool LoadShaderModuleSlang(const char* name, const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
};