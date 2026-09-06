# iOS dependencies, built from source
#
# The iOS SDK ships zlib and the system frameworks (Metal, OpenGLES, UIKit, Security, ...) and nothing else the
# game needs: no SDL2, no OpenAL, no Ogg/Vorbis, no libcurl. There is no prebuilt "jazz2-libraries" archive for
# iOS either - unlike macOS, where the frameworks are fetched ready-made - because every static library has to
# match the exact SDK, architecture (device arm64 / simulator x86_64 or arm64) and deployment target of the
# build, so the only arrangement that always agrees with the build is to compile them as part of it. Each one
# below is fetched as source (like lz4, Zstd, AngelScript and libopenmpt already are on every platform) and
# either added as a subproject with its own CMake build or compiled directly into a static target.
#
# Every target this file creates carries the same name the find modules would have produced, and the matching
# `*_FOUND` variable is set, so the rest of the build (ncine_extra_sources.cmake, ncine_compiler_options.cmake)
# links them without knowing where they came from. What is NOT fetched here follows the platform's own
# rules: libopenmpt, lz4 and Zstd through their find modules (which compile them from source when no library
# is found, and none is found in an iOS SDK), zlib from the SDK.
#
# All of it is gated on NCINE_DOWNLOAD_DEPENDENCIES: without it the same targets can be provided by hand
# (e.g. from a `Libs/` tree), but the game does not look for them anywhere - the find modules assume a
# host system and would match the Mac's own libraries, which do not link into an iOS binary.

if(NOT IOS)
	return()
endif()

if(NOT NCINE_DOWNLOAD_DEPENDENCIES)
	message(WARNING "NCINE_DOWNLOAD_DEPENDENCIES is off, the iOS dependencies (SDL2, OpenAL Soft, Ogg/Vorbis, libcurl) have to be provided as CMake targets by hand")
	return()
endif()

if(CMAKE_VERSION VERSION_LESS "3.18.0")
	message(FATAL_ERROR "The iOS build requires CMake 3.18.0 or newer to download its dependencies")
endif()

include(FetchContent)
include(ncine_helpers)

