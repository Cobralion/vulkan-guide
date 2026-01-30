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
#include <glm/detail/func_packing.inl>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "vk_pipelines.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

constexpr VmaAllocationCreateFlags __VMA_ALLOCATION_STAGING = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

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

	SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN);

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
	InitDefaultData();

	// everything went fine
	_isInitialized = true;
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

		if (ImGui::Begin("Background Settings"))
		{
			ComputeEffect& selected = _backgroundEffect[_currentBackgroundEffect];

			ImGui::Text("Selected Effect: ", selected.name);
			ImGui::SliderInt("Effect Index", &_currentBackgroundEffect, 0, static_cast<int>(_backgroundEffect.size()) - 1);

			ImGui::InputFloat4("Data0", reinterpret_cast<float*>(&selected.data.data0));
			ImGui::InputFloat4("Data1", reinterpret_cast<float*>(&selected.data.data1));
			ImGui::InputFloat4("Data2", reinterpret_cast<float*>(&selected.data.data2));
			ImGui::InputFloat4("Data3", reinterpret_cast<float*>(&selected.data.data3));
		}
		ImGui::End();


		ImGui::Render();

		Draw();
	}
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

		for (auto& mesh : _testMeshes)
		{
			DestroyBuffer(mesh->meshBuffers.vertexBuffer);
			DestroyBuffer(mesh->meshBuffers.indexBuffer);
		}

		_mainDeletionQueue.Flush();

		DestroySwapchain();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}


void VulkanEngine::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) // should be on a background thread
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

GPUMeshBuffers VulkanEngine::UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface{};

	// create gpu local buffer for vertices
	newSurface.vertexBuffer = CreateBuffer(
		vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* Storage buffers (SSBO), generic read/write */ | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT /* access form shader via address */ | VK_BUFFER_USAGE_TRANSFER_DST_BIT /* for memcopy */,
		VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
	);

	// get the device address for the vertex buffer -> access in shader via pointer
	VkBufferDeviceAddressInfo vertexBufferAddressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &vertexBufferAddressInfo);

	// create gpu local buffer for indices
	newSurface.indexBuffer = CreateBuffer(
		indexBufferSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT /* for memcopy */,
		VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
	);

	// write to temp stage buffer -> copy to gpu local buffer
	// create staging buffer, rw cpu only
	AllocatedBuffer staging = CreateBuffer(
		vertexBufferSize + indexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT /* copy src */,
		__VMA_ALLOCATION_STAGING
	);

	void* data = staging.allocation->GetMappedData(); // get cpu pointer to the mapped memory

	// copy vertex and index data to the staging buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy(static_cast<uint8_t*>(data) + vertexBufferSize, indices.data(), indexBufferSize);

	ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			VkBufferCopy vertexCopy{ 0, 0, vertexBufferSize };
			vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

			VkBufferCopy indexCopy{ vertexBufferSize, 0, indexBufferSize };
			vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
		});

	DestroyBuffer(staging);

	return newSurface;
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

	//vulkan 1.2
	VkPhysicalDeviceVulkan11Features features11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features11.shaderDrawParameters = VK_TRUE;


	// Select physical device with features: dynamicRendering, synchronization2, bufferDeviceAddress, descriptorIndexing
	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_required_features_11(features11)
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
	_engineSwapchain = std::make_unique<Engine::Swapchain>(_device, _physicalDevice, _surface);
	_engineSwapchain->CreateSwapchain(_windowExtent.width, _windowExtent.height, VK_FORMAT_B8G8R8A8_UNORM);

	CreateDrawImage(_windowExtent.width, _windowExtent.height);
	CreateDepthImage(_windowExtent.width, _windowExtent.height);
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
	_renderFinishedSemaphores.resize(_engineSwapchain->GetImageCount());
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


