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

#include "queue.h"

#include "command_buffer.h"
#include "device.h"

namespace vkb
{
Queue::Queue(Device &device, uint32_t family_index, vk::QueueFamilyProperties properties, vk::Bool32 can_present, uint32_t index) :
    device{device},
    family_index{family_index},
    index{index},
    can_present{can_present},
    properties{properties}
{
	static_cast<vk::Queue &>(*this) = device.get_handle().getQueue(family_index, index);
}

Queue::Queue(Queue &&other) :
    vk::Queue{other},
    device{other.device},
    family_index{other.family_index},
    index{other.index},
    can_present{other.can_present},
    properties{other.properties}
{
	static_cast<vk::Queue &>(other) = nullptr;
	other.family_index              = {};
	other.properties                = {};
	other.can_present               = VK_FALSE;
	other.index                     = 0;
}

const Device &Queue::get_device() const
{
	return device;
}

const vk::Queue &Queue::get_handle() const
{
	return static_cast<const vk::Queue &>(*this);
}

uint32_t Queue::get_family_index() const
{
	return family_index;
}

uint32_t Queue::get_index() const
{
	return index;
}

const vk::QueueFamilyProperties& Queue::get_properties() const
{
	return properties;
}

vk::Bool32 Queue::support_present() const
{
	return can_present;
}

vk::Result Queue::submit(const CommandBuffer &command_buffer, vk::Fence fence) const
{
	vk::SubmitInfo submit_info;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &command_buffer.get_handle();

	get_handle().submit(submit_info, fence);
	return vk::Result::eSuccess;
}

vk::Result Queue::present(const vk::PresentInfoKHR &present_info) const
{
	if (!can_present)
	{
		return vk::Result::eErrorIncompatibleDisplayKHR;
	}

	return presentKHR(present_info);
}

vk::Result Queue::wait_idle() const
{
	waitIdle();
	return vk::Result::eSuccess;
}
}        // namespace vkb
