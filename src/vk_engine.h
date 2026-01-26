// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <ranges>
#include "DeletionQueue.h"

#include "vk_images.h"
#include "vk_descriptors.h"

// Frane data structure: holds frame specific command pools, command buffers, semaphores and fences
struct FrameData
{
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	VkSemaphore _swapchainSemaphore;
	VkFence _renderFence;
	DeletionQueue _deletionQueue;
};

struct ComputePushConstance
{
	glm::vec4 data0;
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
};


constexpr uint32_t FRAME_OVERLAP = 2;

class VulkanEngine {
public:
	static VulkanEngine& Get();
	void Init();
	void Run();
	void Cleanup();

	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	bool _isInitialized = false;
	int _frameNumber = 0;
	bool stop_rendering = false;
	VkExtent2D _windowExtent { 1700 , 900 };

	struct SDL_Window* _window = nullptr;

	DeletionQueue _mainDeletionQueue;

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkSurfaceKHR _surface;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }
	std::vector<VkSemaphore> _renderFinishedSemaphores;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	vkutil::AllocatedImage _drawImage;
	VkExtent2D _drawExtend;

	VmaAllocator _allocator;

	DescriptorAllocator _globalDescriptorAllocator;
	
	VkDescriptorSet _drawImageDescriptor;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;
	VkDescriptorPool _imguiDescriptorPool;




	void Draw();

	void InitVulkan();
	void InitSwapchain();
	void InitCommands();
	void InitSyncStructures();

	void CreateSwapchain(uint32_t width, uint32_t height);

	void DrawBackground(VkCommandBuffer cmd);

	void InitDescriptors();

	void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

	void InitPipelines();
	void InitBackgroundPipelines();
	void InitImGui();

};