void VulkanEngine::InitDescriptors()
{
	// create a descriptor pool that can hold up to 10 sets with 1 image each
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
	{
		{  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f  } // 100% of possible descriptors sets will storage images
	};

	// Initialize the global descriptor pool
	_globalDescriptorAllocator.Init(_device, 10, sizes);

	// Creates layout for a descriptor set for a storage image at binding 0 in the shader 
	DescriptorLayoutBuilder builder;
	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	_drawImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_COMPUTE_BIT); // used in compute shader
	_drawImageDescriptor = _globalDescriptorAllocator.Allocate(_device, _drawImageDescriptorLayout); // Allocate descriptor set from the global pool 

	// Creates layout for a descriptor set for a uniform buffer at binding 0 in the shader 
	DescriptorLayoutBuilder sceenBuilder;
	sceenBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	_sceneDescriptorSetLayout = sceenBuilder.Build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT); // used in vertex and fragment shader
	
	// Creates layout for single Image descriptor set for texture sampling at binding 0 in the shader
	DescriptorLayoutBuilder singleImageBuilder;
	singleImageBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	_singleImageDescriptorLayout = singleImageBuilder.Build(_device, VK_SHADER_STAGE_FRAGMENT_BIT); // used in vertex and fragment shader

	DescriptorWriter writer;
	// makes the descriptor point to the draw image
	writer.WriteImage(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	// specifies the descriptor sets write operations
	writer.UpdateSet(_device, _drawImageDescriptor);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frameSize = {
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
		};

		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
		_frames[i]._frameDescriptors.Init(_device, 1000, frameSize);

		_mainDeletionQueue.PushFunction([&, i]()
			{
				_frames[i]._frameDescriptors.DestroyPools(_device);
			});
	}

	_mainDeletionQueue.PushFunction([&]()
		{
			_globalDescriptorAllocator.DestroyPools(_device);

			vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
			vkDestroyDescriptorSetLayout(_device, _sceneDescriptorSetLayout, nullptr);
			vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
		});
}

void VulkanEngine::InitPipelines()
{
	InitBackgroundPipelines();
	InitMeshPipeline();
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

	VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
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

	VkFormat imguiSurfaceFormat = _engineSwapchain->GetImageFormat();
	// dynamic rendering parameters for imgui to use
	initInfo.PipelineRenderingCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.pNext = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &imguiSurfaceFormat,
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




void VulkanEngine::InitBackgroundPipelines()
{
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.pSetLayouts = &_drawImageDescriptorLayout; // lets the pipeline know about the descriptor set layout
	pipelineLayoutCreateInfo.setLayoutCount = 1;

	// Push constant range for compute shader
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(ComputePushConstants);
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	// add push constant range to the pipeline layout
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VkPipelineLayout pipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

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

	VkShaderModule gradientComputeDrawShader;
	if (!vkutil::LoadShaderModuleSlang("gradient_color_compute", "../../shaders/gradient_color_compute.slang", _device, &gradientComputeDrawShader))
	{
		fmt::print("Failed to load salang gradient compute shader.\n");
	}

	VkShaderModule skyComputeDrawShader;
	if (!vkutil::LoadShaderModuleSlang("sky_compute", "../../shaders/sky_compute.slang", _device, &skyComputeDrawShader))
	{
		fmt::print("Failed to load salang gradient compute shader.\n");
	}

	// Info for the compute shader stage
	VkPipelineShaderStageCreateInfo computeShaderStageInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	computeShaderStageInfo.pNext = nullptr;
	computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computeShaderStageInfo.module = gradientComputeDrawShader;
	computeShaderStageInfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = pipelineLayout;
	computePipelineCreateInfo.stage = computeShaderStageInfo;

	ComputeEffect gradientEffect{ "gradient", VK_NULL_HANDLE, pipelineLayout,
		{
		glm::vec4(0, 0,0,1),
		glm::vec4(0, 0,0,1)
		}
	};

	// Create the compute pipeline
	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradientEffect.pipeline));

	computePipelineCreateInfo.stage.module = skyComputeDrawShader;
	ComputeEffect skyEffect{ "sky", VK_NULL_HANDLE, pipelineLayout,
	{glm::vec4(0.1, 0.2, 0.4 ,0.97)} };

	// Create the compute pipeline
	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &skyEffect.pipeline));

	_backgroundEffect.push_back(gradientEffect);
	_backgroundEffect.push_back(skyEffect);

	vkDestroyShaderModule(_device, gradientComputeDrawShader, nullptr);
	vkDestroyShaderModule(_device, skyComputeDrawShader, nullptr);

	_mainDeletionQueue.PushFunction([=]() {
		vkDestroyPipelineLayout(_device, pipelineLayout, nullptr);
		vkDestroyPipeline(_device, skyEffect.pipeline, nullptr);
		vkDestroyPipeline(_device, gradientEffect.pipeline, nullptr);
		});

}

