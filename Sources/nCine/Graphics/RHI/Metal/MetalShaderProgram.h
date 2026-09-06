#pragma once

#include "MetalShaderTypes.h"
#include "MetalVertexFormat.h"
#include "../RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

#include <Containers/ArrayView.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace ShaderCompiler
{
	struct ProgramVariant;
}

namespace nCine::RHI::Metal
{
	class MetalBufferObject;
	class MetalShaderUniforms;
	class MetalShaderUniformBlocks;

	/**
		@brief Shader program of the Metal backend (aliased as `RHI::ShaderProgram`)

		Carries the offline ShaderCompiler reflection (set with @ref SetReflection() like the OpenGL backend)
		from which it imports uniforms, uniform blocks and attributes so the pipeline's uniform machinery runs.
		It also compiles the reflection's embedded `MslVsSource`/`MslFsSource` into an `MTL::Library` per stage
		at link time (there is no offline MSL compiler outside Xcode; Metal's own compiler service caches the
		result per source across runs) and keeps the two `MTL::Function` objects, and it derives the argument
		table the device binds each draw from the same reflection (the `_Globals` buffer at index 0, then the
		std140 blocks, then the texture/sampler pairs, in reflection order - the scheme the MSL emitter writes).
		The `MTL::RenderPipelineState` objects themselves are cached by the device per vertex format, blend
		state and attachment formats. @ref Use() records the program as current on the device.
	*/
	class MetalShaderProgram
	{
		friend class MetalShaderUniforms;
		friend class MetalShaderUniformBlocks;

	public:
		enum class Introspection
		{
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

		enum class Status
		{
			NotLinked,
			CompilationFailed,
			LinkingFailed,
			Linked,
			LinkedWithDeferredQueries,
			LinkedWithIntrospection
		};

		enum class QueryPhase
		{
			Immediate,
			Deferred
		};

		/** @brief Default batch size, indicating the shader is not batched */
		static constexpr std::int32_t DefaultBatchSize = -1;

		MetalShaderProgram();
		explicit MetalShaderProgram(QueryPhase queryPhase);
		MetalShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		MetalShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		MetalShaderProgram(StringView vertexFile, StringView fragmentFile);
		~MetalShaderProgram();

		MetalShaderProgram(const MetalShaderProgram&) = delete;
		MetalShaderProgram& operator=(const MetalShaderProgram&) = delete;

		/** @brief Returns a backend-neutral identifier uniquely identifying the program (feeds material sort keys; also keys the device's pipeline cache) */
		inline std::uint32_t GetUniqueId() const {
			return _handle;
		}
		inline Status GetStatus() const {
			return _status;
		}
		inline Introspection GetIntrospection() const {
			return _introspection;
		}
		inline QueryPhase GetQueryPhase() const {
			return _queryPhase;
		}
		inline std::uint32_t GetBatchSize() const {
			return _batchSize;
		}
		inline void SetBatchSize(std::uint32_t value) {
			_batchSize = value;
		}

		bool IsLinked() const;

		std::uint32_t RetrieveInfoLogLength() const {
			return 0;
		}
		void RetrieveInfoLog(std::string& infoLog) const {
			static_cast<void>(infoLog);
		}

		inline std::uint32_t GetUniformsSize() const {
			return _uniformsSize;
		}
		inline std::uint32_t GetUniformBlocksSize() const {
			return _uniformBlocksSize;
		}

		bool AttachShaderFromFile(ShaderStage stage, StringView filename);
		bool AttachShaderFromString(ShaderStage stage, StringView string);
		bool AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings);
		bool AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename);

		/** @brief Sets the offline reflection consumed by @ref Link() to import uniforms/blocks/attributes and compile the MSL */
		inline void SetReflection(const ShaderCompiler::ProgramVariant* reflection) {
			_reflection = reflection;
		}
		/**
			@brief Records the true (program, variant) identity of the loaded shader

			Only the fixed-function console backends consume it (they resolve their generated
			effect tables from this identity instead of the object label); this backend runs
			real shaders and ignores it.
		*/
		inline void SetProgramIdentity(const char* programName, const char* variantName) {
			static_cast<void>(programName);
			static_cast<void>(variantName);
		}

