#pragma once 
#include <vk_types.h>

namespace vkutil {
    bool LoadShaderModuleSPV(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
	bool LoadShaderModuleSlang(const char* name, const char* filePath, VkDevice device, VkShaderModule* outShaderModule);

	class PipelineBuilder
	{
	public:
		std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
		VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
		VkPipelineRasterizationStateCreateInfo _rasterizer;
		VkPipelineColorBlendAttachmentState _colorBlendAttachment;
		VkPipelineMultisampleStateCreateInfo _multisampling;
		VkPipelineLayout _pipelineLayout;
		VkPipelineDepthStencilStateCreateInfo _depthStencil;
		VkPipelineRenderingCreateInfo _renderInfo;
		VkFormat _colorAttachmentFormat;

		PipelineBuilder() { Clear(); }
		void Clear();
		VkPipeline BuildPipeline(VkDevice device);

		void SetPipelineLayout(VkPipelineLayout pipelineLayout);
		void SetShaders(VkShaderModule vertex, VkShaderModule fragment);
		void SetInputTopology(VkPrimitiveTopology topology);
		void SetPolygonMode(VkPolygonMode polygonMode);
		void SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
		void SetMultisamplingNone();
		void DisableBlending();
		void EnableBlendingAdditive();
		void EnableBlendingAlphablend();
		void SetColorAttachmentFormat(VkFormat format);
		void SetDepthFormat(VkFormat format);
		void DisableDepthtest();
		void EnableDepthtest(bool depthWriteEnabled, VkCompareOp op);
	};
};