void VulkanEngine::InitMeshPipeline()
{
	VkShaderModule vertexShader;
	if (!vkutil::LoadShaderModuleSPV("../../shaders/colored_triangle_mesh.vert.spv", _device, &vertexShader))
	{
		fmt::print("Failed to load vertex shader.\n");
	}
	else
	{
		fmt::print("Loaded vertex shader.\n");
	}

	VkShaderModule fragmentShader;
	if (!vkutil::LoadShaderModuleSPV("../../shaders/tex_image.frag.spv", _device, &fragmentShader))
	{
		fmt::print("Failed to load fragment shader.\n");
	}
	else
	{
		fmt::print("Loaded fragment shader.\n");
	}

	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vkinit::pipeline_layout_create_info();
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &_singleImageDescriptorLayout; // adds the image sampler layout
	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutCreateInfo, nullptr, &_meshPipelineLayout));

	vkutil::PipelineBuilder pipelineBuilder;

	pipelineBuilder.SetPipelineLayout(_meshPipelineLayout);
	pipelineBuilder.SetShaders(vertexShader, fragmentShader);
	pipelineBuilder.SetInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST); // default
	pipelineBuilder.SetPolygonMode(VK_POLYGON_MODE_FILL); // default
	pipelineBuilder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE); // no culling, default
	pipelineBuilder.SetMultisamplingNone();
	pipelineBuilder.EnableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL); // 0 far plane, 1 near plane -> only render pixel if depth value is greater or equal
	pipelineBuilder.EnableBlendingAdditive();
	pipelineBuilder.SetColorAttachmentFormat(_drawImage.imageFormat); // use the draw image format
	pipelineBuilder.SetDepthFormat(_depthImage.imageFormat);
	_meshPipeline = pipelineBuilder.BuildPipeline(_device);

	vkDestroyShaderModule(_device, vertexShader, nullptr);
	vkDestroyShaderModule(_device, fragmentShader, nullptr);

	_mainDeletionQueue.PushFunction([&]()
		{
			vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
			vkDestroyPipeline(_device, _meshPipeline, nullptr);
		});

}

void VulkanEngine::CreateDrawImage(uint32_t width, uint32_t height)
{
	// Create image for drawing -> later we copy the data to the swapchain image
	// Size of the draw image -> matches the window extent
	VkExtent3D drawImageExtent{
		width,
		height,
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

void VulkanEngine::CreateDepthImage(uint32_t width, uint32_t height)
{
	// Init depth image
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = { width, height, 1 };
	VkImageUsageFlags depthUsage{};
	depthUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	// Allocation info for depth image
	VmaAllocationCreateInfo depthImageAllocCreateInfo{};
	depthImageAllocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // image can only be used by gpu -> lives in video memory
	depthImageAllocCreateInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // memory is device local

	VkImageCreateInfo depthImageCreateInfo = vkinit::image_create_info(_depthImage.imageFormat, depthUsage, _depthImage.imageExtent);
	vmaCreateImage(_allocator, &depthImageCreateInfo, &depthImageAllocCreateInfo, &_depthImage.image, &_depthImage.allocation, nullptr);

	VkImageViewCreateInfo depthImageViewCreateInfo = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &depthImageViewCreateInfo, nullptr, &_depthImage.imageView));

	_mainDeletionQueue.PushFunction([=]()
		{
			vkDestroyImageView(_device, _depthImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
		});
}

AllocatedBuffer VulkanEngine::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags)
{
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = allocSize;
	bufferCreateInfo.usage = usage;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO; // will almost always be VMA_MEMORY_USAGE_AUTO
	allocCreateInfo.flags = allocationFlags;
	// GPU-only resource: VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
	// staging buffer: VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
	// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferCreateInfo, &allocCreateInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.allocationInfo));

	return newBuffer;
}

vkutil::AllocatedImage VulkanEngine::CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage,
	bool mipmapped)
{
	using vkutil::AllocatedImage;
	AllocatedImage newImage{
		.imageExtent = size,
		.imageFormat = format,
	};

	VkImageCreateInfo imageCreateInfo = vkinit::image_create_info(format, usage, size);
	if (mipmapped)
	{
		imageCreateInfo.mipLevels = static_cast<uint32_t>(std::floor( // handles floats
			std::log2( // each level reduces size by factor 2, log2(dimension) ~ levels
				std::max(size.width, size.height) // mipmap chains must continue until the largest dimension is 1
			))) + 1; // base level (level 0)
	}

	VmaAllocationCreateInfo imageAllocCreateInfo{};
	imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	imageAllocCreateInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	imageAllocCreateInfo.priority = 1.0f;

	VK_CHECK(vmaCreateImage(_allocator, &imageCreateInfo, &imageAllocCreateInfo, &newImage.image, &newImage.allocation, nullptr));

	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) // depth format
	{
		aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageViewCreateInfo imageViewCreateInfo = vkinit::imageview_create_info(format, newImage.image, aspectFlags);
	imageViewCreateInfo.subresourceRange.levelCount = imageCreateInfo.mipLevels; // view all mip levels if mipmapped
	VK_CHECK(vkCreateImageView(_device, &imageViewCreateInfo, nullptr, &newImage.imageView));

	return newImage;
}

