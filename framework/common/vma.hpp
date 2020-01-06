//
//  Created by Bradley Austin Davis on 2020/01/05
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#pragma once

#ifndef AMD_VULKAN_MEMORY_ALLOCATOR_HPP
#	define AMD_VULKAN_MEMORY_ALLOCATOR_HPP

#	define VMA_HPP_NAMESPACE_STRING "vma"

namespace vma
{
using AllocationCallbacks = vk::AllocationCallbacks;
using Buffer              = vk::Buffer;
using BufferCreateInfo    = vk::BufferCreateInfo;
using Device              = vk::Device;
using DeviceMemory        = vk::DeviceMemory;
using DeviceSize          = vk::DeviceSize;
using Image               = vk::Image;
using ImageCreateInfo     = vk::ImageCreateInfo;
using MemoryPropertyFlags = vk::MemoryPropertyFlags;
using MemoryRequirements  = vk::MemoryRequirements;
using PhysicalDevice      = vk::PhysicalDevice;
using Result              = vk::Result;

using DeviceMemoryCallbacks = VmaDeviceMemoryCallbacks;
using VulkanFunctions       = VmaVulkanFunctions;

enum class MemoryUsage
{
	eUnknown  = VMA_MEMORY_USAGE_UNKNOWN,
	eGpuOnly  = VMA_MEMORY_USAGE_GPU_ONLY,
	eCpuOnly  = VMA_MEMORY_USAGE_CPU_ONLY,
	eCpuToGpu = VMA_MEMORY_USAGE_CPU_TO_GPU,
	eGpuToCpu = VMA_MEMORY_USAGE_GPU_TO_CPU,
};

enum class RecordFlagBits
{
	eRecordFlushAfterCall = VMA_RECORD_FLUSH_AFTER_CALL_BIT,
};

using RecordFlags = vk::Flags<RecordFlagBits, VmaRecordFlags>;

enum class DefragmentationFlagBits
{
};

using DefragmentationFlags = vk::Flags<DefragmentationFlagBits, VmaDefragmentationFlagBits>;

struct PoolStats
{
	DeviceSize size{0};

	DeviceSize unusedSize{0};

	size_t allocationCount{0};

	size_t unusedRangeCount{0};

	DeviceSize unusedRangeSizeMax{0};

	size_t blockCount{0};
};

class Allocator;
class Allocation;
class Pool;

template <typename T>
class Wrapper
{
  public:
	Wrapper() = default;

	Wrapper(const Wrapper &) = default;

	Wrapper(std::nullptr_t)
	{}

	operator bool() const
	{
		return wrapped != nullptr;
	}

	operator T &()
	{
		return wrapped;
	}

	operator const T &() const
	{
		return wrapped;
	}

  protected:
	explicit Wrapper(const T &wrapped) :
	    wrapped(wrapped)
	{
	}

	T wrapped{nullptr};
};

class Pool : public Wrapper<VmaPool>
{
	using Parent = Wrapper<VmaPool>;

  public:
	enum class CreateFlagBits
	{
		eIgnoreBufferImageGranularity = VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT,
		eLinearAlgorithm              = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
		eBuddyAlgorithm               = VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT,
	};

	using CreateFlags = vk::Flags<CreateFlagBits, VmaPoolCreateFlags>;

	struct CreateInfo
	{
		uint32_t    memoryTypeIndex;
		CreateFlags flags;
		DeviceSize  blockSize;
		size_t      minBlockCount;
		size_t      maxBlockCount;
		uint32_t    frameInUseCount;
	};

	Pool() = default;
	Pool(std::nullptr_t n) :
	    Parent(n){};

	void destroy();

	PoolStats getStats();

	void makeAllocationsLost(size_t *pLostAllocationCount);

	Result checkPoolCorruption();

  private:
	friend class Allocator;
	Allocator *allocator{nullptr};
};

struct AllocationInfo
{
	uint32_t memoryType{~0U};

	DeviceMemory deviceMemory;

	DeviceSize offset{0};

	DeviceSize size{0};

	void *pMappedData{nullptr};

	void *pUserData{nullptr};
};

class Allocation : public Wrapper<VmaAllocation>
{
	using Parent = Wrapper<VmaAllocation>;

  public:
	Allocation()                   = default;
	Allocation(const Allocation &) = default;
	Allocation(std::nullptr_t n);

	enum class CreateFlagBits
	{
		eDedicatedMemory    = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		eNeverAllocate      = VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT,
		eMapped             = VMA_ALLOCATION_CREATE_MAPPED_BIT,
		eCanBecomeLost      = VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT,
		eCanMakeOtherLost   = VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT,
		eUserDataCopyString = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT,
		eUpperAddress       = VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT,
		eDontBind           = VMA_ALLOCATION_CREATE_DONT_BIND_BIT,
		eStrategyBestFit    = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT,
		eStrategyWorstFit   = VMA_ALLOCATION_CREATE_STRATEGY_WORST_FIT_BIT,
		eStrategyFirstFit   = VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT,
	};

	using CreateFlags = vk::Flags<CreateFlagBits, VmaAllocationCreateFlags>;

	struct CreateInfo
	{
		CreateFlags         flags;
		MemoryUsage         usage;
		MemoryPropertyFlags requiredFlags;
		MemoryPropertyFlags preferredFlags;
		uint32_t            memoryTypeBits{0};
		VmaPool             pool{nullptr};
		void *              pUserData{nullptr};
	};
	static_assert(sizeof(CreateInfo) == sizeof(VmaAllocationCreateInfo), "");

	void freeMemory();
	void resize(DeviceSize newSize);

	AllocationInfo getInfo() const;

	vk::Bool32 touch() const;

	void setUserData(void *pUserData) const;

