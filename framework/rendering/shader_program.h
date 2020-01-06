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

#pragma once

#include <unordered_map>
#include <vector>

#include "core/shader_module.h"

namespace vkb
{
/**
 * @brief A ShaderProgram represents the overall group of programmable shaders used in a pipeline
 *        and the underlying organized resources.
 *
 * A shader program ultimately holds the shader sets that are used to create
 * the DescriptorSetLayout's when given to a PipelineLayout.
 */
class ShaderProgram
{
  public:
	ShaderProgram(const std::vector<ShaderModule *> &shader_modules);

	ShaderProgram(const ShaderProgram &) = delete;

	ShaderProgram(ShaderProgram &&other) = default;

	~ShaderProgram() = default;

	ShaderProgram &operator=(const ShaderProgram &) = delete;

	ShaderProgram &operator=(ShaderProgram &&) = delete;

	const std::vector<ShaderModule *> &get_shader_modules() const;

	const std::vector<ShaderResource> get_resources(const ShaderResourceType &type = ShaderResourceType::All, vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eAll) const;

	const std::unordered_map<uint32_t, std::vector<ShaderResource>> &get_shader_sets() const;

  private:
	// The shader modules that this program uses
	std::vector<ShaderModule *> shader_modules;

	// The shader resources that this program uses, indexed by their name
	std::unordered_map<std::string, ShaderResource> resources;

	// A map of each set and the resources it owns used by the shader program
	std::unordered_map<uint32_t, std::vector<ShaderResource>> sets;
};
}        // namespace vkb