vkutil::AllocatedImage VulkanEngine::CreateImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, // TODO: add size_t dataSize
	bool mipmapped)
{
	size_t bufferSize = size.width * size.height * 4; // assuming 4 bytes per pixel
	AllocatedBuffer stagingBuffer = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, __VMA_ALLOCATION_STAGING);

	void* mappedMemory = stagingBuffer.allocation->GetMappedData();

	memcpy_s(mappedMemory, bufferSize, data, bufferSize);

	vkutil::AllocatedImage newImage = CreateImage(size, format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipmapped);

	ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			vkutil::TransitionImage(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
#
			VkImageAspectFlags aspectFlags = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			VkImageSubresourceLayers subresourceLayers{ aspectFlags, 0, 0, 1 };

			VkBufferImageCopy copyRegion{};			// https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VkBufferImageCopy.html
			copyRegion.bufferOffset = 0;			// offset to start of image
			copyRegion.bufferRowLength = 0;			// tightly packed if either row or image height is 0
			copyRegion.bufferImageHeight = 0;		// tightly packed
			copyRegion.imageSubresource = subresourceLayers;
			copyRegion.imageExtent = size;

			vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			vkutil::TransitionImage(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	DestroyBuffer(stagingBuffer);

	return newImage;
}


void VulkanEngine::InitDefaultData() {
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	_whiteImage = CreateImage(&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT); // image can be used to create a VkImageView suitable for occupying a VkDescriptorSet slot either of type VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, and be sampled by a shader

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	_greyImage = CreateImage(&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
	_blackImage = CreateImage(&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}
	_errorImage = CreateImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);


	VkSamplerCreateInfo samplerNearestInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
	samplerNearestInfo.magFilter = VK_FILTER_NEAREST;
	samplerNearestInfo.minFilter = VK_FILTER_NEAREST;
	vkCreateSampler(_device, &samplerNearestInfo, nullptr, &_defaultSamplerNearest);

	VkSamplerCreateInfo samplerLinearInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
	samplerLinearInfo.magFilter = VK_FILTER_NEAREST;
	samplerLinearInfo.minFilter = VK_FILTER_NEAREST;
	vkCreateSampler(_device, &samplerLinearInfo, nullptr, &_defaultSamplerLinear);

	_mainDeletionQueue.PushFunction([&]()
		{
			vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
			vkDestroySampler(_device, _defaultSamplerNearest, nullptr);

			DestroyImage(_errorImage);
			DestroyImage(_greyImage);
			DestroyImage(_blackImage);
			DestroyImage(_whiteImage);
		});


	_testMeshes = LoadGltfMeshes(this, "..\\..\\assets\\basicmesh.glb").value();
}

void VulkanEngine::Draw()
{

	auto& [swapchainSemaphore, renderFence, commandPool, cmd, deletionQueue, frameDescriptors] = get_current_frame();

	// wait until the GPU has finished rendering the last frame than reset the fence to unsignaled state
	VK_CHECK(vkWaitForFences(_device, 1, &renderFence, VK_TRUE, WAIT_TIME_OUT));
	deletionQueue.Flush();
	frameDescriptors.ClearPools(_device);
	VK_CHECK(vkResetFences(_device, 1, &renderFence));

	// acquire the next image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult acquireResult = vkAcquireNextImageKHR(_device, _engineSwapchain->GetSwapchain(), WAIT_TIME_OUT, swapchainSemaphore, nullptr, &swapchainImageIndex);
	VK_CHECK(acquireResult);

	// reset command buffer to begin recording again
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: the command buffer only is submitted once -> allows driver optimizations
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);


	_drawExtent.height = _drawImage.imageExtent.height;
	_drawExtent.width = _drawImage.imageExtent.width;

	// begin command buffer recording
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	DrawBackground(cmd);

	vkutil::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
	vkutil::TransitionDepthImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL); // transition depth image

	DrawGeometry(cmd);

	// transition the draw image and the swapchain image into their correct transfer layouts-
	vkutil::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::TransitionImage(cmd, _engineSwapchain->GetImage(swapchainImageIndex), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// copy from the draw image to the swapchain image
	vkutil::CopyImageToImage(cmd, _drawImage.image, _engineSwapchain->GetImage(swapchainImageIndex), _drawExtent, _engineSwapchain->GetExtent());

	// transition the swapchain image to attachment optimal for ImGui rendering
	vkutil::TransitionImage(cmd, _engineSwapchain->GetImage(swapchainImageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

	// render imgui on top of the swapchain image
	DrawImGui(cmd, _engineSwapchain->GetImageView(swapchainImageIndex));

	// transition the swapchain image to present layout
	vkutil::TransitionImage(cmd, _engineSwapchain->GetImage(swapchainImageIndex), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	// cmd only proceeds after swapchain finishes acquiring the image at stage VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, swapchainSemaphore);
	// _renderFinishedSemaphores signals when VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT and therefor cmd has finished executing
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, _renderFinishedSemaphores[swapchainImageIndex]);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, renderFence));

	VkSwapchainKHR swapchains[] = { _engineSwapchain->GetSwapchain() };
	// present the image to the swapchain to be displayed
	VkPresentInfoKHR presentInfo{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		nullptr,
		1,
		&_renderFinishedSemaphores[swapchainImageIndex],
		1,
		swapchains,
		&swapchainImageIndex,
		nullptr
	};

	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	VK_CHECK(presentResult);
	++_frameNumber;
}

