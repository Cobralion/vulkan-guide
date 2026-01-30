#include "D:/Source/Repos/vulkan-guide/bin/src/CMakeFiles/engine.dir/Debug/cmake_pch.hxx"
#include "DescriptorWriter.h"


void DescriptorWriter::WriteImage(uint32_t binding, VkImageView image, VkSampler sampler, VkImageLayout layout,
	VkDescriptorType type)
{
	VkDescriptorImageInfo& imageInfo = imageInfos.emplace_back(VkDescriptorImageInfo{ // lets us take a pointer to it
	sampler, image, layout
		});
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = VK_NULL_HANDLE,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pImageInfo = &imageInfo
	};

	writes.push_back(write);
}

void DescriptorWriter::WriteBuffer(uint32_t binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
	VkDescriptorBufferInfo& bufferInfo = bufferInfos.emplace_back(VkDescriptorBufferInfo{ // lets us take a pointer to it
		buffer, offset, size
		});
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = VK_NULL_HANDLE,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &bufferInfo
	};

	writes.push_back(write);
}

void DescriptorWriter::Clear()
{
	imageInfos.clear();
	bufferInfos.clear();
	writes.clear();
}

void DescriptorWriter::UpdateSet(VkDevice device, VkDescriptorSet set)
{
	for (VkWriteDescriptorSet& write : writes)
	{
		write.dstSet = set;
	}

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
