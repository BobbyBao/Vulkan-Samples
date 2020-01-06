#include "vma.hpp"

using namespace vma;

Allocation::Allocation(std::nullptr_t) :
    Parent()
{}

void *Allocation::mapMemory() const
{
	void *result;
	vmaMapMemory(*allocator, *this, &result);
	return result;
}

void Allocation::unmapMemory() const
{
	vmaUnmapMemory(*allocator, *this);
}

void Allocation::flush(DeviceSize offset, DeviceSize size) const
{
	vmaFlushAllocation(*allocator, *this, offset, size);
}

void Allocation::invalidate(DeviceSize offset, DeviceSize size) const
{
	vmaInvalidateAllocation(*allocator, *this, offset, size);
}

void Allocation::bindBufferMemory(Buffer buffer) const
{
	vmaBindBufferMemory(*allocator, *this, buffer.operator VkBuffer());
}

void Allocation::bindImageMemory(Image image) const
{
	vmaBindImageMemory(*allocator, *this, image.operator VkImage());
}

void Allocator::destroy()
{
	vmaDestroyAllocator(wrapped);
}

void Allocator::setCurrentFrameIndex(uint32_t frameIndex) const
{
	vmaSetCurrentFrameIndex(wrapped, frameIndex);
}

AllocatorStats Allocator::calculateStats() const
{
	AllocatorStats result;
	vmaCalculateStats(wrapped, reinterpret_cast<VmaStats *>(&result));
	return result;
}

uint32_t Allocator::findMemoryTypeIndex(
    uint32_t                      memoryTypeBits,
    const Allocation::CreateInfo &createInfo) const
{
	uint32_t result{0};
	vmaFindMemoryTypeIndex(
	    wrapped,
	    memoryTypeBits,
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &result);

	return result;
}

uint32_t Allocator::findMemoryTypeIndexForBufferInfo(
    const BufferCreateInfo &      bufferCreateInfo,
    const Allocation::CreateInfo &createInfo) const
{
	uint32_t result{0};
	vmaFindMemoryTypeIndexForBufferInfo(
	    wrapped,
	    &(bufferCreateInfo.operator const VkBufferCreateInfo &()),
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &result);
	return result;
}

uint32_t Allocator::findMemoryTypeIndexForImageInfo(
    const ImageCreateInfo &       imageCreateInfo,
    const Allocation::CreateInfo &createInfo) const
{
	uint32_t result{0};
	vmaFindMemoryTypeIndexForImageInfo(
	    wrapped,
	    &(imageCreateInfo.operator const VkImageCreateInfo &()),
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &result);
	return result;
}

Pool Allocator::createPool(const Pool::CreateInfo &createInfo) const
{
	Pool result;
	vmaCreatePool(
	    wrapped,
	    reinterpret_cast<const VmaPoolCreateInfo *>(&createInfo),
	    &(result.operator VmaPool &()));
	return result;
}

Allocation Allocator::createLostAllocation() const
{
	Allocation result;
	vmaCreateLostAllocation(
	    wrapped,
	    &(result.operator VmaAllocation &()));
	return result;
}

Allocation Allocator::allocateMemory(
    const MemoryRequirements &   memoryRequirements,
    const Allocator::CreateInfo &createInfo,
    vk::Optional<AllocationInfo> info) const
{
	Allocation result;
	vmaAllocateMemory(
	    wrapped,
	    &(memoryRequirements.operator const VkMemoryRequirements &()),
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &(result.operator VmaAllocation &()),
	    reinterpret_cast<VmaAllocationInfo *>(info.operator vma::AllocationInfo *()));
	return result;
}

Allocation Allocator::allocateMemoryForBuffer(
    const Buffer &                buffer,
    const Allocation::CreateInfo &createInfo,
    vk::Optional<AllocationInfo>  info) const
{
	Allocation result;
	vmaAllocateMemoryForBuffer(
	    wrapped,
	    buffer,
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &(result.operator VmaAllocation &()),
	    reinterpret_cast<VmaAllocationInfo *>(info.operator vma::AllocationInfo *()));
	return result;
}

