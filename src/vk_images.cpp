#include <vk_images.h>

#include "vk_initializers.h"

void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
	VkImageAspectFlags aspectFlags = (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageMemoryBarrier2 imageBarrier{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		nullptr,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
		currentLayout,
		newLayout,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image,
		vkinit::image_subresource_range(aspectFlags)
	};

	VkDependencyInfo dependencyInfo{
	.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	.pNext = nullptr,
	.imageMemoryBarrierCount = 1,
	.pImageMemoryBarriers = &imageBarrier
	};

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
