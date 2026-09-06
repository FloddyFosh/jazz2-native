#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::Metal
{
	class MetalTexture;

	/**
		@brief Renderbuffer stub of the Metal backend (aliased as `RHI::Renderbuffer`)

		Carries no depth/stencil storage (the renderer is 2D); the class just records the format and
		size to satisfy the contract alias. A depth/stencil texture could be added here when needed.
	*/
	class MetalRenderbuffer
	{
	public:
		MetalRenderbuffer() = default;
		void Create(DepthStencilFormat format, std::int32_t width, std::int32_t height) {
			_format = format;
			_width = width;
			_height = height;
		}
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}

	private:
		DepthStencilFormat _format = DepthStencilFormat::None;
		std::int32_t _width = 0;
		std::int32_t _height = 0;
	};

	/**
		@brief Framebuffer stub of the Metal backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		Off-screen rendering is routed through @ref MetalRenderTarget instead.
	*/
	class MetalFramebuffer
	{
	public:
		MetalFramebuffer() = default;
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}
		bool Bind() const {
			return true;
		}
		static bool Unbind() {
			return true;
		}
	};

	/**
		@brief Off-screen render target of the Metal backend (aliased as `RHI::RenderTarget`)

		Records the color textures addressed by attachment index and an optional depth/stencil (ignored for
		2D). @ref BindDraw() records the target on the device so the following clears and draws are associated
		with its color attachments. Metal has no framebuffer object: the device builds an
		`MTL::RenderPassDescriptor` over every contiguously attached color texture whenever it opens a render
		command encoder for the draws routed here (so multiple render targets work).
	*/
	class MetalRenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		MetalRenderTarget();
		~MetalRenderTarget();

		MetalRenderTarget(const MetalRenderTarget&) = delete;
		MetalRenderTarget& operator=(const MetalRenderTarget&) = delete;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(MetalTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage is created) */
		void AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height);
		/** @brief Clears the recorded depth/stencil buffer */
		void DetachDepthStencil(DepthStencilFormat format);

		/** @brief Binds the render target as the current draw target on the device */
		void BindDraw();
		/** @brief Unbinds any render target from the device */
		static void UnbindDraw();
		/** @brief Sets the number of color attachments enabled for drawing */
		bool SetDrawBuffers(std::uint32_t numColorAttachments);

		/** @brief Returns `true` if the target has a usable color attachment 0 */
		bool IsStatusComplete();

		/** @brief Hints that the depth/stencil contents are no longer needed (no-op) */
		void InvalidateDepthStencil(DepthStencilFormat format);

		/** @brief Sets a debug label for the render target (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the texture attached at the given color attachment index, or `nullptr` */
		inline MetalTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}

		/** @brief Returns the number of contiguously attached color textures starting from attachment 0 (the render pass attachment count) */
		std::uint32_t GetAttachedCount() const;

		/** @brief Returns the number of color attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return _numDrawBuffers;
		}

		/** @brief Monotonic counter bumped on every attachment change (the device's open render pass is invalidated when it changes) */
		inline std::uint32_t GetAttachmentGeneration() const {
			return _generation;
		}

	private:
		MetalTexture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;
		std::uint32_t _generation;
	};
}
