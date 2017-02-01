/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Core/Timer.h>
#include <Core/DoubleBuffer.h>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>
#include <Volume/ChunkedRenderPass.h>

#include <UI/imgui/imgui.h>
#include <UI/imgui_impl_sdl_gl3.h>
#include <UI/Turntable.h>
#include <UI/Profiler.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <Volume/TransferFunction.h>
#include <Volume/SyntheticVolume.h>

#include <Misc/TransferFunctionPresets.h>
#include <Misc/Parameters.h>

// openvr includes
#include <openvr.h>
#include <VR/OpenVRTools.h>

////////////////////// PARAMETERS /////////////////////////////
static const char* s_models[] = {"CT Head", "MRT Brain", "Homogeneous", "Radial Gradient"};

static const float MIRROR_SCREEN_FRAME_INTERVAL = 0.03f; // interval time (seconds) to mirror the screen (to avoid wait for vsync stalls)

static glm::vec2 WINDOW_RESOLUTION(1400, 700.0f);

const char* SHADER_DEFINES[] = {
	//"AMBIENT_OCCLUSION",
	"RANDOM_OFFSET",
	"WARP_SET_FAR_PLANE",
	"OCCLUSION_MAP",
	"EMISSION_ABSORPTION_RAW",
	"SCENE_DEPTH",
	//"SHADOW_SAMPLING,"
	"LEVEL_OF_DETAIL",
	"FIRST_HIT",
};

const int FIRST_HIT = 0;
const int CURRENT = 1;
const int LEFT = vr::Eye_Left;
const int RIGHT = vr::Eye_Right;

using namespace RaycastingParameters;
using namespace ViewParameters;
using namespace VolumeParameters;

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// MISC //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

template <class T>
void activateVolume(VolumeData<T>& volumeData ) // set static variables
{
	DEBUGLOG->log("File Info:");
	DEBUGLOG->indent();
		DEBUGLOG->log("min value: ", volumeData.min);
		DEBUGLOG->log("max value: ", volumeData.max);
		DEBUGLOG->log("res. x   : ", volumeData.size_x);
		DEBUGLOG->log("res. y   : ", volumeData.size_y);
		DEBUGLOG->log("res. z   : ", volumeData.size_z);
	DEBUGLOG->outdent();

	// set volume specific parameters
	s_minValue = volumeData.min;
	s_maxValue = volumeData.max;
	s_rayStepSize = 1.0f / (2.0f * volumeData.size_x); // this seems a reasonable size
	s_windowingMinValue = (float) volumeData.min;
	s_windowingMaxValue = (float) volumeData.max;
	s_windowingRange = s_windowingMaxValue - s_windowingMinValue;
}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
class CMainApplication 
{
private: 
	// SDL bookkeeping
	SDL_Window	 *m_pWindow;
	SDL_GLContext m_pContext;

	// OpenVR bookkeeping
	OpenVRSystem* m_pOvr;

	// Render objects bookkeeping
	VolumeData<float> m_volumeData[4];
	GLuint m_volumeTexture[4];
	VolumeSubdiv* volume;
	Quad* quad;
	VertexGrid* vertexGrid;
	Grid* grid;
	Volume* ndcCube;

	// Shader bookkeeping
	ShaderProgram* uvwShaderProgram;
	ShaderProgram* shaderProgram;
	ShaderProgram* occlusionFrustumShader;
	ShaderProgram* occlusionClipFrustumShader;
	ShaderProgram* quadWarpShader;
	ShaderProgram* gridWarpShader;
	ShaderProgram* showTexShader;
	ShaderProgram* depthToTextureShader;

	// FBO bookkeeping
	FrameBufferObject* occlusionFrustumFBO;
	FrameBufferObject* occlusionFrustumFBO_r;
	FrameBufferObject* uvwFBO;
	FrameBufferObject* uvwFBO_r;
	FrameBufferObject* FBO;
	FrameBufferObject* FBO_r;
	FrameBufferObject* FBO_front;
	FrameBufferObject* FBO_front_r;
	FrameBufferObject* occlusionClipFrustumFBO;
	FrameBufferObject* occlusionClipFrustumFBO_r;
	FrameBufferObject* FBO_warp;
	FrameBufferObject* FBO_warp_r;
	FrameBufferObject* FBO_scene_depth;
	FrameBufferObject* FBO_scene_depth_r;
	FrameBufferObject* FBO_debug_depth;
	FrameBufferObject* FBO_debug_depth_r;

	// Renderpass bookkeeping
	RenderPass* uvwRenderPass; 		
	RenderPass* renderPass; 		
	RenderPass* renderPass_r; 		
	ChunkedAdaptiveRenderPass* chunkedRenderPass; 
	ChunkedAdaptiveRenderPass* chunkedRenderPass_r; 
	RenderPass* occlusionFrustum; 		
	RenderPass* occlusionClipFrustum; 		
	RenderPass* quadWarp; 		
	RenderPass* gridWarp; 		
	RenderPass* showTex; 		
	RenderPass* depthToTexture; 	

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> sdlEventHandler;
	std::function<bool(const vr::VREvent_t&)> vrEventHandler;
	std::function<void(bool, int)> handleTrackpad;
	std::function<void(int)> setDebugView;

	// Frame profiling
	struct Frame{
		Profiler FrameProfiler;
		SimpleDoubleBuffer<OpenGLTimings> Timings;
	} m_frame;


	// MISC
	std::vector<std::string> m_shaderDefines;

	Turntable turntable;

	int 		 m_activeModel;

	bool useGridWarp;
	int occlusionBlockSize;
	int vertexGridWidth;
	int vertexGridHeight;
	
	enum DebugViews{
		UVW_BACK,
		UVW_FRONT,
		FIRST_HIT_,
		OCCLUSION,
		CURRENT_,
		FRONT,
		WARPED,
		OCCLUSION_CLIP_FRUSTUM_FRONT,
		OCCLUSION_CLIP_FRUSTUM_BACK,
		NUM_VIEWS // auxiliary
	};
	int activeView;
	int  leftDebugView;
	int rightDebugView;

	bool predictPose;
	
	bool  is_touchpad_touched;
	int   touchpad_tracked_device_index;
	float old_touch_x;
	float old_touch_y;

	float old_x;
	float old_y;

	std::vector<float> m_fpsCounter;
	int m_curFPSidx;

	struct MatrixSet
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 perspective;
	} matrices[2][2]; // left, right, firsthit, current


