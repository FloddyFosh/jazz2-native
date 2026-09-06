// metal-cpp is included FIRST: the contract headers below pull in Death::Containers with a global using-directive,
// and metal-cpp's own NS::String / MTL::Function would otherwise be shadowed by that namespace's String and
// Function templates while its headers are parsed
#include "MetalCommon.h"

#include "MetalTexture.h"
#include "MetalDevice.h"

#include <cstring>

namespace nCine::RHI::Metal
{
	namespace
	{
		// Copies one packed row into the store, expanding a narrower source (RGB8) to a wider store
		// (RGBA8) by filling the extra channel with 255 (opaque). A same-width copy is a plain memcpy.
		void CopyExpandRow(std::uint8_t* dst, std::int32_t dstBpp, const std::uint8_t* src, std::int32_t srcBpp, std::int32_t width)
		{
			if (srcBpp == dstBpp) {
				std::memcpy(dst, src, std::size_t(width) * std::size_t(dstBpp));
				return;
			}
			const std::int32_t shared = (srcBpp < dstBpp ? srcBpp : dstBpp);
			for (std::int32_t x = 0; x < width; x++) {
				std::int32_t c = 0;
				for (; c < shared; c++) {
					dst[x * dstBpp + c] = src[x * srcBpp + c];
				}
				for (; c < dstBpp; c++) {
					dst[x * dstBpp + c] = 255;
				}
			}
		}

		MTL::PixelFormat MapPixelFormat(PixelFormat format)
		{
			switch (format) {
				case PixelFormat::RGBA8: return MTL::PixelFormatRGBA8Unorm;
				case PixelFormat::RGBA16F: return MTL::PixelFormatRGBA16Float;
				case PixelFormat::RGBA32F: return MTL::PixelFormatRGBA32Float;
				default: return MTL::PixelFormatRGBA8Unorm;	// the store is promoted to RGBA8 for R8/RG8/RGB8
			}
		}

		MTL::TextureSwizzle MapSwizzle(SwizzleChannel channel, MTL::TextureSwizzle identity)
		{
			switch (channel) {
				case SwizzleChannel::Red: return MTL::TextureSwizzleRed;
				case SwizzleChannel::Green: return MTL::TextureSwizzleGreen;
				case SwizzleChannel::Blue: return MTL::TextureSwizzleBlue;
				case SwizzleChannel::Alpha: return MTL::TextureSwizzleAlpha;
				case SwizzleChannel::Zero: return MTL::TextureSwizzleZero;
				case SwizzleChannel::One: return MTL::TextureSwizzleOne;
				default: return identity;
			}
		}
	}

	std::uint32_t MetalTexture::_nextHandle = 1;