	void *mapMemory() const;

	void unmapMemory() const;

	void flush(DeviceSize offset, DeviceSize size) const;

	void invalidate(DeviceSize offset, DeviceSize size) const;

	void bindBufferMemory(Buffer buffer) const;

	void bindImageMemory(Image image) const;

  private:
	Allocation(VmaAllocation allocation, const Allocator *allocator) :
	    Parent(allocation),
	    allocator(allocator)
	{}
	friend class Allocator;
	const Allocator *allocator{nullptr};
};

struct AllocatorStats
{
	struct Info
	{
		uint32_t   blockCount;
		uint32_t   allocationCount;
		uint32_t   unusedRangeCount;
		DeviceSize usedBytes;
		DeviceSize unusedBytes;
		DeviceSize allocationSizeMin, allocationSizeAvg, allocationSizeMax;
		DeviceSize unusedRangeSizeMin, unusedRangeSizeAvg, unusedRangeSizeMax;
	};
	static_assert(sizeof(Info) == sizeof(VmaStatInfo), "");

	Info memoryType[VK_MAX_MEMORY_TYPES];
	Info memoryHeap[VK_MAX_MEMORY_HEAPS];
	Info total;
};

static_assert(sizeof(AllocatorStats) == sizeof(VmaStats), "");

struct RecordSettings
{
	RecordFlags flags;
	const char *pFilePath;
};

static_assert(sizeof(RecordSettings) == sizeof(VmaRecordSettings), "");

class Allocator : public Wrapper<VmaAllocator>
{
	using Parent = Wrapper<VmaAllocator>;

  public:
	enum class CreateFlagBits
	{
		eExternallySynchronized = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
		eDedicatedAllocation    = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT,
	};

	using CreateFlags = vk::Flags<CreateFlagBits, VmaAllocatorCreateFlags>;

	struct CreateInfo
	{
		CreateFlags                  flags;
		PhysicalDevice               physicalDevice;
		Device                       device;
		DeviceSize                   preferredLargeHeapBlockSize;
		const AllocationCallbacks *  pAllocationCallbacks{nullptr};
		const DeviceMemoryCallbacks *pDeviceMemoryCallbacks{nullptr};
		uint32_t                     frameInUseCount{0};
		const DeviceSize *           pHeapSizeLimit{nullptr};
		const VulkanFunctions *      pVulkanFunctions{nullptr};
		const RecordSettings *       pRecordSettings{nullptr};
	};
	static_assert(sizeof(CreateInfo) == sizeof(VmaAllocatorCreateInfo), "");

	struct CreateBufferResult
	{
		Buffer     buffer;
		Allocation allocation;
	};

	struct CreateImageResult
	{
		Image      image;
		Allocation allocation;
	};

	Allocator()                  = default;
	Allocator(const Allocator &) = default;
	Allocator(std::nullptr_t);

	void destroy();

	void setCurrentFrameIndex(uint32_t frameIndex) const;

	AllocatorStats calculateStats() const;

	uint32_t findMemoryTypeIndex(
	    uint32_t                      memoryTypeBits,
	    const Allocation::CreateInfo &pAllocationCreateInfo) const;

	uint32_t findMemoryTypeIndexForBufferInfo(
	    const BufferCreateInfo &      pBufferCreateInfo,
	    const Allocation::CreateInfo &pAllocationCreateInfo) const;

	uint32_t findMemoryTypeIndexForImageInfo(
	    const ImageCreateInfo &       imageCreateInfo,
	    const Allocation::CreateInfo &pAllocationCreateInfo) const;

	Pool createPool(const Pool::CreateInfo &createInfo) const;

	Allocation createLostAllocation() const;

	Allocation allocateMemory(
	    const MemoryRequirements &   memoryRequirements,
	    const Allocator::CreateInfo &createInfo,
	    vk::Optional<AllocationInfo> info = nullptr) const;

	std::vector<Allocation> allocateMemoryPages(
	    const MemoryRequirements &    memoryRequirements,
	    const Allocation::CreateInfo &createInfo,
	    size_t                        allocationCount) const;

	Allocation allocateMemoryForBuffer(
	    const Buffer &                buffer,
	    const Allocation::CreateInfo &createInfo,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	Allocation allocateMemoryForImage(
	    Image                         image,
	    const Allocation::CreateInfo &createInfo,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	void freeMemoryPages(
	    const vk::ArrayProxy<const Allocation> &allocations) const;

	void checkCorruption(uint32_t memoryTypeBits) const;

	CreateBufferResult createBuffer(
	    const BufferCreateInfo &      bufferCreateInfo,
	    const Allocation::CreateInfo &createInfo,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	void createBuffer(
	    const BufferCreateInfo &      bufferCreateInfo,
	    const Allocation::CreateInfo &createInfo,
	    Buffer &                      buffer,
	    Allocation &                  allocation,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	void destroyBuffer(
	    const Buffer &    buffer,
	    const Allocation &allocation) const;

	CreateImageResult createImage(
	    const ImageCreateInfo &       imageCreateInfo,
	    const Allocation::CreateInfo &createInfo,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	void createImage(
	    const vk::ImageCreateInfo &   imageCreateInfo,
	    const Allocation::CreateInfo &createInfo,
	    Image &                       image,
	    Allocation &                  allocation,
	    vk::Optional<AllocationInfo>  info = nullptr) const;

	void destroyImage(const Image &image, const Allocation &allocation) const;
};

static_assert(sizeof(Allocator) == sizeof(VmaAllocator), "");

Allocator createAllocator(const Allocator::CreateInfo &createInfo);

}        // namespace vma

#endif        // AMD_VULKAN_MEMORY_ALLOCATOR_HPP
