#pragma once

// Backend-internal header of the Metal RHI. Unlike the contract headers (MetalDevice.h, MetalTexture.h, ...) -
// which stay free of metal-cpp because they are pulled in across the whole render pipeline through Rhi.h - this
// header includes metal-cpp (Apple's header-only C++ bindings of the Metal, Foundation and QuartzCore
// frameworks) and is included only by the Metal backend translation units. The one Objective-C++ file of the
// backend, MetalLayerBridge.mm, deliberately does NOT include it: it talks to the CAMetalLayer through Apple's
// own Objective-C headers and hands the layer's objects across as `void*`, so neither side ever sees the
// other's view of the same class.

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <cstdint>

namespace nCine::RHI::Metal
{
	// -- Shared device context accessors (defined in MetalDevice.cpp) --

	/** @brief The device, or `nullptr` before creation / after teardown */
	MTL::Device* MtlDevice();
	/** @brief The one command queue every frame and one-time command buffer is created from */
	MTL::CommandQueue* MtlQueue();
	/** @brief `true` when the GPU shares memory with the CPU (Apple silicon), so `Shared` textures replace `Managed` ones */
	bool MtlHasUnifiedMemory();

	/**
		@brief Makes the frame's work so far visible to a readback

		Metal command buffers execute only once committed, and the frame's buffer is committed at present time; a
		texture read back mid-frame would therefore see the previous frame's contents. This ends the open encoder,
		commits what was recorded, waits for it and opens a fresh command buffer for the rest of the frame - the
		readback then observes every draw issued before it, exactly like `glGetTexImage` after a GL draw. A no-op
		when no frame is active.
	*/
	void MtlFlushFrameForReadback();

	/**
		@brief Runs @p record on a one-time command buffer and waits for it

		Used by the resource classes for out-of-frame GPU work (the render-target readback copy). @p record
		receives the command buffer to encode into; the function commits it and blocks until it has completed.
		Does nothing when the device is not ready.
	*/
	template<class F>
	void MtlRunOneTimeCommands(F&& record)
	{
		MTL::CommandQueue* queue = MtlQueue();
		if (queue == nullptr) {
			return;
		}
		NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
		MTL::CommandBuffer* cb = queue->commandBuffer();
		if (cb != nullptr) {
			record(cb);
			cb->commit();
			cb->waitUntilCompleted();
		}
		pool->release();
	}

}

// The CAMetalLayer bridge (Objective-C++, MetalLayerBridge.mm): the layer comes from SDL (SDL_Metal_GetLayer) as an
// opaque pointer, and metal-cpp has no CAMetalLayer wrapper of its own
#include "MetalLayerBridge.h"
