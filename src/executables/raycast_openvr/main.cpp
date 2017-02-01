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

static float FRAMEBUFFER_SCALE = 1.0f;
static glm::vec2 FRAMEBUFFER_RESOLUTION(700.f,700.f);
static glm::vec2 WINDOW_RESOLUTION(FRAMEBUFFER_RESOLUTION.x * 2.f, FRAMEBUFFER_RESOLUTION.y);

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

	// set m_pVolume specific parameters
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
	Quad*	m_pQuad;
	Grid*	m_pGrid;
	VertexGrid* m_pVertexGrid;
	Volume*		  m_pNdcCube;
	VolumeSubdiv* m_pVolume;

	// Shader bookkeeping
	ShaderProgram* m_pUvwShader;
	ShaderProgram* m_pRaycastShader;
	ShaderProgram* m_pOcclusionFrustumShader;
	ShaderProgram* m_pOcclusionClipFrustumShader;
	ShaderProgram* m_pQuadWarpShader;
	ShaderProgram* m_pGridWarpShader;
	ShaderProgram* m_pShowTexShader;
	ShaderProgram* m_pDepthToTextureShader;

	// m_pRaycastFBO bookkeeping
	FrameBufferObject* m_pOcclusionFrustumFBO;
	FrameBufferObject* m_pOcclusionFrustumFBO_r;
	FrameBufferObject* m_pUvwFBO;
	FrameBufferObject* m_pUvwFBO_r;
	FrameBufferObject* m_pRaycastFBO;
	FrameBufferObject* m_pRaycastFBO_r;
	FrameBufferObject* m_pRaycastFBO_front;
	FrameBufferObject* m_pRaycastFBO_front_r;
	FrameBufferObject* m_pOcclusionClipFrustumFBO;
	FrameBufferObject* m_pOcclusionClipFrustumFBO_r;
	FrameBufferObject* m_pWarpFBO;
	FrameBufferObject* m_pWarpFBO_r;
	FrameBufferObject* m_SceneDepthFBO;
	FrameBufferObject* m_SceneDepthFBO_r;
	FrameBufferObject* m_DebugDepthFBO;
	FrameBufferObject* m_DebugDepthFBO_r;

	// Renderpass bookkeeping
	RenderPass* m_pUvw; 		
	RenderPass* m_pRaycast; 		
	RenderPass* m_pRaycast_r; 		
	ChunkedAdaptiveRenderPass* m_pRaycastChunked; 
	ChunkedAdaptiveRenderPass* m_pRaycastChunked_r; 
	RenderPass* m_pOcclusionFrustum; 		
	RenderPass* m_pOcclusionClipFrustum; 		
	RenderPass* m_pWarpQuad; 		
	RenderPass* m_pWarpGrid; 		
	RenderPass* m_pShowTex; 		
	RenderPass* m_pDebugDepthToTexture; 	

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> m_sdlEventFunc;
	std::function<bool(const vr::VREvent_t&)> m_vrEventFunc;
	std::function<void(bool, int)> m_trackpadEventFunc;
	std::function<void(int)> m_setDebugViewFunc;

	// Frame profiling
	struct Frame{
		Profiler FrameProfiler;
		SimpleDoubleBuffer<OpenGLTimings> Timings;
	} m_frame;

	//========== MISC ================
	std::vector<std::string> m_shaderDefines;

	Turntable m_turntable;

	int m_iActiveModel;

	bool m_bUseGridWarp;
	int m_iOcclusionBlockSize;
	int m_iVertexGridWidth;
	int m_iVertexGridHeight;
	
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
	int m_iActiveView;
	int  m_iLeftDebugView;
	int m_iRightDebugView;

	bool m_bPredictPose;
	
	bool  m_bIsTouchpadTouched;
	int   m_iTouchpadTrackedDeviceIdx;
	float m_fOldTouchX;
	float m_fOldTouchY;

	float m_fOldX;
	float m_fOldY;

	std::vector<float> m_fpsCounter;
	int m_iCurFpsIdx;

	struct MatrixSet
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 perspective;
	} matrices[2][2]; // left, right, firsthit, current


