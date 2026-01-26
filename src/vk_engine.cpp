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

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "vk_pipelines.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

constexpr bool bUseValidationLayers = true;
constexpr uint32_t WAIT_TIME_OUT = 1000000000; // 1 sec

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::Init()
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

	InitVulkan();
	InitSwapchain();
	InitCommands();
	InitSyncStructures();
	InitDescriptors();
	InitPipelines();
	InitImGui();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::InitVulkan()
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


	// Create Vulkan surface with SDL -> Let's vulkan interface with the windowing system
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	// vulkan 1.3
	VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	//vulkan 1.2
	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;

	// Select physical device with features: dynamicRendering, synchronization2, bufferDeviceAddress, descriptorIndexing
	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

	// Create the logical device from the physical device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;

	// queue for graphics commands
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	// index of the queue family for graphics commands -> checks hardware support for given queue type
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// Create VMA allocator
	VmaAllocatorCreateInfo allocatorCreateInfo{};
	allocatorCreateInfo.physicalDevice = _physicalDevice;
	allocatorCreateInfo.device = _device;
	allocatorCreateInfo.instance = _instance;
	allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // allows gpu pointers
	vmaCreateAllocator(&allocatorCreateInfo, &_allocator);

	_mainDeletionQueue.PushFunction([&]()
		{
			vmaDestroyAllocator(_allocator);
		});
}

void VulkanEngine::InitSwapchain()
{
	CreateSwapchain(_windowExtent.width, _windowExtent.height);
}

void VulkanEngine::InitCommands()
{

	// Create command pool for command buffers => the command pools is specific to the queue family
	// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT: lets us individually reset command buffers
	VkCommandPoolCreateInfo cmdPoolCreateInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (auto& frame : _frames)
	{
		VK_CHECK(vkCreateCommandPool(_device, &cmdPoolCreateInfo, nullptr, &frame._commandPool));

		// Creates one command buffer in the command pool
		VkCommandBufferAllocateInfo cmdBufferAllocateInfo = vkinit::command_buffer_allocate_info(frame._commandPool, 1U);
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdBufferAllocateInfo, &frame._mainCommandBuffer));
	}

	// Create command pool for immediate submit
	VK_CHECK(vkCreateCommandPool(_device, &cmdPoolCreateInfo, nullptr, &_immCommandPool));
	{
		VkCommandBufferAllocateInfo cmdBufferAllocateInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1U);
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdBufferAllocateInfo, &_immCommandBuffer));
	}

	_mainDeletionQueue.PushFunction([&]()
		{
			vkDestroyCommandPool(_device, _immCommandPool, nullptr);
		});

}

void VulkanEngine::InitSyncStructures()
{
	// Fences: used to synchronize the CPU and GPU
	// Semaphores: used to synchronize operations within the GPU ->
	// signaling: get informed when an operation is complete
	// waiting: signals another operation to start

	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (auto& frame : _frames)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &frame._renderFence));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &frame._swapchainSemaphore));
	}

	// one semaphore per swapchain image
	_renderFinishedSemaphores.resize(_swapchainImages.size());
	for (auto& renderFinished : _renderFinishedSemaphores)
	{
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &renderFinished));
	}

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.PushFunction([&]()
		{
			vkDestroyFence(_device, _immFence, nullptr);
		});
}


