#include <vk_pipelines.h>
#include <fstream>
#include <vk_initializers.h>

#include "../bin/src/SlangShaderLoader.h"

bool vkutil::LoadShaderModuleSPV(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	// std::ios::ate: file pointer at the end of file
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	// pos of cursor in the stream -> size of the file
	size_t fileSize = (size_t)file.tellg();
	// allocate a buffer to hold the code
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	// cursor at beginning
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
	file.close();

	// Create shader module
	VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.pNext = nullptr;
	createInfo.codeSize = buffer.size() * sizeof(uint32_t); // size in bytes 
	createInfo.pCode = buffer.data(); // vulkan expects buffer as an uint32_t array

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}

bool vkutil::LoadShaderModuleSlang(const char* name, const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	// std::ios::ate: file pointer at the end of file
	std::ifstream file(filePath, std::ios::ate);

	if (!file.is_open())
	{
		return false;
	}

	// pos of cursor in the stream -> size of the file
	size_t fileSize = file.tellg();
	// allocate a buffer to hold the code
	std::vector<char> buffer(fileSize);
	// cursor at beginning
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	std::vector<uint32_t> dataBuffer;
	if (!SlangShaderLoader::Get().LoadShader(name, filePath, buffer.data(), dataBuffer))
	{
		fmt::print("Slang Shader failed to compile.\n");
		return false;
	}

	VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.pNext = nullptr;
	createInfo.codeSize = dataBuffer.size() * sizeof(uint32_t); // size in bytes
	createInfo.pCode = dataBuffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}

void vkutil::PipelineBuilder::Clear()
{
	_inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

	_rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

	_colorBlendAttachment = {};

	_multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

	_pipelineLayout = {};

	_depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	_renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

	_shaderStages.clear();
}

// https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Fixed_functions
VkPipeline vkutil::PipelineBuilder::BuildPipeline(VkDevice device)
{
	// dynamic viewport
	VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.pNext = nullptr;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// color blending - no blending, just write to the color attachment
	VkPipelineColorBlendStateCreateInfo colorBlending{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlending.pNext = nullptr;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	// not used
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInputInfo.pNext = nullptr;

	VkGraphicsPipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = nullptr;
	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size()); // num of shader stages
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo; // we do not use the vulkan vertex input
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState; // we use a dynamic viewport
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.pNext = &_renderInfo;

	// alow dynamic states of viewport and scissor
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateCreateInfo.pNext = nullptr;
	dynamicStateCreateInfo.dynamicStateCount = 2;
	dynamicStateCreateInfo.pDynamicStates = dynamicStates;

	pipelineInfo.pDynamicState = &dynamicStateCreateInfo;

	VkPipeline pipeline;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		fmt::println("Failed to create graphics pipeline.");
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

void vkutil::PipelineBuilder::SetPipelineLayout(VkPipelineLayout pipelineLayout)
{
	_pipelineLayout = pipelineLayout;
}

void vkutil::PipelineBuilder::SetShaders(VkShaderModule vertex, VkShaderModule fragment)
{
	_shaderStages.clear();
	_shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertex));
	_shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragment));
}

void vkutil::PipelineBuilder::SetInputTopology(VkPrimitiveTopology topology)
{
	_inputAssembly.topology = topology; // should vertex be transformed to points, lines or triangle
	_inputAssembly.primitiveRestartEnable = VK_FALSE; // for optimization like reusing vertices
}

void vkutil::PipelineBuilder::SetPolygonMode(VkPolygonMode polygonMode)
{
	_rasterizer.polygonMode = polygonMode; // points, lines, fill
	_rasterizer.lineWidth = 1.0f;
}

void vkutil::PipelineBuilder::SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	_rasterizer.cullMode = cullMode; // Back-face culling: determines whether a polygon that is part of a solid needs to be drawn
	_rasterizer.frontFace = frontFace; // clock or counter-clockwise
}

void vkutil::PipelineBuilder::SetMultisamplingNone()
{
	_multisampling.sampleShadingEnable = VK_FALSE;
	_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	_multisampling.minSampleShading = 1.0f;
	_multisampling.pSampleMask = nullptr;
	_multisampling.alphaToCoverageEnable = VK_FALSE;
	_multisampling.alphaToOneEnable = VK_FALSE;
}

void vkutil::PipelineBuilder::DisableBlending()
{
	_colorBlendAttachment.blendEnable = VK_FALSE;
	_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}

void vkutil::PipelineBuilder::SetColorAttachmentFormat(VkFormat format)
{
	_colorAttachmentFormat = format;
	_renderInfo.colorAttachmentCount = 1;
	_renderInfo.pColorAttachmentFormats = &_colorAttachmentFormat;
}

void vkutil::PipelineBuilder::SetDepthFormat(VkFormat format)
{
	_renderInfo.depthAttachmentFormat = format;
}

void vkutil::PipelineBuilder::DisableDepthtest()
{
	_depthStencil.depthTestEnable = VK_FALSE;
	_depthStencil.depthWriteEnable = VK_FALSE;
	_depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;
	_depthStencil.front = {};
	_depthStencil.back = {};
	_depthStencil.minDepthBounds = 0.f;
	_depthStencil.maxDepthBounds = 1.f;
}
