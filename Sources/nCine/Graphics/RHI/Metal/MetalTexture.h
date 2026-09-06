#pragma once

#include "../RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::Metal
{
	/**
		@brief Texture object of the Metal backend (aliased as `RHI::Texture`)

		Wraps a single, tightly-packed level-0 pixel buffer in host memory and exposes the neutral upload
		surface `Texture.cpp` drives (`TexImage2D`, `TexSubImage2D`, `TexStorage2D`, filter/wrap/swizzle
		setters). It materializes the real `MTL::Texture` + `MTL::SamplerState` from this surface: every
		texture lives in GPU-private storage, CPU textures are filled through a staging-buffer blit the device
		records into the frame's command buffer right before the draw that samples them (so a texture
		re-uploaded every frame - the palette - lands at exactly the point of the frame the GL upload would,
		with no CPU/GPU race), render targets are colour attachments. The sampling swizzle is applied through
		a texture view (`MTL::TextureSwizzleChannels`, no texel baking, like Vulkan's component mapping). Mip
		levels above 0 and compressed formats are accepted but not stored.
	*/
	class MetalTexture
	{
	public:
		/** @brief Number of texture units tracked by the device */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit MetalTexture(TextureTarget target);
		~MetalTexture();

		MetalTexture(const MetalTexture&) = delete;
		MetalTexture& operator=(const MetalTexture&) = delete;

		// -- GPU accessors (used by the device draw / render-pass path). Handles are opaque pointers so this
		// contract header stays free of metal-cpp; the .cpp casts them. --

		/** @brief Materializes the `MTL::Texture` / view / sampler if missing (does NOT upload texels; see @ref RecordStreamingUpload()) */
		void EnsureGpu() const;
		/** @brief `true` if CPU texels are pending upload to the GPU texture (a streaming update not yet recorded) */
		inline bool HasPendingUpload() const {
			return _contentsDirty && _hasCpuData && !_isRenderTarget && !_pixels.empty();
		}
		/** @brief Records the pending CPU->GPU texel upload (staging buffer + blit of the whole level 0) into @p commandBuffer (an opaque `MTL::CommandBuffer*`, outside any render encoder) */
		void RecordStreamingUpload(void* commandBuffer) const;
		/** @brief Returns the `MTL::Texture*` (as `void*`), creating it on demand; `nullptr` if unavailable */
		void* GpuTexture() const;
		/** @brief Returns the `MTL::Texture*` to sample from (the swizzle view, or the texture itself for an identity swizzle) */
		void* GpuView() const;
		/** @brief Returns the `MTL::SamplerState*` matching the current filter/wrap */
		void* GpuSampler() const;
		/** @brief Returns the `MTL::PixelFormat` of the texture (as an integer) */
		std::uint32_t GpuFormat() const;
		/** @brief Releases the GPU texture / view / sampler (on re-allocation and destruction) */
		void ReleaseGpu() const;

		/** @brief Returns a backend-neutral identifier uniquely identifying the texture (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
			return _handle;
		}
		/** @brief Returns the texture target */
		inline TextureTarget GetTarget() const {
			return _target;
		}

		/** @brief Returns the width of level 0 in texels */
		inline std::int32_t GetWidth() const {
			return _width;
		}
		/** @brief Returns the height of level 0 in texels */
		inline std::int32_t GetHeight() const {
			return _height;
		}
		/** @brief Returns the pixel format of the stored texels (after any promotion to the RGBA8 store) */
		inline PixelFormat GetFormat() const {
			return _format;
		}
		/** @brief Returns the original upload format before promotion (R8/RG8/RGB8 kept) */
		inline PixelFormat GetUploadFormat() const {
			return _uploadFormat;
		}
		/** @brief Returns the byte distance between two consecutive rows of level 0 */
		inline std::int32_t GetStrideBytes() const {
			return _strideBytes;
		}
		/** @brief Returns the base pointer of the level-0 texel store (may be `nullptr` before an upload); the single-level store ignores @p level */
		inline const std::uint8_t* GetPixels(std::int32_t level = 0) const {
			static_cast<void>(level);
			return _pixels.empty() ? nullptr : _pixels.data();
		}
		/** @brief Returns the horizontal texture-coordinate wrap mode */
		inline SamplerWrapping GetWrapS() const {
			return _wrap;
		}
		/** @brief Returns the vertical texture-coordinate wrap mode (single stored mode, same as @ref GetWrapS()) */
		inline SamplerWrapping GetWrapT() const {
			return _wrap;
		}
		/** @brief Returns the four-channel sampling swizzle (identity by default) */
		inline const SwizzleChannel* GetSwizzle() const {
			return _swizzle;
		}
		/** @brief Returns the magnification filter (alias of @ref GetMagFiltering()) */
		inline nCine::SamplerFilter GetMagFilter() const {
			return _magFilter;
		}
		/** @brief Returns `true` if the texture is bound as a color render target */
		inline bool IsRenderTarget() const {
			return _isRenderTarget;
		}
		/** @brief Marks the texture as (or no longer as) a color render target (rebuilds the GPU texture with the render-target usage when it changes) */
		inline void SetRenderTarget(bool isRenderTarget) {
			if (isRenderTarget != _isRenderTarget) {
				_isRenderTarget = isRenderTarget;
				ReleaseGpu();
			}
		}
		/** @brief Returns a writable base pointer of the level-0 texel store (for render-target output) */
		inline std::uint8_t* MutablePixels() {
			return _pixels.empty() ? nullptr : _pixels.data();
		}

		/** @brief Binds the texture to the specified texture unit on the device */
		bool Bind(std::uint32_t textureUnit) const;
		/** @brief Binds the texture to texture unit 0 */
		inline bool Bind() const {
			return Bind(0);
		}
		/** @brief Unbinds the texture from the unit it was last bound to */
		bool Unbind() const;
		/** @brief Unbinds any texture from the specified texture unit */
		static bool Unbind(std::uint32_t textureUnit);

		/** @brief Allocates level-0 storage of the given format/size and optionally uploads its texels */
		void TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data);
		/** @brief Updates a rectangular subregion of level 0 */
		void TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data);
		/** @brief Allocates immutable level-0 storage of the given format/size (no texels yet) */
		void TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height);
		/** @brief Compressed upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Compressed sub-upload (unsupported by this backend, accepted as a no-op) */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Reads back level-0 texels into client memory */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filter (stored) */
		void SetMinFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the magnification filter (stored) */
		void SetMagFiltering(nCine::SamplerFilter filter);
		/** @brief Sets the wrap mode (stored) */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Sets the sampling swizzle (stored; rebuilds the GPU texture view) */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest defined mipmap level (ignored) */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the client pixel-row alignment of uploads (ignored, uploads are tightly packed) */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a debug label for the texture (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the magnification filter */
		inline nCine::SamplerFilter GetMagFiltering() const {
			return _magFilter;
		}

		static bool SupportsImmutableStorage() {
			return false;
		}
		static bool SupportsTextureReadback() {
			return true;
		}
		static void ClearErrors() {}
		static bool CheckErrors() {
			return false;
		}
		static void CheckFormatSupport(PixelFormat format) {
			static_cast<void>(format);
		}

		/** @brief Returns the number of bytes occupied by one texel of the given format (0 if unsupported) */
		static std::int32_t BytesPerPixel(PixelFormat format);

	private:
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		TextureTarget _target;
		PixelFormat _format;
		PixelFormat _uploadFormat;
		std::int32_t _width;
		std::int32_t _height;
		std::int32_t _strideBytes;
		nCine::SamplerFilter _minFilter;
		nCine::SamplerFilter _magFilter;
		SamplerWrapping _wrap;
		SwizzleChannel _swizzle[4];
		mutable std::uint32_t _textureUnit;
		std::vector<std::uint8_t> _pixels;
		bool _isRenderTarget;

		// GPU objects, created lazily from the host store (mutable so the const bind-time accessors can
		// materialize them). Opaque pointers so this header needs no metal-cpp. _contentsDirty forces a texel
		// re-upload; the view is rebuilt together with the texture when the swizzle changes.
		mutable void* _gpuTexture;
		mutable void* _gpuView;
		mutable void* _gpuSampler;
		mutable std::uint32_t _gpuFormat;
		mutable bool _contentsDirty;
		mutable bool _hasCpuData;
		mutable nCine::SamplerFilter _samplerFilter;
		mutable SamplerWrapping _samplerWrap;

		void Allocate(PixelFormat format, std::int32_t width, std::int32_t height);
		bool HasIdentitySwizzle() const;
	};
}
