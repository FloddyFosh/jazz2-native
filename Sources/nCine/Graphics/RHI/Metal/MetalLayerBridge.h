#pragma once

// The CAMetalLayer operations MetalDevice.cpp needs but metal-cpp does not wrap, implemented in Objective-C++ in
// MetalLayerBridge.mm. This header is the one thing both sides include: it names the metal-cpp classes only as
// forward declarations, so the Objective-C++ side (which includes Apple's own Metal/QuartzCore headers and must
// never see metal-cpp) can compile it too. The layer itself comes from SDL (SDL_Metal_GetLayer) as an opaque
// pointer, and the objects handed back are the very same Objective-C object pointers metal-cpp wraps.

#include <cstdint>

namespace MTL { class Device; }
namespace CA { class MetalDrawable; }

namespace nCine::RHI::Metal
{
	/** @brief Points the layer at @p device, sets its pixel format to BGRA8Unorm and its vertical-sync mode */
	void MtlLayerSetup(void* layer, MTL::Device* device, bool vsync);
	/** @brief Sets the size in pixels of the drawables the layer hands out */
	void MtlLayerSetDrawableSize(void* layer, std::int32_t width, std::int32_t height);
	/** @brief Acquires the next drawable (blocks while all are in flight; `nullptr` when none becomes available). The caller owns one reference. */
	CA::MetalDrawable* MtlLayerNextDrawable(void* layer);
	/** @brief `MTL::PixelFormat` of the layer's drawables (as an integer, matching the present pipeline's attachment) */
	std::uint32_t MtlLayerPixelFormat(void* layer);
}
