#pragma once
#include "vk_types.h"

class DescriptorWriter
{
public:
	std::deque<VkDescriptorImageInfo> imageInfos; // std::deque is guaranteed to keep pointers to elements valid
	std::deque<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> writes;

	void WriteImage(uint32_t binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
	void WriteBuffer(uint32_t binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

	void Clear();
	void UpdateSet(VkDevice device, VkDescriptorSet set);
};
