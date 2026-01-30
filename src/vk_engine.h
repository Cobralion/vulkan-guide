// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <ranges>
#include "DeletionQueue.h"
#include "Swapchain.h"

#include "vk_images.h"
#include "DescriptorWriter.h"
#include "DescriptorAllocatorGrowable.h"
#include "vk_loader.h"

// Frane data structure: holds frame specific command pools, command buffers, semaphores and fences
struct FrameData
{
	VkSemaphore _swapchainSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;
};

struct ComputePushConstants
{
	glm::vec4 data0;
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
};

struct ComputeEffect
{
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;
	ComputePushConstants data;
};

constexpr uint32_t FRAME_OVERLAP = 2;

class VulkanEngine {
public:
	static VulkanEngine& Get();
	void Init();
	void Run();
	void Cleanup();

	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	GPUMeshBuffers UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

private:
	void InitVulkan();
	void InitSwapchain();
	void InitCommands();
	void InitSyncStructures();
	void InitDescriptors();
	void InitPipelines();
	void InitImGui();

	void InitBackgroundPipelines();
	void InitMeshPipeline();

	void CreateDrawImage(uint32_t width, uint32_t height);
	void CreateDepthImage(uint32_t width, uint32_t height);
	AllocatedBuffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

	void InitDefaultData();

	void Draw();
	void DrawBackground(VkCommandBuffer cmd);
	void DrawGeometry(VkCommandBuffer cmd);
	void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

	void DestroySwapchain();
	void DestroyBuffer(AllocatedBuffer buffer);


private:
	VkExtent2D _windowExtent{ 1700 , 900 };
	struct SDL_Window* _window = nullptr;

	bool _isInitialized = false;
	int _frameNumber = 0;
	bool stop_rendering = false;

	VmaAllocator _allocator;

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkSurfaceKHR _surface;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	std::unique_ptr<Engine::Swapchain> _engineSwapchain;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }
	std::vector<VkSemaphore> _renderFinishedSemaphores;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	DescriptorAllocatorGrowable _globalDescriptorAllocator;
	VkDescriptorSet _drawImageDescriptor;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	GPUSceneData _sceneData;
	VkDescriptorSetLayout _sceneDescriptorSetLayout;

	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;
	VkDescriptorPool _imguiDescriptorPool;

	vkutil::AllocatedImage _drawImage;
	vkutil::AllocatedImage _depthImage;
	VkExtent2D _drawExtent;

	int32_t _currentBackgroundEffect{ 0 };
	std::vector<ComputeEffect> _backgroundEffect;
	std::vector<std::shared_ptr<MeshAsset>> _testMeshes;

	DeletionQueue _mainDeletionQueue;
};
