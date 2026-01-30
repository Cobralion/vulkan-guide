#include "D:/Source/Repos/vulkan-guide/bin/src/CMakeFiles/engine.dir/Debug/cmake_pch.hxx"
#include "DescriptorAllocatorGrowable.h"

#include <algorithm>

// Initializes the descriptor allocator with an initial pool and the given ratios for descriptor types
void DescriptorAllocatorGrowable::Init(VkDevice device, uint32_t initialMaxSets, std::span<PoolSizeRatio> poolRatios)
{
	_ratios.clear();

	for (auto r : poolRatios)
	{
		_ratios.push_back(r);
	}

	VkDescriptorPool newPool = CreatePool(device, initialMaxSets, poolRatios);
	_setsPerPool = initialMaxSets * 1.5f; // increase pool size on next allocation
	_readyPools.push_back(newPool);
}

void DescriptorAllocatorGrowable::ClearPools(VkDevice device)
{
	for (auto p : _readyPools)
	{
		vkResetDescriptorPool(device, p, 0);
	}

	for (auto p : _fullPools)
	{
		vkResetDescriptorPool(device, p, 0);
		_readyPools.push_back(p);
	}
	_fullPools.clear();
}

void DescriptorAllocatorGrowable::DestroyPools(VkDevice device)
{
	for (auto p : _readyPools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	_readyPools.clear();

	for (auto p : _fullPools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
		
	}
	_fullPools.clear();
}

VkDescriptorSet DescriptorAllocatorGrowable::Allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext)
{
	VkDescriptorPool pool = GetPool(device); // take a pool from the ready pools or create a new one

	VkDescriptorSetAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocInfo.pNext = pNext;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet set;
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &set);

	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) // check if pool is full
	{
		_fullPools.push_back(pool); // move to full pools

		pool = GetPool(device); // take another pool from the ready pools or create a new one

		allocInfo.descriptorPool = pool;
		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &set));
	}
	
	_readyPools.push_back(pool); // push back the pool to ready pools
	return set;
}

VkDescriptorPool DescriptorAllocatorGrowable::GetPool(VkDevice device)
{
	VkDescriptorPool newPool;
	if (!_readyPools.empty())
	{
		newPool = _readyPools.back(); // get last ready pool
		_readyPools.pop_back();
	}
	else
	{
		newPool = CreatePool(device, _setsPerPool, _ratios);

		_setsPerPool = _setsPerPool * 1.5f; // increase pool size for next time
		_setsPerPool = std::min<uint32_t>(_setsPerPool, 4092);
	}

	return newPool;
}

VkDescriptorPool DescriptorAllocatorGrowable::CreatePool(VkDevice device, uint32_t setCount,
	std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (auto& ratio : poolRatios)
	{
		VkDescriptorPoolSize size = {};
		size.type = ratio.type;
		size.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount); // number of descriptors sets of this type for max sets
		poolSizes.push_back(size);
	}

	VkDescriptorPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.flags = 0;
	poolInfo.maxSets = setCount;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	VkDescriptorPool newPool;
	vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool);
	return newPool;
}



// Binds the given binding number to the given descriptor type
void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding newBinding = {};
	newBinding.binding = binding; // binding number in the shader
	newBinding.descriptorType = type; // type of resource
	newBinding.descriptorCount = 1;

	bindings.push_back(newBinding);
}


void DescriptorLayoutBuilder::Clear()
{
	bindings.clear();
}


VkDescriptorSetLayout DescriptorLayoutBuilder::Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext,
	VkDescriptorSetLayoutCreateFlags flags)
{
	for (auto& b : bindings)
	{
		b.stageFlags |= shaderStages; // set which shader stages can access all bindings
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = pNext };
	layoutInfo.flags = flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout set;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &set));

	return set;
}