Allocation Allocator::allocateMemoryForImage(
    Image                         image,
    const Allocation::CreateInfo &createInfo,
    vk::Optional<AllocationInfo>  info) const
{
	Allocation result;
	vmaAllocateMemoryForImage(
	    wrapped,
	    image,
	    reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
	    &(result.operator VmaAllocation &()),
	    reinterpret_cast<VmaAllocationInfo *>(info.operator vma::AllocationInfo *()));
	return result;
}

void Allocator::checkCorruption(uint32_t memoryTypeBits) const
{
	vmaCheckCorruption(
	    operator const VmaAllocator &(),
	    memoryTypeBits);
}

Allocator::CreateBufferResult Allocator::createBuffer(
    const BufferCreateInfo &      bufferCreateInfo,
    const Allocation::CreateInfo &createInfo,
    vk::Optional<AllocationInfo>  info) const
{
	CreateBufferResult createBufferResult;
	createBuffer(
	    bufferCreateInfo,
	    createInfo,
	    createBufferResult.buffer,
	    createBufferResult.allocation,
	    info);
	return createBufferResult;
}

void Allocator::createBuffer(
    const BufferCreateInfo &      bufferCreateInfo,
    const Allocation::CreateInfo &createInfo,
    Buffer &                      buffer,
    Allocation &                  allocation,
    vk::Optional<AllocationInfo>  info) const
{
	allocation.allocator = this;
	Result result        = static_cast<Result>(vmaCreateBuffer(
        operator const VmaAllocator &(),
        reinterpret_cast<const VkBufferCreateInfo *>(&bufferCreateInfo),
        reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
        reinterpret_cast<VkBuffer *>(&buffer),
        reinterpret_cast<VmaAllocation *>(&allocation),
        reinterpret_cast<VmaAllocationInfo *>(info.operator vma::AllocationInfo *())));
	vk::createResultValue(result, VMA_HPP_NAMESPACE_STRING "::Allocator::createBuffer");
}

void Allocator::destroyBuffer(
    const Buffer &    buffer,
    const Allocation &allocation) const
{
	vmaDestroyBuffer(
	    operator const VmaAllocator &(),
	    buffer.    operator VkBuffer(),
	    allocation.operator const VmaAllocation &());
}

Allocator::CreateImageResult Allocator::createImage(
    const vk::ImageCreateInfo &   imageCreateInfo,
    const Allocation::CreateInfo &createInfo,
    vk::Optional<AllocationInfo>  info) const
{
	CreateImageResult createImageResult;
	createImage(
	    imageCreateInfo,
	    createInfo,
	    createImageResult.image,
	    createImageResult.allocation,
	    info);
	return createImageResult;
}

void Allocator::createImage(
    const vk::ImageCreateInfo &   imageCreateInfo,
    const Allocation::CreateInfo &createInfo,
    Image &                       image,
    Allocation &                  allocation,
    vk::Optional<AllocationInfo>  info) const
{
	allocation.allocator = this;
	Result result        = static_cast<Result>(vmaCreateImage(
        operator const VmaAllocator &(),
        reinterpret_cast<const VkImageCreateInfo *>(&imageCreateInfo),
        reinterpret_cast<const VmaAllocationCreateInfo *>(&createInfo),
        reinterpret_cast<VkImage *>(&image),
        &(allocation.operator VmaAllocation &()),
        reinterpret_cast<VmaAllocationInfo *>(info.operator vma::AllocationInfo *())));

	vk::createResultValue(result, VMA_HPP_NAMESPACE_STRING "::Allocator::createImage");
}

void Allocator::destroyImage(
    const Image &     image,
    const Allocation &allocation) const
{
	vmaDestroyImage(
	    operator const VmaAllocator &(),
	    image.     operator VkImage(),
	    allocation.operator const VmaAllocation &());
}

Allocator vma::createAllocator(const Allocator::CreateInfo &createInfo)
{
	Allocator result;
	vmaCreateAllocator(
	    reinterpret_cast<const VmaAllocatorCreateInfo *>(&createInfo),
	    &static_cast<VmaAllocator &>(result));
	return result;
}