		bool Link(Introspection introspection);
		void Use();
		bool Validate() {
			return true;
		}
		bool FinalizeAfterLinking(Introspection introspection);

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(_attributes.size());
		}
		bool HasAttribute(const char* name) const;
		MetalVertexFormat::Attribute* GetAttribute(const char* name);

		inline void DefineVertexFormat(const MetalBufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		inline void DefineVertexFormat(const MetalBufferObject* vbo, const MetalBufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		void DefineVertexFormat(const MetalBufferObject* vbo, const MetalBufferObject* ibo, std::uint32_t vboOffset);

		void Reset();
		void SetObjectLabel(StringView label);

		inline bool GetLogOnErrors() const {
			return _shouldLogOnErrors;
		}
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			_shouldLogOnErrors = shouldLogOnErrors;
		}

		// -- Backend extensions (used by the uniform caches and the device draw path) --

		/** @brief Publishes a committed loose-uniform value pointer for the device to gather into `_Globals` */
		void SetResolvedUniform(const char* name, const std::uint8_t* data);
		/** @brief Returns the last published value pointer of the named loose uniform, or `nullptr` */
		const std::uint8_t* ResolveUniform(const char* name) const;

		/** @brief Returns `true` if the vertex shader reads vertex attributes (needs a vertex descriptor + a vertex buffer) */
		inline bool HasVertexAttributes() const {
			return !_attributes.empty();
		}
		/** @brief Returns the vertex buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const MetalBufferObject* GetBoundVbo() const {
			return _boundVbo;
		}
		/** @brief Returns the index buffer bound by @ref DefineVertexFormat(), or `nullptr` */
		inline const MetalBufferObject* GetBoundIbo() const {
			return _boundIbo;
		}

		// -- Compiled functions and the argument table the device binds each draw. Metal objects are opaque
		// pointers so this contract header stays free of metal-cpp; the .cpp casts them. --

		/** @brief One entry of the program's argument table, matching the offline emitter's binding scheme */
		struct DescriptorBinding
		{
			enum class Kind { Globals, Block, Sampler };
			Kind kind;
			std::uint32_t binding;		// Globals/Block: the [[buffer(N)]] index; Sampler: the [[texture(N)]]/[[sampler(N)]] index
			std::int32_t slot;			// Block: reflection block index (device uniform-range slot); Sampler: texture unit
		};

		/** @brief A loose (default-block) uniform gathered into the "_Globals" buffer, with its std140 placement */
		struct LooseUniform
		{
			String Name;
			std::uint32_t Offset;		// std140 byte offset within _Globals
			std::uint32_t Size;			// byte size of the value
		};

		/** @brief Returns `true` if both stage functions compiled (draws are skipped otherwise) */
		inline bool HasPipelineState() const {
			return (_vsFunction != nullptr && _fsFunction != nullptr);
		}
		/** @brief The vertex-stage `MTL::Function*` (as `void*`) */
		inline void* GetVsFunction() const { return _vsFunction; }
		/** @brief The fragment-stage `MTL::Function*` */
		inline void* GetFsFunction() const { return _fsFunction; }
		/** @brief The argument bindings (in reflection order) the device sets each draw */
		inline const std::vector<DescriptorBinding>& GetDescriptorBindings() const { return _descriptorBindings; }
		/** @brief The loose uniforms gathered into "_Globals" (empty if the program has none) */
		inline const std::vector<LooseUniform>& GetLooseUniforms() const { return _looseUniforms; }
		/** @brief std140 byte size of the "_Globals" buffer (0 if the program has no loose uniforms) */
		inline std::uint32_t GetGlobalsSize() const { return _globalsSize; }
		/** @brief Whether the program has a "_Globals" buffer (always at buffer index 0 when present) */
		inline bool HasGlobals() const { return _globalsSize > 0; }

		/** @brief A vertex attribute for building the pipeline's vertex descriptor */
		struct VertexAttrib
		{
			std::uint32_t Location;
			std::int32_t ComponentCount;
			std::uint32_t Offset;		// byte offset within the vertex
			std::uint32_t Type;			// component type (VertexAttribType numeric value; 0 = float)
			bool Normalized;			// integer components normalized to [0,1] (e.g. u8 colors)
			std::uint8_t ShaderScalar;	// scalar type the SHADER declares: 0 = float, 1 = int, 2 = uint (an integer input is fed a 32-bit integer stream, like glVertexAttribIPointer)
		};
		/**
		 * @brief Fills the current vertex input layout (attributes + per-vertex stride + buffer base offset) from the bound vertex format
		 *
		 * Writes at most @p maxAttribs entries into @p outAttribs (a caller-provided array, so the per-draw
		 * pipeline-key computation never touches the heap) and returns the number written. @p outBaseOffset is
		 * the byte offset within the vertex buffer the layout starts at (the `vboOffset` of @ref DefineVertexFormat()).
		 */
		std::uint32_t GetVertexInput(VertexAttrib* outAttribs, std::uint32_t maxAttribs, std::uint32_t& outStride, std::uint32_t& outBaseOffset) const;

	private:
		static std::uint32_t _nextHandle;

		std::uint32_t _handle;
		Status _status;
		Introspection _introspection;
		QueryPhase _queryPhase;
		std::uint32_t _batchSize;
		bool _shouldLogOnErrors;
		std::uint32_t _uniformsSize;
		std::uint32_t _uniformBlocksSize;

		std::vector<MetalUniform> _uniforms;
		std::vector<MetalUniformBlock> _uniformBlocks;
		std::vector<MetalAttribute> _attributes;

		const ShaderCompiler::ProgramVariant* _reflection;

		MetalVertexFormat _vertexFormat;
		const MetalBufferObject* _boundVbo;
		const MetalBufferObject* _boundIbo;

		struct ResolvedUniform
		{
			String Name;
			const std::uint8_t* Data;
		};
		std::vector<ResolvedUniform> _resolvedUniforms;

		// GPU objects (opaque pointers; see the getters above). The libraries are kept alive for the functions.
		void* _vsLibrary;
		void* _fsLibrary;
		void* _vsFunction;
		void* _fsFunction;
		std::vector<DescriptorBinding> _descriptorBindings;
		std::vector<LooseUniform> _looseUniforms;
		std::uint32_t _globalsSize;

		void PerformIntrospection();
		void ImportReflection();
		/** @brief Compiles the MSL stages and derives the argument table + `_Globals` layout (during introspection, while the reflection is still available) */
		void BuildPipelineState();
		void ReleaseGpu();
	};
}
