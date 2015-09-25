// Copyright (C) 2015 Jérôme Leclercq
// This file is part of the "Nazara Engine - Renderer module"
// For conditions of distribution and use, see copyright notice in Config.hpp

#include <Nazara/Renderer/Renderer.hpp>
#include <Nazara/Core/CallOnExit.hpp>
#include <Nazara/Core/Color.hpp>
#include <Nazara/Core/Error.hpp>
#include <Nazara/Core/ErrorFlags.hpp>
#include <Nazara/Core/Log.hpp>
#include <Nazara/Core/Signal.hpp>
#include <Nazara/Renderer/Config.hpp>
#include <Nazara/Renderer/Context.hpp>
#include <Nazara/Renderer/DebugDrawer.hpp>
#include <Nazara/Renderer/HardwareBuffer.hpp>
#include <Nazara/Renderer/OpenGL.hpp>
#include <Nazara/Renderer/RenderBuffer.hpp>
#include <Nazara/Renderer/RenderTarget.hpp>
#include <Nazara/Renderer/Shader.hpp>
#include <Nazara/Renderer/Texture.hpp>
#include <Nazara/Renderer/UberShader.hpp>
#include <Nazara/Utility/AbstractBuffer.hpp>
#include <Nazara/Utility/IndexBuffer.hpp>
#include <Nazara/Utility/Utility.hpp>
#include <Nazara/Utility/VertexBuffer.hpp>
#include <Nazara/Utility/VertexDeclaration.hpp>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <Nazara/Renderer/Debug.hpp>

namespace Nz
{
	namespace
	{
		const UInt8 r_coreFragmentShader[] = {
			#include <Nazara/Renderer/Resources/Shaders/Debug/core.frag.h>
		};

		const UInt8 r_coreVertexShader[] = {
			#include <Nazara/Renderer/Resources/Shaders/Debug/core.vert.h>
		};

		const UInt8 r_compatibilityFragmentShader[] = {
			#include <Nazara/Renderer/Resources/Shaders/Debug/compatibility.frag.h>
		};

		const UInt8 r_compatibilityVertexShader[] = {
			#include <Nazara/Renderer/Resources/Shaders/Debug/compatibility.vert.h>
		};

		enum ObjectType
		{
			ObjectType_Context,
			ObjectType_IndexBuffer,
			ObjectType_VertexBuffer,
			ObjectType_VertexDeclaration
		};

		enum UpdateFlags
		{
			Update_None = 0,

			Update_Matrices     = 0x1,
			Update_Shader       = 0x2,
			Update_Textures     = 0x4,
			Update_VAO          = 0x8
		};

		struct MatrixUnit
		{
			Matrix4f matrix;
			bool updated;
			int location;
		};

		struct TextureUnit
		{
			TextureSampler sampler;
			const Texture* texture = nullptr;
			bool samplerUpdated = false;
		};

		struct VAO_Entry
		{
			GLuint vao;

			NazaraSlot(IndexBuffer, OnIndexBufferRelease, onIndexBufferReleaseSlot);
			NazaraSlot(VertexBuffer, OnVertexBufferRelease, onVertexBufferReleaseSlot);
			NazaraSlot(VertexDeclaration, OnVertexDeclarationRelease, onInstancingDeclarationReleaseSlot);
			NazaraSlot(VertexDeclaration, OnVertexDeclarationRelease, onVertexDeclarationReleaseSlot);
		};

		using VAO_Key = std::tuple<const IndexBuffer*, const VertexBuffer*, const VertexDeclaration*, const VertexDeclaration*>;
		using VAO_Map = std::map<VAO_Key, VAO_Entry>;

		struct Context_Entry
		{
			VAO_Map vaoMap;

			NazaraSlot(Context, OnContextRelease, onReleaseSlot);
		};

		using Context_Map = std::unordered_map<const Context*, Context_Entry>;

		Context_Map s_vaos;
		std::vector<unsigned int> s_dirtyTextureUnits;
		std::vector<TextureUnit> s_textureUnits;
		GLuint s_currentVAO = 0;
		VertexBuffer s_instanceBuffer;
		VertexBuffer s_fullscreenQuadBuffer;
		MatrixUnit s_matrices[MatrixType_Max+1];
		RenderStates s_states;
		Vector2ui s_targetSize;
		UInt8 s_maxAnisotropyLevel;
		UInt32 s_updateFlags;
		const IndexBuffer* s_indexBuffer;
		const RenderTarget* s_target;
		const Shader* s_shader;
		const VertexBuffer* s_vertexBuffer;
		bool s_capabilities[RendererCap_Max+1];
		bool s_instancing;
		bool s_useSamplerObjects;
		bool s_useVertexArrayObjects;
		unsigned int s_maxColorAttachments;
		unsigned int s_maxRenderTarget;
		unsigned int s_maxTextureSize;
		unsigned int s_maxTextureUnit;
		unsigned int s_maxVertexAttribs;
	}

