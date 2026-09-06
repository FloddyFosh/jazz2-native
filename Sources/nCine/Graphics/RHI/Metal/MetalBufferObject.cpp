// metal-cpp is included FIRST: the contract headers below pull in Death::Containers with a global using-directive,
// and metal-cpp's own NS::String / MTL::Function would otherwise be shadowed by that namespace's String and
// Function templates while its headers are parsed
#include "MetalCommon.h"

#include "MetalBufferObject.h"
#include "MetalDevice.h"

#include <cstring>

namespace nCine::RHI::Metal
{
	std::uint32_t MetalBufferObject::_nextHandle = 1;

	MetalBufferObject::MetalBufferObject(BufferTarget target)
		: _handle(_nextHandle++), _target(target), _gpuBuffer(nullptr), _gpuCapacity(0), _gpuDirty(true)
	{
	}

	MetalBufferObject::~MetalBufferObject()
	{
		ReleaseGpu();
	}

	void MetalBufferObject::ReleaseGpu() const
	{
		// A committed command buffer retains the buffers its draws reference until it completes, and the encoder
		// of the frame being recorded retains them as well, so releasing our reference here - also on a mid-frame
		// grow-realloc - can never free memory the GPU still reads
		if (_gpuBuffer != nullptr) {
			static_cast<MTL::Buffer*>(_gpuBuffer)->release();
			_gpuBuffer = nullptr;
		}
		_gpuCapacity = 0;
	}

	void* MetalBufferObject::GetMtlBuffer() const
	{
		// Only the geometry (Vertex/Index) targets get a real MTL::Buffer; uniform data is routed through the
		// device's per-frame uniform ring instead (see the MetalDevice draw path)
		if (_target == BufferTarget::Uniform) {
			return nullptr;
		}

		MTL::Device* device = MtlDevice();
		if (device == nullptr || _storage.empty()) {
			return nullptr;
		}

		// (Re)create when missing or too small (grow-only). Safe mid-frame: the device waits for the previous
		// frame's command buffer before any draw of the next one records, so the old buffer is no longer read
		// by the GPU, and the fresh one is handed to the encoder from now on.
		if (_gpuBuffer == nullptr || _gpuCapacity < _storage.size()) {
			ReleaseGpu();
			MTL::Buffer* buffer = device->newBuffer(NS::UInteger(_storage.size()), MTL::ResourceStorageModeShared);
			if (buffer == nullptr) {
				return nullptr;
			}
			_gpuBuffer = buffer;
			_gpuCapacity = _storage.size();
			_gpuDirty = true;
		}

		if (_gpuDirty) {
			// Shared storage: the CPU write is visible to the GPU with no flush (no didModifyRange needed)
			std::memcpy(static_cast<MTL::Buffer*>(_gpuBuffer)->contents(), _storage.data(), _storage.size());
			_gpuDirty = false;
		}
		return _gpuBuffer;
	}

	bool MetalBufferObject::Bind() const
	{
		return true;
	}

	bool MetalBufferObject::Unbind() const
	{
		return true;
	}

	void MetalBufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
		_gpuDirty = true;
	}

	void MetalBufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > _storage.size()) {
			return;
		}
		std::memcpy(_storage.data() + offset, data, size);
		_gpuDirty = true;
	}

	void MetalBufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The host store is a plain resizable buffer, so "immutable storage" is just a (re)allocation
		static_cast<void>(flags);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
		_gpuDirty = true;
	}

	void MetalBufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _storage.size());
	}

	void MetalBufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > _storage.size()) {
			return;
		}
		if (offset + size > _storage.size()) {
			size = _storage.size() - offset;
		}
		MetalDevice::BindUniformRange(index, _storage.data() + offset, std::uint32_t(size));
	}

	void* MetalBufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > _storage.size()) {
			return nullptr;
		}
		return _storage.data() + offset;
	}

	void MetalBufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
		_gpuDirty = true;
	}

	bool MetalBufferObject::Unmap()
	{
		_gpuDirty = true;
		return true;
	}

	void MetalBufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