public:
	CMainApplication( int argc, char *argv[] )
		: useGridWarp(false)
		, m_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES))
		, m_fpsCounter(120)
		, m_curFPSidx(0)
		, m_activeModel(0)
		, activeView(WARPED)
	{
		DEBUGLOG->setAutoPrint(true);

		// create m_pWindow and opengl context
		m_pWindow = generateWindow_SDL(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

		printOpenGLInfo();
		printSDLRenderDriverInfo();

		// init imgui
		ImGui_ImplSdlGL3_Init(m_pWindow);
	}

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// INITIALIZE OPENVR   //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	void initOpenVR()
	{
		m_pOvr = new OpenVRSystem(s_near, s_far);
	
		if ( m_pOvr->initialize() )
		{
			DEBUGLOG->log("Alright! OpenVR up and running!");
			m_pOvr->initializeHMDMatrices();

			m_pOvr->CreateShaders();
			m_pOvr->SetupRenderModels();

			s_fovY = m_pOvr->getFovY();

			unsigned int width, height;
			m_pOvr->m_pHMD->GetRecommendedRenderTargetSize(&width, &height);
			WINDOW_RESOLUTION = glm::vec2(width * 2, height);
		}
	}


	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// VOLUME DATA LOADING //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	void loadVolumes()
	{
		// load data set: CT of a Head	// load into 3d texture
		std::string file = RESOURCES_PATH;
		file += std::string( "/volumes/CTHead/CThead");
		
		m_volumeData[0] = Importer::load3DData<float>(file, 256, 256, 113, 2);
		m_volumeTexture[0] = loadTo3DTexture<float>(m_volumeData[0], 5, GL_R16F, GL_RED, GL_FLOAT);
		m_volumeData[0].data.clear(); // set free	

		m_volumeData[1] = Importer::loadBruder<float>();
		m_volumeTexture[1] =  loadTo3DTexture<float>(m_volumeData[1], 5, GL_R16F, GL_RED, GL_FLOAT);
		m_volumeData[1].data.clear(); // set free

		m_volumeData[2] = SyntheticVolume::generateHomogeneousVolume<float>(32, 32, 32, 1000.0f);
		m_volumeTexture[2] =  loadTo3DTexture<float>(m_volumeData[2], 1, GL_R16F, GL_RED, GL_FLOAT);
		m_volumeData[2].data.clear(); // set free	

		m_volumeData[3] = SyntheticVolume::generateRadialGradientVolume<float>( 32,32,32,1000.0f,0.0f);
		m_volumeTexture[3] =  loadTo3DTexture<float>(m_volumeData[3], 3, GL_R16F, GL_RED, GL_FLOAT);
		m_volumeData[3].data.clear(); // set free	

		// TODO wtf is going on here, why does it get corrupted?? 
		DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
		DEBUGLOG->log("Loading Volume Data to 3D-Texture.");

		activateVolume<float>(m_volumeData[0]);
		TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, TransferFunctionPresets::CT_Head);
	}

	void initSceneVariables()
	{
		/////////////////////     Scene / View Settings     //////////////////////////
		if (m_pOvr->m_pHMD)
		{
			s_translation = glm::translate(glm::vec3(0.0f,1.25f,0.0f));
			s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
		}
		else
		{	
			s_translation = glm::translate(glm::vec3(0.0f,0.0f,-3.0f));
			s_scale = glm::scale(glm::vec3(1.0f,1.0f,1.0f));
		}
		s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));

		// use waitgetPoses to update matrices
		s_view = glm::lookAt(glm::vec3(s_eye), glm::vec3(s_center), glm::vec3(0.0f, 1.0f, 0.0f));
		s_view_r = glm::lookAt(glm::vec3(s_eye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(s_center), glm::vec3(0.0f, 1.0f, 0.0f));
		if (!m_pOvr->m_pHMD)
		{
			s_perspective = glm::perspective(glm::radians(45.f), WINDOW_RESOLUTION.x / (2.0f * WINDOW_RESOLUTION.y), s_near, 10.f);
			s_perspective_r = glm::perspective(glm::radians(45.f), WINDOW_RESOLUTION.x / (2.0f * WINDOW_RESOLUTION.y), s_near, 10.f);
		}
		else
		{
			s_view = m_pOvr->m_mat4eyePosLeft * s_view;
			s_view_r = m_pOvr->m_mat4eyePosRight * s_view_r;
			s_perspective = m_pOvr->m_mat4ProjectionLeft; 
			s_perspective_r = m_pOvr->m_mat4ProjectionRight;
		}
	
		s_nearH = s_near * std::tanf( glm::radians(s_fovY/2.0f) );
		s_nearW = s_nearH * WINDOW_RESOLUTION.x / (2.0f * WINDOW_RESOLUTION.y);

		// constant
		s_screenToView = glm::scale(glm::vec3(s_nearW, s_nearH, s_near)) * 
			glm::inverse( 
				glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * 
				glm::scale(glm::vec3(0.5f,0.5f,0.5f)) 
				);
    
		// constant
		s_modelToTexture = glm::mat4( // swap components
			glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), // column 1
			glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), // column 2
			glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),//column 3
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) //column 4 
			* glm:: inverse(glm::scale( 2.0f * s_volumeSize) ) // moves origin to front left
			* glm::translate( glm::vec3(s_volumeSize.x, s_volumeSize.y, -s_volumeSize.z) );
	}

	void loadGeometries()
	{
		// create Volume and VertexGrid
		volume = new VolumeSubdiv(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 3);
		quad = new Quad();

		occlusionBlockSize = 6;
		vertexGridWidth  = (int) WINDOW_RESOLUTION.x/2 / occlusionBlockSize;
		vertexGridHeight = (int) WINDOW_RESOLUTION.y   / occlusionBlockSize;
		vertexGrid = new VertexGrid(vertexGridWidth, vertexGridHeight, false, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(96, 96)); //dunno what is a good group size?

		grid = new Grid(400, 400, 0.0025f, 0.0025f, false);

		ndcCube = new Volume(1.0f); // a cube that spans -1 .. 1 

	}

	void loadShaders()
	{
		uvwShaderProgram = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag", m_shaderDefines);
		shaderProgram = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
		occlusionFrustumShader = new ShaderProgram("/raycast/occlusionFrustum.vert", "/raycast/occlusionFrustum.frag", "/raycast/occlusionFrustum.geom", m_shaderDefines);
		occlusionClipFrustumShader = new ShaderProgram("/raycast/occlusionClipFrustum.vert", "/raycast/occlusionClipFrustum.frag", m_shaderDefines);
		quadWarpShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleWarp.frag", m_shaderDefines);
		gridWarpShader = new ShaderProgram("/raycast/gridWarp.vert", "/raycast/gridWarp.frag", m_shaderDefines);
		showTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
		depthToTextureShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/raycast/debug_depthToTexture.frag");
	}

	void initUniforms()
	{
		uvwShaderProgram->update("model", s_translation * s_rotation * s_scale);
		uvwShaderProgram->update("view", s_view);
		uvwShaderProgram->update("projection", s_perspective);

		shaderProgram->update("uStepSize", s_rayStepSize);
		shaderProgram->update("uViewport", glm::vec4(0,0,WINDOW_RESOLUTION.x/2, WINDOW_RESOLUTION.y));	
		shaderProgram->update("uResolution", glm::vec4(WINDOW_RESOLUTION.x/2, WINDOW_RESOLUTION.y,0,0));

		shaderProgram->update("uShadowRayDirection", glm::normalize(glm::vec3(0.0f,-0.5f,-1.0f))); // full range of values in window
		shaderProgram->update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary

		quadWarpShader->update( "blendColor", 1.0f );
		quadWarpShader->update( "uFarPlane", s_far );

		occlusionFrustumShader->update("uOcclusionBlockSize", occlusionBlockSize);
		occlusionFrustumShader->update("uGridSize", glm::vec4(vertexGridWidth, vertexGridHeight, 1.0f / (float) vertexGridWidth, 1.0f / vertexGridHeight));

	}

	void initFramebuffers()
	{
		DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		uvwFBO = new FrameBufferObject((int) WINDOW_RESOLUTION.x/2,(int) WINDOW_RESOLUTION.y);
		uvwFBO->addColorAttachments(2); // front UVRs and back UVRs
		uvwFBO_r = new FrameBufferObject((int) WINDOW_RESOLUTION.x/2,(int)  WINDOW_RESOLUTION.y);
		uvwFBO_r->addColorAttachments(2); // front UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: ray casting"); DEBUGLOG->indent();
		FBO = new FrameBufferObject(shaderProgram->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		FBO_r = new FrameBufferObject(shaderProgram->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		FBO_front = new FrameBufferObject(shaderProgram->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		FBO_front_r = new FrameBufferObject(shaderProgram->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		DEBUGLOG->outdent();
		

		DEBUGLOG->log("FrameBufferObject Creation: occlusion frustum"); DEBUGLOG->indent();
		occlusionFrustumFBO = new FrameBufferObject(   occlusionFrustumShader->getOutputInfoMap(), uvwFBO->getWidth(),   uvwFBO->getHeight() );
		occlusionFrustumFBO_r = new FrameBufferObject( occlusionFrustumShader->getOutputInfoMap(), uvwFBO_r->getWidth(), uvwFBO_r->getHeight() );
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: occlusion clip frustum"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		occlusionClipFrustumFBO = new FrameBufferObject(   occlusionClipFrustumShader->getOutputInfoMap(), uvwFBO->getWidth(),   uvwFBO->getHeight() );
		occlusionClipFrustumFBO_r = new FrameBufferObject( occlusionClipFrustumShader->getOutputInfoMap(), uvwFBO_r->getWidth(), uvwFBO_r->getHeight() );
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: quad warping"); DEBUGLOG->indent();
		FBO_warp = new FrameBufferObject(quadWarpShader->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		FBO_warp_r = new FrameBufferObject(quadWarpShader->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: scene depth"); DEBUGLOG->indent();
		FBO_scene_depth = new FrameBufferObject((int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y); // has only a depth buffer, no color attachments
		FBO_scene_depth_r = new FrameBufferObject((int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y); // has only a depth buffer, no color attachments
		FBO_scene_depth->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		FBO_scene_depth_r->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: DEBUG depth to texture"); DEBUGLOG->indent();
		FBO_debug_depth = new FrameBufferObject(depthToTextureShader->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		FBO_debug_depth_r = new FrameBufferObject(depthToTextureShader->getOutputInfoMap(), (int) WINDOW_RESOLUTION.x/2, (int) WINDOW_RESOLUTION.y);
		DEBUGLOG->outdent();
	}

	void initTextureUnits()
	{
		OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture[m_activeModel], GL_TEXTURE0, GL_TEXTURE_3D);
		OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D); // transfer function

		OPENGLCONTEXT->bindTextureToUnit(uvwFBO->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
		OPENGLCONTEXT->bindTextureToUnit(uvwFBO->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

		OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
		OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

		OPENGLCONTEXT->bindTextureToUnit(FBO->getDepthTextureHandle(), GL_TEXTURE6, GL_TEXTURE_2D); // left first hit map
		OPENGLCONTEXT->bindTextureToUnit(FBO_r->getDepthTextureHandle(), GL_TEXTURE7, GL_TEXTURE_2D); // right first hit map

		OPENGLCONTEXT->bindTextureToUnit(FBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D); // left  raycasting result
		OPENGLCONTEXT->bindTextureToUnit(FBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE11, GL_TEXTURE_2D);// right raycasting result
	
		OPENGLCONTEXT->bindTextureToUnit(FBO_front->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE12, GL_TEXTURE_2D); // left  raycasting result (for display)
		OPENGLCONTEXT->bindTextureToUnit(FBO_front_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE13, GL_TEXTURE_2D);// right raycasting result (for display)

		OPENGLCONTEXT->bindTextureToUnit(FBO_front->getDepthTextureHandle(), GL_TEXTURE24, GL_TEXTURE_2D); // left  raycasting first hit map  (for display)
		OPENGLCONTEXT->bindTextureToUnit(FBO_front_r->getDepthTextureHandle(), GL_TEXTURE25, GL_TEXTURE_2D);// right  raycasting first hit map (for display)

		OPENGLCONTEXT->bindTextureToUnit(occlusionFrustumFBO->getDepthTextureHandle(), GL_TEXTURE8, GL_TEXTURE_2D); // left occlusion map
		OPENGLCONTEXT->bindTextureToUnit(occlusionFrustumFBO_r->getDepthTextureHandle(), GL_TEXTURE9, GL_TEXTURE_2D); // right occlusion map

		OPENGLCONTEXT->bindTextureToUnit(occlusionClipFrustumFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE26, GL_TEXTURE_2D); // left occlusion clip map back
		OPENGLCONTEXT->bindTextureToUnit(occlusionClipFrustumFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE27, GL_TEXTURE_2D); // right occlusion clip map back
	
		OPENGLCONTEXT->bindTextureToUnit(occlusionClipFrustumFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE28, GL_TEXTURE_2D); // left occlusion clip map front
		OPENGLCONTEXT->bindTextureToUnit(occlusionClipFrustumFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE29, GL_TEXTURE_2D); // right occlusion clip map front
	
		OPENGLCONTEXT->bindTextureToUnit(FBO_warp->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE14, GL_TEXTURE_2D); // left  raycasting result (for display)
		OPENGLCONTEXT->bindTextureToUnit(FBO_warp_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE15, GL_TEXTURE_2D);// right raycasting result (for display)
	
		OPENGLCONTEXT->bindTextureToUnit(FBO_debug_depth->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE16, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(FBO_debug_depth_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE17, GL_TEXTURE_2D);

		OPENGLCONTEXT->bindTextureToUnit(FBO_scene_depth->getDepthTextureHandle(), GL_TEXTURE18, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(FBO_scene_depth_r->getDepthTextureHandle(), GL_TEXTURE19, GL_TEXTURE_2D);
		
		OPENGLCONTEXT->activeTexture(GL_TEXTURE20);
	}

	void updateTextureUniforms()
	{
		shaderProgram->update("volume_texture", 0); // volume texture
		shaderProgram->update("transferFunctionTex", 1);
	}

	void initRenderPasses()
	{
		uvwRenderPass = new RenderPass(uvwShaderProgram, uvwFBO);
		uvwRenderPass->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		uvwRenderPass->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		uvwRenderPass->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
		uvwRenderPass->addRenderable(volume);

		renderPass = new RenderPass(shaderProgram, FBO);
		renderPass->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		renderPass->addRenderable(quad);
		renderPass->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		renderPass->addDisable(GL_BLEND);

		renderPass_r = new RenderPass(shaderProgram, FBO_r);
		renderPass_r->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		renderPass_r->addRenderable(quad);
		renderPass_r->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		renderPass_r->addDisable(GL_BLEND);

		glm::ivec2 viewportSize = glm::ivec2((int) WINDOW_RESOLUTION.x / 2, (int) WINDOW_RESOLUTION.y);
		glm::ivec2 chunkSize = glm::ivec2(96,96);
		chunkedRenderPass = new ChunkedAdaptiveRenderPass(
			renderPass,
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		chunkedRenderPass_r = new ChunkedAdaptiveRenderPass(
			renderPass_r,
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		
		//chunkedRenderPass->activateClearbits();
		//chunkedRenderPass_r->activateClearbits();

		occlusionFrustum = new RenderPass(occlusionFrustumShader, occlusionFrustumFBO);
		occlusionFrustum->addRenderable(vertexGrid);
		occlusionFrustum->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		occlusionFrustum->addEnable(GL_DEPTH_TEST);
		occlusionFrustum->addDisable(GL_BLEND);

		occlusionClipFrustum = new RenderPass(occlusionClipFrustumShader, occlusionClipFrustumFBO);
		occlusionClipFrustum->addRenderable(ndcCube);	
		occlusionClipFrustum->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		occlusionClipFrustum->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		occlusionClipFrustum->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results

		quadWarp = new RenderPass(quadWarpShader, FBO_warp);
		quadWarp->addRenderable(quad);

		gridWarp = new RenderPass(gridWarpShader, FBO_warp);
		gridWarp->addEnable(GL_DEPTH_TEST);
		gridWarp->addRenderable(grid);

		showTex = new RenderPass(showTexShader,0);
		showTex->addRenderable(quad);

		depthToTexture = new RenderPass(depthToTextureShader, FBO_debug_depth);
		depthToTexture->addClearBit(GL_COLOR_BUFFER_BIT);
		depthToTexture->addDisable(GL_DEPTH_TEST);
		depthToTexture->addRenderable(quad);
	}

	void initGUI()
	{
		// Setup ImGui binding
		ImGui_ImplSdlGL3_Init(m_pWindow);
	}


	void initEventHandlers()
	{
		//////////////////////////////////////////////////////////////////////////////
		///////////////////////    GUI / USER INPUT   ////////////////////////////////
		//////////////////////////////////////////////////////////////////////////////	
		//handles all the sdl events
		sdlEventHandler = [&](SDL_Event *event)
		{
			bool imguiHandlesEvent = ImGui_ImplSdlGL3_ProcessEvent(event);
			switch(event->type)
			{
				case SDL_KEYDOWN:
				{
					int k = event->key.keysym.sym;
					switch (k)
					{
						case SDLK_w:
							s_translation = glm::translate( glm::vec3(glm::inverse(s_view) * glm::vec4(0.0f,0.0f,-0.1f,0.0f))) * s_translation;
							break;
						case SDLK_a:
							s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(-0.1f,0.0f,0.0f,0.0f))) * s_translation;
							break;
						case SDLK_s:
							s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(0.0f,0.0f,0.1f,0.0f))) * s_translation;
							break;
						case SDLK_d:
							s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(0.1f,0.0f,0.0f,0.0f))) * s_translation;
							break;
						case SDLK_SPACE:
							activeView = (activeView + 1) % NUM_VIEWS;
							break;
						default:
							break;
					}
					break;
				}
				case SDL_MOUSEMOTION:
				{
					ImGuiIO& io = ImGui::GetIO();
					if ( io.WantCaptureMouse )
					{ break; } // ImGUI is handling this

					float d_x = event->motion.x - old_x;
					float d_y = event->motion.y - old_y;

					if ( turntable.getDragActive() )
					{
						turntable.dragBy(d_x, d_y, s_view);
					}

					old_x = (float) event->motion.x;
					old_y = (float) event->motion.y;
					break;
				}
				case SDL_MOUSEBUTTONDOWN:
				{
					if (event->button.button == SDL_BUTTON_LEFT)
					{
						turntable.setDragActive(true);
					}
					if (event->button.button == SDL_BUTTON_RIGHT)
					{
						unsigned char pick_col[20];
						glReadPixels((int) old_x - 2, (int) WINDOW_RESOLUTION.y - (int) old_y, 5, 1, GL_RGBA, GL_UNSIGNED_BYTE, pick_col);

						for (int i = 0; i < 20; i += 4)
						{
							DEBUGLOG->log("color: ", glm::vec4(pick_col[i + 0], pick_col[i + 1], pick_col[i+2], pick_col[i+3]));
						}
					}
					break;
				}
				case SDL_MOUSEBUTTONUP:
				{
					if (event->button.button == SDL_BUTTON_LEFT)
					{
						turntable.setDragActive(false);
					}
					break;
				}
			}
			return true;
		};

		leftDebugView = 14;
		rightDebugView = 15;
		predictPose = true;
	
		// coordinates of the touch pad
		is_touchpad_touched = false;
		touchpad_tracked_device_index = -1;
		old_touch_x = 0.5f;
		old_touch_y = 0.5f;

		vrEventHandler = [&](const vr::VREvent_t & event)
		{
			switch( event.eventType )
			{
				case vr::VREvent_ButtonPress:
				{
					if (event.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) { return false; } // nevermind
				
					switch(event.data.controller.button)
					{
					case vr::k_EButton_Axis0: // touchpad
							activeView = (activeView + 1) % NUM_VIEWS;
						break;
					case vr::k_EButton_Axis1: // trigger
							activeView = (activeView <= 0) ? NUM_VIEWS - 1 : activeView - 1;
						break;
					case vr::k_EButton_Grip: // grip
						//TODO do something with the model matrix
						useGridWarp = !useGridWarp; // DEBUG
						break;
					}

					DEBUGLOG->log("button press: ", event.data.controller.button);
					break;
				}
				case vr::VREvent_ButtonTouch: // generated for touchpad
					if (event.data.controller.button == vr::k_EButton_Axis0) // reset coords 
					{ 
						touchpad_tracked_device_index = event.trackedDeviceIndex;
						is_touchpad_touched = true;
						turntable.setDragActive(is_touchpad_touched);

						vr::VRControllerState_t state_;
						if (m_pOvr->m_pHMD->GetControllerState(event.trackedDeviceIndex, &state_))

						old_touch_x = state_.rAxis[0].x;
						old_touch_y = state_.rAxis[0].y;
					}
					break;
				case vr::VREvent_ButtonUntouch:
					if (event.data.controller.button == vr::k_EButton_Axis0) // reset coords 
					{
						touchpad_tracked_device_index = -1;
						is_touchpad_touched = false;
						turntable.setDragActive(is_touchpad_touched);
					}
					break;
				//case vr::VREvent_TouchPadMove: // this event is never fired in normal mode
				//	break;
			}
			return false;
		};

		// seperate handler for touchpad, since no event will be generated for touch-movement
		handleTrackpad = [&](bool isTouched, int deviceIdx)
		{
			if (isTouched && deviceIdx != -1)
			{
				vr::VRControllerState_t state;
				m_pOvr->m_pHMD->GetControllerState(deviceIdx, &state);

				float d_x = state.rAxis[0].x - old_touch_x;
				float d_y = state.rAxis[0].y - old_touch_y;

				if (turntable.getDragActive())
				{
					turntable.dragBy(d_x * 40.0f, -d_y * 40.0f, s_view);
				}

				old_touch_x = state.rAxis[0].x;
				old_touch_y = state.rAxis[0].y;
			}
		};

		setDebugView = [&](int view)
		{
			switch (view)
			{
				// regular textures -> show directly
				default:	
				case UVW_BACK:
				case UVW_FRONT:
				case CURRENT_:
				case FRONT:
				case WARPED:
					leftDebugView = view * 2 + 2;
					if (leftDebugView >= 16 ) leftDebugView = 2;
					rightDebugView = leftDebugView + 1;
					break;
				case OCCLUSION:
				case FIRST_HIT_:
					leftDebugView = 16;
					rightDebugView = leftDebugView + 1;
					// convert left
					depthToTextureShader->update("depth_texture", view * 2 + 2);
					depthToTextureShader->update("uProjection", s_perspective);
					depthToTextureShader->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[LEFT][CURRENT].model) * glm::inverse(matrices[LEFT][CURRENT].view) );
					depthToTexture->setFrameBufferObject(FBO_debug_depth);
					depthToTexture->render();

					// convert right
					depthToTextureShader->update("depth_texture", view * 2 + 3);
					depthToTextureShader->update("uProjection", s_perspective_r);
					depthToTextureShader->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[RIGHT][CURRENT].model) * glm::inverse(matrices[RIGHT][CURRENT].view) );
					depthToTexture->setFrameBufferObject(FBO_debug_depth_r);
					depthToTexture->render();
					break;
				case OCCLUSION_CLIP_FRUSTUM_BACK:
					leftDebugView = 26;
					rightDebugView = leftDebugView + 1;
					break;
				case OCCLUSION_CLIP_FRUSTUM_FRONT:
					leftDebugView = 28;
					rightDebugView = leftDebugView + 1;
					break;
			}
		};
	}

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	void loop()
	{
		std::string window_header = "Volume Renderer - OpenVR";
		SDL_SetWindowTitle(m_pWindow, window_header.c_str() );
		OPENGLCONTEXT->activeTexture(GL_TEXTURE20);
		
		float elapsedTime = 0.0;
		float mirrorScreenTimer = 0.0f;
		while (!shouldClose(m_pWindow))
		{
			////////////////////////////////    EVENTS    ////////////////////////////////
			pollSDLEvents(m_pWindow, sdlEventHandler);
			m_pOvr->PollVREvents(vrEventHandler);
			handleTrackpad(is_touchpad_touched, touchpad_tracked_device_index); // handle trackpad touch seperately
		
			////////////////////////////////     GUI      ////////////////////////////////
			ImGuiIO& io = ImGui::GetIO();
			profileFPS(ImGui::GetIO().Framerate);
			ImGui_ImplSdlGL3_NewFrame(m_pWindow);

			ImGui::Value("FPS", io.Framerate);
			mirrorScreenTimer += io.DeltaTime;
			elapsedTime += io.DeltaTime;

			ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) );
			ImGui::PlotLines("FPS", &m_fpsCounter[0], m_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
			ImGui::PopStyleColor();
	
			if (ImGui::CollapsingHeader("Transfer Function Settings"))
			{
				ImGui::Columns(2, "mycolumns2", true);
				ImGui::Separator();
				bool changed = false;
				for (unsigned int n = 0; n < TransferFunctionPresets::s_transferFunction.getValues().size(); n++)
				{
					changed |= ImGui::SliderFloat(("V" + std::to_string(n)).c_str(), &TransferFunctionPresets::s_transferFunction.getValues()[n], 0.0f, 1.0f);
					ImGui::NextColumn();
					changed |= ImGui::ColorEdit4(("C" + std::to_string(n)).c_str(), &TransferFunctionPresets::s_transferFunction.getColors()[n][0]);
					ImGui::NextColumn();
				}
		
				if(changed)
				{
					TransferFunctionPresets::updateTransferFunctionTex();
				}
				ImGui::Columns(1);
				ImGui::Separator();
			}

			ImGui::PushItemWidth(-100);
			if (ImGui::CollapsingHeader("Volume Rendering Settings"))
    		{
				ImGui::Text("Parameters related to volume rendering");
				ImGui::DragFloatRange2("windowing range", &s_windowingMinValue, &s_windowingMaxValue, 5.0f, (float) s_minValue, (float) s_maxValue); // grayscale ramp boundaries
        		ImGui::SliderFloat("ray step size",   &s_rayStepSize,  0.0001f, 0.1f, "%.5f", 2.0f);
			}
        
			ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume

		
    		if (ImGui::ListBox("active model", &m_activeModel, s_models, (int)(sizeof(s_models)/sizeof(*s_models)), 2))
    		{
				activateVolume(m_volumeData[m_activeModel]);
				s_rotation = s_rotation * glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));
				static glm::vec3 shadowDir = glm::normalize(glm::vec3(0.0f,-0.5f,1.0f));
				shadowDir.y = -shadowDir.y;
				shaderProgram->update("uShadowRayDirection", shadowDir ); // full range of values in window
				OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture[m_activeModel], GL_TEXTURE0, GL_TEXTURE_3D);
				TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, (TransferFunctionPresets::Preset) m_activeModel);
    		}

			ImGui::PopItemWidth();
		
			ImGui::Separator();

			static float lodMaxLevel = 4.0f;
			static float lodBegin  = 0.3f;
			static float lodRange  = 4.0f;
			ImGui::DragFloat("Lod Max Level",   &lodMaxLevel,   0.1f, 0.0f, 8.0f);
			ImGui::DragFloat("Lod Begin", &lodBegin, 0.01f,0.0f, s_far);
			ImGui::DragFloat("Lod Range", &lodRange, 0.01f,0.0f, std::max(0.1f, s_far-lodBegin) );
		
			ImGui::Separator();
			ImGui::Columns(2);
			static bool profiler_visible, profiler_visible_r = false;
			ImGui::Checkbox("Chunk Perf Profiler Left", &profiler_visible);
			if (profiler_visible) { chunkedRenderPass->imguiInterface(&profiler_visible); };
			ImGui::NextColumn();
			ImGui::Checkbox("Chunk Perf Profiler Right", &profiler_visible_r);
			if (profiler_visible_r) { chunkedRenderPass_r->imguiInterface(&profiler_visible_r); };
			ImGui::NextColumn();
			ImGui::Columns(1);
		
			ImGui::Separator();
			ImGui::Columns(2);
			ImGui::SliderInt("Left Debug View", &leftDebugView, 2, 15);
			ImGui::NextColumn();
			ImGui::SliderInt("Right Debug View", &rightDebugView, 2, 15);
			ImGui::NextColumn();
			ImGui::Columns(1);
			ImGui::Separator();

			static float warpFarPlane = s_far;
			if (ImGui::SliderFloat("Far", &warpFarPlane, s_near, s_far))
			{
				quadWarpShader->update( "uFarPlane", warpFarPlane ); 
			}

			{
				static float scale = s_scale[0][0];
				static float scaleY = s_scale[1][1];
				if (ImGui::SliderFloat("Scale", &scale, 0.01f, 5.0f))
				{
					s_scale = glm::scale(glm::vec3(scale, scaleY * scale, scale));
				}
				if (ImGui::SliderFloat("Scale Y", &scaleY, 0.5f, 1.5f))
				{
					s_scale = glm::scale(glm::vec3(scale, scaleY * scale, scale));
				}

			}

			static bool frame_profiler_visible = false;
			static bool pause_frame_profiler = false;
			ImGui::Checkbox("Frame Profiler", &frame_profiler_visible);
			ImGui::Checkbox("Pause Frame Profiler", &pause_frame_profiler);
			m_frame.Timings.getFront().setEnabled(!pause_frame_profiler);
			m_frame.Timings.getBack().setEnabled(!pause_frame_profiler);
		
			// update whatever is finished
			m_frame.Timings.getFront().updateReadyTimings();
			m_frame.Timings.getBack().updateReadyTimings();

			float frame_begin = 0.0;
			float frame_end = 17.0;
			if (m_frame.Timings.getFront().m_timestamps.find("Frame Begin") != m_frame.Timings.getFront().m_timestamps.end())
			{
				frame_begin = (float) m_frame.Timings.getFront().m_timestamps.at("Frame Begin").lastTime;
			}
			if (m_frame.Timings.getFront().m_timestamps.find("Frame End") != m_frame.Timings.getFront().m_timestamps.end())
			{
				frame_end = (float) m_frame.Timings.getFront().m_timestamps.at("Frame End").lastTime;
			}

			if (frame_profiler_visible) 
			{ 
				for (auto e : m_frame.Timings.getFront().m_timers)
				{
					m_frame.FrameProfiler.setRangeByTag(e.first, (float) e.second.lastTime - frame_begin, (float) e.second.lastTime - frame_begin + (float) e.second.lastTiming);
				}
				for (auto e : m_frame.Timings.getFront().m_timersElapsed)
				{
					m_frame.FrameProfiler.setRangeByTag(e.first, (float) e.second.lastTime - frame_begin, (float) e.second.lastTime - frame_begin + (float) e.second.lastTiming);
				}
				for (auto e : m_frame.Timings.getFront().m_timestamps)
				{
					m_frame.FrameProfiler.setMarkerByTag(e.first, (float) e.second.lastTime - frame_begin);
				}

				m_frame.FrameProfiler.imguiInterface(0.0f, std::max(frame_end-frame_begin, 10.0f), &frame_profiler_visible);
			}
			if(!pause_frame_profiler) m_frame.Timings.swap();
			m_frame.Timings.getBack().timestamp("Frame Begin");
			//////////////////////////////////////////////////////////////////////////////

			///////////////////////////// MATRIX UPDATING ///////////////////////////////
			if (s_isRotating) // update s_view matrix
			{
				s_rotation = glm::rotate(glm::mat4(1.0f), (float) io.DeltaTime, glm::vec3(0.0f, 1.0f, 0.0f) ) * s_rotation;
			}

			// use waitgetPoses to update matrices, or just use regular stuff
			if (!m_pOvr->m_pHMD)
			{
				s_view = glm::lookAt(glm::vec3(s_eye), glm::vec3(s_center), glm::vec3(0.0f, 1.0f, 0.0f));
				s_view_r = glm::lookAt(glm::vec3(s_eye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(s_center), glm::vec3(0.0f, 1.0f, 0.0f));
			}
			else
			{
				m_pOvr->updateTrackedDevicePoses();
				s_view = m_pOvr->m_mat4eyePosLeft * m_pOvr->m_mat4HMDPose;
				s_view_r = m_pOvr->m_mat4eyePosRight * m_pOvr->m_mat4HMDPose;
			}

			// Update Matrices
			// compute current auxiliary matrices
			glm::mat4 model = s_translation * turntable.getRotationMatrix() * s_rotation * s_scale;

			//++++++++++++++ DEBUG
			static bool animateView = false;
			static bool animateTranslation = false;
			ImGui::Checkbox("Animate View", &animateView); ImGui::SameLine(); ImGui::Checkbox("Animate Translation", &animateTranslation);
			if (animateView)
			{
				glm::vec4 warpCenter  = glm::vec4(sin(elapsedTime*2.0)*0.25f, cos(elapsedTime*2.0)*0.125f, 0.0f, 1.0f);
				glm::vec4 warpEye = s_eye;
				if (animateTranslation) warpEye = s_eye + glm::vec4(-sin(elapsedTime*1.0)*0.125f, -cos(elapsedTime*2.0)*0.125f, 0.0f, 1.0f);
				s_view   = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
				s_view_r = glm::lookAt(glm::vec3(warpEye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(warpCenter), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
			}

			ImGui::Checkbox("Predict HMD Pose", &predictPose);
			//++++++++++++++ DEBUG

			//++++++++++++++ DEBUG
			ImGui::Checkbox("Use Grid Warp", &useGridWarp);
			//++++++++++++++ DEBUG

			//////////////////////////////////////////////////////////////////////////////
				
			////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////

			/************* update color mapping parameters ******************/
			// ray start/end parameters
			shaderProgram->update("uStepSize", s_rayStepSize); 	  // ray step size
			shaderProgram->update("uLodMaxLevel", lodMaxLevel);
			shaderProgram->update("uLodBegin", lodBegin);
			shaderProgram->update("uLodRange", lodRange);  

			// color mapping parameters
			shaderProgram->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
			shaderProgram->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
			shaderProgram->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

			//////////////////////////////////////////////////////////////////////////////
		
			////////////////////////////////  RENDERING //// /////////////////////////////
			OPENGLCONTEXT->setEnabled(GL_BLEND, false);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
			// check for finished left/right images, copy to Front FBOs
			if (chunkedRenderPass->isFinished())
			{
				m_frame.Timings.getBack().beginTimerElapsed("Copy Result LEFT");
				copyFBOContent(FBO, FBO_front, GL_COLOR_BUFFER_BIT);
				copyFBOContent(FBO, FBO_front, GL_DEPTH_BUFFER_BIT);
				m_frame.Timings.getBack().stopTimerElapsed();
			}
			if (chunkedRenderPass_r->isFinished())
			{
				m_frame.Timings.getBack().beginTimerElapsed("Copy Result RIGHT");
				copyFBOContent(FBO_r, FBO_front_r, GL_COLOR_BUFFER_BIT);
				copyFBOContent(FBO_r, FBO_front_r, GL_DEPTH_BUFFER_BIT);
				m_frame.Timings.getBack().stopTimerElapsed();
			}
			//%%%%%%%%%%%% render left image
			if (chunkedRenderPass->isFinished())
			{
				matrices[LEFT][FIRST_HIT] = matrices[LEFT][CURRENT]; // first hit map was rendered with last "current" matrices
			
				matrices[LEFT][CURRENT].model = model; // overwrite with current  matrices
				matrices[LEFT][CURRENT].view = s_view;
				matrices[LEFT][CURRENT].perspective = s_perspective; 

				//++++++++++++++ DEBUG +++++++++++//
				if (predictPose)
				{
					static vr::TrackedDevicePose_t predictedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
					//float predictSecondsAhead = ((float)chunkedRenderPass->getLastNumFramesElapsed()) * 0.011f; // number of frames rendering took
					float predictSecondsAhead = (chunkedRenderPass->getLastTotalRenderTime() + chunkedRenderPass_r->getLastTotalRenderTime()) / 1000.0f;

					if (m_pOvr->m_pHMD)
					{
						m_pOvr->m_pHMD->GetDeviceToAbsoluteTrackingPose(
							vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
							predictSecondsAhead,
							predictedDevicePose,
							vr::k_unMaxTrackedDeviceCount
							);

						if (predictedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
						{
							glm::mat4 predictedHMDPose = glm::inverse(m_pOvr->ConvertSteamVRMatrixToGLMMat4(predictedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking));
							matrices[LEFT][CURRENT].view = m_pOvr->m_mat4eyePosLeft * predictedHMDPose;
						}
					}
					else
					{
						if (animateView)
						{
							glm::vec4 warpCenter = glm::vec4(sin((elapsedTime + predictSecondsAhead)*2.0)*0.25f, cos((elapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);
							glm::vec4 warpEye = s_eye;
							if (animateTranslation) warpEye = s_eye + glm::vec4(-sin((elapsedTime + predictSecondsAhead)*1.0)*0.125f, -cos((elapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);
							matrices[LEFT][CURRENT].view = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), glm::normalize(glm::vec3(sin((elapsedTime + predictSecondsAhead))*0.25f, 1.0f, 0.0f)));
						}
					}
				}
				//++++++++++++++++++++++++++++++++//

				//++++++++++++++ DEBUG +++++++++++//
				// quickly do a depth pass of the models
				if ( m_pOvr->m_pHMD )
				{
					m_frame.Timings.getBack().beginTimerElapsed("Depth Models");
					FBO_scene_depth->bind();
					glClear(GL_DEPTH_BUFFER_BIT);
					OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
					m_pOvr->renderModels(vr::Eye_Left);
					OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
					m_frame.Timings.getBack().stopTimerElapsed();
				}
				//++++++++++++++++++++++++++++++++//

				MatrixSet& firstHit = matrices[LEFT][FIRST_HIT]; /// convenient access
				MatrixSet& current = matrices[LEFT][CURRENT];
			
				//update raycasting matrices for next iteration	// for occlusion frustum
				glm::mat4 firstHitViewToCurrentView  = current.view * (current.model * glm::inverse(firstHit.model)) * glm::inverse(firstHit.view);
				glm::mat4 firstHitViewToTexture = s_modelToTexture * glm::inverse(firstHit.model) * glm::inverse(firstHit.view);
			
				// uvw maps
				m_frame.Timings.getBack().beginTimerElapsed("UVW LEFT");
				uvwRenderPass->setFrameBufferObject( uvwFBO );
				uvwShaderProgram->update("view", current.view);
				uvwShaderProgram->update("model", current.model);
				uvwShaderProgram->update("projection", current.perspective);
				uvwRenderPass->render();
				m_frame.Timings.getBack().stopTimerElapsed();

				// occlusion maps
				m_frame.Timings.getBack().beginTimerElapsed("Occlusion Frustum LEFT");
				occlusionFrustum->setFrameBufferObject( occlusionFrustumFBO );
				occlusionFrustumShader->update("first_hit_map", 6); // left first hit map
				occlusionFrustumShader->update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
				occlusionFrustumShader->update("uFirstHitViewToTexture", firstHitViewToTexture);
				occlusionFrustumShader->update("uProjection", current.perspective);
				occlusionFrustum->render();

				occlusionClipFrustumShader->update("uProjection", current.perspective);
				occlusionClipFrustum->setFrameBufferObject(occlusionClipFrustumFBO);
				occlusionClipFrustumShader->update("uView", current.view);
				occlusionClipFrustumShader->update("uModel", glm::inverse(firstHit.view) );
				occlusionClipFrustumShader->update("uWorldToTexture", s_modelToTexture * glm::inverse(firstHit.model) );
				occlusionClipFrustum->render();

				m_frame.Timings.getBack().stopTimerElapsed();
			}

			// raycasting (chunked)
			shaderProgram->update( "uScreenToTexture", s_modelToTexture * glm::inverse( matrices[LEFT][CURRENT].model ) * glm::inverse( matrices[LEFT][CURRENT].view ) * s_screenToView );
			shaderProgram->update( "uViewToTexture", s_modelToTexture * glm::inverse(matrices[LEFT][CURRENT].model) * glm::inverse(matrices[LEFT][CURRENT].view) );
			shaderProgram->update( "uProjection", matrices[LEFT][CURRENT].perspective);
			shaderProgram->update( "back_uvw_map",  2 );
			shaderProgram->update( "front_uvw_map", 4 );
			shaderProgram->update( "scene_depth_map", 18 );
			shaderProgram->update( "occlusion_map", 8 );
			//shaderProgram->update( "occlusion_clip_frustum_back", 26 );
			shaderProgram->update( "occlusion_clip_frustum_front", 28 );

			m_frame.Timings.getBack().beginTimer("Chunked Raycast LEFT");
			chunkedRenderPass->render();
			m_frame.Timings.getBack().stopTimer("Chunked Raycast LEFT");
		
			//+++++++++ DEBUG  +++++++++++++++++++++++++++++++++++++++++++ 
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		
			//%%%%%%%%%%%% render right image
			// uvw maps
			if (chunkedRenderPass_r->isFinished())
			{
				matrices[RIGHT][FIRST_HIT] = matrices[RIGHT][CURRENT]; // first hit map was rendered with last "current" matrices
			
				matrices[RIGHT][CURRENT].model = model; // overwrite with current  matrices
				matrices[RIGHT][CURRENT].view = s_view_r;
				matrices[RIGHT][CURRENT].perspective = s_perspective_r; 

				//++++++++++++++ DEBUG +++++++++++//
				if (predictPose)
				{
					static vr::TrackedDevicePose_t predictedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
					//float predictSecondsAhead = ((float)chunkedRenderPass_r->getLastNumFramesElapsed()) * 0.011f; // number of frames rendering took
					float predictSecondsAhead = (chunkedRenderPass->getLastTotalRenderTime() + chunkedRenderPass_r->getLastTotalRenderTime() )/ 1000.0f;
					if (m_pOvr->m_pHMD)
					{ 
						m_pOvr->m_pHMD->GetDeviceToAbsoluteTrackingPose(
							vr::ETrackingUniverseOrigin::TrackingUniverseStanding,
							predictSecondsAhead,
							predictedDevicePose,
							vr::k_unMaxTrackedDeviceCount
							);
						if (predictedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
						{
							glm::mat4 predictedHMDPose = glm::inverse(m_pOvr->ConvertSteamVRMatrixToGLMMat4(predictedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking));
							matrices[RIGHT][CURRENT].view = m_pOvr->m_mat4eyePosRight * predictedHMDPose;
						}
					}
					else
					{
						if (animateView)
						{
							glm::vec4 warpCenter = glm::vec4(sin((elapsedTime + predictSecondsAhead)*2.0)*0.25f, cos((elapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);
							glm::vec4 warpEye = s_eye;
							if (animateTranslation) warpEye = s_eye + glm::vec4(-sin((elapsedTime + predictSecondsAhead)*1.0)*0.125f, -cos((elapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);
							matrices[RIGHT][CURRENT].view = glm::lookAt(glm::vec3(warpEye) + glm::vec3(0.15, 0.0, 0.0), glm::vec3(warpCenter), glm::normalize(glm::vec3(sin((elapsedTime + predictSecondsAhead))*0.25f, 1.0f, 0.0f)));
						}
					}
				}

				// quickly do a depth pass of the models
				if ( m_pOvr->m_pHMD )
				{
					FBO_scene_depth_r->bind();
					glClear(GL_DEPTH_BUFFER_BIT);
					OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
					m_pOvr->renderModels(vr::Eye_Right);
					OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
				}

				//++++++++++++++++++++++++++++++++//

				MatrixSet& firstHit = matrices[RIGHT][FIRST_HIT]; /// convenient access
				MatrixSet& current = matrices[RIGHT][CURRENT];

				//update raycasting matrices for next iteration	// for occlusion frustum
				glm::mat4 firstHitViewToCurrentView  = current.view * (current.model * glm::inverse(firstHit.model)) * glm::inverse(firstHit.view);
				glm::mat4 firstHitViewToTexture = s_modelToTexture * glm::inverse(firstHit.model) * glm::inverse(firstHit.view);
			
				m_frame.Timings.getBack().beginTimerElapsed("UVW RIGHT");
				uvwRenderPass->setFrameBufferObject( uvwFBO_r );
				uvwShaderProgram->update( "view", current.view );
				uvwShaderProgram->update( "model", current.model );
				uvwShaderProgram->update( "projection", current.perspective );
				uvwRenderPass->render();
				m_frame.Timings.getBack().stopTimerElapsed();
		
				// occlusion maps 
				m_frame.Timings.getBack().beginTimerElapsed("Occlusion Frustum RIGHT");
				occlusionFrustum->setFrameBufferObject( occlusionFrustumFBO_r );
				occlusionFrustumShader->update("first_hit_map", 7); // right first hit map
				occlusionFrustumShader->update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
				occlusionFrustumShader->update("uFirstHitViewToTexture", firstHitViewToTexture);
				occlusionFrustumShader->update("uProjection", current.perspective);
				occlusionFrustum->render();

				occlusionClipFrustumShader->update("uProjection", current.perspective);
				occlusionClipFrustum->setFrameBufferObject(occlusionClipFrustumFBO_r);
				occlusionClipFrustumShader->update("uView", current.view);
				occlusionClipFrustumShader->update("uModel", glm::inverse(firstHit.view) );
				occlusionClipFrustumShader->update("uWorldToTexture", s_modelToTexture * glm::inverse(current.model));
				occlusionClipFrustum->render();

				m_frame.Timings.getBack().stopTimerElapsed();
			}

			// raycasting (chunked)
			shaderProgram->update("uScreenToTexture", s_modelToTexture * glm::inverse( matrices[RIGHT][CURRENT].model ) * glm::inverse( matrices[RIGHT][CURRENT].view ) * s_screenToView );
			shaderProgram->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[RIGHT][CURRENT].model) * glm::inverse(matrices[RIGHT][CURRENT].view) );
			shaderProgram->update("uProjection", matrices[RIGHT][CURRENT].perspective);
			shaderProgram->update("back_uvw_map",  3);
			shaderProgram->update("front_uvw_map", 5);
			shaderProgram->update("scene_depth_map", 19 );
			shaderProgram->update("occlusion_map", 9);
			//shaderProgram->update( "occlusion_clip_frustum_back", 27 );
			shaderProgram->update( "occlusion_clip_frustum_front", 29 );

			//renderPass_r->render();
			m_frame.Timings.getBack().beginTimer("Chunked Raycast RIGHT");
			chunkedRenderPass_r->render();
			m_frame.Timings.getBack().stopTimer("Chunked Raycast RIGHT");

			//%%%%%%%%%%%% Image Warping		
			glClearColor(0.0f,0.0f,0.0f,0.0f);
			FBO_warp->bind();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			FBO_warp_r->bind();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glClearColor(0.f,0.f,0.f,0.f);
			if (m_pOvr->m_pHMD) // render controller models if possible
			{
				m_frame.Timings.getBack().beginTimerElapsed("Render Models");
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				FBO_warp->bind();
				m_pOvr->renderModels(vr::Eye_Left);

				FBO_warp_r->bind();
				m_pOvr->renderModels(vr::Eye_Right);
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
				m_frame.Timings.getBack().stopTimerElapsed();
			}

			OPENGLCONTEXT->setEnabled(GL_BLEND, true);
			m_frame.Timings.getBack().beginTimerElapsed("Warping");
			if (!useGridWarp)
			{
				// warp left
				quadWarp->setFrameBufferObject( FBO_warp );
				quadWarpShader->update( "tex", 12 ); // last result left
				quadWarpShader->update( "oldView", matrices[LEFT][FIRST_HIT].view ); // update with old view
				quadWarpShader->update( "newView", s_view ); // most current view
				quadWarpShader->update( "projection",  matrices[LEFT][FIRST_HIT].perspective ); 
				quadWarp->render();

				// warp right
				quadWarp->setFrameBufferObject( FBO_warp_r );
				quadWarpShader->update( "tex", 13 ); // last result right
				quadWarpShader->update( "oldView", matrices[RIGHT][FIRST_HIT].view ); // update with old view
				quadWarpShader->update( "newView", s_view_r); // most current view
				quadWarpShader->update( "projection",  matrices[RIGHT][FIRST_HIT].perspective ); 
				quadWarp->render();
			}
			else
			{
				glBlendFunc(GL_ONE, GL_ZERO); // frontmost fragment takes it all
				gridWarp->setFrameBufferObject(FBO_warp);
				gridWarpShader->update( "tex", 12 ); // last result left
				gridWarpShader->update( "depth_map", 24); // last first hit map
				gridWarpShader->update( "uViewOld", matrices[LEFT][FIRST_HIT].view ); // update with old view
				gridWarpShader->update( "uViewNew", s_view ); // most current view
				gridWarpShader->update( "uProjection",  matrices[LEFT][FIRST_HIT].perspective ); 
				gridWarp->render();

				gridWarp->setFrameBufferObject(FBO_warp_r);
				gridWarpShader->update( "tex", 13 ); // last result left
				gridWarpShader->update( "depth_map", 25); // last first hit map
				gridWarpShader->update( "uViewOld", matrices[RIGHT][FIRST_HIT].view ); // update with old view
				gridWarpShader->update( "uViewNew", s_view_r ); // most current view
				gridWarpShader->update( "uProjection",  matrices[RIGHT][FIRST_HIT].perspective );
				gridWarp->render();
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
			}
			OPENGLCONTEXT->setEnabled(GL_BLEND, false);
			m_frame.Timings.getBack().stopTimerElapsed();

			//%%%%%%%%%%%% Submit/Display images
			setDebugView(activeView);

			if ( m_pOvr->m_pHMD ) // submit images only when finished
			{
				//OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				//FBO_warp->bind();
				//m_pOvr->renderModels(vr::Eye_Left);

				//FBO_warp_r->bind();
				//m_pOvr->renderModels(vr::Eye_Right);
				//OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);

				//OPENGLCONTEXT->setEnabled(GL_BLEND, true);
				//showTexShader->update("tex", 14);
				//showTex->setFrameBufferObject(FBO_warp);
				//showTex->setViewport(0, 0, FBO_warp.getWidth(), FBO_warp.getHeight());
				//showTex->render();

				//showTexShader->update("tex", 15);
				//showTex->setFrameBufferObject(FBO_warp_r);
				//showTex->setViewport(0, 0, FBO_warp_r->getWidth(), FBO_warp_r->getHeight());
				//showTex->render();
				//OPENGLCONTEXT->setEnabled(GL_BLEND, false);

				showTex->setFrameBufferObject(0);

				m_pOvr->submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + leftDebugView ], vr::Eye_Left);
				m_pOvr->submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + rightDebugView], vr::Eye_Right);
			}
		
			m_frame.Timings.getBack().timestamp("Frame End");
			if (mirrorScreenTimer > MIRROR_SCREEN_FRAME_INTERVAL || !m_pOvr->m_pHMD)
			{
				{
					showTexShader->update("tex", leftDebugView);
					showTex->setViewport(0, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y);
					showTex->render();
				}
				{
					showTexShader->update("tex", rightDebugView);
					showTex->setViewport((int)getResolution(m_pWindow).x / 2, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y);
					showTex->render();
				}
				//////////////////////////////////////////////////////////////////////////////
				ImGui::Render();
				SDL_GL_SwapWindow( m_pWindow ); // swap buffers

				mirrorScreenTimer = 0.0f;
			}
			else
			{
				glFlush(); // just Flush
			}

		}
	
		ImGui_ImplSdlGL3_Shutdown();
		m_pOvr->shutdown();
		destroyWindow(m_pWindow);
		SDL_Quit();
	
	}

	void profileFPS(float fps)
	{
		m_fpsCounter[m_curFPSidx] = fps;
		m_curFPSidx = (m_curFPSidx + 1) % m_fpsCounter.size(); 
	}

	virtual ~CMainApplication()
	{
		delete m_pOvr;
		delete volume;
		delete quad;
		delete vertexGrid;
		delete grid;
		delete ndcCube;

		delete uvwShaderProgram;
		delete shaderProgram;
		delete occlusionFrustumShader;
		delete occlusionClipFrustumShader;
		delete quadWarpShader;
		delete gridWarpShader;
		delete showTexShader;
		delete depthToTextureShader;

		delete occlusionFrustumFBO;
		delete occlusionFrustumFBO_r;
		delete uvwFBO;
		delete uvwFBO_r;
		delete FBO;
		delete FBO_r;
		delete FBO_front;
		delete FBO_front_r;
		delete occlusionClipFrustumFBO;
		delete occlusionClipFrustumFBO_r;
		delete FBO_warp;
		delete FBO_warp_r;
		delete FBO_scene_depth;
		delete FBO_scene_depth_r;
		delete FBO_debug_depth;
		delete FBO_debug_depth_r;

		delete uvwRenderPass;
		delete renderPass;
		delete renderPass_r;
		delete chunkedRenderPass;
		delete chunkedRenderPass_r;
		delete occlusionFrustum;
		delete occlusionClipFrustum;
		delete quadWarp;
		delete gridWarp;
		delete showTex;
		delete depthToTexture;
	}

};




//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	pMainApplication->initOpenVR();

	pMainApplication->initSceneVariables();

	pMainApplication->loadVolumes();

	pMainApplication->loadGeometries();

	pMainApplication->loadShaders();

	pMainApplication->initFramebuffers();

	pMainApplication->initRenderPasses();
	
	pMainApplication->initTextureUnits();

	pMainApplication->updateTextureUniforms();

	pMainApplication->initUniforms();

	pMainApplication->initGUI();

	pMainApplication->initEventHandlers();

	pMainApplication->loop();

	return 0;
}