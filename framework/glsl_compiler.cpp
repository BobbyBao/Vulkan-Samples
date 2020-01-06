/* Copyright (c) 2019, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "glsl_compiler.h"

VKBP_DISABLE_WARNINGS()
#include <SPIRV/GLSL.std.450.h>
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/ResourceLimits.h>
#include <glslang/Include/ShHandle.h>
#include <glslang/Include/revision.h>
#include <glslang/OSDependent/osinclude.h>
VKBP_ENABLE_WARNINGS()

namespace vkb
{
namespace
{
inline EShLanguage FindShaderLanguage(vk::ShaderStageFlagBits stage)
{
	switch (stage)
	{
		case vk::ShaderStageFlagBits::eVertex:
			return EShLangVertex;

		case vk::ShaderStageFlagBits::eTessellationControl:
			return EShLangTessControl;

		case vk::ShaderStageFlagBits::eTessellationEvaluation:
			return EShLangTessEvaluation;

		case vk::ShaderStageFlagBits::eGeometry:
			return EShLangGeometry;

		case vk::ShaderStageFlagBits::eFragment:
			return EShLangFragment;

		case vk::ShaderStageFlagBits::eCompute:
			return EShLangCompute;

		case vk::ShaderStageFlagBits::eRaygenNV:
			return EShLangRayGenNV;

		case vk::ShaderStageFlagBits::eMissNV:
			return EShLangMissNV;

		case vk::ShaderStageFlagBits::eClosestHitNV:
			return EShLangClosestHitNV;

		default:
			return EShLangVertex;
	}
}
}        // namespace

bool GLSLCompiler::compile_to_spirv(vk::ShaderStageFlagBits     stage,
                                    const std::vector<uint8_t> &glsl_source,
                                    const std::string &         entry_point,
                                    const ShaderVariant &       shader_variant,
                                    std::vector<std::uint32_t> &spirv,
                                    std::string &               info_log)
{
	// Initialize glslang library.
	glslang::InitializeProcess();

	EShMessages messages = static_cast<EShMessages>(EShMsgDefault | EShMsgVulkanRules | EShMsgSpvRules);

	EShLanguage language = FindShaderLanguage(stage);
	std::string source   = std::string(glsl_source.begin(), glsl_source.end());

	const char *file_name_list[1] = {""};
	const char *shader_source     = reinterpret_cast<const char *>(source.data());

	glslang::TShader shader(language);
	shader.setStringsWithLengthsAndNames(&shader_source, nullptr, file_name_list, 1);
	shader.setEntryPoint(entry_point.c_str());
	shader.setSourceEntryPoint(entry_point.c_str());
	shader.setPreamble(shader_variant.get_preamble().c_str());
	shader.addProcesses(shader_variant.get_processes());

	if (!shader.parse(&glslang::DefaultTBuiltInResource, 100, false, messages))
	{
		info_log = std::string(shader.getInfoLog()) + "\n" + std::string(shader.getInfoDebugLog());
		return false;
	}

	// Add shader to new program object.
	glslang::TProgram program;
	program.addShader(&shader);

	// Link program.
	if (!program.link(messages))
	{
		info_log = std::string(program.getInfoLog()) + "\n" + std::string(program.getInfoDebugLog());
		return false;
	}

	// Save any info log that was generated.
	if (shader.getInfoLog())
	{
		info_log += std::string(shader.getInfoLog()) + "\n" + std::string(shader.getInfoDebugLog()) + "\n";
	}

	if (program.getInfoLog())
	{
		info_log += std::string(program.getInfoLog()) + "\n" + std::string(program.getInfoDebugLog());
	}

	glslang::TIntermediate *intermediate = program.getIntermediate(language);

	// Translate to SPIRV.
	if (!intermediate)
	{
		info_log += "Failed to get shared intermediate code.\n";
		return false;
	}

	spv::SpvBuildLogger logger;

	glslang::GlslangToSpv(*intermediate, spirv, &logger);

	info_log += logger.getAllMessages() + "\n";

	// Shutdown glslang library.
	glslang::FinalizeProcess();

	return true;
}
}        // namespace vkb
