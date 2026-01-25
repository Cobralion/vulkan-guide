#include <vk_pipelines.h>
#include <fstream>
#include <vk_initializers.h>

#include "../bin/src/SlangShaderLoader.h"

bool vkutil::LoadShaderModuleSPV(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	// std::ios::ate: file pointer at the end of file
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	// pos of cursor in the stream -> size of the file
	size_t fileSize = (size_t)file.tellg();
	// allocate a buffer to hold the code
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	// cursor at beginning
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
	file.close();

	// Create shader module
	VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.pNext = nullptr;
	createInfo.codeSize = buffer.size() * sizeof(uint32_t); // size in bytes 
	createInfo.pCode = buffer.data(); // vulkan expects buffer as an uint32_t array

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}

bool vkutil::LoadShaderModuleSlang(const char* name, const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	// std::ios::ate: file pointer at the end of file
	std::ifstream file(filePath, std::ios::ate);

	if (!file.is_open())
	{
		return false;
	}

	// pos of cursor in the stream -> size of the file
	size_t fileSize = file.tellg();
	// allocate a buffer to hold the code
	std::vector<char> buffer(fileSize);
	// cursor at beginning
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	std::vector<uint32_t> dataBuffer;
	if (!SlangShaderLoader::Get().LoadShader(name, filePath, buffer.data(), dataBuffer))
	{
		fmt::print("Slang Shader failed to compile.\n");
		return false;
	}

	VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.pNext = nullptr;
	createInfo.codeSize = dataBuffer.size() * sizeof(uint32_t); // size in bytes
	createInfo.pCode = dataBuffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}
