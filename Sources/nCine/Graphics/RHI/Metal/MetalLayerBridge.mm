// The one Objective-C++ translation unit of the Metal backend: the CAMetalLayer operations MetalDevice.cpp needs
// but metal-cpp does not wrap. It includes Apple's Objective-C Metal/QuartzCore headers and NOT metal-cpp, so the
// two views of the same classes never meet in one file; objects cross the boundary as `void*` / the metal-cpp
// pointer types, which are the very same Objective-C object pointers underneath (metal-cpp's classes are
// thin wrappers over `id`), so a plain cast is the bridge.
//
// Compiled without ARC: the C++ side owns whatever this file hands out with an explicit `retain`, and releases
// it through metal-cpp's `release()` - one ownership model on both sides of the boundary.

#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "MetalLayerBridge.h"

namespace nCine::RHI::Metal
{
	void MtlLayerSetup(void* layer, MTL::Device* device, bool vsync)
	{
		CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
		metalLayer.device = (id<MTLDevice>)device;
		metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		// The present pass RENDERS into the drawable (a fullscreen triangle sampling the screen texture), which
		// framebufferOnly permits; it only rules out sampling from / blitting the drawable itself
		metalLayer.framebufferOnly = YES;
#if TARGET_OS_OSX
		if (@available(macOS 10.13, *)) {
			metalLayer.displaySyncEnabled = vsync ? YES : NO;
		}
#else
		// iOS presents in step with the display regardless; the layer has no switch for it
		(void)vsync;
#endif
		// Three drawables let the CPU record a frame while one is on screen and one is queued; nextDrawable
		// blocks when all are in flight, which is what paces the game to the display under vsync
		metalLayer.maximumDrawableCount = 3;
	}

	void MtlLayerSetDrawableSize(void* layer, std::int32_t width, std::int32_t height)
	{
		CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
		metalLayer.drawableSize = CGSizeMake(width, height);
	}

	CA::MetalDrawable* MtlLayerNextDrawable(void* layer)
	{
		CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
		id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
		if (drawable == nil) {
			return nullptr;
		}
		[drawable retain];		// the C++ side releases it through metal-cpp once the frame is committed
		return (CA::MetalDrawable*)drawable;
	}

	std::uint32_t MtlLayerPixelFormat(void* layer)
	{
		CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
		return (std::uint32_t)metalLayer.pixelFormat;
	}
}

#endif