	void Renderer::BeginCondition(const GpuQuery& query, GpuQueryCondition condition)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_capabilities[RendererCap_ConditionalRendering])
		{
			NazaraError("Conditional rendering is not supported");
			return;
		}
		#endif

		glBeginConditionalRender(query.GetOpenGLID(), OpenGL::QueryCondition[condition]);
	}

	void Renderer::Clear(UInt32 flags)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		if (flags)
		{
			// On n'oublie pas de mettre à jour la cible
			s_target->EnsureTargetUpdated();
			// Les états du rendu sont suceptibles d'influencer glClear
			OpenGL::ApplyStates(s_states);

			GLenum mask = 0;

			if (flags & RendererBuffer_Color)
				mask |= GL_COLOR_BUFFER_BIT;

			if (flags & RendererBuffer_Depth)
				mask |= GL_DEPTH_BUFFER_BIT;

			if (flags & RendererBuffer_Stencil)
				mask |= GL_STENCIL_BUFFER_BIT;

			glClear(mask);
		}
	}

	void Renderer::DrawFullscreenQuad()
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		EnableInstancing(false);
		SetIndexBuffer(nullptr);
		SetVertexBuffer(&s_fullscreenQuadBuffer);

		if (!EnsureStateUpdate())
		{
			NazaraError("Failed to update states: " + Error::GetLastError());
			return;
		}

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if (s_useVertexArrayObjects)
			glBindVertexArray(0);
	}

	void Renderer::DrawIndexedPrimitives(PrimitiveMode mode, unsigned int firstIndex, unsigned int indexCount)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}

		if (mode > PrimitiveMode_Max)
		{
			NazaraError("Primitive mode out of enum");
			return;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_indexBuffer)
		{
			NazaraError("No index buffer");
			return;
		}
		#endif

		EnableInstancing(false);

		if (!EnsureStateUpdate())
		{
			NazaraError("Failed to update states: " + Error::GetLastError());
			return;
		}

		GLenum type;
		UInt8* offset = reinterpret_cast<UInt8*>(s_indexBuffer->GetStartOffset());

		if (s_indexBuffer->HasLargeIndices())
		{
			offset += firstIndex*sizeof(UInt32);
			type = GL_UNSIGNED_INT;
		}
		else
		{
			offset += firstIndex*sizeof(UInt16);
			type = GL_UNSIGNED_SHORT;
		}

		glDrawElements(OpenGL::PrimitiveMode[mode], indexCount, type, offset);

		if (s_useVertexArrayObjects)
			glBindVertexArray(0);
	}

	void Renderer::DrawIndexedPrimitivesInstanced(unsigned int instanceCount, PrimitiveMode mode, unsigned int firstIndex, unsigned int indexCount)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}

		if (mode > PrimitiveMode_Max)
		{
			NazaraError("Primitive mode out of enum");
			return;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_capabilities[RendererCap_Instancing])
		{
			NazaraError("Instancing not supported");
			return;
		}

		if (!s_indexBuffer)
		{
			NazaraError("No index buffer");
			return;
		}

		if (instanceCount == 0)
		{
			NazaraError("Instance count must be over zero");
			return;
		}

		unsigned int maxInstanceCount = s_instanceBuffer.GetVertexCount();
		if (instanceCount > maxInstanceCount)
		{
			NazaraError("Instance count is over maximum instance count (" + String::Number(instanceCount) + " >= " NazaraStringifyMacro(NAZARA_RENDERER_MAX_INSTANCES) ")" );
			return;
		}
		#endif

		EnableInstancing(true);

		if (!EnsureStateUpdate())
		{
			NazaraError("Failed to update states: " + Error::GetLastError());
			return;
		}

		GLenum type;
		UInt8* offset = reinterpret_cast<UInt8*>(s_indexBuffer->GetStartOffset());

		if (s_indexBuffer->HasLargeIndices())
		{
			offset += firstIndex*sizeof(UInt32);
			type = GL_UNSIGNED_INT;
		}
		else
		{
			offset += firstIndex*sizeof(UInt16);
			type = GL_UNSIGNED_SHORT;
		}

		glDrawElementsInstanced(OpenGL::PrimitiveMode[mode], indexCount, type, offset, instanceCount);

		if (s_useVertexArrayObjects)
			glBindVertexArray(0);
	}

	void Renderer::DrawPrimitives(PrimitiveMode mode, unsigned int firstVertex, unsigned int vertexCount)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}

		if (mode > PrimitiveMode_Max)
		{
			NazaraError("Primitive mode out of enum");
			return;
		}
		#endif

		EnableInstancing(false);

		if (!EnsureStateUpdate())
		{
			NazaraError("Failed to update states: " + Error::GetLastError());
			return;
		}

		glDrawArrays(OpenGL::PrimitiveMode[mode], firstVertex, vertexCount);

		if (s_useVertexArrayObjects)
			glBindVertexArray(0);
	}

	void Renderer::DrawPrimitivesInstanced(unsigned int instanceCount, PrimitiveMode mode, unsigned int firstVertex, unsigned int vertexCount)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}

		if (mode > PrimitiveMode_Max)
		{
			NazaraError("Primitive mode out of enum");
			return;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_capabilities[RendererCap_Instancing])
		{
			NazaraError("Instancing not supported");
			return;
		}

		if (instanceCount == 0)
		{
			NazaraError("Instance count must be over zero");
			return;
		}

		unsigned int maxInstanceCount = s_instanceBuffer.GetVertexCount();
		if (instanceCount > maxInstanceCount)
		{
			NazaraError("Instance count is over maximum instance count (" + String::Number(instanceCount) + " >= " NazaraStringifyMacro(NAZARA_RENDERER_MAX_INSTANCES) ")" );
			return;
		}
		#endif

		EnableInstancing(true);

		if (!EnsureStateUpdate())
		{
			NazaraError("Failed to update states: " + Error::GetLastError());
			return;
		}

		glDrawArraysInstanced(OpenGL::PrimitiveMode[mode], firstVertex, vertexCount, instanceCount);

		if (s_useVertexArrayObjects)
			glBindVertexArray(0);
	}

	void Renderer::Enable(RendererParameter parameter, bool enable)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}

		if (parameter > RendererParameter_Max)
		{
			NazaraError("Renderer parameter out of enum");
			return;
		}
		#endif

		s_states.parameters[parameter] = enable;
	}

	void Renderer::EndCondition()
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_capabilities[RendererCap_ConditionalRendering])
		{
			NazaraError("Conditional rendering is not supported");
			return;
		}
		#endif

		glEndConditionalRender();
	}

	void Renderer::Flush()
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		glFlush();
	}

	RendererComparison Renderer::GetDepthFunc()
	{
		return s_states.depthFunc;
	}

	VertexBuffer* Renderer::GetInstanceBuffer()
	{
		#if NAZARA_RENDERER_SAFE
		if (!s_capabilities[RendererCap_Instancing])
		{
			NazaraError("Instancing not supported");
			return nullptr;
		}
		#endif

		s_updateFlags |= Update_VAO;
		return &s_instanceBuffer;
	}

	float Renderer::GetLineWidth()
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return 0.f;
		}
		#endif

		return s_states.lineWidth;
	}

	Matrix4f Renderer::GetMatrix(MatrixType type)
	{
		#ifdef NAZARA_DEBUG
		if (type > MatrixType_Max)
		{
			NazaraError("Matrix type out of enum");
			return Matrix4f();
		}
		#endif

		if (!s_matrices[type].updated)
			UpdateMatrix(type);

		return s_matrices[type].matrix;
	}

	UInt8 Renderer::GetMaxAnisotropyLevel()
	{
		return s_maxAnisotropyLevel;
	}

	unsigned int Renderer::GetMaxColorAttachments()
	{
		return s_maxColorAttachments;
	}

	unsigned int Renderer::GetMaxRenderTargets()
	{
		return s_maxRenderTarget;
	}

	unsigned int Renderer::GetMaxTextureSize()
	{
		return s_maxTextureSize;
	}

	unsigned int Renderer::GetMaxTextureUnits()
	{
		return s_maxTextureUnit;
	}

	unsigned int Renderer::GetMaxVertexAttribs()
	{
		return s_maxVertexAttribs;
	}

	float Renderer::GetPointSize()
	{
		return s_states.pointSize;
	}

	const RenderStates& Renderer::GetRenderStates()
	{
		return s_states;
	}

	Recti Renderer::GetScissorRect()
	{
		return OpenGL::GetCurrentScissorBox();
	}

	const Shader* Renderer::GetShader()
	{
		return s_shader;
	}

	const RenderTarget* Renderer::GetTarget()
	{
		return s_target;
	}

	Recti Renderer::GetViewport()
	{
		return OpenGL::GetCurrentViewport();
	}

	bool Renderer::HasCapability(RendererCap capability)
	{
		#ifdef NAZARA_DEBUG
		if (capability > RendererCap_Max)
		{
			NazaraError("Renderer capability out of enum");
			return false;
		}
		#endif

		return s_capabilities[capability];
	}

	bool Renderer::Initialize()
	{
		if (s_moduleReferenceCounter > 0)
		{
			s_moduleReferenceCounter++;
			return true; // Déjà initialisé
		}

		// Initialisation des dépendances
		if (!Utility::Initialize())
		{
			NazaraError("Failed to initialize Utility module");
			return false;
		}

		s_moduleReferenceCounter++;

		// Initialisation du module
		CallOnExit onExit(Renderer::Uninitialize);

		// Initialisation d'OpenGL
		if (!OpenGL::Initialize()) // Initialise également Context
		{
			NazaraError("Failed to initialize OpenGL");
			return false;
		}

		Buffer::SetBufferFactory(DataStorage_Hardware, [](Buffer* parent, BufferType type) -> AbstractBuffer*
		{
			return new HardwareBuffer(parent, type);
		});

		for (unsigned int i = 0; i <= MatrixType_Max; ++i)
		{
			MatrixUnit& unit = s_matrices[i];
			unit.location = -1;
			unit.matrix.MakeIdentity();
			unit.updated = true;
		}

		// Récupération des capacités d'OpenGL
		s_capabilities[RendererCap_AnisotropicFilter]     = OpenGL::IsSupported(OpenGLExtension_AnisotropicFilter);
		s_capabilities[RendererCap_ConditionalRendering]  = OpenGL::IsSupported(OpenGLExtension_ConditionalRender);
		s_capabilities[RendererCap_FP64]                  = OpenGL::IsSupported(OpenGLExtension_FP64);
		s_capabilities[RendererCap_HardwareBuffer]        = true; // Natif depuis OpenGL 1.5
		s_capabilities[RendererCap_Instancing]            = OpenGL::IsSupported(OpenGLExtension_DrawInstanced) && OpenGL::IsSupported(OpenGLExtension_InstancedArray);
		s_capabilities[RendererCap_MultipleRenderTargets] = (glBindFragDataLocation != nullptr); // Natif depuis OpenGL 2.0 mais inutile sans glBindFragDataLocation
		s_capabilities[RendererCap_OcclusionQuery]        = true; // Natif depuis OpenGL 1.5
		s_capabilities[RendererCap_PixelBufferObject]     = OpenGL::IsSupported(OpenGLExtension_PixelBufferObject);
		s_capabilities[RendererCap_RenderTexture]         = OpenGL::IsSupported(OpenGLExtension_FrameBufferObject);
		s_capabilities[RendererCap_Texture3D]             = true; // Natif depuis OpenGL 1.2
		s_capabilities[RendererCap_TextureCubemap]        = true; // Natif depuis OpenGL 1.3
		s_capabilities[RendererCap_TextureMulti]          = true; // Natif depuis OpenGL 1.3
		s_capabilities[RendererCap_TextureNPOT]           = true; // Natif depuis OpenGL 2.0

		Context::EnsureContext();

		if (s_capabilities[RendererCap_AnisotropicFilter])
		{
			GLfloat maxAnisotropy;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);

			s_maxAnisotropyLevel = static_cast<UInt8>(maxAnisotropy);
		}
		else
			s_maxAnisotropyLevel = 1;

		if (s_capabilities[RendererCap_RenderTexture])
		{
			GLint maxColorAttachments;
			glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);

			s_maxColorAttachments = static_cast<unsigned int>(maxColorAttachments);
		}
		else
			s_maxColorAttachments = 1;

		if (s_capabilities[RendererCap_MultipleRenderTargets])
		{
			GLint maxDrawBuffers;
			glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);

			s_maxRenderTarget = static_cast<unsigned int>(maxDrawBuffers);
		}
		else
			s_maxRenderTarget = 1;

		if (s_capabilities[RendererCap_TextureMulti])
		{
			GLint maxTextureUnits;
			glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);

			s_maxTextureUnit = static_cast<unsigned int>(maxTextureUnits);
		}
		else
			s_maxTextureUnit = 1;

		GLint maxTextureSize;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
		s_maxTextureSize = maxTextureSize;

		GLint maxVertexAttribs;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
		s_maxVertexAttribs = static_cast<unsigned int>(maxVertexAttribs);

		s_states = RenderStates();

		s_indexBuffer = nullptr;
		s_shader = nullptr;
		s_target = nullptr;
		s_targetSize.Set(0U);
		s_textureUnits.resize(s_maxTextureUnit);
		s_useSamplerObjects = OpenGL::IsSupported(OpenGLExtension_SamplerObjects);
		s_useVertexArrayObjects = OpenGL::IsSupported(OpenGLExtension_VertexArrayObjects);
		s_updateFlags = Update_Matrices | Update_Shader | Update_VAO;
		s_vertexBuffer = nullptr;

		s_fullscreenQuadBuffer.Reset(VertexDeclaration::Get(VertexLayout_XY), 4, DataStorage_Hardware, BufferUsage_Static);

		float vertices[4*2] =
		{
			-1.f, -1.f,
			1.f, -1.f,
			-1.f, 1.f,
			1.f, 1.f,
		};

		if (!s_fullscreenQuadBuffer.Fill(vertices, 0, 4))
		{
			NazaraError("Failed to fill fullscreen quad buffer");
			return false;
		}

		if (s_capabilities[RendererCap_Instancing])
		{
			try
			{
				ErrorFlags errFlags(ErrorFlag_ThrowException, true);
				s_instanceBuffer.Reset(nullptr, NAZARA_RENDERER_INSTANCE_BUFFER_SIZE, DataStorage_Hardware, BufferUsage_Dynamic);
			}
			catch (const std::exception& e)
			{
				s_capabilities[RendererCap_Instancing] = false;

				ErrorFlags flags(ErrorFlag_ThrowExceptionDisabled);
				NazaraError("Failed to create instancing buffer: " + String(e.what()));
			}
		}

		if (!RenderBuffer::Initialize())
		{
			NazaraError("Failed to initialize render buffers");
			return false;
		}

		if (!Shader::Initialize())
		{
			NazaraError("Failed to initialize shaders");
			return false;
		}

		if (!Texture::Initialize())
		{
			NazaraError("Failed to initialize textures");
			return false;
		}

		if (!TextureSampler::Initialize())
		{
			NazaraError("Failed to initialize texture samplers");
			return false;
		}

		if (!UberShader::Initialize())
		{
			NazaraError("Failed to initialize uber shaders");
			return false;
		}

		// Création du shader de Debug
		ShaderRef debugShader = Shader::New();
		if (!debugShader->Create())
		{
			NazaraError("Failed to create debug shader");
			return false;
		}

		const char* fragmentShader;
		const char* vertexShader;
		unsigned int fragmentShaderLength;
		unsigned int vertexShaderLength;
		if (OpenGL::GetGLSLVersion() >= 140)
		{
			fragmentShader = reinterpret_cast<const char*>(r_coreFragmentShader);
			fragmentShaderLength = sizeof(r_coreFragmentShader);
			vertexShader = reinterpret_cast<const char*>(r_coreVertexShader);
			vertexShaderLength = sizeof(r_coreVertexShader);
		}
		else
		{
			fragmentShader = reinterpret_cast<const char*>(r_compatibilityFragmentShader);
			fragmentShaderLength = sizeof(r_compatibilityFragmentShader);
			vertexShader = reinterpret_cast<const char*>(r_compatibilityVertexShader);
			vertexShaderLength = sizeof(r_compatibilityVertexShader);
		}

		if (!debugShader->AttachStageFromSource(ShaderStageType_Fragment, fragmentShader, fragmentShaderLength))
		{
			NazaraError("Failed to attach fragment stage");
			return false;
		}

		if (!debugShader->AttachStageFromSource(ShaderStageType_Vertex, vertexShader, vertexShaderLength))
		{
			NazaraError("Failed to attach vertex stage");
			return false;
		}

		if (!debugShader->Link())
		{
			NazaraError("Failed to link shader");
			return false;
		}

		ShaderLibrary::Register("DebugSimple", debugShader);

		onExit.Reset();

		NazaraNotice("Initialized: Renderer module");
		return true;
	}

	bool Renderer::IsComponentTypeSupported(ComponentType type)
	{
		switch (type)
		{
			case ComponentType_Color:
			case ComponentType_Float1:
			case ComponentType_Float2:
			case ComponentType_Float3:
			case ComponentType_Float4:
				return true; // Supportés nativement

			case ComponentType_Double1:
			case ComponentType_Double2:
			case ComponentType_Double3:
			case ComponentType_Double4:
				return glVertexAttribLPointer != nullptr; // Fonction requise pour envoyer des doubles

			case ComponentType_Int1:
			case ComponentType_Int2:
			case ComponentType_Int3:
			case ComponentType_Int4:
				return glVertexAttribIPointer != nullptr; // Fonction requise pour envoyer des entiers

			case ComponentType_Quaternion:
				return false;
		}

		NazaraError("Attribute type not handled (0x" + String::Number(type, 16) + ')');
		return false;
	}

	bool Renderer::IsEnabled(RendererParameter parameter)
	{
		#ifdef NAZARA_DEBUG
		if (parameter > RendererParameter_Max)
		{
			NazaraError("Renderer parameter out of enum");
			return false;
		}
		#endif

		return s_states.parameters[parameter];
	}

	bool Renderer::IsInitialized()
	{
		return s_moduleReferenceCounter != 0;
	}

	void Renderer::SetBlendFunc(BlendFunc srcBlend, BlendFunc dstBlend)
	{
		#ifdef NAZARA_DEBUG
		if (srcBlend > BlendFunc_Max)
		{
			NazaraError("Blend func out of enum");
			return;
		}

		if (dstBlend > BlendFunc_Max)
		{
			NazaraError("Blend func out of enum");
			return;
		}
		#endif

		s_states.srcBlend = srcBlend;
		s_states.dstBlend = dstBlend;
	}

	void Renderer::SetClearColor(const Color& color)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		glClearColor(color.r/255.f, color.g/255.f, color.b/255.f, color.a/255.f);
	}

	void Renderer::SetClearColor(UInt8 r, UInt8 g, UInt8 b, UInt8 a)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		glClearColor(r/255.f, g/255.f, b/255.f, a/255.f);
	}

	void Renderer::SetClearDepth(double depth)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		glClearDepth(depth);
	}

	void Renderer::SetClearStencil(unsigned int value)
	{
		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return;
		}
		#endif

		glClearStencil(value);
	}

	void Renderer::SetDepthFunc(RendererComparison compareFunc)
	{
		#ifdef NAZARA_DEBUG
		if (compareFunc > RendererComparison_Max)
		{
			NazaraError("Renderer comparison out of enum");
			return;
		}
		#endif

		s_states.depthFunc = compareFunc;
	}

	void Renderer::SetFaceCulling(FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		s_states.faceCulling = faceSide;
	}

	void Renderer::SetFaceFilling(FaceFilling fillingMode)
	{
		#ifdef NAZARA_DEBUG
		if (fillingMode > FaceFilling_Max)
		{
			NazaraError("Face filling out of enum");
			return;
		}
		#endif

		s_states.faceFilling = fillingMode;
	}

	void Renderer::SetIndexBuffer(const IndexBuffer* indexBuffer)
	{
		#if NAZARA_RENDERER_SAFE
		if (indexBuffer && !indexBuffer->IsHardware())
		{
			NazaraError("Buffer must be hardware");
			return;
		}
		#endif

		if (s_indexBuffer != indexBuffer)
		{
			s_indexBuffer = indexBuffer;
			s_updateFlags |= Update_VAO;
		}
	}

	void Renderer::SetLineWidth(float width)
	{
		#if NAZARA_RENDERER_SAFE
		if (width <= 0.f)
		{
			NazaraError("Width must be over zero");
			return;
		}
		#endif

		s_states.lineWidth = width;
	}

	void Renderer::SetMatrix(MatrixType type, const Matrix4f& matrix)
	{
		#ifdef NAZARA_DEBUG
		if (type > MatrixType_Max)
		{
			NazaraError("Matrix type out of enum");
			return;
		}
		#endif

		s_matrices[type].matrix = matrix;
		s_matrices[type].updated = true;

		// Invalidation des combinaisons
		switch (type)
		{
			// Matrices de base
			case MatrixType_Projection:
				s_matrices[MatrixType_InvProjection].updated = false;
				s_matrices[MatrixType_InvViewProj].updated = false;
				s_matrices[MatrixType_InvWorldViewProj].updated = false;
				s_matrices[MatrixType_ViewProj].updated = false;
				s_matrices[MatrixType_WorldViewProj].updated = false;
				break;

			case MatrixType_View:
				s_matrices[MatrixType_InvView].updated = false;
				s_matrices[MatrixType_InvViewProj].updated = false;
				s_matrices[MatrixType_InvWorld].updated = false;
				s_matrices[MatrixType_InvWorldViewProj].updated = false;
				s_matrices[MatrixType_ViewProj].updated = false;
				s_matrices[MatrixType_World].updated = false;
				s_matrices[MatrixType_WorldViewProj].updated = false;
				break;

			case MatrixType_World:
				s_matrices[MatrixType_InvWorld].updated = false;
				s_matrices[MatrixType_InvWorldView].updated = false;
				s_matrices[MatrixType_InvWorldViewProj].updated = false;
				s_matrices[MatrixType_WorldView].updated = false;
				s_matrices[MatrixType_WorldViewProj].updated = false;
				break;

			// Matrices combinées
			case MatrixType_ViewProj:
				s_matrices[MatrixType_InvViewProj].updated = false;
				break;

			case MatrixType_WorldView:
				s_matrices[MatrixType_InvWorldView].updated = false;
				s_matrices[MatrixType_WorldViewProj].updated = false;
				break;

			case MatrixType_WorldViewProj:
				s_matrices[MatrixType_InvWorldViewProj].updated = false;
				break;

			case MatrixType_InvProjection:
			case MatrixType_InvView:
			case MatrixType_InvViewProj:
			case MatrixType_InvWorld:
			case MatrixType_InvWorldView:
			case MatrixType_InvWorldViewProj:
				break;
		}

		s_updateFlags |= Update_Matrices;
	}

	void Renderer::SetPointSize(float size)
	{
		#if NAZARA_RENDERER_SAFE
		if (size <= 0.f)
		{
			NazaraError("Size must be over zero");
			return;
		}
		#endif

		s_states.pointSize = size;
	}

	void Renderer::SetRenderStates(const RenderStates& states)
	{
		s_states = states;
	}

	void Renderer::SetScissorRect(const Recti& rect)
	{
		OpenGL::BindScissorBox(rect);
	}

	void Renderer::SetShader(const Shader* shader)
	{
		#if NAZARA_RENDERER_SAFE
		if (shader)
		{
			if (!shader->IsValid() || !shader->IsLinked())
			{
				NazaraError("Invalid shader");
				return;
			}
		}
		#endif

		if (s_shader != shader)
		{
			s_shader = shader;
			s_updateFlags |= Update_Shader;
		}
	}

	void Renderer::SetStencilCompareFunction(RendererComparison compareFunc, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (compareFunc > RendererComparison_Max)
		{
			NazaraError("Renderer comparison out of enum");
			return;
		}

		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilCompare = compareFunc;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilCompare = compareFunc;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilCompare = compareFunc;
				s_states.frontFace.stencilCompare = compareFunc;
				break;
		}
	}

	void Renderer::SetStencilFailOperation(nzStencilOperation failOperation, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (failOperation > nzStencilOperation_Max)
		{
			NazaraError("Stencil fail operation out of enum");
			return;
		}

		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilFail = failOperation;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilFail = failOperation;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilFail = failOperation;
				s_states.frontFace.stencilFail = failOperation;
				break;
		}
	}

	void Renderer::SetStencilMask(UInt32 mask, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilMask = mask;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilMask = mask;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilMask = mask;
				s_states.frontFace.stencilMask = mask;
				break;
		}
	}

	void Renderer::SetStencilPassOperation(nzStencilOperation passOperation, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (passOperation > nzStencilOperation_Max)
		{
			NazaraError("Stencil pass operation out of enum");
			return;
		}

		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilPass = passOperation;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilPass = passOperation;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilPass = passOperation;
				s_states.frontFace.stencilPass = passOperation;
				break;
		}
	}

	void Renderer::SetStencilReferenceValue(unsigned int refValue, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilReference = refValue;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilReference = refValue;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilReference = refValue;
				s_states.frontFace.stencilReference = refValue;
				break;
		}
	}

	void Renderer::SetStencilZFailOperation(nzStencilOperation zfailOperation, FaceSide faceSide)
	{
		#ifdef NAZARA_DEBUG
		if (zfailOperation > nzStencilOperation_Max)
		{
			NazaraError("Stencil pass operation out of enum");
			return;
		}

		if (faceSide > FaceSide_Max)
		{
			NazaraError("Face side out of enum");
			return;
		}
		#endif

		switch (faceSide)
		{
			case FaceSide_Back:
				s_states.backFace.stencilZFail = zfailOperation;
				break;

			case FaceSide_Front:
				s_states.frontFace.stencilZFail = zfailOperation;
				break;

			case FaceSide_FrontAndBack:
				s_states.backFace.stencilZFail = zfailOperation;
				s_states.frontFace.stencilZFail = zfailOperation;
				break;
		}
	}

	bool Renderer::SetTarget(const RenderTarget* target)
	{
		if (s_target == target)
			return true;

		if (s_target)
		{
			if (!s_target->HasContext())
				s_target->Desactivate();

			s_target = nullptr;
		}

		if (target)
		{
			#if NAZARA_RENDERER_SAFE
			if (!target->IsRenderable())
			{
				NazaraError("Target not renderable");
				return false;
			}
			#endif

			if (!target->Activate())
			{
				NazaraError("Failed to activate target");
				return false;
			}

			s_target = target;
		}

		OpenGL::SetTarget(s_target);

		return true;
	}

	void Renderer::SetTexture(UInt8 unit, const Texture* texture)
	{
		#if NAZARA_RENDERER_SAFE
		if (unit >= s_maxTextureUnit)
		{
			NazaraError("Texture unit out of range (" + String::Number(unit) + " >= " + String::Number(s_maxTextureUnit) + ')');
			return;
		}
		#endif

		if (s_textureUnits[unit].texture != texture)
		{
			s_textureUnits[unit].texture = texture;

			if (texture)
			{
				if (s_textureUnits[unit].sampler.UseMipmaps(texture->HasMipmaps()))
					s_textureUnits[unit].samplerUpdated = false;
			}

			s_dirtyTextureUnits.push_back(unit);
			s_updateFlags |= Update_Textures;
		}
	}

	void Renderer::SetTextureSampler(UInt8 unit, const TextureSampler& sampler)
	{
		#if NAZARA_RENDERER_SAFE
		if (unit >= s_maxTextureUnit)
		{
			NazaraError("Texture unit out of range (" + String::Number(unit) + " >= " + String::Number(s_maxTextureUnit) + ')');
			return;
		}
		#endif

		s_textureUnits[unit].sampler = sampler;
		s_textureUnits[unit].samplerUpdated = false;

		if (s_textureUnits[unit].texture)
			s_textureUnits[unit].sampler.UseMipmaps(s_textureUnits[unit].texture->HasMipmaps());

		s_dirtyTextureUnits.push_back(unit);
		s_updateFlags |= Update_Textures;
	}

	void Renderer::SetVertexBuffer(const VertexBuffer* vertexBuffer)
	{
		#if NAZARA_RENDERER_SAFE
		if (vertexBuffer && !vertexBuffer->IsHardware())
		{
			NazaraError("Buffer must be hardware");
			return;
		}
		#endif

		if (vertexBuffer && s_vertexBuffer != vertexBuffer)
		{
			s_vertexBuffer = vertexBuffer;
			s_updateFlags |= Update_VAO;
		}
	}

	void Renderer::SetViewport(const Recti& viewport)
	{
		OpenGL::BindViewport(viewport);
	}

	void Renderer::Uninitialize()
	{
		if (s_moduleReferenceCounter != 1)
		{
			// Le module est soit encore utilisé, soit pas initialisé
			if (s_moduleReferenceCounter > 1)
				s_moduleReferenceCounter--;

			return;
		}

		// Libération du module
		s_moduleReferenceCounter = 0;

		ShaderLibrary::Unregister("DebugSimple");

		UberShader::Uninitialize();
		TextureSampler::Uninitialize();
		Texture::Uninitialize();
		Shader::Uninitialize();
		RenderBuffer::Uninitialize();
		DebugDrawer::Uninitialize();

		s_textureUnits.clear();

		// Libération des buffers
		s_fullscreenQuadBuffer.Reset();
		s_instanceBuffer.Reset();

		// Libération des VAOs
		for (auto& pair : s_vaos)
		{
			const Context* context = pair.first;
			const Context_Entry& contextEntry = pair.second;

			for (auto& pair2 : contextEntry.vaoMap)
			{
				const VAO_Entry& entry = pair2.second;
				OpenGL::DeleteVertexArray(context, entry.vao);
			}
		}
		s_vaos.clear();

		OpenGL::Uninitialize();

		NazaraNotice("Uninitialized: Renderer module");

		// Libération des dépendances
		Utility::Uninitialize();
	}

	void Renderer::EnableInstancing(bool instancing)
	{
		if (s_instancing != instancing)
		{
			s_updateFlags |= Update_VAO;
			s_instancing = instancing;
		}
	}

	bool Renderer::EnsureStateUpdate()
	{
		// Toutes les erreurs sont silencieuses car l'erreur est gérée par la fonction appelante
		ErrorFlags flags(ErrorFlag_Silent | ErrorFlag_ThrowExceptionDisabled);

		#ifdef NAZARA_DEBUG
		if (Context::GetCurrent() == nullptr)
		{
			NazaraError("No active context");
			return false;
		}
		#endif

		#if NAZARA_RENDERER_SAFE
		if (!s_shader)
		{
			NazaraError("No shader");
			return false;
		}

		if (!s_target)
		{
			NazaraError("No target");
			return false;
		}
		#endif

		s_target->EnsureTargetUpdated();

		s_shader->Bind(); // Active le programme si ce n'est pas déjà le cas

		// Si le programme a été changé depuis la dernière fois
		if (s_updateFlags & Update_Shader)
		{
			// Récupération des indices des variables uniformes (-1 si la variable n'existe pas)
			s_matrices[MatrixType_Projection].location = s_shader->GetUniformLocation(ShaderUniform_ProjMatrix);
			s_matrices[MatrixType_View].location = s_shader->GetUniformLocation(ShaderUniform_ViewMatrix);
			s_matrices[MatrixType_World].location = s_shader->GetUniformLocation(ShaderUniform_WorldMatrix);

			s_matrices[MatrixType_ViewProj].location = s_shader->GetUniformLocation(ShaderUniform_ViewProjMatrix);
			s_matrices[MatrixType_WorldView].location = s_shader->GetUniformLocation(ShaderUniform_WorldViewMatrix);
			s_matrices[MatrixType_WorldViewProj].location = s_shader->GetUniformLocation(ShaderUniform_WorldViewProjMatrix);

			s_matrices[MatrixType_InvProjection].location = s_shader->GetUniformLocation(ShaderUniform_InvProjMatrix);
			s_matrices[MatrixType_InvView].location = s_shader->GetUniformLocation(ShaderUniform_InvViewMatrix);
			s_matrices[MatrixType_InvViewProj].location = s_shader->GetUniformLocation(ShaderUniform_InvViewProjMatrix);
			s_matrices[MatrixType_InvWorld].location = s_shader->GetUniformLocation(ShaderUniform_InvWorldMatrix);
			s_matrices[MatrixType_InvWorldView].location = s_shader->GetUniformLocation(ShaderUniform_InvWorldViewMatrix);
			s_matrices[MatrixType_InvWorldViewProj].location = s_shader->GetUniformLocation(ShaderUniform_InvWorldViewProjMatrix);

			s_targetSize.Set(0U); // On force l'envoi des uniformes
			s_updateFlags |= Update_Matrices; // Changement de programme, on renvoie toutes les matrices demandées

			s_updateFlags &= ~Update_Shader;
		}

		// Envoi des uniformes liées au Renderer
		Vector2ui targetSize(s_target->GetWidth(), s_target->GetHeight());
		if (s_targetSize != targetSize)
		{
			int location;

			location = s_shader->GetUniformLocation(ShaderUniform_InvTargetSize);
			if (location != -1)
				s_shader->SendVector(location, 1.f/Vector2f(targetSize));

			location = s_shader->GetUniformLocation(ShaderUniform_TargetSize);
			if (location != -1)
				s_shader->SendVector(location, Vector2f(targetSize));

			s_targetSize.Set(targetSize);
		}

		if (s_updateFlags != Update_None)
		{
			if (s_updateFlags & Update_Textures)
			{
				if (s_useSamplerObjects)
				{
					for (unsigned int i : s_dirtyTextureUnits)
					{
						TextureUnit& unit = s_textureUnits[i];

						if (unit.texture && !unit.samplerUpdated)
						{
							unit.sampler.Bind(i);
							unit.samplerUpdated = true;
						}
					}
				}
				else
				{
					for (unsigned int i : s_dirtyTextureUnits)
					{
						TextureUnit& unit = s_textureUnits[i];

						if (unit.texture && !unit.samplerUpdated)
						{
							OpenGL::BindTextureUnit(i);
							unit.sampler.Apply(unit.texture);
							unit.samplerUpdated = true;
						}
					}
				}

				s_dirtyTextureUnits.clear(); // Ne change pas la capacité
				s_updateFlags &= ~Update_Textures;
			}

			if (s_updateFlags & Update_Matrices)
			{
				for (unsigned int i = 0; i <= MatrixType_Max; ++i)
				{
					MatrixUnit& unit = s_matrices[i];
					if (unit.location != -1) // On ne traite que les matrices existant dans le programme
					{
						if (!unit.updated)
							UpdateMatrix(static_cast<MatrixType>(i));

						s_shader->SendMatrix(unit.location, unit.matrix);
					}
				}

				s_updateFlags &= ~Update_Matrices;
			}

			if (s_updateFlags & Update_VAO)
			{
				#if NAZARA_RENDERER_SAFE
				if (!s_vertexBuffer)
				{
					NazaraError("No vertex buffer");
					return false;
				}
				#endif

				bool update;
				VAO_Map::iterator vaoIt;

				// Si les VAOs sont supportés, on entoure nos appels par ceux-ci
				if (s_useVertexArrayObjects)
				{
					// Note: Les VAOs ne sont pas partagés entre les contextes, nous avons donc un tableau de VAOs par contexte
					const Context* context = Context::GetCurrent();

					auto it = s_vaos.find(context);
					if (it == s_vaos.end())
					{
						Context_Entry entry;
						entry.onReleaseSlot.Connect(context->OnContextRelease, OnContextRelease);

						it = s_vaos.insert(std::make_pair(context, std::move(entry))).first;
					}

					VAO_Map& vaoMap = it->second.vaoMap;

					// Notre clé est composée de ce qui définit un VAO
					const VertexDeclaration* vertexDeclaration = s_vertexBuffer->GetVertexDeclaration();
					const VertexDeclaration* instancingDeclaration = (s_instancing) ? s_instanceBuffer.GetVertexDeclaration() : nullptr;
					VAO_Key key(s_indexBuffer, s_vertexBuffer, vertexDeclaration, instancingDeclaration);

					// On recherche un VAO existant avec notre configuration
					vaoIt = vaoMap.find(key);
					if (vaoIt == vaoMap.end())
					{
						// On créé notre VAO
						glGenVertexArrays(1, &s_currentVAO);
						glBindVertexArray(s_currentVAO);

						// On l'ajoute à notre liste
						VAO_Entry entry;
						entry.vao = s_currentVAO;

						// Connect the slots
						if (s_indexBuffer)
							entry.onIndexBufferReleaseSlot.Connect(s_indexBuffer->OnIndexBufferRelease, OnIndexBufferRelease);

						if (instancingDeclaration)
							entry.onInstancingDeclarationReleaseSlot.Connect(instancingDeclaration->OnVertexDeclarationRelease, OnVertexDeclarationRelease);

						entry.onVertexBufferReleaseSlot.Connect(s_vertexBuffer->OnVertexBufferRelease, OnVertexBufferRelease);
						entry.onVertexDeclarationReleaseSlot.Connect(vertexDeclaration->OnVertexDeclarationRelease, OnVertexDeclarationRelease);

						vaoIt = vaoMap.insert(std::make_pair(key, std::move(entry))).first;

						// Et on indique qu'on veut le programmer
						update = true;
					}
					else
					{
						// Notre VAO existe déjà, il est donc inutile de le reprogrammer
						s_currentVAO = vaoIt->second.vao;

						update = false;
					}
				}
				else
					update = true; // Fallback si les VAOs ne sont pas supportés

				bool updateFailed = false;

				if (update)
				{
					// Pour éviter la duplication de code, on va utiliser une astuce via une boucle for
					for (unsigned int i = 0; i < (s_instancing ? 2U : 1U); ++i)
					{
						// Selon l'itération nous choisissons un buffer différent
						const VertexBuffer* vertexBuffer = (i == 0) ? s_vertexBuffer : &s_instanceBuffer;

						HardwareBuffer* vertexBufferImpl = static_cast<HardwareBuffer*>(vertexBuffer->GetBuffer()->GetImpl());
						glBindBuffer(OpenGL::BufferTarget[BufferType_Vertex], vertexBufferImpl->GetOpenGLID());

						unsigned int bufferOffset = vertexBuffer->GetStartOffset();
						const VertexDeclaration* vertexDeclaration = vertexBuffer->GetVertexDeclaration();
						unsigned int stride = vertexDeclaration->GetStride();

						// On définit les bornes (une fois de plus selon l'itération)
						unsigned int start = (i == 0) ? VertexComponent_FirstVertexData : VertexComponent_FirstInstanceData;
						unsigned int end = (i == 0) ? VertexComponent_LastVertexData : VertexComponent_LastInstanceData;
						for (unsigned int j = start; j <= end; ++j)
						{
							ComponentType type;
							bool enabled;
							unsigned int offset;
							vertexDeclaration->GetComponent(static_cast<VertexComponent>(j), &enabled, &type, &offset);

							if (enabled)
							{
								if (!IsComponentTypeSupported(type))
								{
									NazaraError("Invalid vertex declaration " + String::Pointer(vertexDeclaration) + ": Vertex component 0x" + String::Number(j, 16) + " (type: 0x" + String::Number(type, 16) + ") is not supported");
									updateFailed = true;
									break;
								}

								glEnableVertexAttribArray(OpenGL::VertexComponentIndex[j]);

								switch (type)
								{
									case ComponentType_Color:
									{
										glVertexAttribPointer(OpenGL::VertexComponentIndex[j],
															  Utility::ComponentCount[type],
															  OpenGL::ComponentType[type],
															  GL_TRUE,
															  stride,
															  reinterpret_cast<void*>(bufferOffset + offset));

										break;
									}

									case ComponentType_Double1:
									case ComponentType_Double2:
									case ComponentType_Double3:
									case ComponentType_Double4:
									{
										glVertexAttribLPointer(OpenGL::VertexComponentIndex[j],
															   Utility::ComponentCount[type],
															   OpenGL::ComponentType[type],
															   stride,
															   reinterpret_cast<void*>(bufferOffset + offset));

										break;
									}

									case ComponentType_Float1:
									case ComponentType_Float2:
									case ComponentType_Float3:
									case ComponentType_Float4:
									{
										glVertexAttribPointer(OpenGL::VertexComponentIndex[j],
															  Utility::ComponentCount[type],
															  OpenGL::ComponentType[type],
															  GL_FALSE,
															  stride,
															  reinterpret_cast<void*>(bufferOffset + offset));

										break;
									}

									case ComponentType_Int1:
									case ComponentType_Int2:
									case ComponentType_Int3:
									case ComponentType_Int4:
									{
										glVertexAttribIPointer(OpenGL::VertexComponentIndex[j],
															   Utility::ComponentCount[type],
															   OpenGL::ComponentType[type],
															   stride,
															   reinterpret_cast<void*>(bufferOffset + offset));

										break;
									}

									default:
									{
										NazaraInternalError("Unsupported component type (0x" + String::Number(type, 16) + ')');
										break;
									}
								}
								// Les attributs d'instancing ont un diviseur spécifique (pour dépendre de l'instance en cours)
								if (i == 1)
									glVertexAttribDivisor(OpenGL::VertexComponentIndex[j], 1);
							}
							else
								glDisableVertexAttribArray(OpenGL::VertexComponentIndex[j]);
						}
					}

					if (!s_instancing)
					{
						// Je ne sais pas si c'est vraiment nécessaire de désactiver les attributs, sur mon ordinateur ça ne pose aucun problème
						// mais dans le doute, je laisse ça comme ça.
						for (unsigned int i = VertexComponent_FirstInstanceData; i <= VertexComponent_LastInstanceData; ++i)
							glDisableVertexAttribArray(OpenGL::VertexComponentIndex[i]);
					}

					// Et on active l'index buffer (Un seul index buffer par VAO)
					if (s_indexBuffer)
					{
						HardwareBuffer* indexBufferImpl = static_cast<HardwareBuffer*>(s_indexBuffer->GetBuffer()->GetImpl());
						glBindBuffer(OpenGL::BufferTarget[BufferType_Index], indexBufferImpl->GetOpenGLID());
					}
					else
						glBindBuffer(OpenGL::BufferTarget[BufferType_Index], 0);

					// On invalide les bindings des buffers (car nous les avons défini manuellement)
					OpenGL::SetBuffer(BufferType_Index, 0);
					OpenGL::SetBuffer(BufferType_Vertex, 0);
				}

				if (s_useVertexArrayObjects)
				{
					if (update)
					{
						if (updateFailed)
						{
							// La création de notre VAO a échoué, libérons-le et marquons-le comme problématique
							glDeleteVertexArrays(1, &vaoIt->second.vao);
							vaoIt->second.vao = 0;
							s_currentVAO = 0;
						}
						else
							glBindVertexArray(0); // On marque la fin de la construction du VAO en le débindant
					}

					// En cas de non-support des VAOs, les attributs doivent être respécifiés à chaque frame
					s_updateFlags &= ~Update_VAO;
				}
			}

			#ifdef NAZARA_DEBUG
			if (s_updateFlags != Update_None && !s_useVertexArrayObjects && s_updateFlags != Update_VAO)
				NazaraWarning("Update flags not fully cleared");
			#endif
		}

		// On bind notre VAO
		if (s_useVertexArrayObjects)
		{
			if (!s_currentVAO)
			{
				NazaraError("Failed to create VAO");
				return false;
			}

			glBindVertexArray(s_currentVAO);
		}

		// On vérifie que les textures actuellement bindées sont bien nos textures
		// Ceci à cause du fait qu'il est possible que des opérations sur les textures aient eu lieu
		// entre le dernier rendu et maintenant
		for (unsigned int i = 0; i < s_maxTextureUnit; ++i)
		{
			const Texture* texture = s_textureUnits[i].texture;
			if (texture)
			{
				OpenGL::BindTexture(i, texture->GetType(), texture->GetOpenGLID());
				texture->EnsureMipmapsUpdate();
			}
		}

		// Et on termine par envoyer nos états au driver
		OpenGL::ApplyStates(s_states);

		return true;
	}

	void Renderer::OnContextRelease(const Context* context)
	{
		s_vaos.erase(context);
	}

	void Renderer::OnIndexBufferRelease(const IndexBuffer* indexBuffer)
	{
		for (auto& pair : s_vaos)
		{
			const Context* context = pair.first;
			VAO_Map& vaos = pair.second.vaoMap;

			auto it = vaos.begin();
			while (it != vaos.end())
			{
				const VAO_Key& key = it->first;
				const IndexBuffer* vaoIndexBuffer = std::get<0>(key);

				if (vaoIndexBuffer == indexBuffer)
				{
					// Suppression du VAO:
					// Comme celui-ci est local à son contexte de création, sa suppression n'est possible que si
					// son contexte d'origine est actif, sinon il faudra le mettre en file d'attente
					// Ceci est géré par la méthode OpenGL::DeleteVertexArray

					OpenGL::DeleteVertexArray(context, it->second.vao);
					vaos.erase(it++);
				}
				else
					++it;
			}
		}
	}

	void Renderer::OnShaderReleased(const Shader* shader)
	{
		if (s_shader == shader)
		{
			s_shader = nullptr;
			s_updateFlags |= Update_Shader;
		}
	}

	void Renderer::OnTextureReleased(const Texture* texture)
	{
		for (TextureUnit& unit : s_textureUnits)
		{
			if (unit.texture == texture)
				unit.texture = nullptr;

			// Inutile de changer le flag pour une texture désactivée
		}
	}

	void Renderer::OnVertexBufferRelease(const VertexBuffer* vertexBuffer)
	{
		for (auto& pair : s_vaos)
		{
			const Context* context = pair.first;
			VAO_Map& vaos = pair.second.vaoMap;

			auto it = vaos.begin();
			while (it != vaos.end())
			{
				const VAO_Key& key = it->first;
				const VertexBuffer* vaoVertexBuffer = std::get<1>(key);

				if (vaoVertexBuffer == vertexBuffer)
				{
					// Suppression du VAO:
					// Comme celui-ci est local à son contexte de création, sa suppression n'est possible que si
					// son contexte d'origine est actif, sinon il faudra le mettre en file d'attente
					// Ceci est géré par la méthode OpenGL::DeleteVertexArray

					OpenGL::DeleteVertexArray(context, it->second.vao);
					vaos.erase(it++);
				}
				else
					++it;
			}
		}
	}

	void Renderer::OnVertexDeclarationRelease(const VertexDeclaration* vertexDeclaration)
	{
		for (auto& pair : s_vaos)
		{
			const Context* context = pair.first;
			VAO_Map& vaos = pair.second.vaoMap;

			auto it = vaos.begin();
			while (it != vaos.end())
			{
				const VAO_Key& key = it->first;
				const VertexDeclaration* vaoVertexDeclaration = std::get<2>(key);
				const VertexDeclaration* vaoInstancingDeclaration = std::get<3>(key);

				if (vaoVertexDeclaration == vertexDeclaration || vaoInstancingDeclaration == vertexDeclaration)
				{
					// Suppression du VAO:
					// Comme celui-ci est local à son contexte de création, sa suppression n'est possible que si
					// son contexte d'origine est actif, sinon il faudra le mettre en file d'attente
					// Ceci est géré par la méthode OpenGL::DeleteVertexArray

					OpenGL::DeleteVertexArray(context, it->second.vao);
					vaos.erase(it++);
				}
				else
					++it;
			}
		}
	}

	void Renderer::UpdateMatrix(MatrixType type)
	{
		#ifdef NAZARA_DEBUG
		if (type > MatrixType_Max)
		{
			NazaraError("Matrix type out of enum");
			return;
		}
		#endif

		switch (type)
		{
			// Matrices de base
			case MatrixType_Projection:
			case MatrixType_View:
			case MatrixType_World:
				s_matrices[type].updated = true;
				break;

			// Matrices combinées
			case MatrixType_ViewProj:
				s_matrices[MatrixType_ViewProj].matrix = s_matrices[MatrixType_View].matrix;
				s_matrices[MatrixType_ViewProj].matrix.Concatenate(s_matrices[MatrixType_Projection].matrix);
				s_matrices[MatrixType_ViewProj].updated = true;
				break;

			case MatrixType_WorldView:
				s_matrices[MatrixType_WorldView].matrix = s_matrices[MatrixType_World].matrix;
				s_matrices[MatrixType_WorldView].matrix.ConcatenateAffine(s_matrices[MatrixType_View].matrix);
				s_matrices[MatrixType_WorldView].updated = true;
				break;

			case MatrixType_WorldViewProj:
				if (!s_matrices[MatrixType_WorldView].updated)
					UpdateMatrix(MatrixType_WorldView);

				s_matrices[MatrixType_WorldViewProj].matrix = s_matrices[MatrixType_WorldView].matrix;
				s_matrices[MatrixType_WorldViewProj].matrix.Concatenate(s_matrices[MatrixType_Projection].matrix);
				s_matrices[MatrixType_WorldViewProj].updated = true;
				break;

			// Matrices inversées
			case MatrixType_InvProjection:
				if (!s_matrices[MatrixType_Projection].updated)
					UpdateMatrix(MatrixType_Projection);

				if (!s_matrices[MatrixType_Projection].matrix.GetInverse(&s_matrices[MatrixType_InvProjection].matrix))
					NazaraWarning("Failed to inverse Proj matrix");

				s_matrices[MatrixType_InvProjection].updated = true;
				break;

			case MatrixType_InvView:
				if (!s_matrices[MatrixType_View].updated)
					UpdateMatrix(MatrixType_View);

				if (!s_matrices[MatrixType_View].matrix.GetInverse(&s_matrices[MatrixType_InvView].matrix))
					NazaraWarning("Failed to inverse View matrix");

				s_matrices[MatrixType_InvView].updated = true;
				break;

			case MatrixType_InvViewProj:
				if (!s_matrices[MatrixType_ViewProj].updated)
					UpdateMatrix(MatrixType_ViewProj);

				if (!s_matrices[MatrixType_ViewProj].matrix.GetInverse(&s_matrices[MatrixType_InvViewProj].matrix))
					NazaraWarning("Failed to inverse ViewProj matrix");

				s_matrices[MatrixType_InvViewProj].updated = true;
				break;

			case MatrixType_InvWorld:
				if (!s_matrices[MatrixType_World].updated)
					UpdateMatrix(MatrixType_World);

				if (!s_matrices[MatrixType_World].matrix.GetInverse(&s_matrices[MatrixType_InvWorld].matrix))
					NazaraWarning("Failed to inverse World matrix");

				s_matrices[MatrixType_InvWorld].updated = true;
				break;

			case MatrixType_InvWorldView:
				if (!s_matrices[MatrixType_WorldView].updated)
					UpdateMatrix(MatrixType_WorldView);

				if (!s_matrices[MatrixType_WorldView].matrix.GetInverse(&s_matrices[MatrixType_InvWorldView].matrix))
					NazaraWarning("Failed to inverse WorldView matrix");

				s_matrices[MatrixType_InvWorldView].updated = true;
				break;

			case MatrixType_InvWorldViewProj:
				if (!s_matrices[MatrixType_WorldViewProj].updated)
					UpdateMatrix(MatrixType_WorldViewProj);

				if (!s_matrices[MatrixType_WorldViewProj].matrix.GetInverse(&s_matrices[MatrixType_InvWorldViewProj].matrix))
					NazaraWarning("Failed to inverse WorldViewProj matrix");

				s_matrices[MatrixType_InvWorldViewProj].updated = true;
				break;
		}
	}

	unsigned int Renderer::s_moduleReferenceCounter = 0;
}
