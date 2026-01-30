#pragma once
#include <iostream>
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#include <slang/slang-com-helper.h>

namespace vkutil
{
	class SlangShaderLoader
	{
	public:
		static SlangShaderLoader& Get()
		{
			static SlangShaderLoader instance;
			return instance;
		}
		virtual ~SlangShaderLoader();

		bool LoadShader(const char* shaderName, const char* shaderPath, const char* shaderSource, std::vector<uint32_t>& data) const;


		SlangShaderLoader(const SlangShaderLoader& other) = delete;
		SlangShaderLoader(SlangShaderLoader&& other) noexcept = delete;
		SlangShaderLoader& operator=(const SlangShaderLoader& other) = delete;
		SlangShaderLoader& operator=(SlangShaderLoader&& other) noexcept = delete;

	private:
		explicit SlangShaderLoader();
		Slang::ComPtr<slang::IGlobalSession> _globalSession;
		Slang::ComPtr<slang::ISession> _session;

		static void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob);
	};
}

