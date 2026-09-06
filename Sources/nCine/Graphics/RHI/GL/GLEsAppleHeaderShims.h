#pragma once

// Compile-compatibility shims for building the OpenGL|ES 3.0 profile against Apple's OpenGLES framework
// headers (`OpenGLES/ES3/gl.h` + `OpenGLES/ES3/glext.h`) on iOS. Included from CommonHeaders.h only on that
// path. Apple's `glext.h` declares just the extensions its own GPUs implement, so the compressed-texture
// enums of formats the engine can name but iOS hardware never had (S3TC, ATC, ETC1) are missing, and the
// texture-format switch that maps every `PixelFormat` to a GL enum would not compile. Every macro below is
// the canonical Khronos value; none of them is ever requested from the driver on iOS, because no iOS asset
// carries these formats (and a request for an unsupported format would simply fail with GL_INVALID_ENUM).

#if !defined(DEATH_TARGET_IOS)
#	error GLEsAppleHeaderShims.h must only be included on iOS
#endif

// GL_EXT_texture_compression_s3tc
#if !defined(GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
#	define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#if !defined(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
#	define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#if !defined(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
#	define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#if !defined(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
#	define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// GL_AMD_compressed_ATC_texture
#if !defined(GL_ATC_RGB_AMD)
#	define GL_ATC_RGB_AMD 0x8C92
#endif
#if !defined(GL_ATC_RGBA_EXPLICIT_ALPHA_AMD)
#	define GL_ATC_RGBA_EXPLICIT_ALPHA_AMD 0x8C93
#endif
#if !defined(GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD)
#	define GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD 0x87EE
#endif

// GL_OES_compressed_ETC1_RGB8_texture
#if !defined(GL_ETC1_RGB8_OES)
#	define GL_ETC1_RGB8_OES 0x8D64
#endif