// Swapchain:
// Decouples the rendering from the windowing system
// -> allows us to render images in advance before they are presented on the screen => double buffering, triple buffering
// this helps to avoid stuttering and tearing effects
void VulkanEngine::CreateSwapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder builder(_physicalDevice, _device, _surface);

	// image format for the swapchain images
	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	// VK_IMAGE_USAGE_TRANSFER_DST_BIT: 
	vkb::Swapchain vkbSwapchain = builder
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat })
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT) // allows vkCmdBlitImage or copying to the swapchain image
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // hard vsync
		.set_desired_extent(width, height)
		.build()
		.value();

	// the width and height of the swapchain images
	_swapchainExtent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	// images and imageview in the swapchain
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	// Create image for drawing -> later we copy the data to the swapchain image
	// Size of the draw image -> matches the window extent
	VkExtent3D drawImageExtent{
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;		// copy from the draw image
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;		// copy to the draw image
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;			// compute shader write
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;	// draw geometry to the image

	VkImageCreateInfo drawImageCreateInfo = vkinit::image_create_info(
		_drawImage.imageFormat,
		drawImageUsages,
		drawImageExtent
	);

	// Allocation info for image
	VmaAllocationCreateInfo drawImageAllocCreateInfo{};
	drawImageAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // image can only be used by gpu -> lives in video memory
	drawImageAllocCreateInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // memory is device local

	vmaCreateImage(_allocator, &drawImageCreateInfo, &drawImageAllocCreateInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	// image view as handel the image
	VkImageViewCreateInfo drawImageViewCreateInfo = vkinit::imageview_create_info(
		_drawImage.imageFormat,
		_drawImage.image,
		VK_IMAGE_ASPECT_COLOR_BIT
	);

	// Image view to access the draw image 
	VK_CHECK(vkCreateImageView(_device, &drawImageViewCreateInfo, nullptr, &_drawImage.imageView));

	_mainDeletionQueue.PushFunction([&]()
		{
			vkDestroyImageView(_device, _drawImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

		});
}

void VulkanEngine::DrawBackground(VkCommandBuffer cmd)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptor, 0, nullptr);

	ComputePushConstance pushConst
	{
		glm::vec4(1, 0,0,1), // red
		glm::vec4(0, 1,0,1) // green
	};

	vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstance), &pushConst); // send the push constance to the shader

	vkCmdDispatch(cmd, std::ceil(_drawExtend.width / 32.0), std::ceil(_drawExtend.height / 32.0), 1);
}

void VulkanEngine::InitDescriptors()
{
	// create a descriptor pool that can hold up to 10 sets with 1 image each
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
	{
		{  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f  } // 100% of possible descriptors sets will storage images
	};

	// Initialize the global descriptor pool
	_globalDescriptorAllocator.InitPool(_device, 10, sizes);

	// Creates layout for a descriptor set for a storage image at binding 0 in the shader 
	DescriptorLayoutBuilder builder;
	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	_drawImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_COMPUTE_BIT); // used in compute shader



	_drawImageDescriptor = _globalDescriptorAllocator.Allocate(_device, _drawImageDescriptorLayout); // Allocate descriptor set from the global pool 

	
	// makes the descriptor point to the draw image
	VkDescriptorImageInfo drawImageInfo = {};
	drawImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // image layout for storage image
	drawImageInfo.imageView = _drawImage.imageView;

	// secifies the descriptor sets write operations
	VkWriteDescriptorSet drawImageWrite{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	drawImageWrite.pNext = nullptr;
	drawImageWrite.dstSet = _drawImageDescriptor;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &drawImageInfo;

	vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

	_mainDeletionQueue.PushFunction([&]()
		{
			_globalDescriptorAllocator.DestroyPool(_device);

			vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
		});
}

void VulkanEngine::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderingInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
}

void VulkanEngine::InitPipelines()
{
	InitBackgroundPipelines();
}

void VulkanEngine::InitBackgroundPipelines()
{
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.pSetLayouts = &_drawImageDescriptorLayout; // lets the pipeline know about the descriptor set layout
	pipelineLayoutCreateInfo.setLayoutCount = 1;

	// Push constant range for compute shader
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(ComputePushConstance);
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	// add push constant range to the pipeline layout
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutCreateInfo, nullptr, &_gradientPipelineLayout));

	//VkShaderModule computeDrawShader;
	//if (!vkutil::LoadShaderModuleSPV("../../shaders/gradient.comp.spv", _device, &computeDrawShader))
	//{
	//	fmt::print("Failed to load gradient compute shader.\n");
	//}

	//VkShaderModule computeDrawShader;
	//if (!vkutil::LoadShaderModuleSlang("gradient", "../../shaders/gradient.slang", _device, &computeDrawShader))
	//{
	//	fmt::print("Failed to load salang gradient compute shader.\n");
	//}

	//VkShaderModule computeDrawShader;
	//if (!vkutil::LoadShaderModuleSlang("triangle", "../../shaders/triangle.slang", _device, &computeDrawShader))
	//{
	//	fmt::print("Failed to load salang gradient compute shader.\n");
	//}

	VkShaderModule computeDrawShader;
	if (!vkutil::LoadShaderModuleSlang("gradient_color_compute", "../../shaders/gradient_color_compute.slang", _device, &computeDrawShader))
	{
		fmt::print("Failed to load salang gradient compute shader.\n");
	}

	// Info for the compute shader stage
	VkPipelineShaderStageCreateInfo computeShaderStageInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	computeShaderStageInfo.pNext = nullptr;
	computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computeShaderStageInfo.module = computeDrawShader;
	computeShaderStageInfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _gradientPipelineLayout;
	computePipelineCreateInfo.stage = computeShaderStageInfo;

	// Create the compute pipeline
	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_gradientPipeline));

	vkDestroyShaderModule(_device, computeDrawShader, nullptr);

	_mainDeletionQueue.PushFunction([&]() {
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _gradientPipeline, nullptr);
		});

}

