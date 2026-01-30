#include "D:/Source/Repos/vulkan-guide/bin/src/CMakeFiles/engine.dir/Debug/cmake_pch.hxx"
#include "SlangShaderLoader.h"

#include <array>
#include <format>

#include "fmt/core.h"

vkutil::SlangShaderLoader::SlangShaderLoader()
{
	slang::createGlobalSession(_globalSession.writeRef());
	slang::TargetDesc targetDesc = {};
	targetDesc.format = SLANG_SPIRV;
	targetDesc.profile = _globalSession->findProfile("spirv_1_6");
	slang::SessionDesc desc = {};
	desc.targetCount = 1;
	desc.targets = &targetDesc;
	std::array<slang::CompilerOptionEntry, 1> options =
	{
		{
			slang::CompilerOptionName::EmitSpirvDirectly,
			{slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
		}
	};
	desc.compilerOptionEntries = options.data();
	desc.compilerOptionEntryCount = options.size();
	_globalSession->createSession(desc, _session.writeRef());
}

vkutil::SlangShaderLoader::~SlangShaderLoader() = default;

void vkutil::SlangShaderLoader::diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
	if (diagnosticsBlob != nullptr)
	{
		fmt::print("Slang Compiler Diagnostics:\n {}\n", static_cast<const char*>(diagnosticsBlob->getBufferPointer()));
	}
}

bool vkutil::SlangShaderLoader::LoadShader(const char* shaderName, const char* shaderPath, const char* shaderSource, std::vector<uint32_t>& data) const
{
	Slang::ComPtr<slang::IModule> slangModule;
	{
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		slangModule = _session->loadModuleFromSourceString(
			shaderName,
			shaderPath,
			shaderSource,
			diagnosticsBlob.writeRef());
		diagnoseIfNeeded(diagnosticsBlob);
		if (!slangModule) return false;
	}

	Slang::ComPtr<slang::IEntryPoint> entryPoint;
	{
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		slangModule->findEntryPointByName("main", entryPoint.writeRef());
		if (!entryPoint)
		{
			fmt::print("Slang Compiler Error: Error getting entry point.\n");
			return false;
		}
	}

	std::array<slang::IComponentType*, 2> componentTypes =
	{
		slangModule,
		entryPoint
	};

	Slang::ComPtr<slang::IComponentType> composedProgram;
	{
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		Slang::Result result = _session->createCompositeComponentType(componentTypes.data(), componentTypes.size(),
			composedProgram.writeRef(), diagnosticsBlob.writeRef());
		diagnoseIfNeeded(diagnosticsBlob);
		SLANG_RETURN_FALSE_ON_FAIL(result)
	}


	Slang::ComPtr<slang::IComponentType> linkedProgram;
	{
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		SlangResult result = composedProgram->link(
			linkedProgram.writeRef(),
			diagnosticsBlob.writeRef());
		diagnoseIfNeeded(diagnosticsBlob);
		SLANG_RETURN_FALSE_ON_FAIL(result)
	}

	Slang::ComPtr<slang::IBlob> spirvCode;
	{
		Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		SlangResult result = linkedProgram->getEntryPointCode(
			0,
			0,
			spirvCode.writeRef(),
			diagnosticsBlob.writeRef());
		diagnoseIfNeeded(diagnosticsBlob);
		SLANG_RETURN_FALSE_ON_FAIL(result)
	}

	size_t bufferSize = spirvCode->getBufferSize() / sizeof(uint32_t);
	data = std::vector<uint32_t>(bufferSize);
	memcpy_s(data.data(), data.size() * sizeof(uint32_t), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
	return true;
}
