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

#include "buffer.h"

#include "device.h"

namespace vkb
{
namespace core
{
Buffer::Buffer(Device &device, vk::DeviceSize size, vk::BufferUsageFlags buffer_usage, vma::MemoryUsage memory_usage, vma::Allocation::CreateFlags flags) :
    device{device},
    size{size}
{
#ifdef VK_USE_PLATFORM_MACOS_MVK
	// Workaround for Mac (MoltenVK requires unmapping https://github.com/KhronosGroup/MoltenVK/issues/175)
	// Force cleares the flag VMA_ALLOCATION_CREATE_MAPPED_BIT
    flags &= ~vma::Allocation::CreateFlags{vma::Allocation::CreateFlagBits::eMapped};
#endif

	persistent = (flags & vma::Allocation::CreateFlagBits::eMapped).operator bool();

	vk::BufferCreateInfo buffer_info;
	buffer_info.usage = buffer_usage;
	buffer_info.size  = size;

	vma::Allocation::CreateInfo memory_info;
	memory_info.flags = flags;
	memory_info.usage = memory_usage;

	vma::AllocationInfo allocation_info;

	device.get_memory_allocator().createBuffer(
	    buffer_info,
	    memory_info,
	    *this,
	    allocation,
	    allocation_info);

	memory = allocation_info.deviceMemory;

	if (persistent)
	{
		mapped_data = static_cast<uint8_t *>(allocation_info.pMappedData);
	}
}

Buffer::Buffer(Buffer &&other) :
    device{other.device},
    allocation{other.allocation},
    memory{other.memory},
    size{other.size},
    mapped_data{other.mapped_data},
    mapped{other.mapped}
{
	static_cast<vk::Buffer &>(*this) = static_cast<vk::Buffer &>(other);
	// Reset other handles to avoid releasing on destruction
	static_cast<vk::Buffer &>(other) = nullptr;
	other.allocation                 = nullptr;
	other.memory                     = nullptr;
	other.mapped_data                = nullptr;
	other.mapped                     = false;
}

Buffer::~Buffer()
{
	auto &self = static_cast<vk::Buffer &>(*this);
	if (self && allocation)
	{
		unmap();
        device.get_memory_allocator().destroyBuffer(self, allocation);
        allocation = nullptr;
        self = nullptr;
	}
}

const Device &Buffer::get_device() const
{
	return device;
}

const vk::Buffer &Buffer::get_handle() const
{
	return static_cast<const vk::Buffer &>(*this);
}

const vk::Buffer *Buffer::get() const
{
	return static_cast<const vk::Buffer *>(this);
}

VmaAllocation Buffer::get_allocation() const
{
	return allocation;
}

vk::DeviceMemory Buffer::get_memory() const
{
	return memory;
}

vk::DeviceSize Buffer::get_size() const
{
	return size;
}

uint8_t *Buffer::map()
{
	if (!mapped && !mapped_data)
	{
        mapped_data = (uint8_t*)allocation.mapMemory();
		mapped = true;
	}
	return mapped_data;
}

void Buffer::unmap()
{
	if (mapped)
	{
        allocation.unmapMemory();
		mapped_data = nullptr;
		mapped      = false;
	}
}

void Buffer::flush() const
{
    allocation.flush(0, size);
}

void Buffer::update(const std::vector<uint8_t> &data, size_t offset)
{
	update(data.data(), data.size(), offset);
}

void Buffer::update(void *data, size_t size, size_t offset)
{
	update(reinterpret_cast<const uint8_t *>(data), size, offset);
}

void Buffer::update(const uint8_t *data, const size_t size, const size_t offset)
{
	if (persistent)
	{
		std::copy(data, data + size, mapped_data + offset);
		flush();
	}
	else
	{
		map();
		std::copy(data, data + size, mapped_data + offset);
		flush();
		unmap();
	}
}

}        // namespace core
}        // namespace vkb
