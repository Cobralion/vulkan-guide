#include <vk_descriptors.h>

void DescriptorAllocator::InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (auto& ratio : poolRatios)
	{
		VkDescriptorPoolSize size = {};
		size.type = ratio.type;
		size.descriptorCount = static_cast<uint32_t>(ratio.ratio * maxSets);
		poolSizes.push_back(size);
	}

	VkDescriptorPoolCreateInfo poolInfo { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.flags = 0;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);

}

void DescriptorAllocator::ClearDescriptors(VkDevice device)
{
	vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::DestroyPool(VkDevice device)
{
	vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::Allocate(VkDevice devic, VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocInfo { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocInfo.pNext = nullptr;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet set;
	VK_CHECK(vkAllocateDescriptorSets(devic, &allocInfo, &set));
	return set;
}

void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding newBinding = {};
	newBinding.binding = binding;
	newBinding.descriptorType = type;
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
		b.stageFlags |= shaderStages;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = pNext };
	layoutInfo.flags = flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout set;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &set));

	return set;
}
