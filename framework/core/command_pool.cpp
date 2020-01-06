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

#include "command_pool.h"

#include "device.h"
#include "rendering/render_frame.h"

namespace vkb
{
CommandPool::CommandPool(Device &d, uint32_t queue_family_index, RenderFrame *render_frame, size_t thread_index, CommandBuffer::ResetMode reset_mode) :
    device{d},
    render_frame{render_frame},
    thread_index{thread_index},
    reset_mode{reset_mode}
{
	vk::CommandPoolCreateFlags flags;
	switch (reset_mode)
	{
		case CommandBuffer::ResetMode::ResetIndividually:
		case CommandBuffer::ResetMode::AlwaysAllocate:
			flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			break;
		case CommandBuffer::ResetMode::ResetPool:
		default:
			flags = vk::CommandPoolCreateFlagBits::eTransient;
			break;
	}

	static_cast<vk::CommandPool &>(*this) = device.get_handle().createCommandPool({flags, queue_family_index});
}

CommandPool::~CommandPool()
{
	primary_command_buffers.clear();
	secondary_command_buffers.clear();

	// Destroy command pool
	if (operator bool())
	{
		device.get_handle().destroy(*this);
	}
}

CommandPool::CommandPool(CommandPool &&other) :
    vk::CommandPool{other},
    device{other.device},
    queue_family_index{other.queue_family_index},
    primary_command_buffers{std::move(other.primary_command_buffers)},
    active_primary_command_buffer_count{other.active_primary_command_buffer_count},
    secondary_command_buffers{std::move(other.secondary_command_buffers)},
    active_secondary_command_buffer_count{other.active_secondary_command_buffer_count},
    render_frame{other.render_frame},
    thread_index{other.thread_index},
    reset_mode{other.reset_mode}
{
	static_cast<vk::CommandPool &&>(other) = nullptr;

	other.queue_family_index = 0;

	other.active_primary_command_buffer_count = 0;

	other.active_secondary_command_buffer_count = 0;
}

Device &CommandPool::get_device()
{
	return device;
}

uint32_t CommandPool::get_queue_family_index() const
{
	return queue_family_index;
}

vk::CommandPool CommandPool::get_handle() const
{
	return static_cast<const vk::CommandPool &>(*this);
}

RenderFrame *CommandPool::get_render_frame()
{
	return render_frame;
}

size_t CommandPool::get_thread_index() const
{
	return thread_index;
}

vk::Result CommandPool::reset_pool()
{
	vk::Result result = vk::Result::eSuccess;

	switch (reset_mode)
	{
		case CommandBuffer::ResetMode::ResetIndividually:
		{
			result = reset_command_buffers();

			break;
		}
		case CommandBuffer::ResetMode::ResetPool:
		{
			device.get_handle().resetCommandPool(*this, {});

			result = reset_command_buffers();

			break;
		}
		case CommandBuffer::ResetMode::AlwaysAllocate:
		{
			primary_command_buffers.clear();
			active_primary_command_buffer_count = 0;

			secondary_command_buffers.clear();
			active_secondary_command_buffer_count = 0;

			break;
		}
		default:
			throw std::runtime_error("Unknown reset mode for command pools");
	}

	return result;
}

vk::Result CommandPool::reset_command_buffers()
{
	vk::Result result = vk::Result::eSuccess;

	for (auto &cmd_buf : primary_command_buffers)
	{
		result = cmd_buf->reset(reset_mode);

		if (result != vk::Result::eSuccess)
		{
			return result;
		}
	}

	active_primary_command_buffer_count = 0;

	for (auto &cmd_buf : secondary_command_buffers)
	{
		result = cmd_buf->reset(reset_mode);

		if (result != vk::Result::eSuccess)
		{
			return result;
		}
	}

	active_secondary_command_buffer_count = 0;

	return result;
}

CommandBuffer &CommandPool::request_command_buffer(vk::CommandBufferLevel level)
{
	if (level == vk::CommandBufferLevel::ePrimary)
	{
		if (active_primary_command_buffer_count < primary_command_buffers.size())
		{
			return *primary_command_buffers.at(active_primary_command_buffer_count++);
		}

		primary_command_buffers.emplace_back(std::make_unique<CommandBuffer>(*this, level));

		active_primary_command_buffer_count++;

		return *primary_command_buffers.back();
	}
	else
	{
		if (active_secondary_command_buffer_count < secondary_command_buffers.size())
		{
			return *secondary_command_buffers.at(active_secondary_command_buffer_count++);
		}

		secondary_command_buffers.emplace_back(std::make_unique<CommandBuffer>(*this, level));

		active_secondary_command_buffer_count++;

		return *secondary_command_buffers.back();
	}
}

CommandBuffer::ResetMode const CommandPool::get_reset_mode() const
{
	return reset_mode;
}
}        // namespace vkb