void VulkanEngine::InitImGui()
{
	// descriptor pool for imgui
	VkDescriptorPoolSize poolSize[] =
		{ { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolInfo.pNext = nullptr;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // descriptor sets can return allocation to the pool individually
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSize));
	poolInfo.pPoolSizes = poolSize;
	
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_imguiDescriptorPool));

	ImGui::CreateContext();
	// SDL init
	ImGui_ImplSDL2_InitForVulkan(_window);
	// vulkan init
	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = _instance;
	initInfo.PhysicalDevice = _physicalDevice;
	initInfo.Device = _device;
	initInfo.Queue = _graphicsQueue;
	initInfo.DescriptorPool = _imguiDescriptorPool;
	initInfo.MinImageCount = 3;
	initInfo.ImageCount = 3;
	initInfo.UseDynamicRendering = true;

	// dynamic rendering parameters for imgui to use
	initInfo.PipelineRenderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.pNext = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &_swapchainImageFormat,
	};

	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);
	ImGui_ImplVulkan_CreateFontsTexture();

	_mainDeletionQueue.PushFunction([&]()
		{
			ImGui_ImplVulkan_Shutdown();
			vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
		});
}


void VulkanEngine::Cleanup()
{
	if (_isInitialized) {
		vkDeviceWaitIdle(_device);

		for (auto& renderFinished : _renderFinishedSemaphores)
		{
			vkDestroySemaphore(_device, renderFinished, nullptr);
		}


		for (auto& frame : _frames)
		{
			vkDestroySemaphore(_device, frame._swapchainSemaphore, nullptr);
			vkDestroyFence(_device, frame._renderFence, nullptr);
			vkDestroyCommandPool(_device, frame._commandPool, nullptr);

			frame._deletionQueue.Flush();
		}

		_mainDeletionQueue.Flush();

		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		for (auto imageView : _swapchainImageViews) {
			vkDestroyImageView(_device, imageView, nullptr);
		}

		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}

void VulkanEngine::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(_immCommandBuffer, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));
	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdInfo, nullptr, nullptr);
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, _immFence));
	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, VK_TRUE, 9999999999));
}

void VulkanEngine::Draw()
{
	auto& [commandPool, cmd, swapchainSemaphore, renderFence, deletionQueue] = get_current_frame();

	// wait until the GPU has finished rendering the last frame than reset the fence to unsignaled state
	VK_CHECK(vkWaitForFences(_device, 1, &renderFence, VK_TRUE, WAIT_TIME_OUT));
	deletionQueue.Flush();
	VK_CHECK(vkResetFences(_device, 1, &renderFence));

	// acquire the next image from the swapchain
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, WAIT_TIME_OUT, swapchainSemaphore, nullptr, &swapchainImageIndex));

	// reset command buffer to begin recording again
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: the command buffer only is submitted once -> allows driver optimizations
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	_drawExtend.width = _drawImage.imageExtent.width;
	_drawExtend.height = _drawImage.imageExtent.height;

	// begin command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	DrawBackground(cmd);

	// transition the draw image and the swapchain image into their correct transfer layouts-
	vkutil::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// copy from the draw image to the swapchain image
	vkutil::CopyImageToImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtend, _swapchainExtent);

	// transition the swapchain image to attachment optimal for ImGui rendering
	vkutil::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	// render imgui on top of the swapchain image
	DrawImGui(cmd, _swapchainImageViews[swapchainImageIndex]);

	// transition the swapchain image to present layout
	vkutil::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	// cmd only proceeds after swapchain finishes acquiring the image at stage VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, swapchainSemaphore);
	// _renderFinishedSemaphores signals when VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT and therefor cmd has finished executing
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, _renderFinishedSemaphores[swapchainImageIndex]);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, renderFence));

	// present the image to the swapchain to be displayed
	VkPresentInfoKHR presentInfo{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		nullptr,
		1,
		&_renderFinishedSemaphores[swapchainImageIndex],
		1,
		&_swapchain,
		&swapchainImageIndex,
		nullptr
	};

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	++_frameNumber;
}

void VulkanEngine::Run()
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

			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		// do not draw if we are minimized
		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::ShowDemoWindow();
		ImGui::Render();

		Draw();
	}
}
