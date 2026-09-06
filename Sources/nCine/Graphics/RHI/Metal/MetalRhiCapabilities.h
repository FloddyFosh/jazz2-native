#pragma once

#include "../RhiCapabilitiesBase.h"
#include "MetalDevice.h"
#include "MetalRenderTarget.h"
#include "MetalTexture.h"

namespace nCine::RHI::Metal
{
	/**
		@brief Runtime capabilities of the Metal backend

		Metal has no queryable API context of the OpenGL kind, so the limits come from the device the window
		backend has already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class MetalRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		MetalRhiCapabilities()
		{
			// Limits of the MTL::Device selected at device creation: the largest 2D texture the GPU family
			// supports (16384 on every Mac), the 64 KB uniform range the batch size is derived from (Metal
			// constant buffers have no such limit, but the offline MSL bakes BATCH_SIZE from exactly this
			// budget, so the runtime must derive the same count - see MslEmitter) and the 256-byte constant
			// buffer offset alignment of macOS GPUs, which the per-frame uniform ring honors. The render passes
			// drive up to MetalRenderTarget::MaxColorAttachments colour attachments.
			SetDeviceCapabilities("Metal", MetalDevice::GetMaxTextureDimension(), std::int32_t(MetalTexture::MaxTextureUnits),
				MetalDevice::GetMaxUniformBufferRange(), MetalDevice::GetUniformBufferOffsetAlignment(),
				std::int32_t(MetalRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
