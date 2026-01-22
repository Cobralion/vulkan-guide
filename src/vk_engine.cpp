//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#include "vk_images.h"

constexpr bool bUseValidationLayers = true;
constexpr uint32_t WAIT_TIME_OUT = 1000000000;
VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::init()
{
	// only one engine initialization is allowed with the application.
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	// We initialize SDL and create a window with it.
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN;

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags);

	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
{

	// Create the Vulkan instance and setup debug messenger
	vkb::InstanceBuilder builder;
	auto inst_ret = builder
		.set_app_name("Vulkan Engine Application")
		.set_engine_name("Vulkan Engine")
		.require_api_version(1, 3, 0)
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkbInstance = inst_ret.value();
	_instance = vkbInstance.instance;
	_debug_messenger = vkbInstance.debug_messenger;




	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	// vulkan 1.3
	VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	//vulkan 1.2
	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;


	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VulkanEngine::init_swapchain()
{
	create_swapchain(_windowExtent.width, _windowExtent.height);
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo cmdPoolCreateInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (auto& frame : _frames)
	{
		VK_CHECK(vkCreateCommandPool(_device, &cmdPoolCreateInfo, nullptr, &frame._commandPool));

		VkCommandBufferAllocateInfo cmdBufferAllocateInfo = vkinit::command_buffer_allocate_info(frame._commandPool, 1U);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdBufferAllocateInfo, &frame._mainCommandBuffer));
	}
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (auto& frame : _frames)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &frame._renderFence));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &frame._renderSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &frame._swapchainSemaphore));
	}
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder builder(_physicalDevice, _device, _surface);

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = builder
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat })
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	for (auto imageView : _swapchainImageViews) {
		vkDestroyImageView(_device, imageView, nullptr);
	}
}

void VulkanEngine::cleanup()
{
	if (_isInitialized) {
		vkDeviceWaitIdle(_device);

		for (auto& frame : _frames)
		{
			vkDestroySemaphore(_device, frame._swapchainSemaphore, nullptr);
			vkDestroySemaphore(_device, frame._renderSemaphore, nullptr);
			vkDestroyFence(_device, frame._renderFence, nullptr);
		}

		for (auto& frame : _frames)
		{
			vkDestroyCommandPool(_device, frame._commandPool, nullptr);
		}

		destroy_swapchain();
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}

void VulkanEngine::draw()
{
	auto& [commandPool, cmd, swapchainSemaphore, renderSemaphore, renderFence] = get_current_frame(); //???

	VK_CHECK(vkWaitForFences(_device, 1, &renderFence, VK_TRUE, WAIT_TIME_OUT));
	VK_CHECK(vkResetFences(_device, 1, &renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, WAIT_TIME_OUT, swapchainSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	float flash = std::abs(std::sin(_frameNumber / 120.0f));
	VkClearColorValue clearColor = { {0.0f, 1 - flash, flash, 1.0f} };
	VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &clearRange);
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, renderSemaphore);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, renderFence));

	VkPresentInfoKHR presentInfo{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		nullptr,
		1,
		&renderSemaphore,
		1,
		& _swapchain,
		&swapchainImageIndex,
		nullptr
	};

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	++_frameNumber;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT)
				bQuit = true;

			if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					stop_rendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					stop_rendering = false;
				}
			}
		}

		// do not draw if we are minimized
		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		draw();
	}
}