public:
	CMainApplication( int argc, char *argv[] )
		: m_bUseGridWarp(false)
		, m_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES))
		, m_fpsCounter(120)
		, m_iCurFpsIdx(0)
		, m_iActiveModel(0)
		, m_iActiveView(WARPED)
		, m_iLeftDebugView(14)
		, m_iRightDebugView(15)
		, m_fOldX(0.0f)
		, m_fOldY(0.0f)
		, m_fOldTouchX(0.5f)
		, m_fOldTouchY(0.5f)
		, m_iTouchpadTrackedDeviceIdx(-1)
		, m_bIsTouchpadTouched(false)
		, m_bPredictPose(true)
		, m_iOcclusionBlockSize(6)
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
			FRAMEBUFFER_RESOLUTION = glm::vec2(width, height) * FRAMEBUFFER_SCALE;
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
		if (!m_pOvr->m_pHMD)
		{
			s_aspect = FRAMEBUFFER_RESOLUTION.x / (FRAMEBUFFER_RESOLUTION.y);
			updatePerspective();
		}
		else
		{
			s_view = m_pOvr->m_mat4eyePosLeft * s_view;
			s_view_r = m_pOvr->m_mat4eyePosRight * s_view_r;
			s_perspective = m_pOvr->m_mat4ProjectionLeft; 
			s_perspective_r = m_pOvr->m_mat4ProjectionRight;
		}
	
		updateNearHeightWidth();
		updateScreenToViewMatrix();
	}

	void loadGeometries()
	{
		m_pQuad = new Quad();
	
		m_pVolume = new VolumeSubdiv(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 4);
	
		m_pGrid = new Grid(400, 400, 0.0025f, 0.0025f, false);

		m_iVertexGridWidth = (int) FRAMEBUFFER_RESOLUTION.x / m_iOcclusionBlockSize;
		m_iVertexGridHeight = (int) FRAMEBUFFER_RESOLUTION.y   / m_iOcclusionBlockSize;
		
		m_pVertexGrid = new VertexGrid(m_iVertexGridWidth, m_iVertexGridHeight, false, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(96, 96)); //dunno what is a good group size?

		m_pNdcCube = new Volume(1.0f); // a cube that spans -1 .. 1 
	}

	void loadShaders()
	{
		m_pUvwShader = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag", m_shaderDefines);
		m_pRaycastShader = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
		m_pOcclusionFrustumShader = new ShaderProgram("/raycast/occlusionFrustum.vert", "/raycast/occlusionFrustum.frag", "/raycast/occlusionFrustum.geom", m_shaderDefines);
		m_pOcclusionClipFrustumShader = new ShaderProgram("/raycast/occlusionClipFrustum.vert", "/raycast/occlusionClipFrustum.frag", m_shaderDefines);
		m_pQuadWarpShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleWarp.frag", m_shaderDefines);
		m_pGridWarpShader = new ShaderProgram("/raycast/gridWarp.vert", "/raycast/gridWarp.frag", m_shaderDefines);
		m_pShowTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
		m_pDepthToTextureShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/raycast/debug_depthToTexture.frag");
	}

	void initUniforms()
	{
		m_pUvwShader->update("model", s_translation * s_rotation * s_scale);
		m_pUvwShader->update("view", s_view);
		m_pUvwShader->update("projection", s_perspective);

		m_pRaycastShader->update("uStepSize", s_rayStepSize);
		m_pRaycastShader->update("uViewport", glm::vec4(0,0,FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y));	
		m_pRaycastShader->update("uResolution", glm::vec4(FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y,0,0));

		{bool hasShadow = false; for (auto e : m_shaderDefines) { hasShadow |= (e == "SHADOW_SAMPLING"); } if ( hasShadow ){
		m_pRaycastShader->update("uShadowRayDirection", glm::normalize(glm::vec3(0.0f,-0.5f,-1.0f))); // full range of values in window
		m_pRaycastShader->update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary
		}}

		m_pQuadWarpShader->update( "blendColor", 1.0f );
		m_pQuadWarpShader->update( "uFarPlane", s_far );

		m_pOcclusionFrustumShader->update("uOcclusionBlockSize", m_iOcclusionBlockSize);
		m_pOcclusionFrustumShader->update("uGridSize", glm::vec4(m_iVertexGridWidth, m_iVertexGridHeight, 1.0f / (float) m_iVertexGridWidth, 1.0f / m_iVertexGridHeight));

	}

	void initFramebuffers()
	{
		DEBUGLOG->log("FrameBufferObject Creation: m_pVolume uvw coords"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pUvwFBO = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO->addColorAttachments(2); // front UVRs and back UVRs
		m_pUvwFBO_r = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int)  FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO_r->addColorAttachments(2); // front UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: ray casting"); DEBUGLOG->indent();
		m_pRaycastFBO = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)	FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO_r = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO_front = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO_front_r = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		DEBUGLOG->outdent();
		

		DEBUGLOG->log("FrameBufferObject Creation: occlusion frustum"); DEBUGLOG->indent();
		m_pOcclusionFrustumFBO = new FrameBufferObject(   m_pOcclusionFrustumShader->getOutputInfoMap(), m_pUvwFBO->getWidth(),   m_pUvwFBO->getHeight() );
		m_pOcclusionFrustumFBO_r = new FrameBufferObject( m_pOcclusionFrustumShader->getOutputInfoMap(), m_pUvwFBO_r->getWidth(), m_pUvwFBO_r->getHeight() );
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: occlusion clip frustum"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pOcclusionClipFrustumFBO = new FrameBufferObject(   m_pOcclusionClipFrustumShader->getOutputInfoMap(), m_pUvwFBO->getWidth(),   m_pUvwFBO->getHeight() );
		m_pOcclusionClipFrustumFBO_r = new FrameBufferObject( m_pOcclusionClipFrustumShader->getOutputInfoMap(), m_pUvwFBO_r->getWidth(), m_pUvwFBO_r->getHeight() );
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: quad warping"); DEBUGLOG->indent();
		m_pWarpFBO = new FrameBufferObject(m_pQuadWarpShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pWarpFBO_r = new FrameBufferObject(m_pQuadWarpShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: scene depth"); DEBUGLOG->indent();
		m_SceneDepthFBO = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y); // has only a depth buffer, no color attachments
		m_SceneDepthFBO_r = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y); // has only a depth buffer, no color attachments
		m_SceneDepthFBO->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		m_SceneDepthFBO_r->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: DEBUG depth to texture"); DEBUGLOG->indent();
		m_DebugDepthFBO = new FrameBufferObject(m_pDepthToTextureShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_DebugDepthFBO_r = new FrameBufferObject(m_pDepthToTextureShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		DEBUGLOG->outdent();
	}

	void initTextureUnits()
	{
		OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture[m_iActiveModel], GL_TEXTURE0, GL_TEXTURE_3D);
		OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D); // transfer function

		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO->getDepthTextureHandle(), GL_TEXTURE6, GL_TEXTURE_2D); // left first hit map
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_r->getDepthTextureHandle(), GL_TEXTURE7, GL_TEXTURE_2D); // right first hit map

		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D); // left  raycasting result
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE11, GL_TEXTURE_2D);// right raycasting result
	
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_front->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE12, GL_TEXTURE_2D); // left  raycasting result (for display)
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_front_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE13, GL_TEXTURE_2D);// right raycasting result (for display)

		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_front->getDepthTextureHandle(), GL_TEXTURE24, GL_TEXTURE_2D); // left  raycasting first hit map  (for display)
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO_front_r->getDepthTextureHandle(), GL_TEXTURE25, GL_TEXTURE_2D);// right  raycasting first hit map (for display)

		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionFrustumFBO->getDepthTextureHandle(), GL_TEXTURE8, GL_TEXTURE_2D); // left occlusion map
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionFrustumFBO_r->getDepthTextureHandle(), GL_TEXTURE9, GL_TEXTURE_2D); // right occlusion map

		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE26, GL_TEXTURE_2D); // left occlusion clip map back
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE27, GL_TEXTURE_2D); // right occlusion clip map back
	
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE28, GL_TEXTURE_2D); // left occlusion clip map front
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE29, GL_TEXTURE_2D); // right occlusion clip map front
	
		OPENGLCONTEXT->bindTextureToUnit(m_pWarpFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE14, GL_TEXTURE_2D); // left  raycasting result (for display)
		OPENGLCONTEXT->bindTextureToUnit(m_pWarpFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE15, GL_TEXTURE_2D);// right raycasting result (for display)
	
		OPENGLCONTEXT->bindTextureToUnit(m_DebugDepthFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE16, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_DebugDepthFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE17, GL_TEXTURE_2D);

		OPENGLCONTEXT->bindTextureToUnit(m_SceneDepthFBO->getDepthTextureHandle(), GL_TEXTURE18, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_SceneDepthFBO_r->getDepthTextureHandle(), GL_TEXTURE19, GL_TEXTURE_2D);
		
		OPENGLCONTEXT->activeTexture(GL_TEXTURE20);
	}

	void updateTextureUniforms()
	{
		m_pRaycastShader->update("volume_texture", 0); // m_pVolume texture
		m_pRaycastShader->update("transferFunctionTex", 1);
	}

	void initRenderPasses()
	{
		m_pUvw = new RenderPass(m_pUvwShader, m_pUvwFBO);
		m_pUvw->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		m_pUvw->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		m_pUvw->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
		m_pUvw->addRenderable(m_pVolume);

		m_pRaycast = new RenderPass(m_pRaycastShader, m_pRaycastFBO);
		m_pRaycast->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast->addRenderable(m_pQuad);
		m_pRaycast->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		m_pRaycast->addDisable(GL_BLEND);

		m_pRaycast_r = new RenderPass(m_pRaycastShader, m_pRaycastFBO_r);
		m_pRaycast_r->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast_r->addRenderable(m_pQuad);
		m_pRaycast_r->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		m_pRaycast_r->addDisable(GL_BLEND);

		glm::ivec2 viewportSize = glm::ivec2((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		glm::ivec2 chunkSize = glm::ivec2(96,96);
		m_pRaycastChunked = new ChunkedAdaptiveRenderPass(
			m_pRaycast,
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		m_pRaycastChunked_r = new ChunkedAdaptiveRenderPass(
			m_pRaycast_r,
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		
		//m_pRaycastChunked->activateClearbits();
		//m_pRaycastChunked_r->activateClearbits();

		m_pOcclusionFrustum = new RenderPass(m_pOcclusionFrustumShader, m_pOcclusionFrustumFBO);
		m_pOcclusionFrustum->addRenderable(m_pVertexGrid);
		m_pOcclusionFrustum->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		m_pOcclusionFrustum->addEnable(GL_DEPTH_TEST);
		m_pOcclusionFrustum->addDisable(GL_BLEND);

		m_pOcclusionClipFrustum = new RenderPass(m_pOcclusionClipFrustumShader, m_pOcclusionClipFrustumFBO);
		m_pOcclusionClipFrustum->addRenderable(m_pNdcCube);	
		m_pOcclusionClipFrustum->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		m_pOcclusionClipFrustum->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		m_pOcclusionClipFrustum->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results

		m_pWarpQuad = new RenderPass(m_pQuadWarpShader, m_pWarpFBO);
		m_pWarpQuad->addRenderable(m_pQuad);

		m_pWarpGrid = new RenderPass(m_pGridWarpShader, m_pWarpFBO);
		m_pWarpGrid->addEnable(GL_DEPTH_TEST);
		m_pWarpGrid->addRenderable(m_pGrid);

		m_pShowTex = new RenderPass(m_pShowTexShader,0);
		m_pShowTex->addRenderable(m_pQuad);

		m_pDebugDepthToTexture = new RenderPass(m_pDepthToTextureShader, m_DebugDepthFBO);
		m_pDebugDepthToTexture->addClearBit(GL_COLOR_BUFFER_BIT);
		m_pDebugDepthToTexture->addDisable(GL_DEPTH_TEST);
		m_pDebugDepthToTexture->addRenderable(m_pQuad);
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
		m_sdlEventFunc = [&](SDL_Event *event)
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
							m_iActiveView = (m_iActiveView + 1) % NUM_VIEWS;
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

					float d_x = event->motion.x - m_fOldX;
					float d_y = event->motion.y - m_fOldY;

					if ( m_turntable.getDragActive() )
					{
						m_turntable.dragBy(d_x, d_y, s_view);
					}

					m_fOldX = (float) event->motion.x;
					m_fOldY = (float) event->motion.y;
					break;
				}
				case SDL_MOUSEBUTTONDOWN:
				{
					if (event->button.button == SDL_BUTTON_LEFT)
					{
						m_turntable.setDragActive(true);
					}
					if (event->button.button == SDL_BUTTON_RIGHT)
					{
						unsigned char pick_col[20];
						glReadPixels((int) m_fOldX - 2, (int) WINDOW_RESOLUTION.y - (int) m_fOldY, 5, 1, GL_RGBA, GL_UNSIGNED_BYTE, pick_col);

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
						m_turntable.setDragActive(false);
					}
					break;
				}
			}
			return true;
		};

		m_vrEventFunc = [&](const vr::VREvent_t & event)
		{
			switch( event.eventType )
			{
				case vr::VREvent_ButtonPress:
				{
					if (event.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) { return false; } // nevermind
				
					switch(event.data.controller.button)
					{
					case vr::k_EButton_Axis0: // touchpad
							m_iActiveView = (m_iActiveView + 1) % NUM_VIEWS;
						break;
					case vr::k_EButton_Axis1: // trigger
							m_iActiveView = (m_iActiveView <= 0) ? NUM_VIEWS - 1 : m_iActiveView - 1;
						break;
					case vr::k_EButton_Grip: // grip
						//TODO do something with the model matrix
						m_bUseGridWarp = !m_bUseGridWarp; // DEBUG
						break;
					}

					DEBUGLOG->log("button press: ", event.data.controller.button);
					break;
				}
				case vr::VREvent_ButtonTouch: // generated for touchpad
					if (event.data.controller.button == vr::k_EButton_Axis0) // reset coords 
					{ 
						m_iTouchpadTrackedDeviceIdx = event.trackedDeviceIndex;
						m_bIsTouchpadTouched = true;
						m_turntable.setDragActive(m_bIsTouchpadTouched);

						vr::VRControllerState_t state_;
						if (m_pOvr->m_pHMD->GetControllerState(event.trackedDeviceIndex, &state_))

						m_fOldTouchX = state_.rAxis[0].x;
						m_fOldTouchY = state_.rAxis[0].y;
					}
					break;
				case vr::VREvent_ButtonUntouch:
					if (event.data.controller.button == vr::k_EButton_Axis0) // reset coords 
					{
						m_iTouchpadTrackedDeviceIdx = -1;
						m_bIsTouchpadTouched = false;
						m_turntable.setDragActive(m_bIsTouchpadTouched);
					}
					break;
				//case vr::VREvent_TouchPadMove: // this event is never fired in normal mode
				//	break;
			}
			return false;
		};

		// seperate handler for touchpad, since no event will be generated for touch-movement
		m_trackpadEventFunc = [&](bool isTouched, int deviceIdx)
		{
			if (isTouched && deviceIdx != -1)
			{
				vr::VRControllerState_t state;
				m_pOvr->m_pHMD->GetControllerState(deviceIdx, &state);

				float d_x = state.rAxis[0].x - m_fOldTouchX;
				float d_y = state.rAxis[0].y - m_fOldTouchY;

				if (m_turntable.getDragActive())
				{
					m_turntable.dragBy(d_x * 40.0f, -d_y * 40.0f, s_view);
				}

				m_fOldTouchX = state.rAxis[0].x;
				m_fOldTouchY = state.rAxis[0].y;
			}
		};

		m_setDebugViewFunc = [&](int view)
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
					m_iLeftDebugView = view * 2 + 2;
					if (m_iLeftDebugView >= 16 ) m_iLeftDebugView = 2;
					m_iRightDebugView = m_iLeftDebugView + 1;
					break;
				case OCCLUSION:
				case FIRST_HIT_:
					m_iLeftDebugView = 16;
					m_iRightDebugView = m_iLeftDebugView + 1;
					// convert left
					m_pDepthToTextureShader->update("depth_texture", view * 2 + 2);
					m_pDepthToTextureShader->update("uProjection", s_perspective);
					m_pDepthToTextureShader->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[LEFT][CURRENT].model) * glm::inverse(matrices[LEFT][CURRENT].view) );
					m_pDebugDepthToTexture->setFrameBufferObject(m_DebugDepthFBO);
					m_pDebugDepthToTexture->render();

					// convert right
					m_pDepthToTextureShader->update("depth_texture", view * 2 + 3);
					m_pDepthToTextureShader->update("uProjection", s_perspective_r);
					m_pDepthToTextureShader->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[RIGHT][CURRENT].model) * glm::inverse(matrices[RIGHT][CURRENT].view) );
					m_pDebugDepthToTexture->setFrameBufferObject(m_DebugDepthFBO_r);
					m_pDebugDepthToTexture->render();
					break;
				case OCCLUSION_CLIP_FRUSTUM_BACK:
					m_iLeftDebugView = 26;
					m_iRightDebugView = m_iLeftDebugView + 1;
					break;
				case OCCLUSION_CLIP_FRUSTUM_FRONT:
					m_iLeftDebugView = 28;
					m_iRightDebugView = m_iLeftDebugView + 1;
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
			pollSDLEvents(m_pWindow, m_sdlEventFunc);
			m_pOvr->PollVREvents(m_vrEventFunc);
			m_trackpadEventFunc(m_bIsTouchpadTouched, m_iTouchpadTrackedDeviceIdx); // handle trackpad touch seperately
		
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
				ImGui::Text("Parameters related to m_pVolume rendering");
				ImGui::DragFloatRange2("windowing range", &s_windowingMinValue, &s_windowingMaxValue, 5.0f, (float) s_minValue, (float) s_maxValue); // grayscale ramp boundaries
        		ImGui::SliderFloat("ray step size",   &s_rayStepSize,  0.0001f, 0.1f, "%.5f", 2.0f);
			}
        
			ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating m_pVolume

		
    		if (ImGui::ListBox("active model", &m_iActiveModel, s_models, (int)(sizeof(s_models)/sizeof(*s_models)), 2))
    		{
				activateVolume(m_volumeData[m_iActiveModel]);
				s_rotation = s_rotation * glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));
				static glm::vec3 shadowDir = glm::normalize(glm::vec3(0.0f,-0.5f,1.0f));
				shadowDir.y = -shadowDir.y;
				m_pRaycastShader->update("uShadowRayDirection", shadowDir ); // full range of values in window
				OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture[m_iActiveModel], GL_TEXTURE0, GL_TEXTURE_3D);
				TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, (TransferFunctionPresets::Preset) m_iActiveModel);
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
			if (profiler_visible) { m_pRaycastChunked->imguiInterface(&profiler_visible); };
			ImGui::NextColumn();
			ImGui::Checkbox("Chunk Perf Profiler Right", &profiler_visible_r);
			if (profiler_visible_r) { m_pRaycastChunked_r->imguiInterface(&profiler_visible_r); };
			ImGui::NextColumn();
			ImGui::Columns(1);
		
			ImGui::Separator();
			ImGui::Columns(2);
			ImGui::SliderInt("Left Debug View", &m_iLeftDebugView, 2, 15);
			ImGui::NextColumn();
			ImGui::SliderInt("Right Debug View", &m_iRightDebugView, 2, 15);
			ImGui::NextColumn();
			ImGui::Columns(1);
			ImGui::Separator();

			static float warpFarPlane = s_far;
			if (ImGui::SliderFloat("Far", &warpFarPlane, s_near, s_far))
			{
				m_pQuadWarpShader->update( "uFarPlane", warpFarPlane ); 
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
				updateView();
			}
			else
			{
				m_pOvr->updateTrackedDevicePoses();
				s_view = m_pOvr->m_mat4eyePosLeft * m_pOvr->m_mat4HMDPose;
				s_view_r = m_pOvr->m_mat4eyePosRight * m_pOvr->m_mat4HMDPose;
			}

			// Update Matrices
			// compute current auxiliary matrices
			glm::mat4 model = s_translation * m_turntable.getRotationMatrix() * s_rotation * s_scale;

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
				s_view_r = glm::lookAt(glm::vec3(warpEye) +  glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(warpCenter) + glm::vec3(s_eyeDistance,0.0,0.0), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
			}

			ImGui::Checkbox("Predict HMD Pose", &m_bPredictPose);
			//++++++++++++++ DEBUG

			//++++++++++++++ DEBUG
			ImGui::Checkbox("Use Grid Warp", &m_bUseGridWarp);
			//++++++++++++++ DEBUG

			//////////////////////////////////////////////////////////////////////////////
				
			////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////

			/************* update color mapping parameters ******************/
			// ray start/end parameters
			m_pRaycastShader->update("uStepSize", s_rayStepSize); 	  // ray step size
			m_pRaycastShader->update("uLodMaxLevel", lodMaxLevel);
			m_pRaycastShader->update("uLodBegin", lodBegin);
			m_pRaycastShader->update("uLodRange", lodRange);  

			// color mapping parameters
			m_pRaycastShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
			m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
			m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

			//////////////////////////////////////////////////////////////////////////////
		
			////////////////////////////////  RENDERING //// /////////////////////////////
			OPENGLCONTEXT->setEnabled(GL_BLEND, false);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
			// check for finished left/right images, copy to Front FBOs
			if (m_pRaycastChunked->isFinished())
			{
				m_frame.Timings.getBack().beginTimerElapsed("Copy Result LEFT");
				copyFBOContent(m_pRaycastFBO, m_pRaycastFBO_front, GL_COLOR_BUFFER_BIT);
				copyFBOContent(m_pRaycastFBO, m_pRaycastFBO_front, GL_DEPTH_BUFFER_BIT);
				m_frame.Timings.getBack().stopTimerElapsed();
			}
			if (m_pRaycastChunked_r->isFinished())
			{
				m_frame.Timings.getBack().beginTimerElapsed("Copy Result RIGHT");
				copyFBOContent(m_pRaycastFBO_r, m_pRaycastFBO_front_r, GL_COLOR_BUFFER_BIT);
				copyFBOContent(m_pRaycastFBO_r, m_pRaycastFBO_front_r, GL_DEPTH_BUFFER_BIT);
				m_frame.Timings.getBack().stopTimerElapsed();
			}
			//%%%%%%%%%%%% render left image
			if (m_pRaycastChunked->isFinished())
			{
				matrices[LEFT][FIRST_HIT] = matrices[LEFT][CURRENT]; // first hit map was rendered with last "current" matrices
			
				matrices[LEFT][CURRENT].model = model; // overwrite with current  matrices
				matrices[LEFT][CURRENT].view = s_view;
				matrices[LEFT][CURRENT].perspective = s_perspective; 

				//++++++++++++++ DEBUG +++++++++++//
				if (m_bPredictPose)
				{
					static vr::TrackedDevicePose_t predictedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
					//float predictSecondsAhead = ((float)m_pRaycastChunked->getLastNumFramesElapsed()) * 0.011f; // number of frames rendering took
					float predictSecondsAhead = (m_pRaycastChunked->getLastTotalRenderTime() + m_pRaycastChunked_r->getLastTotalRenderTime()) / 1000.0f;

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
					m_SceneDepthFBO->bind();
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
				m_pUvw->setFrameBufferObject( m_pUvwFBO );
				m_pUvwShader->update("view", current.view);
				m_pUvwShader->update("model", current.model);
				m_pUvwShader->update("projection", current.perspective);
				m_pUvw->render();
				m_frame.Timings.getBack().stopTimerElapsed();

				// occlusion maps
				m_frame.Timings.getBack().beginTimerElapsed("Occlusion Frustum LEFT");
				m_pOcclusionFrustum->setFrameBufferObject( m_pOcclusionFrustumFBO );
				m_pOcclusionFrustumShader->update("first_hit_map", 6); // left first hit map
				m_pOcclusionFrustumShader->update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
				m_pOcclusionFrustumShader->update("uFirstHitViewToTexture", firstHitViewToTexture);
				m_pOcclusionFrustumShader->update("uProjection", current.perspective);
				m_pOcclusionFrustum->render();

				m_pOcclusionClipFrustumShader->update("uProjection", current.perspective);
				m_pOcclusionClipFrustum->setFrameBufferObject(m_pOcclusionClipFrustumFBO);
				m_pOcclusionClipFrustumShader->update("uView", current.view);
				m_pOcclusionClipFrustumShader->update("uModel", glm::inverse(firstHit.view) );
				m_pOcclusionClipFrustumShader->update("uWorldToTexture", s_modelToTexture * glm::inverse(firstHit.model) );
				m_pOcclusionClipFrustum->render();

				m_frame.Timings.getBack().stopTimerElapsed();
			}

			// raycasting (chunked)
			m_pRaycastShader->update( "uScreenToTexture", s_modelToTexture * glm::inverse( matrices[LEFT][CURRENT].model ) * glm::inverse( matrices[LEFT][CURRENT].view ) * s_screenToView );
			m_pRaycastShader->update( "uViewToTexture", s_modelToTexture * glm::inverse(matrices[LEFT][CURRENT].model) * glm::inverse(matrices[LEFT][CURRENT].view) );
			m_pRaycastShader->update( "uProjection", matrices[LEFT][CURRENT].perspective);
			m_pRaycastShader->update( "back_uvw_map",  2 );
			m_pRaycastShader->update( "front_uvw_map", 4 );
			m_pRaycastShader->update( "scene_depth_map", 18 );
			m_pRaycastShader->update( "occlusion_map", 8 );
			//m_pRaycastShader->update( "occlusion_clip_frustum_back", 26 );
			m_pRaycastShader->update( "occlusion_clip_frustum_front", 28 );

			m_frame.Timings.getBack().beginTimer("Chunked Raycast LEFT");
			m_pRaycastChunked->render();
			m_frame.Timings.getBack().stopTimer("Chunked Raycast LEFT");
		
			//+++++++++ DEBUG  +++++++++++++++++++++++++++++++++++++++++++ 
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		
			//%%%%%%%%%%%% render right image
			// uvw maps
			if (m_pRaycastChunked_r->isFinished())
			{
				matrices[RIGHT][FIRST_HIT] = matrices[RIGHT][CURRENT]; // first hit map was rendered with last "current" matrices
			
				matrices[RIGHT][CURRENT].model = model; // overwrite with current  matrices
				matrices[RIGHT][CURRENT].view = s_view_r;
				matrices[RIGHT][CURRENT].perspective = s_perspective_r; 

				//++++++++++++++ DEBUG +++++++++++//
				if (m_bPredictPose)
				{
					static vr::TrackedDevicePose_t predictedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
					//float predictSecondsAhead = ((float)m_pRaycastChunked_r->getLastNumFramesElapsed()) * 0.011f; // number of frames rendering took
					float predictSecondsAhead = (m_pRaycastChunked->getLastTotalRenderTime() + m_pRaycastChunked_r->getLastTotalRenderTime() )/ 1000.0f;
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
					m_SceneDepthFBO_r->bind();
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
				m_pUvw->setFrameBufferObject( m_pUvwFBO_r );
				m_pUvwShader->update( "view", current.view );
				m_pUvwShader->update( "model", current.model );
				m_pUvwShader->update( "projection", current.perspective );
				m_pUvw->render();
				m_frame.Timings.getBack().stopTimerElapsed();
		
				// occlusion maps 
				m_frame.Timings.getBack().beginTimerElapsed("Occlusion Frustum RIGHT");
				m_pOcclusionFrustum->setFrameBufferObject( m_pOcclusionFrustumFBO_r );
				m_pOcclusionFrustumShader->update("first_hit_map", 7); // right first hit map
				m_pOcclusionFrustumShader->update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
				m_pOcclusionFrustumShader->update("uFirstHitViewToTexture", firstHitViewToTexture);
				m_pOcclusionFrustumShader->update("uProjection", current.perspective);
				m_pOcclusionFrustum->render();

				m_pOcclusionClipFrustumShader->update("uProjection", current.perspective);
				m_pOcclusionClipFrustum->setFrameBufferObject(m_pOcclusionClipFrustumFBO_r);
				m_pOcclusionClipFrustumShader->update("uView", current.view);
				m_pOcclusionClipFrustumShader->update("uModel", glm::inverse(firstHit.view) );
				m_pOcclusionClipFrustumShader->update("uWorldToTexture", s_modelToTexture * glm::inverse(current.model));
				m_pOcclusionClipFrustum->render();

				m_frame.Timings.getBack().stopTimerElapsed();
			}

			// raycasting (chunked)
			m_pRaycastShader->update("uScreenToTexture", s_modelToTexture * glm::inverse( matrices[RIGHT][CURRENT].model ) * glm::inverse( matrices[RIGHT][CURRENT].view ) * s_screenToView );
			m_pRaycastShader->update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[RIGHT][CURRENT].model) * glm::inverse(matrices[RIGHT][CURRENT].view) );
			m_pRaycastShader->update("uProjection", matrices[RIGHT][CURRENT].perspective);
			m_pRaycastShader->update("back_uvw_map",  3);
			m_pRaycastShader->update("front_uvw_map", 5);
			m_pRaycastShader->update("scene_depth_map", 19 );
			m_pRaycastShader->update("occlusion_map", 9);
			//m_pRaycastShader->update( "occlusion_clip_frustum_back", 27 );
			m_pRaycastShader->update( "occlusion_clip_frustum_front", 29 );

			//m_pRaycast_r->render();
			m_frame.Timings.getBack().beginTimer("Chunked Raycast RIGHT");
			m_pRaycastChunked_r->render();
			m_frame.Timings.getBack().stopTimer("Chunked Raycast RIGHT");

			//%%%%%%%%%%%% Image Warping		
			glClearColor(0.0f,0.0f,0.0f,0.0f);
			m_pWarpFBO->bind();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			m_pWarpFBO_r->bind();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glClearColor(0.f,0.f,0.f,0.f);
			if (m_pOvr->m_pHMD) // render controller models if possible
			{
				m_frame.Timings.getBack().beginTimerElapsed("Render Models");
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				m_pWarpFBO->bind();
				m_pOvr->renderModels(vr::Eye_Left);

				m_pWarpFBO_r->bind();
				m_pOvr->renderModels(vr::Eye_Right);
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
				m_frame.Timings.getBack().stopTimerElapsed();
			}

			OPENGLCONTEXT->setEnabled(GL_BLEND, true);
			m_frame.Timings.getBack().beginTimerElapsed("Warping");
			if (!m_bUseGridWarp)
			{
				// warp left
				m_pWarpQuad->setFrameBufferObject( m_pWarpFBO );
				m_pQuadWarpShader->update( "tex", 12 ); // last result left
				m_pQuadWarpShader->update( "oldView", matrices[LEFT][FIRST_HIT].view ); // update with old view
				m_pQuadWarpShader->update( "newView", s_view ); // most current view
				m_pQuadWarpShader->update( "projection",  matrices[LEFT][FIRST_HIT].perspective ); 
				m_pWarpQuad->render();

				// warp right
				m_pWarpQuad->setFrameBufferObject( m_pWarpFBO_r );
				m_pQuadWarpShader->update( "tex", 13 ); // last result right
				m_pQuadWarpShader->update( "oldView", matrices[RIGHT][FIRST_HIT].view ); // update with old view
				m_pQuadWarpShader->update( "newView", s_view_r); // most current view
				m_pQuadWarpShader->update( "projection",  matrices[RIGHT][FIRST_HIT].perspective ); 
				m_pWarpQuad->render();
			}
			else
			{
				glBlendFunc(GL_ONE, GL_ZERO); // frontmost fragment takes it all
				m_pWarpGrid->setFrameBufferObject(m_pWarpFBO);
				m_pGridWarpShader->update( "tex", 12 ); // last result left
				m_pGridWarpShader->update( "depth_map", 24); // last first hit map
				m_pGridWarpShader->update( "uViewOld", matrices[LEFT][FIRST_HIT].view ); // update with old view
				m_pGridWarpShader->update( "uViewNew", s_view ); // most current view
				m_pGridWarpShader->update( "uProjection",  matrices[LEFT][FIRST_HIT].perspective ); 
				m_pWarpGrid->render();

				m_pWarpGrid->setFrameBufferObject(m_pWarpFBO_r);
				m_pGridWarpShader->update( "tex", 13 ); // last result left
				m_pGridWarpShader->update( "depth_map", 25); // last first hit map
				m_pGridWarpShader->update( "uViewOld", matrices[RIGHT][FIRST_HIT].view ); // update with old view
				m_pGridWarpShader->update( "uViewNew", s_view_r ); // most current view
				m_pGridWarpShader->update( "uProjection",  matrices[RIGHT][FIRST_HIT].perspective );
				m_pWarpGrid->render();
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
			}
			OPENGLCONTEXT->setEnabled(GL_BLEND, false);
			m_frame.Timings.getBack().stopTimerElapsed();

			//%%%%%%%%%%%% Submit/Display images
			m_setDebugViewFunc(m_iActiveView);

			if ( m_pOvr->m_pHMD ) // submit images only when finished
			{
				//OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				//m_pWarpFBO->bind();
				//m_pOvr->renderModels(vr::Eye_Left);

				//m_pWarpFBO_r->bind();
				//m_pOvr->renderModels(vr::Eye_Right);
				//OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);

				//OPENGLCONTEXT->setEnabled(GL_BLEND, true);
				//m_pShowTexShader->update("tex", 14);
				//m_pShowTex->setFrameBufferObject(m_pWarpFBO);
				//m_pShowTex->setViewport(0, 0, m_pWarpFBO.getWidth(), m_pWarpFBO.getHeight());
				//m_pShowTex->render();

				//m_pShowTexShader->update("tex", 15);
				//m_pShowTex->setFrameBufferObject(m_pWarpFBO_r);
				//m_pShowTex->setViewport(0, 0, m_pWarpFBO_r->getWidth(), m_pWarpFBO_r->getHeight());
				//m_pShowTex->render();
				//OPENGLCONTEXT->setEnabled(GL_BLEND, false);

				m_pShowTex->setFrameBufferObject(0);

				m_pOvr->submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + m_iLeftDebugView ], vr::Eye_Left);
				m_pOvr->submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + m_iRightDebugView], vr::Eye_Right);
			}
		
			m_frame.Timings.getBack().timestamp("Frame End");
			if (mirrorScreenTimer > MIRROR_SCREEN_FRAME_INTERVAL || !m_pOvr->m_pHMD)
			{
				{
					m_pShowTexShader->update("tex", m_iLeftDebugView);
					m_pShowTex->setViewport(0, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y);
					m_pShowTex->render();
				}
				{
					m_pShowTexShader->update("tex", m_iRightDebugView);
					m_pShowTex->setViewport((int)getResolution(m_pWindow).x / 2, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y);
					m_pShowTex->render();
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
		m_fpsCounter[m_iCurFpsIdx] = fps;
		m_iCurFpsIdx = (m_iCurFpsIdx + 1) % m_fpsCounter.size(); 
	}

	virtual ~CMainApplication()
	{
		delete m_pOvr;
		delete m_pVolume;
		delete m_pQuad;
		delete m_pVertexGrid;
		delete m_pGrid;
		delete m_pNdcCube;

		delete m_pUvwShader;
		delete m_pRaycastShader;
		delete m_pOcclusionFrustumShader;
		delete m_pOcclusionClipFrustumShader;
		delete m_pQuadWarpShader;
		delete m_pGridWarpShader;
		delete m_pShowTexShader;
		delete m_pDepthToTextureShader;

		delete m_pOcclusionFrustumFBO;
		delete m_pOcclusionFrustumFBO_r;
		delete m_pUvwFBO;
		delete m_pUvwFBO_r;
		delete m_pRaycastFBO;
		delete m_pRaycastFBO_r;
		delete m_pRaycastFBO_front;
		delete m_pRaycastFBO_front_r;
		delete m_pOcclusionClipFrustumFBO;
		delete m_pOcclusionClipFrustumFBO_r;
		delete m_pWarpFBO;
		delete m_pWarpFBO_r;
		delete m_SceneDepthFBO;
		delete m_SceneDepthFBO_r;
		delete m_DebugDepthFBO;
		delete m_DebugDepthFBO_r;

		delete m_pUvw;
		delete m_pRaycast;
		delete m_pRaycast_r;
		delete m_pRaycastChunked;
		delete m_pRaycastChunked_r;
		delete m_pOcclusionFrustum;
		delete m_pOcclusionClipFrustum;
		delete m_pWarpQuad;
		delete m_pWarpGrid;
		delete m_pShowTex;
		delete m_pDebugDepthToTexture;
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