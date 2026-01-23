#pragma once
#include <vk_types.h>

namespace vkutil {

	struct AllocatedImage {
		VkImage image;
		VkImageView imageView;
		VmaAllocation allocation;
		VkExtent3D imageExtent;
		VkFormat imageFormat;
	};

	void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
	void CopyImageToImage(VkCommandBuffer cmd, VkImage srcImage, VkImage dstImage, VkExtent2D srcExtend, VkExtent2D dstExtent);

};