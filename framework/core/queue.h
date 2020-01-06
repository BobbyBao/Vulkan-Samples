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

#pragma once

#include "common/helpers.h"
#include "common/vk_common.h"
#include "core/swapchain.h"

namespace vkb
{
class Device;
class CommandBuffer;

class Queue : protected vk::Queue
{
  public:
	Queue(Device &device, uint32_t family_index, vk::QueueFamilyProperties properties, vk::Bool32 can_present, uint32_t index);

	Queue(const Queue &) = default;

	Queue(Queue &&other);

	Queue &operator=(const Queue &) = delete;

	Queue &operator=(Queue &&) = delete;

	const Device &get_device() const;

	const vk::Queue &get_handle() const;

	uint32_t get_family_index() const;

	uint32_t get_index() const;

	const vk::QueueFamilyProperties& get_properties() const;

	vk::Bool32 support_present() const;

	vk::Result submit(const CommandBuffer &command_buffer, vk::Fence fence) const;

	vk::Result present(const vk::PresentInfoKHR &present_infos) const;

	vk::Result wait_idle() const;

  private:
	Device &device;

	uint32_t family_index{0};

	uint32_t index{0};

	vk::Bool32 can_present{VK_FALSE};

	vk::QueueFamilyProperties properties;
};
}        // namespace vkb
