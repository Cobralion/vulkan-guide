#include "D:/Source/Repos/vulkan-guide/bin/src/CMakeFiles/engine.dir/Debug/cmake_pch.hxx"
#include "Swapchain.h"


Engine::Swapchain::Swapchain(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
	const VkSurfaceKHR& surface) : _device(device), _physicalDevice(physicalDevice), _surface(surface)
{}

Engine::Swapchain::~Swapchain() = default;


// Swapchain:
// Decouples the rendering from the windowing system
// -> allows us to render images in advance before they are presented on the screen => double buffering, triple buffering
// this helps to avoid stuttering and tearing effects
void Engine::Swapchain::CreateSwapchain(uint32_t width, uint32_t height, VkFormat format)
{
	vkb::SwapchainBuilder builder(_physicalDevice, _device, _surface);

	// image format for the swapchain images
	_imageFormat = format;

	// VK_IMAGE_USAGE_TRANSFER_DST_BIT: 
	vkb::Swapchain vkbSwapchain = builder
		.set_desired_format(VkSurfaceFormatKHR{ .format = _imageFormat })
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT) // allows vkCmdBlitImage or copying to the swapchain image
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // hard vsync
		.set_desired_extent(width, height)
		.build()
		.value();

	// the width and height of the swapchain images
	_extent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	// images and imageview in the swapchain
	_images = vkbSwapchain.get_images().value();
	_imageViews = vkbSwapchain.get_image_views().value();
}

void Engine::Swapchain::ResizeSwapchain(uint32_t width, uint32_t height, VkFormat format)
{
	vkDeviceWaitIdle(_device);
	DestroySwapchain();
	CreateSwapchain(width, height, format);
}

void Engine::Swapchain::DestroySwapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	for (auto imageView : _imageViews) {
		vkDestroyImageView(_device, imageView, nullptr);
	}

	_swapchain = VK_NULL_HANDLE;
	_imageViews = {};
	_images = {};
	_extent = { 0, 0 };
	_imageFormat = VK_FORMAT_UNDEFINED;
}