void VulkanEngine::DrawBackground(VkCommandBuffer cmd)
{
	ComputeEffect& effect = _backgroundEffect[_currentBackgroundEffect];

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.layout, 0, 1, &_drawImageDescriptor, 0, nullptr);

	vkCmdPushConstants(cmd, effect.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data); // send the push constance to the shader

	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 32.0), std::ceil(_drawExtent.height / 32.0), 1);
}

void VulkanEngine::DrawGeometry(VkCommandBuffer cmd)
{
	// Allocate descriptor set from the global pool
	AllocatedBuffer sceneBuffer = CreateBuffer(
		sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
	);
	// Schedule the buffer for deletion at the end of the current frame
	get_current_frame()._deletionQueue.PushFunction([=, this]()
		{
			DestroyBuffer(sceneBuffer);
		});

	// copy scene data to the buffer
	GPUSceneData* sceneUniformData = static_cast<GPUSceneData*>(sceneBuffer.allocation->GetMappedData());
	*sceneUniformData = _sceneData;
	// create descriptor set for the scene data
	VkDescriptorSet globalDescriptorSet = get_current_frame()._frameDescriptors.Allocate(_device, _sceneDescriptorSetLayout);

	// write the scene buffer info to the descriptor set
	DescriptorWriter sceneWriter;
	sceneWriter.WriteBuffer(0, sceneBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	sceneWriter.UpdateSet(_device, globalDescriptorSet);

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL); // color attachment for the renderer
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL); // depth attachment for the renderer
	VkRenderingInfo renderingInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderingInfo); // start rendering

	// transformation from the image to the framebuffer -> what part of the window is rendered to
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(_drawExtent.width);
	viewport.height = static_cast<float>(_drawExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	// which part of the image is stored -> cropping
	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { _drawExtent.width, _drawExtent.height };

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);
	vkCmdSetViewport(cmd, 0, 1, &viewport); // set viewport
	vkCmdSetScissor(cmd, 0, 1, &scissor); // set scissor

	VkDescriptorSet imageSet = get_current_frame()._frameDescriptors.Allocate(_device, _singleImageDescriptorLayout);
	DescriptorWriter imageWriter;
	imageWriter.WriteImage(0, _errorImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	imageWriter.UpdateSet(_device, imageSet);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);

	glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3{ 0,0,-5 });
	glm::mat4 proj = glm::perspective(glm::radians(70.0f), static_cast<float>(_drawExtent.width) / static_cast<float>(_drawExtent.height), 10000.0f, 0.1f); //??
	proj[1][1] *= -1; // flip y for vulkan

	GPUDrawPushConstants testPushConstants
	{
		proj * view,
		_testMeshes[2]->meshBuffers.vertexBufferAddress
	};

	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &testPushConstants);
	vkCmdBindIndexBuffer(cmd, _testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _testMeshes[2]->surfaces[0].count, 1, _testMeshes[2]->surfaces[0].startIndex, 0, 0);
	vkCmdEndRendering(cmd);
}

void VulkanEngine::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderingInfo = vkinit::rendering_info(_engineSwapchain->GetExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
}


void VulkanEngine::DestroySwapchain()
{
	_engineSwapchain->DestroySwapchain();
}

void VulkanEngine::DestroyBuffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void VulkanEngine::DestroyImage(const vkutil::AllocatedImage& image)
{
	vkDestroyImageView(_device, image.imageView, nullptr);
	vmaDestroyImage(_allocator, image.image, image.allocation);
}
