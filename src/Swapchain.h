#pragma once
#include "vk_types.h"
#include <VkBootstrap.h>

namespace Engine
{
	struct EngineSwapchain;

	 class Swapchain final
	{
	public:
		explicit Swapchain(const VkDevice& device, const VkPhysicalDevice& physicalDevice, const VkSurfaceKHR& surface);
	 	virtual ~Swapchain();
		//Swapchain(Swapchain&& other) noexcept;
		//Swapchain& operator=(Swapchain&& other) noexcept;

		void CreateSwapchain(uint32_t width, uint32_t height, VkFormat format);
		void ResizeSwapchain(uint32_t width, uint32_t height, VkFormat format);
		void DestroySwapchain();

		VkSwapchainKHR GetSwapchain() const { return _swapchain; }
		VkFormat GetImageFormat() const { return _imageFormat; }
		VkExtent2D GetExtent() const { return _extent; }
		size_t GetImageCount() const { return _images.size(); }
		VkImage GetImage(uint32_t imageIndex) { return _images[imageIndex]; }
		VkImageView GetImageView(uint32_t imageIndex) { return _imageViews[imageIndex]; }

	private:
		VkDevice _device;
		VkPhysicalDevice _physicalDevice;
		VkSurfaceKHR _surface;

		VkSwapchainKHR _swapchain;
		VkFormat _imageFormat;
		std::vector<VkImage> _images;
		std::vector<VkImageView> _imageViews;
		VkExtent2D _extent;
	};
}
