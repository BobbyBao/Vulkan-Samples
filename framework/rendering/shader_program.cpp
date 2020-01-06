/* Copyright (c) 2019-2020, Arm Limited and Contributors
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

#include "shader_program.h"

namespace vkb
{
ShaderProgram::ShaderProgram(const std::vector<ShaderModule *> &shader_modules) :
    shader_modules{shader_modules}
{
	// Collect and combine all the shader resources from each of the shader modules
	// Collate them all into a map that is indexed by the name of the resource
	for (auto *shader_module : shader_modules)
	{
		for (const auto &shader_resource : shader_module->get_resources())
		{
			std::string key = shader_resource.name;

			// Since 'Input' and 'Output' resources can have the same name, we modify the key string
			if (shader_resource.type == ShaderResourceType::Input || shader_resource.type == ShaderResourceType::Output)
			{
				key = vk::to_string(shader_resource.stages) + "_" + key;
			}

			auto it = resources.find(key);

			if (it != resources.end())
			{
				// Append stage flags if resource already exists
				it->second.stages |= shader_resource.stages;
			}
			else
			{
				// Create a new entry in the map
				resources.emplace(key, shader_resource);
			}
		}
	}

	// Sift through the map of name indexed shader resources
	// Seperate them into their respective sets
	for (auto &it : resources)
	{
		auto &shader_resource = it.second;

		// Find binding by set index in the map.
		auto it2 = sets.find(shader_resource.set);

		if (it2 != sets.end())
		{
			// Add resource to the found set index
			it2->second.push_back(shader_resource);
		}
		else
		{
			// Create a new set index and with the first resource
			sets.emplace(shader_resource.set, std::vector<ShaderResource>{shader_resource});
		}
	}
}

const std::vector<ShaderModule *> &ShaderProgram::get_shader_modules() const
{
	return shader_modules;
}

const std::vector<ShaderResource> ShaderProgram::get_resources(const ShaderResourceType &type, vk::ShaderStageFlagBits stage) const
{
	std::vector<ShaderResource> found_resources;

	for (auto &it : resources)
	{
		auto &shader_resource = it.second;

		if (shader_resource.type == type || type == ShaderResourceType::All)
		{
			if (shader_resource.stages == stage || stage == vk::ShaderStageFlagBits::eAll)
			{
				found_resources.push_back(shader_resource);
			}
		}
	}

	return found_resources;
}

const std::unordered_map<uint32_t, std::vector<ShaderResource>> &ShaderProgram::get_shader_sets() const
{
	return sets;
}

}        // namespace vkb