# ── SDL2 ─────────────────────────────────────────────────────────────────────────────────────────────
# The window, touch, game-controller and lifecycle backend of the port (see Docs/Building.dox). Built as a
# static library together with SDL2main, whose iOS variant provides the real `main()` - it starts the
# UIApplication and calls the game's `main()` (renamed to `SDL_main` by <SDL_main.h>, see Sources/Main.cpp)
# from the application delegate once UIKit is up. SDL's own CMake aliases `SDL2::SDL2` to the static library;
# SDL2main is linked next to it by the SDL backend arm in ncine_extra_sources.cmake (see there).
if(NOT TARGET SDL2::SDL2)
	set(SDL2_URL "https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz")
	message(STATUS "Downloading dependencies from \"${SDL2_URL}\"...")
	FetchContent_Declare(
		Sdl2Git
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${SDL2_URL}
	)

	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_TEST OFF CACHE BOOL "" FORCE)
	set(SDL_TESTS OFF CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
	set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
	# Subsystems the game never touches; the audio one in particular, because OpenAL Soft's CoreAudio backend
	# owns the audio session and a second client of it would only compete for the output
	set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
	set(SDL_RENDER OFF CACHE BOOL "" FORCE)
	set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
	if(NCINE_PREFERRED_RHI STREQUAL "Software")
		# The software rasterizer presents through SDL_Renderer (see SdlGfxDevice::initSoftwarePresent)
		set(SDL_RENDER ON CACHE BOOL "" FORCE)
	endif()
	# SDL's EAGL context support is only wanted by the OpenGL|ES backend; leaving it in would make a Metal build
	# link the deprecated OpenGLES framework for nothing
	if(NCINE_PREFERRED_RHI STREQUAL "OpenGL")
		set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
	else()
		set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
	endif()
	FetchContent_MakeAvailable(Sdl2Git)

	foreach(_sdlTarget SDL2-static SDL2main)
		if(TARGET ${_sdlTarget})
			set_target_properties(${_sdlTarget} PROPERTIES FOLDER "Dependencies")
		endif()
	endforeach()

	if(NOT TARGET SDL2::SDL2)
		# (SDL 2.32 creates this alias itself when no shared library is built; older releases do not)
		add_library(SDL2::SDL2 ALIAS SDL2-static)
	endif()
	set(SDL2_INCLUDE_DIR "${sdl2git_SOURCE_DIR}/include")
	set(SDL2_FOUND 1)
	set(SDL2_STATIC TRUE)
	mark_as_advanced(SDL2_STATIC)
endif()

# ── OpenAL Soft ──────────────────────────────────────────────────────────────────────────────────────
# iOS does carry an OpenAL.framework, but it has been deprecated since iOS 12 and lacks every ALC_SOFT/AL_SOFT
# extension the engine's OpenAL backend queries (the source resampler, reopen-device and the disconnect
# behavior), so the same OpenAL Soft the macOS build ships as openal.framework is compiled here, with its
# CoreAudio backend. Static, which the library's own headers are told about through the AL_LIBTYPE_STATIC
# definition its target exports (see CommonHeaders.h for the include paths used on iOS).
if(NCINE_WITH_AUDIO AND NOT TARGET OpenAL::OpenAL)
	set(OPENALSOFT_URL "https://github.com/kcat/openal-soft/archive/refs/tags/1.24.3.tar.gz")
	message(STATUS "Downloading dependencies from \"${OPENALSOFT_URL}\"...")
	FetchContent_Declare(
		OpenAlSoftGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${OPENALSOFT_URL}
	)

	set(LIBTYPE "STATIC" CACHE STRING "" FORCE)
	set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
	set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(ALSOFT_INSTALL_UTILS OFF CACHE BOOL "" FORCE)
	set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)
	set(ALSOFT_EMBED_HRTF_DATA OFF CACHE BOOL "" FORCE)
	set(ALSOFT_BACKEND_COREAUDIO ON CACHE BOOL "" FORCE)
	set(ALSOFT_REQUIRE_COREAUDIO ON CACHE BOOL "" FORCE)
	set(ALSOFT_BACKEND_WAVE OFF CACHE BOOL "" FORCE)
	set(ALSOFT_BACKEND_SDL2 OFF CACHE BOOL "" FORCE)
	set(ALSOFT_BACKEND_SDL3 OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(OpenAlSoftGit)

	foreach(_alTarget OpenAL alcommon al-excommon)
		if(TARGET ${_alTarget})
			set_target_properties(${_alTarget} PROPERTIES FOLDER "Dependencies")
		endif()
	endforeach()

	set(OPENAL_INCLUDE_DIR "${openalsoftgit_SOURCE_DIR}/include/AL")
	set(OPENAL_FOUND 1)
	set(OPENAL_STATIC TRUE)
	mark_as_advanced(OPENAL_STATIC)
endif()

# ── Ogg + Vorbis ─────────────────────────────────────────────────────────────────────────────────────
# Compiled directly (the same way FindLz4/FindZstd do it) rather than as subprojects: libvorbis' own CMake
# insists on a find_package(Ogg) that a sibling subproject cannot satisfy without pretending to be an
# installed library. Only the decoder side is needed, so vorbisenc.c and the psytune/barkmel/tone tools stay
# out. Ogg's `config_types.h` is normally generated by its configure run; the sizes are the <stdint.h> ones on
# every Apple platform, so it is written here.
if(NCINE_WITH_AUDIO AND NCINE_WITH_VORBIS AND NOT TARGET Vorbis::Vorbisfile)
	set(LIBOGG_URL "https://github.com/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.tar.gz")
	set(LIBVORBIS_URL "https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz")
	message(STATUS "Downloading dependencies from \"${LIBOGG_URL}\"...")
	# SOURCE_SUBDIR points at a directory without a CMakeLists.txt on purpose: FetchContent_MakeAvailable() then only
	# downloads and extracts, without adding the libraries' own CMake projects (CMake 3.18+)
	FetchContent_Declare(
		LiboggGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${LIBOGG_URL}
		SOURCE_SUBDIR "doc"
	)
	message(STATUS "Downloading dependencies from \"${LIBVORBIS_URL}\"...")
	FetchContent_Declare(
		LibvorbisGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${LIBVORBIS_URL}
		SOURCE_SUBDIR "doc"
	)
	FetchContent_MakeAvailable(LiboggGit LibvorbisGit)

	set(OGG_DIR "${libogggit_SOURCE_DIR}")
	set(OGG_INCLUDE_DIR "${OGG_DIR}/include")
	set(VORBIS_DIR "${libvorbisgit_SOURCE_DIR}")
	set(VORBIS_INCLUDE_DIR "${VORBIS_DIR}/include")

	set(INCLUDE_INTTYPES_H 1)
	set(INCLUDE_STDINT_H 1)
	set(INCLUDE_SYS_TYPES_H 1)
	set(SIZE16 int16_t)
	set(USIZE16 uint16_t)
	set(SIZE32 int32_t)
	set(USIZE32 uint32_t)
	set(SIZE64 int64_t)
	set(USIZE64 uint64_t)
	configure_file("${OGG_INCLUDE_DIR}/ogg/config_types.h.in" "${OGG_INCLUDE_DIR}/ogg/config_types.h" @ONLY)

	ncine_add_dependency(Ogg STATIC)
	set_target_properties(Ogg PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${OGG_INCLUDE_DIR})
	set(OGG_SOURCES
		${OGG_DIR}/src/bitwise.c
		${OGG_DIR}/src/framing.c
	)
	ncine_assign_source_group(PATH_PREFIX ${OGG_DIR} FILES ${OGG_SOURCES} SKIP_EXTERNAL)
	target_sources(Ogg PRIVATE ${OGG_SOURCES})
	target_include_directories(Ogg PRIVATE "${OGG_INCLUDE_DIR}")

	ncine_add_dependency(Vorbis STATIC)
	set_target_properties(Vorbis PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES ${VORBIS_INCLUDE_DIR})
	set(VORBIS_SOURCES
		${VORBIS_DIR}/lib/analysis.c
		${VORBIS_DIR}/lib/bitrate.c
		${VORBIS_DIR}/lib/block.c
		${VORBIS_DIR}/lib/codebook.c
		${VORBIS_DIR}/lib/envelope.c
		${VORBIS_DIR}/lib/floor0.c
		${VORBIS_DIR}/lib/floor1.c
		${VORBIS_DIR}/lib/info.c
		${VORBIS_DIR}/lib/lookup.c
		${VORBIS_DIR}/lib/lpc.c
		${VORBIS_DIR}/lib/lsp.c
		${VORBIS_DIR}/lib/mapping0.c
		${VORBIS_DIR}/lib/mdct.c
		${VORBIS_DIR}/lib/psy.c
		${VORBIS_DIR}/lib/registry.c
		${VORBIS_DIR}/lib/res0.c
		${VORBIS_DIR}/lib/sharedbook.c
		${VORBIS_DIR}/lib/smallft.c
		${VORBIS_DIR}/lib/synthesis.c
		${VORBIS_DIR}/lib/vorbisfile.c
		${VORBIS_DIR}/lib/window.c
	)
	ncine_assign_source_group(PATH_PREFIX ${VORBIS_DIR} FILES ${VORBIS_SOURCES} SKIP_EXTERNAL)
	target_sources(Vorbis PRIVATE ${VORBIS_SOURCES})
	target_include_directories(Vorbis PRIVATE "${VORBIS_INCLUDE_DIR}" "${VORBIS_DIR}/lib")
	target_link_libraries(Vorbis PUBLIC Ogg)
	# The library's own build defines these on Unix; without them the decoder falls back to slower paths
	target_compile_definitions(Vorbis PRIVATE "HAVE_ALLOCA_H" "USE_MEMORY_H")

	# The imported-target names the audio backend links (see ncine_imported_targets.cmake); the one target
	# above already carries vorbisfile, so all three aliases point at it
	add_library(Ogg::Ogg ALIAS Ogg)
	add_library(Vorbis::Vorbis ALIAS Vorbis)
	add_library(Vorbis::Vorbisfile ALIAS Vorbis)
	set(VORBIS_FOUND 1)
	set(VORBIS_STATIC TRUE)
	mark_as_advanced(VORBIS_STATIC)
endif()

# ── libcurl ──────────────────────────────────────────────────────────────────────────────────────────
# Only the online features need it (the update check and the public server list, see `WebRequest`); the ENet
# transport of the multiplayer itself does not. Built over SecureTransport, the system TLS, so the device's
# own root store is used and no certificate bundle has to be shipped - which is also why the version is
# pinned: SecureTransport support is deprecated in libcurl and later releases may drop it, and the
# replacement (its own TLS library) would bring a bundle along. HTTP(S) only, nothing else is used.
if(WITH_ONLINE_MULTIPLAYER AND NOT TARGET CURL::libcurl)
	set(CURL_URL "https://github.com/curl/curl/releases/download/curl-8_11_1/curl-8.11.1.tar.gz")
	message(STATUS "Downloading dependencies from \"${CURL_URL}\"...")
	FetchContent_Declare(
		CurlGit
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		URL ${CURL_URL}
	)

	set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
	set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
	set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
	set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
	set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
	set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
	set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
	set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
	set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
	set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
	set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
	set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
	set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
	set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
	set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
	set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
	set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
	set(CURL_ZLIB ON CACHE BOOL "" FORCE)
	set(HTTP_ONLY ON CACHE BOOL "" FORCE)
	set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
	set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
	set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
	set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(CurlGit)

	if(TARGET libcurl_static)
		set_target_properties(libcurl_static PROPERTIES FOLDER "Dependencies")
	endif()
	set(CURL_INCLUDE_DIR "${curlgit_SOURCE_DIR}/include")
	set(CURL_FOUND 1)
endif()