	MetalTexture::MetalTexture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false),
			_gpuTexture(nullptr), _gpuView(nullptr), _gpuSampler(nullptr), _gpuFormat(std::uint32_t(MTL::PixelFormatRGBA8Unorm)),
			_contentsDirty(false), _hasCpuData(false),
			_samplerFilter(nCine::SamplerFilter::Unknown), _samplerWrap(SamplerWrapping::Unknown)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	MetalTexture::~MetalTexture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture via _boundTextures
		MetalDevice::UnbindTexture(this);
		ReleaseGpu();
	}

	void MetalTexture::ReleaseGpu() const
	{
		// A Metal command buffer retains every resource its encoders reference until it has completed, so
		// dropping our reference here is safe even while a frame that samples this texture is still in flight -
		// no deferred-destruction queue is needed (unlike the Vulkan backend)
		if (_gpuView != nullptr && _gpuView != _gpuTexture) {
			static_cast<MTL::Texture*>(_gpuView)->release();
		}
		_gpuView = nullptr;
		if (_gpuTexture != nullptr) {
			static_cast<MTL::Texture*>(_gpuTexture)->release();
			_gpuTexture = nullptr;
		}
		if (_gpuSampler != nullptr) {
			static_cast<MTL::SamplerState*>(_gpuSampler)->release();
			_gpuSampler = nullptr;
		}
	}

	std::int32_t MetalTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	bool MetalTexture::HasIdentitySwizzle() const
	{
		return (_swizzle[0] == SwizzleChannel::Red && _swizzle[1] == SwizzleChannel::Green &&
			_swizzle[2] == SwizzleChannel::Blue && _swizzle[3] == SwizzleChannel::Alpha);
	}

	void MetalTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Promote the narrower runtime formats (RGB8 render targets, palette-index R8 / RG8) to an RGBA8 store
		_uploadFormat = format;
		_format = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		_width = width;
		_height = height;
		const std::int32_t bpp = BytesPerPixel(_format);
		_strideBytes = width * bpp;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so the GPU texture must be rebuilt on the next bind
		ReleaseGpu();
		_contentsDirty = true;
	}

	void MetalTexture::EnsureGpu() const
	{
		MTL::Device* device = MtlDevice();
		if (device == nullptr || _width <= 0 || _height <= 0) {
			return;
		}

		if (_gpuTexture == nullptr) {
			_gpuFormat = std::uint32_t(MapPixelFormat(_format));
			MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
			desc->setTextureType(MTL::TextureType2D);
			desc->setPixelFormat(MTL::PixelFormat(_gpuFormat));
			desc->setWidth(NS::UInteger(_width));
			desc->setHeight(NS::UInteger(_height));
			desc->setMipmapLevelCount(1);
			desc->setSampleCount(1);
			MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
			if (_isRenderTarget) {
				usage |= MTL::TextureUsageRenderTarget;
			}
			// Every texture lives in GPU-private memory: render targets are only ever written by the GPU and read
			// back (rarely) through a blit into a shared buffer (GetTexImage()), and CPU textures are filled through
			// a staging-buffer blit recorded into the frame (RecordStreamingUpload()) - a replaceRegion() on a
			// managed texture would instead become visible to EVERY encoder of the not-yet-committed frame, the
			// ones recorded before the upload included
			desc->setStorageMode(MTL::StorageModePrivate);
			if (!HasIdentitySwizzle()) {
				// A swizzled view of a texture requires the pixel-format-view usage on its parent
				usage |= MTL::TextureUsagePixelFormatView;
			}
			desc->setUsage(usage);
			MTL::Texture* texture = device->newTexture(desc);
			desc->release();
			if (texture == nullptr) {
				return;
			}
			_gpuTexture = texture;
			_gpuView = nullptr;

			if (!HasIdentitySwizzle()) {
				// The sampling swizzle is applied through a view (no texel baking) - e.g. RG8 palette textures map A<-Green
				MTL::TextureSwizzleChannels swizzle;
				swizzle.red = MapSwizzle(_swizzle[0], MTL::TextureSwizzleRed);
				swizzle.green = MapSwizzle(_swizzle[1], MTL::TextureSwizzleGreen);
				swizzle.blue = MapSwizzle(_swizzle[2], MTL::TextureSwizzleBlue);
				swizzle.alpha = MapSwizzle(_swizzle[3], MTL::TextureSwizzleAlpha);
				MTL::Texture* view = texture->newTextureView(MTL::PixelFormat(_gpuFormat), MTL::TextureType2D,
					NS::Range::Make(0, 1), NS::Range::Make(0, 1), swizzle);
				_gpuView = (view != nullptr ? static_cast<void*>(view) : static_cast<void*>(texture));
			} else {
				_gpuView = texture;
			}
			// A fresh texture holds no texels yet, whatever the store says
			if (_hasCpuData && !_isRenderTarget) {
				_contentsDirty = true;
			}
		}

		// NOTE: CPU texel uploads are NOT recorded here. EnsureGpu() only materializes the texture / view /
		// sampler; the staging blit is recorded into the current FRAME's command buffer by RecordStreamingUpload()
		// (driven from the device's pre-draw phase, outside any render encoder), so per-frame texture streaming
		// (the palette) is ordered with the draws exactly like the GL upload. _contentsDirty stays set until then.

		if (_gpuSampler == nullptr || _samplerFilter != _magFilter || _samplerWrap != _wrap) {
			if (_gpuSampler != nullptr) {
				static_cast<MTL::SamplerState*>(_gpuSampler)->release();
				_gpuSampler = nullptr;
			}
			const bool linear = (_magFilter == nCine::SamplerFilter::Linear ||
				_magFilter == nCine::SamplerFilter::LinearMipmapNearest || _magFilter == nCine::SamplerFilter::LinearMipmapLinear);
			MTL::SamplerAddressMode address;
			switch (_wrap) {
				case SamplerWrapping::Repeat: address = MTL::SamplerAddressModeRepeat; break;
				case SamplerWrapping::MirroredRepeat: address = MTL::SamplerAddressModeMirrorRepeat; break;
				default: address = MTL::SamplerAddressModeClampToEdge; break;
			}
			MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
			sd->setMinFilter(linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
			sd->setMagFilter(linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
			sd->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
			sd->setSAddressMode(address);
			sd->setTAddressMode(address);
			sd->setRAddressMode(address);
			// Normalized coordinates, like GL: the shaders sample with [0,1] UVs
			sd->setNormalizedCoordinates(true);
			MTL::SamplerState* sampler = device->newSamplerState(sd);
			sd->release();
			_gpuSampler = sampler;
			_samplerFilter = _magFilter;
			_samplerWrap = _wrap;
		}
	}

	void MetalTexture::RecordStreamingUpload(void* commandBuffer) const
	{
		MTL::Device* device = MtlDevice();
		if (device == nullptr || commandBuffer == nullptr) {
			return;
		}
		EnsureGpu();	// materialize the texture / view / sampler (no upload)
		if (_gpuTexture == nullptr || !_contentsDirty || !_hasCpuData || _isRenderTarget || _pixels.empty()) {
			return;
		}
		MTL::CommandBuffer* cb = static_cast<MTL::CommandBuffer*>(commandBuffer);
		MTL::Texture* texture = static_cast<MTL::Texture*>(_gpuTexture);

		// Shared staging buffer holding the whole level-0 store, copied into the private texture by a blit at
		// this point of the frame. The command buffer retains the staging buffer until it has executed, so our
		// reference can go right away - no per-frame staging list is needed (unlike the Vulkan backend).
		MTL::Buffer* staging = device->newBuffer(_pixels.data(), NS::UInteger(_pixels.size()), MTL::ResourceStorageModeShared);
		if (staging == nullptr) {
			return;
		}
		MTL::BlitCommandEncoder* blit = cb->blitCommandEncoder();
		if (blit != nullptr) {
			blit->copyFromBuffer(staging, 0, NS::UInteger(_strideBytes), NS::UInteger(_pixels.size()),
				MTL::Size::Make(NS::UInteger(_width), NS::UInteger(_height), 1), texture, 0, 0, MTL::Origin::Make(0, 0, 0));
			blit->endEncoding();
			_contentsDirty = false;
		}
		staging->release();
	}

	void* MetalTexture::GpuTexture() const
	{
		EnsureGpu();
		return _gpuTexture;
	}

	void* MetalTexture::GpuView() const
	{
		EnsureGpu();
		return _gpuView;
	}

	void* MetalTexture::GpuSampler() const
	{
		EnsureGpu();
		return _gpuSampler;
	}

	std::uint32_t MetalTexture::GpuFormat() const
	{
		return _gpuFormat;
	}

	bool MetalTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		MetalDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool MetalTexture::Unbind() const
	{
		MetalDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool MetalTexture::Unbind(std::uint32_t textureUnit)
	{
		MetalDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void MetalTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;
		}
		Allocate(format, width, height);
		if (data != nullptr && !_pixels.empty()) {
			_hasCpuData = true;
			const std::int32_t srcBpp = BytesPerPixel(format);
			const std::int32_t dstBpp = BytesPerPixel(_format);
			if (srcBpp == dstBpp) {
				std::memcpy(_pixels.data(), data, _pixels.size());
			} else {
				const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
				for (std::int32_t y = 0; y < _height; y++) {
					CopyExpandRow(_pixels.data() + std::size_t(y) * _strideBytes,
						dstBpp, src + std::size_t(y) * std::size_t(_width) * srcBpp, srcBpp, _width);
				}
			}
		}
	}

	void MetalTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			return;
		}
		_hasCpuData = true;
		_contentsDirty = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(_format);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= _height) {
				continue;
			}
			std::int32_t dstX = xoffset;
			std::int32_t copyW = width;
			std::int32_t srcX0 = 0;
			if (dstX < 0) {
				srcX0 = -dstX;
				copyW += dstX;
				dstX = 0;
			}
			if (dstX + copyW > _width) {
				copyW = _width - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = _pixels.data() + std::size_t(dstY) * _strideBytes + std::size_t(dstX) * dstBpp;
			CopyExpandRow(dstRow, dstBpp, srcRow, srcBpp, copyW);
		}
	}

	void MetalTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void MetalTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void MetalTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(xoffset);
		static_cast<void>(yoffset);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(format);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void MetalTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels == nullptr) {
			return;
		}

		// A render target's contents only exist on the GPU (draws never touch the host store), so read them
		// back through a shared buffer with a one-time command buffer. Readback is a screenshot-rate operation,
		// so the synchronous wait is acceptable. The frame's work so far is committed first, so the readback
		// observes every draw issued before it (Metal executes only committed command buffers).
		if (_isRenderTarget && _gpuTexture != nullptr && _width > 0 && _height > 0) {
			MTL::Device* device = MtlDevice();
			if (device != nullptr) {
				MtlFlushFrameForReadback();
				const std::size_t bytesPerRow = std::size_t(_width) * 4;
				const std::size_t byteSize = bytesPerRow * std::size_t(_height);
				MTL::Buffer* staging = device->newBuffer(NS::UInteger(byteSize), MTL::ResourceStorageModeShared);
				if (staging != nullptr) {
					MTL::Texture* texture = static_cast<MTL::Texture*>(_gpuTexture);
					MtlRunOneTimeCommands([&](MTL::CommandBuffer* cb) {
						MTL::BlitCommandEncoder* blit = cb->blitCommandEncoder();
						blit->copyFromTexture(texture, 0, 0, MTL::Origin::Make(0, 0, 0),
							MTL::Size::Make(NS::UInteger(_width), NS::UInteger(_height), 1),
							staging, 0, NS::UInteger(bytesPerRow), NS::UInteger(byteSize));
						blit->endEncoding();
					});
					std::memcpy(pixels, staging->contents(), byteSize);
					staging->release();
					return;
				}
			}
		}

		if (!_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void MetalTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void MetalTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void MetalTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void MetalTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		const bool changed = (_swizzle[0] != r || _swizzle[1] != g || _swizzle[2] != b || _swizzle[3] != a);
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
		if (changed && _gpuTexture != nullptr) {
			// Applied through a texture view created with the texture (and the pixel-format-view usage a
			// swizzled view needs), so a change rebuilds the texture; the host store re-uploads its texels
			ReleaseGpu();
			if (_hasCpuData) {
				_contentsDirty = true;
			}
		}
	}

	void MetalTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void MetalTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void MetalTexture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
