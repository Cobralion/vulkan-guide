#pragma once
#include "vk_types.h"
#include <span>

class DescriptorAllocatorGrowable final
{
public:
	
	struct PoolSizeRatio
	{
		VkDescriptorType type;
		float ratio;
	};

	void Init(VkDevice device, uint32_t initialMaxSets, std::span<PoolSizeRatio> poolRatios);
	void ClearPools(VkDevice device);
	void DestroyPools(VkDevice device);

	VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr);

private:
	VkDescriptorPool GetPool(VkDevice device);
	VkDescriptorPool CreatePool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

	std::vector<PoolSizeRatio> _ratios;
	std::vector<VkDescriptorPool> _fullPools;
	std::vector<VkDescriptorPool> _readyPools;
	uint32_t _setsPerPool {0};
};

struct DescriptorLayoutBuilder
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void AddBinding(uint32_t binding, VkDescriptorType type);
	void Clear();
	VkDescriptorSetLayout Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};
