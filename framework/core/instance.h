/* Copyright (c) 2018-2020, Arm Limited and Contributors
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

#include "common/helpers.h"
#include "common/vk_common.h"

namespace vkb
{
/**
 * @brief Returns a list of Khronos/LunarG supported validation layers
 *        Attempting to enable them in order of preference, starting with later Vulkan SDK versions
 * @param supported_instance_layers A list of validation layers to check against
 */
std::vector<const char *> get_optimal_validation_layers(const std::vector<vk::LayerProperties> &supported_instance_layers);

/**
 * @brief A wrapper class for vk::Instance
 *
 * This class is responsible for initializing volk, enumerating over all available extensions and validation layers
 * enabling them if they exist, setting up debug messaging and querying all the physical devices existing on the machine.
 */
class Instance : protected vk::Instance
{
  public:
	/**
	 * @brief Initializes the connection to Vulkan
	 * @param application_name The name of the application
	 * @param required_extensions The extensions requested to be enabled
	 * @param required_validation_layers The validation layers to be enabled
	 * @param headless Whether the application is requesting a headless setup or not
	 * @throws runtime_error if the required extensions and validation layers are not found
	 */
	Instance(const std::string &              application_name,
	         const std::vector<const char *> &required_extensions        = {},
	         const std::vector<const char *> &required_validation_layers = {},
	         bool                             headless                   = false);

	/**
	 * @brief Queries the GPUs of a vk::Instance that is already created
	 * @param instance A valid vk::Instance
	 */
	Instance(vk::Instance instance);

	Instance(const Instance &) = delete;

	Instance(Instance &&) = delete;

	~Instance();

	Instance &operator=(const Instance &) = delete;

	Instance &operator=(Instance &&) = delete;

	/**
	 * @brief Queries the instance for the physical devices on the machine
	 */
	void query_gpus();

	/**
	 * @brief Tries to find the first available discrete GPU
	 * @returns A valid physical device
	 */
	vk::PhysicalDevice get_gpu();

	/**
	 * @brief Checks if the given extension is enabled in the vk::Instance
	 * @param extension An extension to check
	 */
	bool is_enabled(const char *extension);

	vk::Instance get_handle() const;

	const std::vector<const char *> &get_extensions();

  private:
	/**
	 * @brief The enabled extensions
	 */
	std::vector<const char *> extensions;

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	/**
	 * @brief The debug report callback
	 */
	vk::DebugReportCallbackEXT debug_report_callback;
#endif

	/**
	 * @brief The physical devices found on the machine
	 */
	std::vector<vk::PhysicalDevice> gpus;
};
}        // namespace vkb
