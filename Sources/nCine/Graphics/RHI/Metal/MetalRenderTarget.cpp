#include "MetalRenderTarget.h"
#include "MetalDevice.h"
#include "MetalTexture.h"

namespace nCine::RHI::Metal
{
	MetalRenderTarget::MetalRenderTarget()
		: _numDrawBuffers(1), _generation(0)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	MetalRenderTarget::~MetalRenderTarget()
	{
		// Clear from the device first so a destroyed render target can't dangle as the current one
		MetalDevice::UnbindRenderTarget(this);
	}

	std::uint32_t MetalRenderTarget::GetAttachedCount() const
	{
		std::uint32_t count = 0;
		while (count < MaxColorAttachments && _colorTextures[count] != nullptr) {
			count++;
		}
		return count;
	}

	void MetalRenderTarget::AttachColorTexture(MetalTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			texture.SetRenderTarget(true);
			_generation++;
			MetalDevice::OnRenderTargetChanged(this);
		}
	}

	void MetalRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
			_generation++;
			MetalDevice::OnRenderTargetChanged(this);
		}
	}

	void MetalRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void MetalRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void MetalRenderTarget::BindDraw()
	{
		MetalDevice::SetRenderTarget(this);
	}

	void MetalRenderTarget::UnbindDraw()
	{
		MetalDevice::SetRenderTarget(nullptr);
	}

	bool MetalRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool MetalRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
	}

	void MetalRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void MetalRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
