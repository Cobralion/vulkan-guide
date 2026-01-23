#include <vk_images.h>

#include "vk_initializers.h"

void vkutil::TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
	// VK_IMAGE_ASPECT_DEPTH_BIT: depth/stencil buffers
	// VK_IMAGE_ASPECT_COLOR_BIT: rgba textures
	VkImageAspectFlags aspectFlags = (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageMemoryBarrier2 imageBarrier{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		nullptr,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, // wait for all previous GPU work to complete
		VK_ACCESS_2_MEMORY_WRITE_BIT, // make sure writes are flushed to the image
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, // blocks all subsequent commands from executing until the transition finishes
		VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT, // invalidates caches so the next command doesn't read "stale" old data
		currentLayout,
		newLayout, // reorganize the image in gpu memory
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image,
		vkinit::image_subresource_range(aspectFlags)
	};

	// mipmaps: downscaled versions of the original image for better performance when viewed from a distance / prevents aliasing
	// array layers: layers in a texture array, useful for storing multiple textures in a single image resource

	VkDependencyInfo dependencyInfo{
	.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	.pNext = nullptr,
	.imageMemoryBarrierCount = 1,
	.pImageMemoryBarriers = &imageBarrier
	};

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void vkutil::CopyImageToImage(VkCommandBuffer cmd, VkImage srcImage, VkImage dstImage, VkExtent2D srcSize,
	VkExtent2D dstSize)
{
	VkImageBlit2 blitRegion { .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.mipLevel = 0;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.mipLevel = 0;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;

	VkBlitImageInfo2 blitImageInfo {
		.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
		.pNext = nullptr,
		.srcImage = srcImage,
		.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.dstImage = dstImage,
		.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.regionCount = 1,
		.pRegions = &blitRegion,
		.filter = VK_FILTER_LINEAR
	};

	vkCmdBlitImage2(cmd, &blitImageInfo); // VkCmdCopyImage is faster but way less versatile -> later use fragment shader
}
