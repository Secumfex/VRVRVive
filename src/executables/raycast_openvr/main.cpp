/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Core/Timer.h>
#include <Core/DoubleBuffer.h>
#include <Core/FileReader.h>

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
#include <Misc/VolumePresets.h>

// openvr includes
#include <openvr.h>
#include <VR/OpenVRTools.h>

////////////////////// PARAMETERS /////////////////////////////
static const float MIRROR_SCREEN_FRAME_INTERVAL = 0.03f; // interval time (seconds) to mirror the screen (to avoid wait for vsync stalls)

static float FRAMEBUFFER_SCALE = 0.5f;
static glm::vec2 FRAMEBUFFER_RESOLUTION(700.f,700.f);
static glm::vec2 WINDOW_RESOLUTION(FRAMEBUFFER_RESOLUTION.x * 2.f, FRAMEBUFFER_RESOLUTION.y);

const char* SHADER_DEFINES[] = {
	"AMBIENT_OCCLUSION",
	"CUBEMAP_SAMPLING",
	"CULL_PLANES",
	"EMISSION_ABSORPTION_RAW",
	//"FIRST_HIT",
	"LEVEL_OF_DETAIL",
	"OCCLUSION_MAP",
	"RANDOM_OFFSET",
	"SCENE_DEPTH",
	"SHADOW_SAMPLING",
	"STEREO_SINGLE_PASS",
};

const int LAST_RESULT = 0;
const int CURRENT = 1;
const int LEFT = vr::Eye_Left;
const int RIGHT = vr::Eye_Right;

const string STR_SUFFIX[2] = {" LEFT", " RIGHT"};

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
	VolumeData<float> m_volumeData;
	GLuint m_volumeTexture;
	Quad*	m_pQuad;
	Grid*	m_pGrid;
	VertexGrid* m_pVertexGrid;
	Volume*		  m_pNdcCube;
	VolumeSubdiv* m_pVolume;

	// Shader bookkeeping
	ShaderProgram* m_pUvwShader;
	ShaderProgram* m_pRaycastShader;
	ShaderProgram* m_pRaycastLayersShader;
	ShaderProgram* m_pOcclusionFrustumShader;
	ShaderProgram* m_pOcclusionClipFrustumShader;
	ShaderProgram* m_pQuadWarpShader;
	ShaderProgram* m_pGridWarpShader;
	ShaderProgram* m_pNovelViewWarpShader;
	ShaderProgram* m_pComposeTexArrayShader;
	ShaderProgram* m_pShowTexShader;
	ShaderProgram* m_pDepthToTextureShader;

	// FBO bookkeeping
	FrameBufferObject* m_pOcclusionFrustumFBO[2];
	FrameBufferObject* m_pUvwFBO[4];
	SimpleDoubleBuffer<FrameBufferObject*> m_pRaycastFBO[4];
	FrameBufferObject* m_pOcclusionClipFrustumFBO[2];
	FrameBufferObject* m_pWarpFBO[2];
	FrameBufferObject* m_pSceneDepthFBO[2];
	FrameBufferObject* m_pDebugDepthFBO[2];

	GLuint m_stereoOutputTextureArray; // singlepass stereo reprojection buffer

	// Renderpass bookkeeping
	RenderPass* m_pUvw; 		
	RenderPass* m_pRaycast[4]; 		
	ChunkedAdaptiveRenderPass* m_pRaycastChunked[4]; 
	RenderPass* m_pOcclusionFrustum; 		
	RenderPass* m_pOcclusionClipFrustum;
	RenderPass* m_pQuadWarp; 		
	RenderPass* m_pGridWarp; 		
	RenderPass* m_pNovelViewWarp;
	RenderPass* m_pShowTex; 		
	RenderPass* m_pComposeTexArray;
	RenderPass* m_pDebugDepthToTexture; 	

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> m_sdlEventFunc;
	std::function<bool(const vr::VREvent_t&)> m_vrEventFunc;
	std::function<void(bool, int)> m_trackpadEventFunc;
	std::function<void(bool, int)> m_triggerPressedFunc;
	std::function<void(bool, int)> m_touchpadPressedFunc;
	std::function<void(int)> m_setDebugViewFunc;

	// Frame profiling
	struct Frame{
		Profiler FrameProfiler;
		SimpleDoubleBuffer<OpenGLTimings> Timings;
	} m_frame;

	//========== MISC ================
	std::vector<std::string> m_shaderDefines;  // defines in shader
	std::vector<std::string> m_issetDefineStr; // define strings that can be set in GUI
	std::vector<int> m_issetDefine;            // define booleans (as int) settable by GUI

	Turntable m_turntable;

	int m_iActiveModel;

	int m_iOcclusionBlockSize;
	int m_iVertexGridWidth;
	int m_iVertexGridHeight;
	
	enum WarpingTechniques{
		QUAD,
		GRID,
		NOVELVIEW,
		NUM_WARPTECHNIQUES // auxiliary
	}; 
	int m_iActiveWarpingTechnique;

	enum DebugViews{
		UVW_BACK,
		UVW_FRONT,
		FIRST_HIT_,
		OCCLUSION,
		FRONT,
		WARPED,
		OCCLUSION_CLIP_FRUSTUM_FRONT,
		SCENE_DEPTH,
		RAYCAST_LAYERS_DEBUG,
		RAYCAST_UVW_DEBUG,
		NUM_VIEWS // auxiliary
	};
	int m_iActiveView;
	int m_iLeftDebugView;
	int m_iRightDebugView;

	bool m_bPredictPose;
	
	bool m_bAnimateView;
	bool m_bAnimateRotation;
	bool m_bAnimateTranslation;

	bool  m_bIsTouchpadTouched;
	int   m_iTouchpadTrackedDeviceIdx;
	float m_fOldTouchX;
	float m_fOldTouchY;

	bool  m_bIsTriggerPressed;
	int   m_iTriggerPressedTrackedDeviceIdx;
	bool  m_bIsTouchpadPressed;
	int   m_iTouchpadPressedTrackedDeviceIdx;

	float m_fOldX;
	float m_fOldY;

	std::vector<float> m_fpsCounter;
	int m_iCurFpsIdx;

	float m_fElapsedTime;
	float m_fMirrorScreenTimer;
	
	std::string m_executableName;
	std::string m_sResourceDirectory;
	std::string m_sShaderDirectory;

	glm::mat4 m_volumeScale;

	int m_iNumSamples;
	int m_iNumLayers;
	
	float m_pixelOffsetNear;
	float m_pixelOffsetFar;

	std::vector<float> m_texData;
	GLuint m_pboHandle; // PBO 

	glm::vec4 m_clearColor;

	struct MatrixSet
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 perspective;
	} matrices[2][2]; // left, right, firsthit, current

	glm::vec3 m_cullAxis;
	glm::vec4 m_lastControllerPos;
	int m_iLowerOrUpper; // -1: lower, 0: unset, 1: upper boundary

public:


	virtual ~CMainApplication();
	void profileFPS(float fps);
	void loop();

	void renderViews();
	void submitView(int eye);
	void renderGui();
	void renderGuiToTexture();
	void renderGuiToScene(int eye);
	void renderToScreen();
	void renderDisplayImages();
	void renderWarpedImages();
	void renderNovelViewWarp(int eye);
	void renderGridWarp(int eye);
	void renderQuadWarp(int eye);
	void clearWarpFBO(int eye);
	void renderModels(int eye);
	void renderImage(int eye);
	void renderNextBaseData(int eye);
	void updateSimulationFrameData(int eye);
	void renderVolumeLayersIteration(int eye, MatrixSet& matrixSet);
	void renderVolumeIteration(int eye, MatrixSet& matrixSet);
	void renderOcclusionMap(int eye, MatrixSet& current, MatrixSet& last);
	void renderUVWs(int eye, MatrixSet& matrixSet );
	void renderModelsDepth(int eye);
	//void copyResult(int eye);
	void predictPose(int eye);
	void updateCommonUniforms();
	void updateGui();
	void pollEvents();
	void initEventHandlers();
	void initGUI();
	void initRenderPasses();
	void initTextureUniforms();
	void initTextureUnits();
	void initLayerTexture();
	void initFramebuffers();
	void initUniforms();
	void clearLayerTexture();
	void loadShaderDefines();
	void updateShaderDefines();
	void recompileShaders();
	void loadRaycastingShaders();
	void loadShaders();
	void loadGeometries();
	void initSceneVariables();
	void handleVolume();
	void loadVolume();
	void initOpenVR();
	void handleFrameType();

	float getIdealNearValue(); //!< returns the computed 'near' value
	
	CMainApplication( int argc, char *argv[] );
};


	CMainApplication::CMainApplication( int argc, char *argv[] )
		: m_issetDefineStr(SHADER_DEFINES, std::end(SHADER_DEFINES)) // all possible defines
		, m_issetDefine(m_issetDefineStr.size(), (int) false) // define states
		, m_shaderDefines(0) // currently set defines
		, m_fpsCounter(120)
		, m_iCurFpsIdx(0)
		, m_iActiveModel(VolumePresets::CT_Head)
		, m_iActiveView(WARPED)
		, m_iLeftDebugView(14)
		, m_iRightDebugView(15)
		, m_iNumLayers(32)
		, m_fOldX(0.0f)
		, m_fOldY(0.0f)
		, m_fOldTouchX(0.5f)
		, m_fOldTouchY(0.5f)
		, m_iTriggerPressedTrackedDeviceIdx(-1)
		, m_bIsTriggerPressed(false)
		, m_bIsTouchpadPressed(false)
		, m_iTouchpadPressedTrackedDeviceIdx(-1)
		, m_bIsTouchpadTouched(false)
		, m_iTouchpadTrackedDeviceIdx(-1)
		, m_bPredictPose(false)
		, m_iOcclusionBlockSize(6)
		, m_fMirrorScreenTimer(0.f)
		, m_fElapsedTime(0.f)
		, m_bAnimateView(false)
		, m_bAnimateRotation(false)
		, m_bAnimateTranslation(false)
		, m_iActiveWarpingTechnique(QUAD)
		, m_iNumSamples(4)
		, m_pboHandle(-1) // PBO
		, m_pixelOffsetFar(0.0f)
		, m_pixelOffsetNear(0.0f)
		, m_clearColor(0.17f, 0.211f, 0.266f, 0.0f)
		, m_cullAxis(0.0f)
		, m_lastControllerPos(0.0f)
		, m_iLowerOrUpper(0)
		, m_sResourceDirectory(RESOURCES_PATH)
		, m_sShaderDirectory(SHADERS_PATH)
	{
		DEBUGLOG->setAutoPrint(true);

		m_texData.resize((int) WINDOW_RESOLUTION.x * (int) WINDOW_RESOLUTION.y * 4, 0.0f);

		std::string fullExecutableName( argv[0] );
		int lastBackSlashIdx = fullExecutableName.rfind("\\");
		
		std::string executableDirectory =  fullExecutableName.substr(0,lastBackSlashIdx);
		m_executableName = fullExecutableName.substr( lastBackSlashIdx + 1);
		m_executableName = m_executableName.substr(0, m_executableName.find(".exe"));
		DEBUGLOG->log("Executable name: " + m_executableName);

		int argIdx = 1;
		while (argIdx < argc)
		{
			DEBUGLOG->log("Arg[" + std::to_string(argIdx) + "]: " + argv[argIdx]);
			
			if ( strcmp( argv[argIdx], "-scale")  == 0)
			{
				argIdx++;
				FRAMEBUFFER_SCALE = atof(argv[argIdx]);
				DEBUGLOG->log("Set Framebuffer scale: ", FRAMEBUFFER_SCALE);
			}

			if (strcmp( argv[argIdx], "-relative") == 0)
			{
				m_sResourceDirectory = executableDirectory;
				m_sShaderDirectory   = executableDirectory;
				DEBUGLOG->log("Set Resources Directory: " + m_sResourceDirectory);
				DEBUGLOG->log("Set Shader Directory: " + m_sResourceDirectory);
			}

			if (strcmp( argv[argIdx], "-resourcesdir") == 0)
			{
				argIdx++;
				m_sResourceDirectory = argv[argIdx];
				DEBUGLOG->log("Set Resources Directory: " + m_sResourceDirectory);
			}

			if (strcmp( argv[argIdx], "-shadersdir") == 0)
			{
				argIdx++;
				m_sShaderDirectory = argv[argIdx];
				DEBUGLOG->log("Set Shaders Directory: " + m_sShaderDirectory);
			}
			argIdx++;
		}

		updateNearHeightWidth();
		updatePerspective();
		updateScreenToViewMatrix();

		// create m_pWindow and opengl context
		m_pWindow = generateWindow_SDL(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

		printOpenGLInfo();
		printSDLRenderDriverInfo();
	}

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// INITIALIZE OPENVR   //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	void CMainApplication::initOpenVR()
	{
		m_pOvr = new OpenVRSystem(s_near, s_far);
	
		if ( m_pOvr->initialize() )
		{
			DEBUGLOG->log("Alright! OpenVR up and running!");
			m_pOvr->initializeHMDMatrices();

			m_pOvr->CreateShaders();
			m_pOvr->SetupRenderModels();

			s_fovY = m_pOvr->getFovY();
			//s_fovY = 110.0f;

			unsigned int width, height;
			m_pOvr->m_pHMD->GetRecommendedRenderTargetSize(&width, &height);
			FRAMEBUFFER_RESOLUTION = glm::vec2(width, height) * FRAMEBUFFER_SCALE;
		}
	}


	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// VOLUME DATA LOADING //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	void CMainApplication::loadVolume()
	{
		int numLevels = 1;
		{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
			numLevels = 4;
		}}

		VolumePresets::loadPreset( m_volumeData, (VolumePresets::Preset) m_iActiveModel, m_sResourceDirectory);
		m_volumeTexture = loadTo3DTexture<float>(m_volumeData, numLevels, GL_R16F, GL_RED, GL_FLOAT);
		m_volumeData.data.clear(); // set free	

		DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
		checkGLError(true);
	}

	void CMainApplication::initSceneVariables()
	{
		/////////////////////     Scene / View Settings     //////////////////////////
		s_volumeSize = glm::vec3(1.0f);
		updateModelToTexture();

		if (m_pOvr->m_pHMD)
		{
			s_translation = glm::translate(glm::vec3(0.0f,1.25f,0.0f));
			s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
		}
		else
		{	
			s_translation = glm::translate(glm::vec3(0.0f,0.0f,-3.0f));
			s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
		}
		s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));

		updateNearHeightWidth();
		updatePerspective();
		updateScreenToViewMatrix();

		{bool hasProperty = false; for (auto e : m_shaderDefines) { hasProperty |= (e == "STEREO_SINGLE_PASS"); } if ( hasProperty){
			s_near = getIdealNearValue();
			updatePerspective();
			if (m_pOvr->m_pHMD)
			{
				m_pOvr->setNear(s_near);
			}
		}}

		updateNearHeightWidth();
		updatePerspective();
		updateScreenToViewMatrix();

		// use waitgetPoses to update matrices
		if (m_pOvr->m_pHMD)
		{
			s_view = m_pOvr->m_mat4eyePosLeft * s_view;
			s_view_r = m_pOvr->m_mat4eyePosRight * s_view_r;

			//++++++++++++++DEBUG++++++++++++
			{
			glm::mat4 p0 = s_perspective;
			glm::mat4 p1 = m_pOvr->m_mat4ProjectionLeft;
			float c0 = p0[2][2];
			float d0 = p0[3][2];
			float c1 = p1[2][2];
			float d1 = p1[3][2];
			float n1 = d0 / (c0 - 1.0f);
			float n2 = d1 / (c1 - 1.0f);
			DEBUGLOG->log("GLM near:", n1);
			DEBUGLOG->log("OVR near:", n2);
			}
			//+++++++++++++++++++++++++++++++

			s_perspective = m_pOvr->m_mat4ProjectionLeft; 
			s_perspective_r = m_pOvr->m_mat4ProjectionRight;

			//++++++++++++++DEBUG+++++++++++<
			{
			DEBUGLOG->log("Perspective matrices"); DEBUGLOG->indent();
			DEBUGLOG->log("p_l: ", s_perspective);
			DEBUGLOG->log("p_r: ", s_perspective_r);
			glm::vec4 plf = glm::inverse(s_perspective)   * glm::vec4(0, 0, 1, 1);
			glm::vec4 prf = glm::inverse(s_perspective_r) * glm::vec4(0, 0, 1, 1);
			glm::vec4 plc = glm::inverse(s_perspective)   * glm::vec4(0, 0, 0, 1);
			glm::vec4 prc = glm::inverse(s_perspective_r) * glm::vec4(0, 0, 0, 1);
			glm::vec4 pln = glm::inverse(s_perspective)   * glm::vec4(0, 0, -1, 1);
			glm::vec4 prn = glm::inverse(s_perspective_r) * glm::vec4(0, 0, -1, 1);

			DEBUGLOG->log("View Space Points"); DEBUGLOG->indent();
			DEBUGLOG->log("plf", plf / plf.w);
			DEBUGLOG->log("prf", prf / prf.w);
			DEBUGLOG->log("plc", plc / plc.w);
			DEBUGLOG->log("prc", prc / prc.w);
			DEBUGLOG->log("pln", pln / pln.w);
			DEBUGLOG->log("prn", prn / prn.w);
			DEBUGLOG->outdent();

			DEBUGLOG->log("World Space Points"); DEBUGLOG->indent();
			DEBUGLOG->log("wlf",  glm::inverse(s_view) * (plf / plf.w));
			DEBUGLOG->log("wlc", glm::inverse(s_view) * (plc / plc.w));
			DEBUGLOG->log("wln",  glm::inverse(s_view) * (pln / pln.w));
			DEBUGLOG->log("wrf", glm::inverse(s_view_r) * (prf / prf.w));
			DEBUGLOG->log("wrc", glm::inverse(s_view_r) * (prc / prc.w));
			DEBUGLOG->log("wrn", glm::inverse(s_view_r) * (prn / prn.w));
			DEBUGLOG->outdent();

			DEBUGLOG->log("World Space Distances"); DEBUGLOG->indent();
			DEBUGLOG->log("df", glm::distance(glm::inverse(s_view_r) * (prf / prf.w), glm::inverse(s_view) * (plf / plf.w)));
			DEBUGLOG->log("dc", glm::distance(glm::inverse(s_view_r) * (prc / prc.w), glm::inverse(s_view) * (plc / plc.w)));
			DEBUGLOG->log("dn", glm::distance(glm::inverse(s_view_r) * (prn / prn.w), glm::inverse(s_view) * (pln / pln.w)));
			DEBUGLOG->outdent();
			DEBUGLOG->outdent();
			}
			//+++++++++++++++++++++++++++++++



		}
	}

	float CMainApplication::getIdealNearValue()
	{
		// what we have right now
		float b = s_eyeDistance;
		float w = FRAMEBUFFER_RESOLUTION.x;
		float d_p = (float) m_iNumLayers;
		float alpha = glm::radians(s_fovY * s_aspect * 0.5f);
		
		glm::vec3 sceneVolSize = glm::vec3(s_scale * m_volumeScale * glm::vec4(s_volumeSize, 0.0f));
		float radius = sqrtf( powf( sceneVolSize.x, 2.0f) + powf(sceneVolSize.y, 2.0f) + powf(sceneVolSize.z, 2.0f));

		// variant 1: ray from near to infinity
		//float f = b / ( tanf( alpha ) * (2.0f / w) * d_p );

		// variant 2: ray from near to outer bounds
		float r = radius;
		float s = d_p * tanf(alpha) * (2.0f / w);
		float p = 2.0f * r;
		float q = -( (b * 2.0f * r) / s );
		float f = -(p /2.0f) + sqrtf( powf(p/2.0f,2.0f) - q); 

		return f;
	}

	void CMainApplication::loadGeometries()
	{
		m_pQuad = new Quad();
	
		m_pVolume = new VolumeSubdiv(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 4);
	
		m_pGrid = new Grid(400, 400, 0.0025f, 0.0025f, false);

		m_iVertexGridWidth = (int) FRAMEBUFFER_RESOLUTION.x / m_iOcclusionBlockSize;
		m_iVertexGridHeight = (int) FRAMEBUFFER_RESOLUTION.y   / m_iOcclusionBlockSize;
		
		m_pVertexGrid = new VertexGrid(m_iVertexGridWidth, m_iVertexGridHeight, false, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(96, 96)); //dunno what is a good group size?

		m_pNdcCube = new Volume(1.0f); // a cube that spans -1 .. 1 
	}

	void CMainApplication::loadRaycastingShaders()
	{
		DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
		m_pRaycastShader = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines, m_sShaderDirectory);
		m_pRaycastLayersShader = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/synth_raycastLayer_simple.frag", m_shaderDefines, m_sShaderDirectory); 
		DEBUGLOG->outdent();

		DEBUGLOG->log("Shader Compilation: compose tex array shader"); DEBUGLOG->indent();
		m_pComposeTexArrayShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/composeTextureArray.frag", m_shaderDefines, m_sShaderDirectory); 
		DEBUGLOG->outdent();

		DEBUGLOG->log("Shader Compilation: novel view warp shader"); DEBUGLOG->indent();
		m_pNovelViewWarpShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/raycast/synth_novelView_simple.frag", m_shaderDefines, m_sShaderDirectory);
		DEBUGLOG->outdent();
	
		DEBUGLOG->log("Shader Compilation: grid warp shader"); DEBUGLOG->indent();
		m_pGridWarpShader = new ShaderProgram("/raycast/gridWarp.vert", "/raycast/gridWarp.frag", m_shaderDefines, m_sShaderDirectory);
		DEBUGLOG->outdent();
	
	}

	void CMainApplication::loadShaders()
	{
		DEBUGLOG->log("Shader Compilation: shaders");
		DEBUGLOG->indent();
		m_pUvwShader = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag", m_shaderDefines, m_sShaderDirectory);
		DEBUGLOG->outdent();

		loadRaycastingShaders();

		DEBUGLOG->indent();
		m_pOcclusionFrustumShader = new ShaderProgram("/raycast/occlusionFrustum.vert", "/raycast/occlusionFrustum.frag", "/raycast/occlusionFrustum.geom", m_shaderDefines, m_sShaderDirectory);
		m_pOcclusionClipFrustumShader = new ShaderProgram("/raycast/occlusionClipFrustum.vert", "/raycast/occlusionClipFrustum.frag", m_shaderDefines, m_sShaderDirectory);
		m_pQuadWarpShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleWarp.frag", m_shaderDefines, m_sShaderDirectory);
		m_pShowTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", std::vector<std::string>(), m_sShaderDirectory);
		m_pDepthToTextureShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/raycast/debug_depthToTexture.frag", std::vector<std::string>(), m_sShaderDirectory);
		DEBUGLOG->outdent();
	}

	void CMainApplication::initUniforms()
	{
		m_pUvwShader->update("model", s_translation * s_rotation * s_scale * m_volumeScale);
		m_pUvwShader->update("view", s_view);
		m_pUvwShader->update("projection", s_perspective);

		m_pRaycastShader->update("uStepSize", s_rayStepSize);
		m_pRaycastShader->update("uViewport", glm::vec4(0,0,FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y));	
		m_pRaycastShader->update("uResolution", glm::vec4(FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y,0,0));

		m_pRaycastLayersShader->update("uStepSize", s_rayStepSize);
		m_pRaycastLayersShader->update("uViewport", glm::vec4(0,0,FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y));	
		m_pRaycastLayersShader->update("uResolution", glm::vec4(FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y,0,0));

		{bool hasShadow = false; for (auto e : m_shaderDefines) { hasShadow |= (e == "SHADOW_SAMPLING"); } if ( hasShadow ){
		m_pRaycastShader->update("uShadowRayDirection", glm::normalize(glm::vec3(0.0f,-0.5f,-1.0f))); // full range of values in window
		m_pRaycastShader->update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary
		}}

		m_pQuadWarpShader->update( "blendColor", 1.0f );
		m_pGridWarpShader->update("color", m_clearColor);

		m_pOcclusionFrustumShader->update("uOcclusionBlockSize", m_iOcclusionBlockSize);
		m_pOcclusionFrustumShader->update("uGridSize", glm::vec4(m_iVertexGridWidth, m_iVertexGridHeight, 1.0f / (float) m_iVertexGridWidth, 1.0f / m_iVertexGridHeight));

		m_pNovelViewWarpShader->update("uThreshold", 32);

	}
	void CMainApplication::initLayerTexture()
	{
		DEBUGLOG->log("FrameBufferObject Creation: single-pass stereo output texture array"); DEBUGLOG->indent();
		m_stereoOutputTextureArray = createTextureArray((int)FRAMEBUFFER_RESOLUTION.x, (int)FRAMEBUFFER_RESOLUTION.y, m_iNumLayers, GL_RGBA16F);
		DEBUGLOG->outdent();
	}

	void CMainApplication::clearLayerTexture()
	{
		if(m_pboHandle == -1)
		{
			glGenBuffers(1,&m_pboHandle);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboHandle);
			glBufferStorage(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int)FRAMEBUFFER_RESOLUTION.x * (int)FRAMEBUFFER_RESOLUTION.y * m_iNumLayers * 4, NULL, GL_DYNAMIC_STORAGE_BIT);
			checkGLError(true);
			for (int i = 0; i < m_iNumLayers; i++)
			{
				glBufferSubData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) *  (int)FRAMEBUFFER_RESOLUTION.x * (int)FRAMEBUFFER_RESOLUTION.y * i * 4, m_texData.size(), &m_texData[0]);
			}
			checkGLError(true);
			//glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) m_textureResolution.x * (int) m_textureResolution.y * m_iNumLayers * 4, &m_texData[0], GL_STATIC_DRAW);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
	
		OPENGLCONTEXT->activeTexture(GL_TEXTURE26);
		OPENGLCONTEXT->bindTexture(m_stereoOutputTextureArray, GL_TEXTURE_2D_ARRAY);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboHandle);
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,  (int)FRAMEBUFFER_RESOLUTION.x, (int)FRAMEBUFFER_RESOLUTION.y, m_iNumLayers, GL_RGBA, GL_FLOAT, 0); // last param 0 => will read from pbo
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}


	void CMainApplication::initFramebuffers()
	{
		DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pUvwFBO[LEFT]   = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO[LEFT]->addColorAttachments(2); // front UVRs and back UVRs
		m_pUvwFBO[RIGHT] = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int)  FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO[RIGHT]->addColorAttachments(2); // front UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: ray casting"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;

		//FrameBufferObject::s_depthFormat = GL_DEPTH_STENCIL;
		//FrameBufferObject::s_internalDepthFormat = GL_DEPTH24_STENCIL8;
		//FrameBufferObject::s_depthType = GL_UNSIGNED_INT_24_8; // 24 for depth, 8 for stencil
		m_pRaycastFBO[LEFT].getBack()   = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)	FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO[RIGHT].getBack()  = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		//FrameBufferObject::s_depthType = GL_FLOAT;
		//FrameBufferObject::s_internalDepthFormat = GL_DEPTH_COMPONENT24;
		//FrameBufferObject::s_depthFormat = GL_DEPTH_COMPONENT;
		m_pRaycastFBO[LEFT].getFront()   = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO[RIGHT].getFront()  = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);		
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		/*
		checkGLError(true);
		m_pRaycastFBO[LEFT]->bind();
		OPENGLCONTEXT->bindTexture( m_pRaycastFBO[LEFT]->getDepthTextureHandle() );
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_pRaycastFBO[LEFT]->getDepthTextureHandle(), 0);
		m_pRaycastFBO[RIGHT]->bind();
		OPENGLCONTEXT->bindTexture( m_pRaycastFBO[RIGHT]->getDepthTextureHandle() );
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_pRaycastFBO[RIGHT]->getDepthTextureHandle(), 0);			

		checkGLError(true);
		// Add Stencil buffers
		if (m_pOvr->m_pHMD)
		{
			checkGLError(true);
			ShaderProgram stencilShader("/screenSpace/stencil.vert", "/screenSpace/stencil.frag");
			stencilShader.use();
			{
				m_pRaycastFBO[LEFT]->bind();
				//GLuint stencil_rb;
				//glGenRenderbuffers(1, &stencil_rb);
				//glBindRenderbuffer(GL_RENDERBUFFER, stencil_rb);
				//glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, m_pRaycastFBO->getWidth(), m_pRaycastFBO->getHeight());
				//glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil_rb);
				checkGLError(true);
				
				auto maskMesh = m_pOvr->m_pHMD->GetHiddenAreaMesh(vr::Eye_Left);
				auto model = CGLHiddenMeshModel("Hidden Mesh Model LEFT");
				model.BInit( maskMesh );
				// enable simple fragment shader
				stencilShader.use();
				model.Draw(); // fill stencil buffer
				checkGLError(true);
			}

			{
				m_pRaycastFBO[RIGHT]->bind();
				//GLuint stencil_rb;
				//glGenRenderbuffers(1, &stencil_rb);
				//glBindRenderbuffer(GL_RENDERBUFFER, stencil_rb);
				//glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, m_pRaycastFBO[RIGHT]->getWidth(), m_pRaycastFBO[RIGHT]->getHeight());
				//glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil_rb);
				checkGLError(true);

				auto maskMesh = m_pOvr->m_pHMD->GetHiddenAreaMesh(vr::Eye_Right);
				auto model = CGLHiddenMeshModel("Hidden Mesh Model RIGHT");
				model.BInit( maskMesh );
				// enable simple fragment shader
				stencilShader.use();
				model.Draw(); // fill stencil buffer
				checkGLError(true);
			}
		}
		*/
		
		DEBUGLOG->log("FrameBufferObject Creation: Raycast Layers"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA32F;
		m_pRaycastFBO[2 + LEFT].getBack()  = new FrameBufferObject(m_pRaycastLayersShader->getOutputInfoMap(), (int)	FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO[2 + RIGHT].getBack() = new FrameBufferObject(m_pRaycastLayersShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);

		m_pRaycastFBO[2 + LEFT].getFront()  = new FrameBufferObject(m_pRaycastLayersShader->getOutputInfoMap(), (int)	FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pRaycastFBO[2 + RIGHT].getFront() = new FrameBufferObject(m_pRaycastLayersShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();
		
		DEBUGLOG->log("FrameBufferObject Creation: TEMP volume uvw coords for novel view synth"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pUvwFBO[2 + LEFT]   = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO[2 + LEFT]->addColorAttachments(2); // front UVRs and back UVRs
		m_pUvwFBO[2 + RIGHT]   = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO[2 + RIGHT]->addColorAttachments(2); // back UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: occlusion frustum"); DEBUGLOG->indent();
		m_pOcclusionFrustumFBO[LEFT]   = new FrameBufferObject(   m_pOcclusionFrustumShader->getOutputInfoMap(), m_pUvwFBO[LEFT]->getWidth(),   m_pUvwFBO[LEFT]->getHeight() );
		m_pOcclusionFrustumFBO[RIGHT] = new FrameBufferObject( m_pOcclusionFrustumShader->getOutputInfoMap(), m_pUvwFBO[RIGHT]->getWidth(), m_pUvwFBO[RIGHT]->getHeight() );
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: occlusion clip frustum"); DEBUGLOG->indent();
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pOcclusionClipFrustumFBO[LEFT] = new FrameBufferObject(   m_pOcclusionClipFrustumShader->getOutputInfoMap(), m_pUvwFBO[LEFT]->getWidth(),   m_pUvwFBO[LEFT]->getHeight() );
		m_pOcclusionClipFrustumFBO[RIGHT] = new FrameBufferObject( m_pOcclusionClipFrustumShader->getOutputInfoMap(), m_pUvwFBO[RIGHT]->getWidth(), m_pUvwFBO[RIGHT]->getHeight() );
		FrameBufferObject::s_internalFormat = GL_RGBA;
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: quad warping"); DEBUGLOG->indent();
		m_pWarpFBO[LEFT]   = new FrameBufferObject(m_pQuadWarpShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pWarpFBO[RIGHT] = new FrameBufferObject(m_pQuadWarpShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: scene depth"); DEBUGLOG->indent();
		m_pSceneDepthFBO[LEFT]   = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y); // has only a depth buffer, no color attachments
		m_pSceneDepthFBO[RIGHT] = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y); // has only a depth buffer, no color attachments
		m_pSceneDepthFBO[LEFT]->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		m_pSceneDepthFBO[RIGHT]->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		DEBUGLOG->outdent();

		DEBUGLOG->log("FrameBufferObject Creation: DEBUG depth to texture"); DEBUGLOG->indent();
		m_pDebugDepthFBO[LEFT]   = new FrameBufferObject(m_pDepthToTextureShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pDebugDepthFBO[RIGHT] = new FrameBufferObject(m_pDepthToTextureShader->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		DEBUGLOG->outdent();
	}

	void CMainApplication::initTextureUnits()
	{
		OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture, GL_TEXTURE0, GL_TEXTURE_3D);
		OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D); // transfer function

		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[LEFT]->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * UVW_BACK + LEFT, GL_TEXTURE_2D); // left uvw back
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * UVW_BACK + RIGHT, GL_TEXTURE_2D); // right uvw back

		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[LEFT]->getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * UVW_FRONT + LEFT, GL_TEXTURE_2D); // left uvw front
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * UVW_FRONT + RIGHT, GL_TEXTURE_2D); // right uvw front

		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[LEFT].getFront()->getDepthTextureHandle(), GL_TEXTURE2 + 2 * FIRST_HIT_ + LEFT, GL_TEXTURE_2D); // left first hit map
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[RIGHT].getFront()->getDepthTextureHandle(), GL_TEXTURE2 + 2 * FIRST_HIT_ + RIGHT, GL_TEXTURE_2D); // right first hit map

		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionFrustumFBO[LEFT]->getDepthTextureHandle(), GL_TEXTURE2 + 2 * OCCLUSION + LEFT, GL_TEXTURE_2D); // left occlusion map
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionFrustumFBO[RIGHT]->getDepthTextureHandle(), GL_TEXTURE2 + 2 * OCCLUSION + RIGHT, GL_TEXTURE_2D); // right occlusion map
	
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[LEFT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * FRONT + LEFT, GL_TEXTURE_2D); // left  raycasting result
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[RIGHT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * FRONT + RIGHT, GL_TEXTURE_2D);// right raycasting result
	
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO[LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * OCCLUSION_CLIP_FRUSTUM_FRONT + LEFT, GL_TEXTURE_2D); // left occlusion clip map front
		OPENGLCONTEXT->bindTextureToUnit(m_pOcclusionClipFrustumFBO[RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * OCCLUSION_CLIP_FRUSTUM_FRONT + RIGHT, GL_TEXTURE_2D); // right occlusion clip map front
	
		OPENGLCONTEXT->bindTextureToUnit(m_pWarpFBO[LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * WARPED + LEFT, GL_TEXTURE_2D); // left warping result
		OPENGLCONTEXT->bindTextureToUnit(m_pWarpFBO[RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * WARPED + RIGHT, GL_TEXTURE_2D);// right warping result
	
		OPENGLCONTEXT->bindTextureToUnit(m_pSceneDepthFBO[LEFT]->getDepthTextureHandle(), GL_TEXTURE2 + 2 * SCENE_DEPTH + LEFT, GL_TEXTURE_2D); // left scene depth
		OPENGLCONTEXT->bindTextureToUnit(m_pSceneDepthFBO[RIGHT]->getDepthTextureHandle(), GL_TEXTURE2 + 2 * SCENE_DEPTH + RIGHT, GL_TEXTURE_2D);// right scene depth
		
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + LEFT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT5), GL_TEXTURE2 + 2 * RAYCAST_LAYERS_DEBUG + LEFT, GL_TEXTURE_2D); // left layer raycast result
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + RIGHT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT5), GL_TEXTURE2 + 2 * RAYCAST_LAYERS_DEBUG + RIGHT, GL_TEXTURE_2D);// right layer raycast result

		OPENGLCONTEXT->bindImageTextureToUnit(m_stereoOutputTextureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
		OPENGLCONTEXT->bindTextureToUnit(m_stereoOutputTextureArray, GL_TEXTURE26, GL_TEXTURE_2D_ARRAY); // for display

		OPENGLCONTEXT->activeTexture(GL_TEXTURE31);
	}

	void CMainApplication::initTextureUniforms()
	{
		m_pRaycastShader->update("volume_texture", 0); // m_pVolume texture
		m_pRaycastShader->update("transferFunctionTex", 1);
		
		m_pRaycastLayersShader->update("volume_texture", 0); // m_pVolume texture
		m_pRaycastLayersShader->update("transferFunctionTex", 1);

		m_pNovelViewWarpShader->update("layer0", 20);
		m_pNovelViewWarpShader->update("layer1", 21);
		m_pNovelViewWarpShader->update("layer2", 22);
		m_pNovelViewWarpShader->update("layer3", 23);
		//m_pNovelViewWarpShader->update("depth0", 24);
		m_pNovelViewWarpShader->update("depth",  25);

		m_pComposeTexArrayShader->update( "tex", 26);
	}

	void CMainApplication::initRenderPasses()
	{
		DEBUGLOG->log("RenderPass Creation: uvw coords"); DEBUGLOG->indent();
		m_pUvw = new RenderPass(m_pUvwShader, m_pUvwFBO[LEFT]);
		m_pUvw->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		m_pUvw->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		m_pUvw->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
		m_pUvw->addRenderable(m_pVolume);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: raycast"); DEBUGLOG->indent();
		m_pRaycast[LEFT] = new RenderPass(m_pRaycastShader, m_pRaycastFBO[LEFT].getBack());
		m_pRaycast[LEFT]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast[LEFT]->addRenderable(m_pQuad);
		m_pRaycast[LEFT]->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		//m_pRaycast[LEFT]->addEnable(GL_STENCIL_TEST); // to allow write to gl_FragDepth (first-hit)
		m_pRaycast[LEFT]->addDisable(GL_BLEND);

		m_pRaycast[RIGHT] = new RenderPass(m_pRaycastShader, m_pRaycastFBO[RIGHT].getBack());
		m_pRaycast[RIGHT]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast[RIGHT]->addRenderable(m_pQuad);
		m_pRaycast[RIGHT]->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		//m_pRaycast[RIGHT]->addEnable(GL_STENCIL_TEST); // to allow write to gl_FragDepth (first-hit)
		m_pRaycast[RIGHT]->addDisable(GL_BLEND);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: chunked renderpass"); DEBUGLOG->indent();
		glm::ivec2 viewportSize = glm::ivec2((int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		glm::ivec2 chunkSize = glm::ivec2(96,96);
		m_pRaycastChunked[LEFT] = new ChunkedAdaptiveRenderPass(
			m_pRaycast[LEFT],
			viewportSize,
			chunkSize,
			8,
			6.0f,
			1.0f
			);
		m_pRaycastChunked[RIGHT] = new ChunkedAdaptiveRenderPass(
			m_pRaycast[RIGHT],
			viewportSize,
			chunkSize,
			8,
			6.0f,
			1.0f
			);

		{bool hasProperty = false; for (auto e : m_shaderDefines) { hasProperty |= (e == "STEREO_SINGLE_PASS"); } if ( hasProperty){
			m_pRaycastChunked[LEFT]->setTargetRenderTime(10.0f);
			m_pRaycastChunked[LEFT]->setRenderTimeBias(1.0f);
		}}

		DEBUGLOG->outdent();
		
		//m_pRaycastChunked->activateClearbits();
		//m_pRaycastChunked[RIGHT]->activateClearbits();

		DEBUGLOG->log("RenderPass Creation: occlusion frustum"); DEBUGLOG->indent();
		m_pOcclusionFrustum = new RenderPass(m_pOcclusionFrustumShader, m_pOcclusionFrustumFBO[LEFT]);
		m_pOcclusionFrustum->addRenderable(m_pVertexGrid);
		m_pOcclusionFrustum->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		m_pOcclusionFrustum->addEnable(GL_DEPTH_TEST);
		m_pOcclusionFrustum->addDisable(GL_BLEND);

		m_pOcclusionClipFrustum = new RenderPass(m_pOcclusionClipFrustumShader, m_pOcclusionClipFrustumFBO[LEFT]);
		m_pOcclusionClipFrustum->addRenderable(m_pNdcCube);	
		m_pOcclusionClipFrustum->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		m_pOcclusionClipFrustum->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
		m_pOcclusionClipFrustum->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: quad warp"); DEBUGLOG->indent();
		m_pQuadWarp = new RenderPass(m_pQuadWarpShader, m_pWarpFBO[LEFT]);
		m_pQuadWarp->addRenderable(m_pQuad);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: grid warp"); DEBUGLOG->indent();
		m_pGridWarp = new RenderPass(m_pGridWarpShader, m_pWarpFBO[LEFT]);
		m_pGridWarp->addEnable(GL_DEPTH_TEST);
		m_pGridWarp->addRenderable(m_pGrid);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: novel view warp"); DEBUGLOG->indent();
		m_pNovelViewWarp= new RenderPass(m_pNovelViewWarpShader, m_pWarpFBO[LEFT]);
		m_pNovelViewWarp->addEnable(GL_DEPTH_TEST);
		m_pNovelViewWarp->addEnable(GL_BLEND);
		m_pNovelViewWarp->addRenderable(m_pQuad);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: show texture"); DEBUGLOG->indent();
		m_pShowTex = new RenderPass(m_pShowTexShader,0);
		m_pShowTex->addRenderable(m_pQuad);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: debug depth to texture"); DEBUGLOG->indent();
		m_pDebugDepthToTexture = new RenderPass(m_pDepthToTextureShader, m_pDebugDepthFBO[LEFT]);
		m_pDebugDepthToTexture->addClearBit(GL_COLOR_BUFFER_BIT);
		m_pDebugDepthToTexture->addDisable(GL_DEPTH_TEST);
		m_pDebugDepthToTexture->addRenderable(m_pQuad);
		DEBUGLOG->outdent();

		//////////////// NOVEL VIEW SYNTH ////////////////
		DEBUGLOG->log("Render Pass Configuration: ray cast layers shader"); DEBUGLOG->indent();
		m_pRaycast[2 + LEFT] = new RenderPass(m_pRaycastLayersShader, m_pRaycastFBO[2 + LEFT].getBack());
		m_pRaycast[2 + LEFT]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast[2 + LEFT]->addRenderable(m_pQuad);
		m_pRaycast[2 + LEFT]->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		//m_pRaycast[2 + LEFT]->addEnable(GL_STENCIL_TEST);
		m_pRaycast[2 + LEFT]->addDisable(GL_BLEND);

		m_pRaycast[2 + RIGHT] = new RenderPass(m_pRaycastLayersShader, m_pRaycastFBO[2 + RIGHT].getBack());
		m_pRaycast[2 + RIGHT]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		m_pRaycast[2 + RIGHT]->addRenderable(m_pQuad);
		m_pRaycast[2 + RIGHT]->addEnable(GL_DEPTH_TEST); // to allow write to gl_FragDepth (first-hit)
		//m_pRaycast[2 + RIGHT]->addEnable(GL_STENCIL_TEST);
		m_pRaycast[2 + RIGHT]->addDisable(GL_BLEND);

		m_pRaycastChunked[LEFT + 2] = new ChunkedAdaptiveRenderPass(
			m_pRaycast[2 + LEFT],
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		m_pRaycastChunked[RIGHT + 2] = new ChunkedAdaptiveRenderPass(
			m_pRaycast[2 + RIGHT],
			viewportSize,
			chunkSize,
			8,
			6.0f
			);
		DEBUGLOG->outdent();

		DEBUGLOG->log("RenderPass Creation: compose texture array"); DEBUGLOG->indent();
		m_pComposeTexArray = new RenderPass(m_pComposeTexArrayShader,  m_pRaycastFBO[2 + RIGHT].getBack());
		m_pComposeTexArray->addRenderable(m_pGrid);
		m_pComposeTexArray->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		DEBUGLOG->outdent();
	}

	void CMainApplication::initGUI()
	{
		// Setup ImGui binding
		ImGui_ImplSdlGL3_Init(m_pWindow);
	}


	void CMainApplication::initEventHandlers()
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
						m_bIsTouchpadPressed = true;
						m_iTouchpadPressedTrackedDeviceIdx = event.trackedDeviceIndex;
						break;
					case vr::k_EButton_Axis1: // trigger
						m_bIsTriggerPressed = true;
						m_iTriggerPressedTrackedDeviceIdx = event.trackedDeviceIndex;
						break;
					case vr::k_EButton_Grip: // grip
						m_iActiveWarpingTechnique = (m_iActiveWarpingTechnique + 1) % NUM_WARPTECHNIQUES;
						break;
					}

					DEBUGLOG->log("button press: ", event.data.controller.button);
					break;
				}
				case vr::VREvent_ButtonUnpress:
					if (event.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) { return false; } // nevermind
					switch(event.data.controller.button)
					{
					case vr::k_EButton_Axis0: // touchpad
						m_bIsTouchpadPressed = false;
						//m_iTouchpadPressedTrackedDeviceIdx = -1;
						break;
					case vr::k_EButton_Axis1: // trigger
						m_bIsTriggerPressed = false;
						m_iTriggerPressedTrackedDeviceIdx = -1; // reset in func
						break;
					}
					break;
				case vr::VREvent_ButtonTouch: // generated for touchpad
					if (event.data.controller.button == vr::k_EButton_Axis0) // reset coords 
					{ 
						m_iTouchpadTrackedDeviceIdx = event.trackedDeviceIndex;
						m_bIsTouchpadTouched = true;
						m_turntable.setDragActive(m_bIsTouchpadTouched);

						vr::VRControllerState_t state_;
						if (m_pOvr->m_pHMD->GetControllerState(event.trackedDeviceIndex, &state_))
						{
							m_fOldTouchX = state_.rAxis[0].x;
							m_fOldTouchY = state_.rAxis[0].y;
						}
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

		m_triggerPressedFunc = [&](bool isPressed, int deviceIdx)
		{
			if (isPressed && deviceIdx != -1)
			{
			bool hasCullPlanes = false; for (auto e : m_shaderDefines) { hasCullPlanes |= (e == "CULL_PLANES"); } if ( hasCullPlanes )
			{		
			if (m_pOvr->m_rTrackedDevicePose[m_iTriggerPressedTrackedDeviceIdx].bPoseIsValid) 
			{
				// transform controller pose to texture space
				glm::mat4 pose = m_pOvr->m_rmat4DevicePose[m_iTriggerPressedTrackedDeviceIdx];
				glm::vec4 point = s_modelToTexture * glm::inverse(s_model) * pose * glm::vec4(0.f, 0.f, 0.f, 1.0f);

				// find nearest cull plane: largest scalar of direction from center to point
				if (m_cullAxis == glm::vec3(0.0f))
				{
					glm::vec3 dirCP = glm::vec3(point) - (s_cullMin + 0.5f * (s_cullMax - s_cullMin));
					dirCP /= (s_cullMax - s_cullMin); // 'scale' to cull bbox space
					dirCP = glm::normalize(dirCP);
					m_cullAxis = glm::vec3( 
						(float) ((abs(dirCP.x) >= abs(dirCP.y)) && (abs(dirCP.x) >= abs(dirCP.z))) * glm::sign(dirCP.x),
						(float) ((abs(dirCP.y) >= abs(dirCP.x)) && (abs(dirCP.y) >= abs(dirCP.z))) * glm::sign(dirCP.y),
						(float) ((abs(dirCP.z) >= abs(dirCP.x)) && (abs(dirCP.z) >= abs(dirCP.y))) * glm::sign(dirCP.z)
						);
				}

				// set corresponding cull scalar to controller pose
				if ( glm::any(glm::lessThan( m_cullAxis, glm::vec3(0.0f) )) ) 
				{ 
					s_cullMin = glm::vec3(
						(m_cullAxis.x == 0.0f) ? s_cullMin.x : min( s_cullMax.x, max(0.0f, point.x)),
						(m_cullAxis.y == 0.0f) ? s_cullMin.y : min( s_cullMax.y, max(0.0f, point.y)),
						(m_cullAxis.z == 0.0f) ? s_cullMin.z : min( s_cullMax.z, max(0.0f, point.z))
						);
				}
				else
				{
					s_cullMax = glm::vec3(
						(m_cullAxis.x == 0.0f) ? s_cullMax.x : max(s_cullMin.x, min(1.0f, point.x)),
						(m_cullAxis.y == 0.0f) ? s_cullMax.y : max(s_cullMin.y, min(1.0f, point.y)),
						(m_cullAxis.z == 0.0f) ? s_cullMax.z : max(s_cullMin.z, min(1.0f, point.z))
						);
				}
			}}}
			else // reset
			{
				m_cullAxis = glm::vec3(0.0f);
			}
		};

		m_touchpadPressedFunc = [&](bool isPressed, int deviceIdx)
		{
			if (isPressed && deviceIdx != -1)
			{
			if (m_pOvr->m_rTrackedDevicePose[deviceIdx].bPoseIsValid //controller pose
				&& m_pOvr->m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)//HMD pose
			{
				// transform  controller pose to view space
				glm::mat4 pose = m_pOvr->m_rmat4DevicePose[deviceIdx];
				glm::vec4 point = m_pOvr->m_mat4HMDPose * pose * glm::vec4(0.f, 0.f, 0.f, 1.f);

				// set pose if just pressed
				if (m_lastControllerPos == glm::vec4(0.0f) || m_iLowerOrUpper == 0)
				{
					m_lastControllerPos = point;
					m_iLowerOrUpper = (int) glm::sign(point.x); // "left" or "right" in view space
				}
				
				// calculate difference to last time
				glm::vec4 diff = point - m_lastControllerPos;

				// set corresponding windowing limit
				float diffVal = (diff.x * (s_maxValue - s_minValue));  // scaled 1 meter = value range
				if (m_iLowerOrUpper == -1)
				{
					s_windowingMinValue = std::max(s_minValue, std::min(s_windowingMinValue + diffVal, s_windowingMaxValue));
				}
				else
				{
					s_windowingMaxValue = std::min(s_maxValue, std::max(s_windowingMaxValue + diffVal, s_windowingMinValue));
				}
				
				//+++++++++++++++DEBUG+++++++++++++++++++++++++++
				DEBUGLOG->log("Controller - Windowing Interaction Info"); DEBUGLOG->indent();
				DEBUGLOG->log("world   : ", pose * glm::vec4(0.f, 0.f, 0.f, 1.0f));
				DEBUGLOG->log("view    : ", point);
				DEBUGLOG->log("diff    : ", diff);
				DEBUGLOG->log("diffVal : ", diffVal);
				DEBUGLOG->outdent();
				//+++++++++++++++++++++++++++++++++++++++++++++++

				// update last position
				m_lastControllerPos = point;
			}}
			else // reset
			{
				m_iTouchpadPressedTrackedDeviceIdx = -1;
				m_iLowerOrUpper = 0;
				m_lastControllerPos = glm::vec4(0.0f);
			}
		};

		m_setDebugViewFunc = [&](int view)
		{
			switch (view)
			{
				// regular textures -> show directly
				default:	
				m_iLeftDebugView = view * 2 + 2;
				if (m_iLeftDebugView >= 2 + 2 * NUM_VIEWS ) m_iLeftDebugView = 2;
				m_iRightDebugView = m_iLeftDebugView + 1;
				break;
			case RAYCAST_UVW_DEBUG:
				m_iLeftDebugView = view * 2 + 2;
				if (m_iLeftDebugView >= 2 + 2 * NUM_VIEWS ) m_iLeftDebugView = 2;

				m_iRightDebugView = m_iLeftDebugView + 1;
				OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[2 + LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * RAYCAST_UVW_DEBUG + LEFT, GL_TEXTURE_2D); // left uvws for novel view synth 
				OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[2 + RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2 + 2 * RAYCAST_UVW_DEBUG + RIGHT, GL_TEXTURE_2D);// right uvws for novel view synth
				break;
			case RAYCAST_LAYERS_DEBUG:
				m_iLeftDebugView = view * 2 + 2;
				if (m_iLeftDebugView >= 2 + 2 * NUM_VIEWS ) m_iLeftDebugView = 2;
				
				static int offset = 5;
				ImGui::SliderInt("Attachment Offset", &offset,0,5); 
				OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + LEFT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0 + offset), GL_TEXTURE2 + 2 * RAYCAST_LAYERS_DEBUG + LEFT, GL_TEXTURE_2D); // left layer raycast result
				OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + RIGHT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0 + offset), GL_TEXTURE2 + 2 * RAYCAST_LAYERS_DEBUG + RIGHT, GL_TEXTURE_2D);// right layer raycast result
				
				m_iRightDebugView = m_iLeftDebugView + 1;
				break;
			}
		};
	}
	
	////////////////////////////////    EVENTS    ////////////////////////////////
	void CMainApplication::pollEvents()
	{
		pollSDLEvents(m_pWindow, m_sdlEventFunc);
		m_pOvr->PollVREvents(m_vrEventFunc);
		m_trackpadEventFunc(m_bIsTouchpadTouched, m_iTouchpadTrackedDeviceIdx); // handle trackpad touch seperately
		m_triggerPressedFunc(m_bIsTriggerPressed, m_iTriggerPressedTrackedDeviceIdx); // handle trigger press seperately
		m_touchpadPressedFunc(m_bIsTouchpadPressed, m_iTouchpadPressedTrackedDeviceIdx); // handle trackpad press seperately
	}
	
	void CMainApplication::handleVolume()
	{
		glDeleteTextures(1, &m_volumeTexture);
		OPENGLCONTEXT->bindTextureToUnit(0, GL_TEXTURE0, GL_TEXTURE_3D);
	
		loadVolume();

		activateVolume(m_volumeData);

		// adjust scale
		m_volumeScale = VolumePresets::getScalation((VolumePresets::Preset) m_iActiveModel);
		s_rotation = VolumePresets::getRotation((VolumePresets::Preset) m_iActiveModel);

		OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture, GL_TEXTURE0, GL_TEXTURE_3D);
		TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, (VolumePresets::Preset) m_iActiveModel );
	}

	////////////////////////////////     GUI      ////////////////////////////////
	void CMainApplication::updateGui()
	{
		ImGui_ImplSdlGL3_NewFrame(m_pWindow);
		ImGuiIO& io = ImGui::GetIO();
		profileFPS(ImGui::GetIO().Framerate);

		ImGui::Value("FPS", io.Framerate);
		m_fMirrorScreenTimer += io.DeltaTime;
		m_fElapsedTime += io.DeltaTime;

		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) );
		ImGui::PlotLines("FPS", &m_fpsCounter[0], m_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
		ImGui::PopStyleColor();
	
		ImGui::Text("Active Model");
		ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());
		if (ImGui::ListBox("##activemodel", &m_iActiveModel, VolumePresets::s_models, (int)(sizeof(VolumePresets::s_models)/sizeof(*VolumePresets::s_models)), 5))
		{
			handleVolume();
		}
		ImGui::PopItemWidth();

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
			ImGui::DragFloatRange2("windowing range", &s_windowingMinValue, &s_windowingMaxValue, 1.0f, (float) s_minValue, (float) s_maxValue); // grayscale ramp boundaries
        	ImGui::SliderFloat("ray step size",   &s_rayStepSize,  0.0001f, 0.1f, "%.5f", 2.0f);
			{bool hasCullPlanes = false; for (auto e : m_shaderDefines) { hasCullPlanes |= (e == "CULL_PLANES"); } if ( hasCullPlanes ){
				ImGui::SliderFloat3("Cull Max", glm::value_ptr(s_cullMax),0.0f, 1.0f);
				ImGui::SliderFloat3("Cull Min", glm::value_ptr(s_cullMin),0.0f, 1.0f);
			}}
		}
        		
		ImGui::Separator();

		{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
		if (ImGui::CollapsingHeader("Level of Detail Settings"))
		{
			ImGui::DragFloat("Lod Max Level", &s_lodMaxLevel, 0.1f, 0.0f, 8.0f);
			ImGui::DragFloat("Lod Begin", &s_lodBegin, 0.01f, 0.0f, s_far);
			ImGui::DragFloat("Lod Range", &s_lodRange, 0.01f, 0.0f, std::max(0.1f, s_far - s_lodBegin));
		}
		}}
		
		{if (ImGui::CollapsingHeader("Defines")){
			for (unsigned int i = 0; i < m_issetDefine.size(); i++)
			{
				ImGui::Checkbox(m_issetDefineStr[i].c_str(), (bool*) &(m_issetDefine[i]) );
			}
		}}

		if (ImGui::Button("Recompile Shaders"))
		{
			recompileShaders();
		}

		ImGui::Separator();
		ImGui::Columns(2);
		static bool profiler_visible, profiler_visible_r = false;
		ImGui::Checkbox("Chunk Perf Profiler Left", &profiler_visible);
		if (profiler_visible) { m_pRaycastChunked[LEFT + 2 * (int) (m_iActiveWarpingTechnique == NOVELVIEW)]->imguiInterface(&profiler_visible, "LEFT "); };
		ImGui::NextColumn();
		ImGui::Checkbox("Chunk Perf Profiler Right", &profiler_visible_r);
		if (profiler_visible_r) { m_pRaycastChunked[RIGHT + 2 * (int) (m_iActiveWarpingTechnique == NOVELVIEW)]->imguiInterface(&profiler_visible_r, "RIGHT "); };
		ImGui::NextColumn();
		ImGui::Columns(1);
		
		ImGui::Separator();
		ImGui::Columns(2);
		ImGui::SliderInt("Left Debug View", &m_iLeftDebugView, 2, 2 + 2 * NUM_VIEWS - 1);
		ImGui::NextColumn();
		ImGui::SliderInt("Right Debug View", &m_iRightDebugView, 2, 2 + 2 * NUM_VIEWS - 1);
		ImGui::NextColumn();
		ImGui::Columns(1);
		ImGui::Separator();
		if (ImGui::ColorEdit4("Clear Color", &m_clearColor[0]))
		{
			m_pGridWarpShader->update("color", m_clearColor);
		}

		ImGui::Separator();

		{bool changed = false;
		changed |= ImGui::SliderFloat("Near", &s_near, 0.1f, s_far);
		changed |= ImGui::SliderFloat("Far", &s_far, s_near, 100.0f);
		if ( changed )
		{
			updateNearHeightWidth();
			updatePerspective();
			updateScreenToViewMatrix();
			{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
				s_lodBegin = s_near;
				s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
			}}
			if (m_pOvr->m_pHMD)
			{
				m_pOvr->setNear(s_near);
				m_pOvr->setFar(s_far);

				s_perspective = m_pOvr->m_mat4ProjectionLeft; 
				s_perspective_r = m_pOvr->m_mat4ProjectionRight;
			}
		}}

		{
			static float scale = s_scale[0][0];
			if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 100.0f))
			{
				s_scale = glm::scale(glm::vec3(scale));

				{bool hasProperty = false; for (auto e : m_shaderDefines) { hasProperty |= (e == "STEREO_SINGLE_PASS"); } if ( hasProperty){
					s_near = getIdealNearValue();
					updateNearHeightWidth();
					updatePerspective();
					updateScreenToViewMatrix();

					if (m_pOvr->m_pHMD)
					{
						m_pOvr->setNear(s_near);
						s_perspective = m_pOvr->m_mat4ProjectionLeft; 
						s_perspective_r = m_pOvr->m_mat4ProjectionRight;
					}

					{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
						s_lodBegin = s_near;
						s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
					}}

				}}
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

			//++++++++++++ DEBUG ++++++++++++++
			{static float lastSwapTimeLeft  = 0.0f;
			static float lastSwapTimeRight = 0.0f;
			static float lastSwapTimestampLeft  = 0.0f;
			static float lastSwapTimestampRight = 0.0f;
			ImGui::Separator();
			if (m_frame.Timings.getFront().m_timestamps.find("Swap Time" + STR_SUFFIX[LEFT]) != m_frame.Timings.getFront().m_timestamps.end())
			{
				float t = m_frame.Timings.getFront().m_timestamps.at("Swap Time" + STR_SUFFIX[LEFT]).lastTime;
				if (t > lastSwapTimestampLeft) { lastSwapTimeLeft = t - lastSwapTimestampLeft; lastSwapTimestampLeft = t; }
				ImGui::Value("Swap Time Left", lastSwapTimeLeft);
			}
			if (m_frame.Timings.getFront().m_timestamps.find("Swap Time" + STR_SUFFIX[RIGHT]) != m_frame.Timings.getFront().m_timestamps.end())
			{
				float t = m_frame.Timings.getFront().m_timestamps.at("Swap Time" + STR_SUFFIX[RIGHT]).lastTime;
				if (t > lastSwapTimestampRight) { lastSwapTimeRight = t - lastSwapTimestampRight; lastSwapTimestampRight = t; }
				ImGui::Value("Swap Time Right", lastSwapTimeRight);
			}
			ImGui::Separator();
			}
			//+++++++++++++++++++++++++++++++++

		}
		if(!pause_frame_profiler) m_frame.Timings.swap();
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
			s_eyeDistance = glm::length(glm::vec3(s_view_r * glm::inverse(s_view) * glm::vec4(0.0,0.0,0.0,1.0f)));
		}

		s_nearH = s_near * std::tanf( glm::radians(s_fovY * 0.5f) );
		s_nearW = s_nearH * s_aspect;

		// constant
		s_screenToView = glm::scale(glm::vec3(s_nearW, s_nearH, s_near)) * 
			glm::inverse( 
				glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * 
				glm::scale(glm::vec3(0.5f,0.5f,0.5f)) 
			);

		// Update Matrices

		//++++++++++++++ DEBUG
		ImGui::Checkbox("Animate View", &m_bAnimateView); ImGui::SameLine(); ImGui::Checkbox("Anim Translation", &m_bAnimateTranslation); ImGui::SameLine(); ImGui::Checkbox("Anim Rotation", &m_bAnimateRotation);
		if (m_bAnimateView)
		{
			glm::vec4 warpCenter = s_center;
			glm::vec4 warpEye = s_eye;
			glm::vec3 warpUp = glm::vec3(s_up);

			if (m_bAnimateRotation)
			{
				warpCenter  = glm::vec4(sin(m_fElapsedTime*2.0)*0.25f, cos(m_fElapsedTime*2.0)*0.125f, 0.0f, 1.0f);
				warpUp = glm::normalize(glm::vec3( sin(m_fElapsedTime)*0.25f, 1.0f, 0.0f));
			}
			if (m_bAnimateTranslation) warpEye = s_eye + glm::vec4(-sin(m_fElapsedTime*1.0)*0.125f, -cos(m_fElapsedTime*2.0)*0.125f, 0.0f, 1.0f);

			s_view   = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), warpUp);
			s_view_r = glm::lookAt(glm::vec3(warpEye) + glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(warpCenter) + glm::vec3(s_eyeDistance,0.0,0.0), warpUp);
		}

		ImGui::Checkbox("Predict HMD Pose", &m_bPredictPose);
		//++++++++++++++ DEBUG

		ImGui::Checkbox("Auto-rotate", &s_isRotating); // enable/disable rotating m_pVolume

		//++++++++++++++ DEBUG
		ImGui::SliderInt("Active Warpin Technique", &m_iActiveWarpingTechnique, 0, NUM_WARPTECHNIQUES - 1);
		if (m_iActiveWarpingTechnique == NOVELVIEW)
		{
			static int numSamples = 32;
			if (ImGui::SliderInt("Num Novel-View Samples", &numSamples, 4, 96))
			{
				m_pNovelViewWarpShader->update("uThreshold", numSamples);
			}
		}

		//++++++++++++++ DEBUG
	}

	////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
	void CMainApplication::updateCommonUniforms()
	{
		/************* update color mapping parameters ******************/
		m_pRaycastShader->update("uStepSize", s_rayStepSize); 	  // ray step size
		m_pRaycastLayersShader->update("uStepSize", s_rayStepSize); 	  // ray step size

		// Level of Detail
		{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
			m_pRaycastShader->update("uLodMaxLevel", s_lodMaxLevel);
			m_pRaycastShader->update("uLodBegin", s_lodBegin);
			m_pRaycastShader->update("uLodRange", s_lodRange);
			
			m_pRaycastLayersShader->update("uLodMaxLevel", s_lodMaxLevel);
			m_pRaycastLayersShader->update("uLodBegin", s_lodBegin);
			m_pRaycastLayersShader->update("uLodRange", s_lodRange);  
		}}

		// cull planes
		{bool hasCullPlanes = false; for (auto e : m_shaderDefines) { hasCullPlanes |= (e == "CULL_PLANES"); } if ( hasCullPlanes ){
			m_pRaycastShader->update("uCullMin", s_cullMin);
			m_pRaycastShader->update("uCullMax", s_cullMax);
			
			m_pRaycastLayersShader->update("uCullMin", s_cullMin);
			m_pRaycastLayersShader->update("uCullMax", s_cullMax); 
		}}

		// color mapping parameters
		m_pRaycastShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		m_pRaycastLayersShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		m_pRaycastLayersShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		m_pRaycastLayersShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		
		/************* update experimental  parameters ******************/
		{bool hasShadow = false; for (auto e : m_shaderDefines) { hasShadow |= (e == "SHADOW_SAMPLING"); } if ( hasShadow ){
			static float angles[2] = {-0.5f, -0.5f};
			float alpha = angles[0] * glm::pi<float>();
			float beta  = angles[1] * glm::half_pi<float>();
			static int numSteps = 16;
			static glm::vec3 shadowDir(std::cos( alpha ) * std::cos(beta), std::sin( alpha ) * std::cos( beta ), std::tan( beta ) );
			if (ImGui::CollapsingHeader("Shadow Properties"))
			{	
				if (ImGui::SliderFloat2("Shadow Dir", angles, -0.999f, 0.999f) )
				{
					shadowDir = glm::vec3(std::cos( alpha ) * std::cos(beta), std::sin( alpha ) * std::cos( beta ), std::tan( beta ) );
				}
				ImGui::SliderInt("Num Steps", &numSteps, 0, 32);
			}

			m_pRaycastShader->update("uShadowRayDirection", glm::normalize(shadowDir)); 
			m_pRaycastShader->update("uShadowRayNumSteps", numSteps); 

			m_pRaycastLayersShader->update("uShadowRayDirection", glm::normalize(shadowDir));
			m_pRaycastLayersShader->update("uShadowRayNumSteps", numSteps); 
		}}

		{bool hasCubemap = false; for (auto e : m_shaderDefines) { hasCubemap |= (e == "CUBEMAP_SAMPLING"); } if ( hasCubemap ){
			ImGui::SliderInt("Num Samples", &m_iNumSamples, 0, 128);
			m_pRaycastShader->update("uNumSamples", m_iNumSamples);
			m_pRaycastLayersShader->update("uNumSamples", m_iNumSamples);
		}}

		glm::vec3 sceneVolSize = glm::vec3(s_scale * m_volumeScale * glm::vec4(s_volumeSize, 0.0f));
		float radius = sqrtf( powf( sceneVolSize.x, 2.0f) + powf(sceneVolSize.y, 2.0f) + powf(sceneVolSize.z, 2.0f));
		glm::vec4 objectCenter = s_view * s_model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0);
		float s_zRayEnd   = max(s_near, -(objectCenter.z - radius));
		float s_zRayStart = max(s_near, -(objectCenter.z + radius));

		// TODO this is broken for Vive matrices
		/** 
		float t_near = (s_zRayStart) / s_near;
		float t_far  = (s_zRayEnd)  / s_near;
		float nW = s_nearW;

		//float m_pixelOffsetFar  = (1.0f / t_far)  * (b * w) / (nW * 2.0f); // pixel offset between points at zRayEnd distance to image planes
		//float m_pixelOffsetNear = (1.0f / t_near) * (b * w) / (nW * 2.0f); // pixel offset between points at zRayStart distance to image planes

		float b = s_eyeDistance;
		float w = FRAMEBUFFER_RESOLUTION.x;
		float alpha = glm::radians(s_fovY * s_aspect * 0.5f);
		// variant 2: ray from near to outer bounds
		float s = w / (2.0f * s_near * tanf(alpha) );
		float imageOffset =  b * s;
		m_pixelOffsetNear = ((b * s_near) / s_zRayStart) * s;
		m_pixelOffsetFar  = ((b * s_near) / s_zRayEnd) * s;
		**/

		// image offset:
		//+++++++++++++++++ DEBUG +++++++++++++++++
		//TODO project left and right view's center points to left pixel space: how far apart?
		//TODO project left and right view's center points to right pixel space: same as above?
		//TODO project LEFT view's center near point to right pixel space
		//TODO project LEFT view's center far point to right pixel space
		{
			static bool useRayStartEnd = true;
			
			float n = (useRayStartEnd) ? -s_zRayStart : -s_near;
			float f = (useRayStartEnd) ? -s_zRayEnd : -s_far;

			glm::vec4 b = s_view * glm::inverse(s_view_r) * glm::vec4(0.f,0.f,0.f,1.f); // vector from left to right camera pos
			glm::vec4 bf = s_view * glm::inverse(s_view_r) * glm::vec4(0.f,0.f,-1000.f,1.f); // vector from left to right camera pos (the same but at large distance)

			glm::vec4 cnl = glm::vec4(0.f, 0.f, n, 1.0f); // near center 
			glm::vec4 cfl = glm::vec4(0.f, 0.f, f, 1.0f); // far center

			glm::vec4 cnl_r = s_view_r * glm::inverse(s_view) * cnl; // left near ctr point in right view
			glm::vec4 cfl_r = s_view_r * glm::inverse(s_view) * cfl; // left far ctr point in right view
			
			glm::vec4 pcnl = s_perspective * cnl; // left near ctr point in left proj space
			glm::vec4 pcnl_r = s_perspective_r * cnl_r; // left near ctr point in right proj space
			pcnl /= pcnl.w;
			pcnl_r /= pcnl_r.w;

			glm::vec4 pcfl = s_perspective * cfl; // left near ctr point in left proj space
			glm::vec4 pcfl_r = s_perspective_r * cfl_r; // left near ctr point in right proj space
			pcfl /= pcfl.w;
			pcfl_r /= pcfl_r.w;

			glm::vec4 scnl = ((pcnl * 0.5f) + 0.5f) * glm::vec4(FRAMEBUFFER_RESOLUTION, 1.0f, 1.0f);
			glm::vec4 scnl_r = ((pcnl_r * 0.5f) + 0.5f) * glm::vec4(FRAMEBUFFER_RESOLUTION, 1.0f, 1.0f);

			glm::vec4 scfl = ((pcfl * 0.5f) + 0.5f) * glm::vec4(FRAMEBUFFER_RESOLUTION, 1.0f, 1.0f);
			glm::vec4 scfl_r = ((pcfl_r * 0.5f) + 0.5f) * glm::vec4(FRAMEBUFFER_RESOLUTION, 1.0f, 1.0f);

			glm::vec4 sdn = (scnl_r - scnl); // pixel space difference
			glm::vec4 sdf = (scfl_r - scfl); // pixel space difference

			m_pixelOffsetNear = abs(sdn.x);
			m_pixelOffsetFar  = abs(sdf.x);
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

			if (ImGui::CollapsingHeader("Epipolar Info"))
			{
			ImGui::Checkbox("Switch Near-Far / RayStart-End", &useRayStartEnd);
			ImGui::Value("Res. Width", m_pWarpFBO[LEFT]->getWidth() ); ImGui::SameLine(); ImGui::Value("Res. Height", m_pWarpFBO[LEFT]->getHeight());
			ImGui::Value("Approx Distance to Ray Start", s_zRayStart);
			ImGui::Value("Approx Distance to Ray End", s_zRayEnd);
			//ImGui::Value("b", b); ImGui::SameLine(); ImGui::Value("s", s);
			//ImGui::Value("Pixel Offset of Images", imageOffset);
			ImGui::Value("Pixel Offset at Ray Start", m_pixelOffsetNear);
			ImGui::Value("Pixel Offset at Ray End", m_pixelOffsetFar);
			ImGui::Value("Pixel Range of a Ray", m_pixelOffsetNear - m_pixelOffsetFar);
			}

			if (ImGui::CollapsingHeader("Epipolar Info Details"))
			{
				ImGui::Separator();
				ImGui::InputFloat4("b", glm::value_ptr(b)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base line (left to right eye)");
				ImGui::InputFloat4("bf", glm::value_ptr(bf)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance between view vectors at 1000m distance");
				ImGui::Separator();
				ImGui::InputFloat4("cnl", glm::value_ptr(cnl)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in left view space");
				ImGui::InputFloat4("cnl_r", glm::value_ptr(cnl_r));if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in right view space");
				ImGui::InputFloat4("cfl", glm::value_ptr(cfl)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in left view space");
				ImGui::InputFloat4("cfl_r", glm::value_ptr(cfl_r)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in right view space");
				ImGui::Separator();
				ImGui::InputFloat4("pcnl", glm::value_ptr(pcnl)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in left projective space");
				ImGui::InputFloat4("pcnl_r", glm::value_ptr(pcnl_r));if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in right projective space");
				ImGui::InputFloat4("pcfl", glm::value_ptr(pcfl));if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in left projective space");
				ImGui::InputFloat4("pcfl_r", glm::value_ptr(pcfl_r));if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in right projective space");
				ImGui::Separator();
				ImGui::InputFloat4("scnl", glm::value_ptr(scnl)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in left pixel (screen) coordinates");
				ImGui::InputFloat4("scnl_r", glm::value_ptr(scnl_r)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at near distance in right pixel (screen) coordinates");
				ImGui::InputFloat4("scfl", glm::value_ptr(scfl)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in left pixel (screen) coordinates");
				ImGui::InputFloat4("scfl_r", glm::value_ptr(scfl_r)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center of left view at far distance in right pixel (screen) coordinates");
				ImGui::Separator();
				ImGui::InputFloat4("sdn", glm::value_ptr(sdn)); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixel (screen) distance of point at near distance");
				ImGui::InputFloat4("sdf", glm::value_ptr(sdf));	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixel (screen) distance of point at far distance");
				ImGui::Separator();
			}
		}

		{bool hasDebugLayerDefine = false;bool hasDebugIdxDefine = false; for (auto e : m_shaderDefines) { hasDebugIdxDefine |= (e == "DEBUG_IDX"); hasDebugLayerDefine |= (e == "DEBUG_LAYER") ; } if ( hasDebugLayerDefine || hasDebugIdxDefine){
		static int debugIdx = -1;
		ImGui::SliderInt( "DEBUG LAYER", &debugIdx, -1, m_iNumLayers - 1 ); 
		if(hasDebugLayerDefine) {m_pComposeTexArrayShader->update("uDebugLayer", debugIdx);}
		if(hasDebugIdxDefine) {m_pComposeTexArrayShader->update("uDebugIdx", debugIdx);}
		}}
	}

	void CMainApplication::predictPose(int eye)
	{
		static vr::TrackedDevicePose_t predictedDevicePose[ vr::k_unMaxTrackedDeviceCount ];
		//float predictSecondsAhead = ((float)m_pRaycastChunked->getLastNumFramesElapsed()) * 0.011f; // number of frames rendering took
		float predictSecondsAhead = (m_pRaycastChunked[eye]->getLastTotalRenderTime() + m_pRaycastChunked[eye]->getLastTotalRenderTime()) / 1000.0f; // absolutely arbitrary

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
				matrices[eye][CURRENT].view = ( (eye == LEFT) ? m_pOvr->m_mat4eyePosLeft : m_pOvr->m_mat4eyePosRight) * predictedHMDPose;
			}
		}
		else
		{
			if (m_bAnimateView)
			{
				glm::vec4 warpCenter = s_center;
				glm::vec4 warpEye = s_eye;
				glm::vec3 warpUp = glm::vec3(s_up);

				if (m_bAnimateRotation)
				{
					warpCenter  = glm::vec4(sin((m_fElapsedTime + predictSecondsAhead)*2.0)*0.25f, cos((m_fElapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);
					warpUp = glm::normalize(glm::vec3(sin((m_fElapsedTime + predictSecondsAhead))*0.25f, 1.0f, 0.0f));
				}
				if (m_bAnimateTranslation) warpEye = s_eye + glm::vec4(-sin((m_fElapsedTime + predictSecondsAhead)*1.0)*0.125f, -cos((m_fElapsedTime + predictSecondsAhead)*2.0)*0.125f, 0.0f, 1.0f);

				matrices[eye][CURRENT].view = glm::lookAt( (eye == LEFT) ? glm::vec3(warpEye) : glm::vec3(warpEye) + glm::vec3(s_eyeDistance,0.0,0.0), (eye == LEFT) ? glm::vec3(warpCenter) : glm::vec3(warpCenter) + glm::vec3(s_eyeDistance,0.0,0.0), warpUp);
			}
		}
	}

	/*
	void CMainApplication::copyResult(int eye)
	{
		m_frame.Timings.getBack().beginTimerElapsed("Copy Result" + STR_SUFFIX[eye]);
		
		int idx = eye;
		if (m_iActiveWarpingTechnique == NOVELVIEW)
		{
			idx = 2 + eye;
			for (int i = 0; i <= ((int) (m_iActiveWarpingTechnique == NOVELVIEW)) * m_pRaycastFBO[idx].getBack()->getNumColorAttachments(); i++)
			{
				m_pRaycastFBO[idx].getFront()->bind();
				GLenum drawBuffer = (GL_COLOR_ATTACHMENT0 + i);
				glDrawBuffers(1, &drawBuffer); // copy one by one
				copyFBOContent(m_pRaycastFBO[idx].getBack(), m_pRaycastFBO[idx].getFront(), GL_COLOR_BUFFER_BIT, GL_COLOR_ATTACHMENT0 + i);
			}
			glDrawBuffers(m_pRaycastFBO[idx].getFront()->getDrawBuffers().size(), &m_pRaycastFBO[idx].getFront()->getDrawBuffers()[0]);
		}
		else
		{
			copyFBOContent(m_pRaycastFBO[idx].getBack(), m_pRaycastFBO[idx].getFront(), GL_COLOR_BUFFER_BIT, GL_COLOR_ATTACHMENT0);
		}

		copyFBOContent(m_pRaycastFBO[idx].getBack(), m_pRaycastFBO[idx].getFront(), GL_DEPTH_BUFFER_BIT);
		m_frame.Timings.getBack().stopTimerElapsed();
	}
	*/

	void CMainApplication::renderModelsDepth(int eye)
	{
		m_frame.Timings.getBack().beginTimerElapsed("Depth Models" + STR_SUFFIX[eye]);
		m_pSceneDepthFBO[eye]->bind();
		glClear(GL_DEPTH_BUFFER_BIT);
		OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
		m_pOvr->renderModels( (vr::Hmd_Eye) eye );
		OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
		m_frame.Timings.getBack().stopTimerElapsed();
	}

	void CMainApplication::renderUVWs(int eye, MatrixSet& matrixSet )
	{
		// uvw maps
		m_frame.Timings.getBack().beginTimerElapsed("UVW" + STR_SUFFIX[eye]);
		m_pUvw->setFrameBufferObject( m_pUvwFBO[eye] );
		m_pUvwShader->update("view", matrixSet.view);
		m_pUvwShader->update("model", matrixSet.model);
		m_pUvwShader->update("projection", matrixSet.perspective);
		m_pUvw->render();
		m_frame.Timings.getBack().stopTimerElapsed();
	}

	void CMainApplication::renderOcclusionMap(int eye, MatrixSet& current, MatrixSet& last)
	{
		//update raycasting matrices for next iteration	// for occlusion frustum
		glm::mat4 firstHitViewToCurrentView  = current.view * (current.model * glm::inverse(last.model)) * glm::inverse(last.view);
		glm::mat4 firstHitViewToTexture = s_modelToTexture * glm::inverse(last.model) * glm::inverse(last.view);

		// occlusion maps
		m_frame.Timings.getBack().beginTimerElapsed("Occlusion Frustum" + STR_SUFFIX[eye]);
		m_pOcclusionFrustum->setFrameBufferObject( m_pOcclusionFrustumFBO[eye] );
		m_pOcclusionFrustumShader->update("first_hit_map", 2 + 2 * FIRST_HIT_ + eye); // first hit map
		m_pOcclusionFrustumShader->update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
		m_pOcclusionFrustumShader->update("uFirstHitViewToTexture", firstHitViewToTexture);
		m_pOcclusionFrustumShader->update("uProjection", current.perspective);
		m_pOcclusionFrustum->render();

		// occlusion clip frustum
		m_pOcclusionClipFrustumShader->update("uProjection", current.perspective);
		m_pOcclusionClipFrustum->setFrameBufferObject( m_pOcclusionClipFrustumFBO[eye] );
		m_pOcclusionClipFrustumShader->update("uView", current.view);
		m_pOcclusionClipFrustumShader->update("uModel", glm::inverse(last.view) );
		m_pOcclusionClipFrustumShader->update("uWorldToTexture", s_modelToTexture * glm::inverse(last.model) );
		m_pOcclusionClipFrustum->render();

		m_frame.Timings.getBack().stopTimerElapsed();
	}

	void CMainApplication::renderVolumeIteration(int eye, MatrixSet& matrixSet)
	{
		// raycasting (chunked)
		//m_pRaycastShader->update( "uScreenToTexture", s_modelToTexture * glm::inverse( matrixSet.model ) * glm::inverse( matrixSet.view ) * s_screenToView );
		m_pRaycastShader->update( "uViewToTexture", s_modelToTexture * glm::inverse( matrixSet.model) * glm::inverse( matrixSet.view) );
		m_pRaycastShader->update( "uProjection", matrices[eye][CURRENT].perspective);
		m_pRaycastShader->update( "back_uvw_map",  2 + 2 * UVW_BACK + eye );
		m_pRaycastShader->update( "front_uvw_map", 2 + 2 * UVW_FRONT + eye );
		
		{bool hasProperty= false; for (auto e : m_shaderDefines) { hasProperty |= (e == "SCENE_DEPTH"); } if ( hasProperty){
			m_pRaycastShader->update( "scene_depth_map",2 + 2 * SCENE_DEPTH  + eye );
		}}

		{bool hasProperty= false; for (auto e : m_shaderDefines) { hasProperty |= (e == "OCCLUSION_MAP"); } if ( hasProperty){
			m_pRaycastShader->update( "occlusion_map", 2 + 2 * OCCLUSION + eye );
			m_pRaycastShader->update( "occlusion_clip_frustum_front", 2 + 2 * OCCLUSION_CLIP_FRUSTUM_FRONT + eye );
		}}

		m_frame.Timings.getBack().beginTimer("Chunked Raycast" + STR_SUFFIX[eye]);
		m_pRaycastChunked[eye]->render();
		m_frame.Timings.getBack().stopTimer("Chunked Raycast" + STR_SUFFIX[eye]);
	}

	void CMainApplication::renderVolumeLayersIteration(int eye, MatrixSet& matrixSet)
	{
		// raycasting (chunked)
		//m_pRaycastLayersShader->update( "uScreenToView", s_screenToView );
		//m_pRaycastLayersShader->update( "uScreenToTexture", s_modelToTexture * glm::inverse(matrixSet.model) * glm::inverse(matrixSet.view) * s_screenToView);
		m_pRaycastLayersShader->update( "uViewToTexture", s_modelToTexture * glm::inverse( matrixSet.model) * glm::inverse( matrixSet.view) );
		m_pRaycastLayersShader->update( "uProjection", matrices[eye][CURRENT].perspective);
		m_pRaycastLayersShader->update( "back_uvw_map",  2 + 2 * UVW_BACK + eye );
		m_pRaycastLayersShader->update( "front_uvw_map", 2 + 2 * UVW_FRONT + eye );

		m_frame.Timings.getBack().beginTimer("Chunked Raycast" + STR_SUFFIX[eye]);
		m_pRaycastChunked[2 + eye]->render();
		m_frame.Timings.getBack().stopTimer("Chunked Raycast" + STR_SUFFIX[eye]);
	}

	void CMainApplication::renderImage(int eye)
	{
		if (m_pRaycastChunked[eye + 2 * (int) (m_iActiveWarpingTechnique == NOVELVIEW)]->isFinished())
		{
			renderNextBaseData(eye);
		}

		if ((m_iActiveWarpingTechnique == NOVELVIEW)){
			renderVolumeLayersIteration(eye, matrices[eye][CURRENT]);
		}else{
			renderVolumeIteration(eye, matrices[eye][CURRENT]);
		}
		//+++++++++ DEBUG  +++++++++++++++++++++++++++++++++++++++++++ 
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	}

	void CMainApplication::renderModels(int eye)
	{
		OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
		m_pWarpFBO[eye]->bind();
		m_pOvr->renderModels((vr::Hmd_Eye) eye);
		OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
	}

	void CMainApplication::clearWarpFBO(int eye)
	{
		// clear
		glClearColor(m_clearColor.r,m_clearColor.g,m_clearColor.b,m_clearColor.a);
		m_pWarpFBO[eye]->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void CMainApplication::renderQuadWarp(int eye)
	{
		// warp left
		m_pQuadWarp->setFrameBufferObject( m_pWarpFBO[eye] );
		m_pQuadWarpShader->update( "tex", 2 + 2 * FRONT + eye ); // last result
		m_pQuadWarpShader->update( "oldView", matrices[eye][LAST_RESULT].view ); // update with old view
		m_pQuadWarpShader->update( "newView", (eye == LEFT) ? s_view : s_view_r ); // most current view
		m_pQuadWarpShader->update( "projection",  matrices[eye][LAST_RESULT].perspective ); 
		m_pQuadWarp->render();
	}

	void CMainApplication::renderGridWarp(int eye)
	{
		glBlendFunc(GL_ONE, GL_ZERO); // frontmost fragment takes it all
		m_pGridWarp->setFrameBufferObject(m_pWarpFBO[eye]);
		m_pGridWarpShader->update( "tex", 2 + 2 * FRONT + eye ); // last result
		m_pGridWarpShader->update( "uViewNew", (eye == LEFT) ? s_view : s_view_r); // most current view

		{bool hasProperty = false; for (auto e : m_shaderDefines) { hasProperty |= (e == "STEREO_SINGLE_PASS"); } if ( hasProperty){
			m_pGridWarpShader->update( "depth_map", 2 + 2 * FIRST_HIT_ + LEFT); // last LEFT first hit map
			m_pGridWarpShader->update( "uViewOld", matrices[LEFT][LAST_RESULT].view ); // update with old LEFT view
			m_pGridWarpShader->update( "uViewOld_eye", matrices[eye][LAST_RESULT].view ); // update with old view
		} else {
			m_pGridWarpShader->update( "depth_map", 2 + 2 * FIRST_HIT_ + eye); // last first hit map
			m_pGridWarpShader->update( "uViewOld", matrices[eye][LAST_RESULT].view ); // update with old view
		}}

		m_pGridWarpShader->update( "uProjection",  matrices[eye][LAST_RESULT].perspective ); 
		m_pGridWarp->render();
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
	}

	void CMainApplication::renderNovelViewWarp(int eye)
	{
		// render UVWs
		m_pUvw->setFrameBufferObject( m_pUvwFBO[2 + eye] );
		m_pUvwShader->update("view", (eye == LEFT) ? s_view : s_view_r);
		m_pUvwShader->update("model", matrices[eye][LAST_RESULT].model);
		m_pUvwShader->update("projection", (eye == LEFT) ? s_perspective : s_perspective_r);
		m_pUvw->render();

		// activate textures
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE20, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT2), GL_TEXTURE21, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT3), GL_TEXTURE22, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT4), GL_TEXTURE23, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getDepthTextureHandle(),								  GL_TEXTURE24, GL_TEXTURE_2D); //depth 0
		OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[2 + eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE25, GL_TEXTURE_2D); //depth 1-4
		
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[2 + eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE27, GL_TEXTURE_2D); // uvw back novel
		OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO[2 + eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE28, GL_TEXTURE_2D); // uvw front novel
		
		{bool hasProperty= false; for (auto e : m_shaderDefines) { hasProperty |= (e == "SCENE_DEPTH"); } if ( hasProperty){
		OPENGLCONTEXT->bindTextureToUnit(m_pWarpFBO[eye]->getDepthTextureHandle(), GL_TEXTURE29, GL_TEXTURE_2D); // scene depth
		m_pNovelViewWarpShader->update("scene_depth_map", 29);
		}}

		// update matrices
		m_pNovelViewWarp->setFrameBufferObject( m_pWarpFBO[eye] );
		m_pNovelViewWarpShader->update("uProjection", matrices[eye][LAST_RESULT].perspective);
		m_pNovelViewWarpShader->update("uViewOld", matrices[eye][LAST_RESULT].view);
		m_pNovelViewWarpShader->update("uViewNovel", (eye == LEFT) ? s_view : s_view_r);
		
		// render novel view
		m_pNovelViewWarpShader->update("back_uvw_map", 27);
		m_pNovelViewWarpShader->update("front_uvw_map", 28);
		m_pNovelViewWarp->render();
	}

	void CMainApplication::renderWarpedImages()
	{
		m_frame.Timings.getBack().beginTimerElapsed("Warping");
		OPENGLCONTEXT->setEnabled(GL_BLEND, true);
		switch(m_iActiveWarpingTechnique)
		{
		default:
		case QUAD: 
			renderQuadWarp(LEFT);
			renderQuadWarp(RIGHT);
			break;
		case GRID:
			renderGridWarp(LEFT);
			renderGridWarp(RIGHT);
			
			break;
		case NOVELVIEW:
			renderNovelViewWarp(LEFT);
			renderNovelViewWarp(RIGHT);
		}
		OPENGLCONTEXT->setEnabled(GL_BLEND, false);
		m_frame.Timings.getBack().stopTimerElapsed();
	}

	void CMainApplication::renderDisplayImages()
	{
		clearWarpFBO(LEFT);
		clearWarpFBO(RIGHT);

		if (m_pOvr->m_pHMD) // render controller models if possible
		{
			m_frame.Timings.getBack().beginTimerElapsed("Render Models");
			renderModels(LEFT);

			renderModels(RIGHT);
			m_frame.Timings.getBack().stopTimerElapsed();
		}

		// render warping results ontop
		renderWarpedImages();
	}

	void CMainApplication::renderToScreen()
	{
		OPENGLCONTEXT->bindFBO(0);
		glClear(GL_COLOR_BUFFER_BIT);

		{
			m_pShowTexShader->update("tex", m_iLeftDebugView);
			m_pShowTex->setViewport(0, 0, (int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), (int)std::min( getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f ));
			m_pShowTex->render();
		}
		{
			m_pShowTexShader->update("tex", m_iRightDebugView);
			m_pShowTex->setViewport((int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), 0, (int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), (int)std::min( getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f ));
			m_pShowTex->render();
		}
		//////////////////////////////////////////////////////////////////////////////

		m_fMirrorScreenTimer = 0.0f;
	}

	void CMainApplication::renderGui()
	{
		ImGui::Render();
		OPENGLCONTEXT->setEnabled(GL_BLEND, false);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
	}

	void CMainApplication::submitView(int eye)
	{
		m_pOvr->submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE2 + 2 * WARPED + eye], (vr::Hmd_Eye) eye);
	}

	void CMainApplication::updateSimulationFrameData(int eye)
	{
		// first hit map was rendered with last "current" matrices
		matrices[eye][LAST_RESULT] = matrices[eye][CURRENT]; 

		// overwrite with current matrices
		matrices[eye][CURRENT].model = s_model; 
		matrices[eye][CURRENT].view = (eye == LEFT) ? s_view : s_view_r;
		matrices[eye][CURRENT].perspective = (eye == LEFT) ? s_perspective : s_perspective_r;

		{if (eye == LEFT) {bool hasProperty = false; for (auto e : m_shaderDefines) { hasProperty |= (e == "STEREO_SINGLE_PASS"); } if ( hasProperty ){
			glm::mat4 textureToProjection_r = s_perspective_r * s_view_r * s_model * glm::inverse( s_modelToTexture ); // texture to model
			m_pRaycastShader->update( "uTextureToProjection_r", textureToProjection_r ); //since position map contains s_view space coords

			m_pComposeTexArrayShader->update("uPixelOffsetFar",  m_pixelOffsetFar);
			m_pComposeTexArrayShader->update("uPixelOffsetNear", m_pixelOffsetNear);
			//m_pComposeTexArrayShader->update("uImageOffset", imageOffset);
		}}}

		//++++++++++++++ DEBUG +++++++++++//
		if (m_bPredictPose)
		{
			predictPose(eye);
		}
		//++++++++++++++++++++++++++++++++//
	}

	void CMainApplication::renderNextBaseData(int eye)
	{
		updateSimulationFrameData(eye);

		//++++++++++++++ DEBUG +++++++++++//
		// quickly do a depth pass of the models
		if ( m_pOvr->m_pHMD )
		{
			renderModelsDepth(eye);
		}
		//++++++++++++++++++++++++++++++++//
			
		renderUVWs(eye, matrices[eye][CURRENT]);

		{bool hasProperty= false; for (auto e : m_shaderDefines) { hasProperty |= (e == "OCCLUSION_MAP"); } if ( hasProperty){
			renderOcclusionMap(eye, matrices[eye][CURRENT], matrices[eye][LAST_RESULT]);
		}}
	}

	void CMainApplication::handleFrameType()
	{
		int idx = 2 * (int) (m_iActiveWarpingTechnique == NOVELVIEW);
		auto timings = m_frame.Timings.getFront();

		bool hasStereo = false; for (auto e : m_shaderDefines) { hasStereo |= (e == "STEREO_SINGLE_PASS"); }
		bool hasOccl = false; for (auto e : m_shaderDefines) { hasOccl |= (e == "OCCLUSION_MAP"); }

		// gather information about what will be done this frame, and calculate residual available render time
		float estTime = 0.0f;
		if (m_pRaycastChunked[LEFT + idx]->isFinished())
		{
			// UVWs
			if (timings.m_timersElapsed.find("UVW" + STR_SUFFIX[LEFT]) != timings.m_timersElapsed.end())
			{
				estTime += timings.m_timersElapsed.at("UVW" + STR_SUFFIX[LEFT]).lastTiming;
			} else {
				estTime += 0.2f; // eh...
			}

			// Depth Models
			if (m_pOvr->m_pHMD) { if(timings.m_timersElapsed.find("Depth Models" + STR_SUFFIX[LEFT]) != timings.m_timersElapsed.end())
			{
				estTime += timings.m_timersElapsed.at("Depth Models" + STR_SUFFIX[LEFT]).lastTiming;
			}
			else
			{
				estTime += 0.5f;
			}}

			// Stereo Compositing + Clear Array
			if (hasStereo)
			{
				if (timings.m_timersElapsed.find("Clear Array") != timings.m_timersElapsed.end())
				{
					estTime += timings.m_timersElapsed.at("Clear Array").lastTiming;
				} else {
					estTime += 2.0f; // slow eh...
				}
				if (timings.m_timersElapsed.find("Compose Right") != timings.m_timersElapsed.end())
				{
					estTime += timings.m_timersElapsed.at("Compose Right").lastTiming;
				} else {
					estTime += 1.0f; // eh...
				}
			}

			// Occlusion Map
			if (hasOccl)
			{
				if (timings.m_timersElapsed.find("Occlusion Frustum" + STR_SUFFIX[LEFT]) != timings.m_timersElapsed.end())
				{
					estTime += timings.m_timersElapsed.at("Occlusion Frustum" + STR_SUFFIX[LEFT]).lastTiming;
				} else {
					estTime += 2.0f; // slow eh...
				}
			}
		}

		if (m_pRaycastChunked[RIGHT + idx]->isFinished() && !hasStereo)
		{
			// UVWs
			if (timings.m_timersElapsed.find("UVW" + STR_SUFFIX[RIGHT]) != timings.m_timersElapsed.end())
			{
				estTime += timings.m_timersElapsed.at("UVW" + STR_SUFFIX[RIGHT]).lastTiming;
			} else {
				estTime += 0.5f; // eh...
			}

			// Depth Models
			if (m_pOvr->m_pHMD) { if(timings.m_timersElapsed.find("Depth Models" + STR_SUFFIX[RIGHT]) != timings.m_timersElapsed.end())
			{
				estTime += timings.m_timersElapsed.at("Depth Models" + STR_SUFFIX[RIGHT]).lastTiming;
			}
			else
			{
				estTime += 0.5f;
			}}

			// Occlusion Map
			if (hasOccl)
			{
				if (timings.m_timersElapsed.find("Occlusion Frustum" + STR_SUFFIX[RIGHT]) != timings.m_timersElapsed.end())
				{
					estTime += timings.m_timersElapsed.at("Occlusion Frustum" + STR_SUFFIX[RIGHT]).lastTiming;
				} else {
					estTime += 2.0f; // slow eh...
				}
			}
		}

		// Warping
		if (timings.m_timersElapsed.find("Warping") != timings.m_timersElapsed.end())
		{
			estTime += timings.m_timersElapsed.at("Warping").lastTiming;
		}
		else
		{
			estTime += 1.0f;
		}

		// Models
		if (m_pOvr->m_pHMD) {if(timings.m_timersElapsed.find("Render Models") != timings.m_timersElapsed.end())
		{
			estTime += timings.m_timersElapsed.at("Render Models").lastTiming;
		}
		else
		{
			estTime += 0.5f;
		}}
		
		// Calculate estimated available time
		float frameTime = 10.0f;
		float availTime = max( frameTime - estTime, 0.1f ); // at least something
		if (hasStereo)
		{
			m_pRaycastChunked[LEFT]->setTargetRenderTime(availTime); // all goes to left chunked render pass
		}
		else
		{
			m_pRaycastChunked[LEFT]->setTargetRenderTime( availTime / 2.0f );
			m_pRaycastChunked[RIGHT]->setTargetRenderTime( availTime / 2.0f );
		}
	}


	////////////////////////////////  RENDERING //// /////////////////////////////
	void CMainApplication::renderViews()
	{
		// check for finished left/right images, copy to Front FBOs
		int idx = 2 * (int) (m_iActiveWarpingTechnique == NOVELVIEW);

		// check if single pass stereo is active
		if (m_pRaycastChunked[LEFT + idx]->isFinished())
		{
			//copyResult(LEFT);
			m_frame.Timings.getBack().timestamp("Swap Time" + STR_SUFFIX[LEFT]);
			m_pRaycastFBO[LEFT + idx].swap();
			m_pRaycast[LEFT + idx]->setFrameBufferObject(
				m_pRaycastFBO[LEFT + idx].getBack()
			);
			OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[LEFT].getFront()->getDepthTextureHandle(), GL_TEXTURE2 + 2 * FIRST_HIT_ + LEFT, GL_TEXTURE_2D); // left first hit map
			OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[LEFT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * FRONT + LEFT, GL_TEXTURE_2D); // left  raycasting result
			OPENGLCONTEXT->activeTexture(GL_TEXTURE31);
		}

		bool isSinglePass = false;
		for (auto e : m_shaderDefines) { isSinglePass |= (e == "STEREO_SINGLE_PASS"); }

		if (isSinglePass && m_pRaycastChunked[LEFT + idx]->isFinished())
		{
			// Compose right image
			m_pComposeTexArray->setFrameBufferObject(
				m_pRaycastFBO[RIGHT + idx].getBack()
			);
			m_frame.Timings.getBack().beginTimerElapsed("Compose Right");
			m_pComposeTexArray->render();
			m_frame.Timings.getBack().stopTimerElapsed();
			
			m_frame.Timings.getBack().timestamp("Swap Time" + STR_SUFFIX[RIGHT]);
			m_pRaycastFBO[RIGHT + idx].swap();
			OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[RIGHT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * FRONT + RIGHT, GL_TEXTURE_2D); // left  raycasting result
			
			m_frame.Timings.getBack().beginTimerElapsed("Clear Array");
			clearLayerTexture();
			m_frame.Timings.getBack().stopTimerElapsed();

			updateSimulationFrameData(RIGHT);
		}

		if (!isSinglePass && m_pRaycastChunked[RIGHT + idx]->isFinished())
		{
			//copyResult(RIGHT);
			m_frame.Timings.getBack().timestamp("Swap Time" + STR_SUFFIX[RIGHT]);
			m_pRaycastFBO[RIGHT + idx].swap();
			m_pRaycast[RIGHT + idx]->setFrameBufferObject(
				m_pRaycastFBO[RIGHT + idx].getBack()
			);
			OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[RIGHT].getFront()->getDepthTextureHandle(), GL_TEXTURE2 + 2 * FIRST_HIT_ + RIGHT, GL_TEXTURE_2D); // left first hit map
			OPENGLCONTEXT->bindTextureToUnit(m_pRaycastFBO[RIGHT].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 + 2 * FRONT + RIGHT, GL_TEXTURE_2D); // left  raycasting result
			OPENGLCONTEXT->activeTexture(GL_TEXTURE31);
		}
		
		//%%%%%%%%%%%% render left image
		renderImage(LEFT);
		
		//%%%%%%%%%%%% render right image
		if (!isSinglePass)
		{
			renderImage(RIGHT);
		}

		//%%%%%%%%%%%% render display images
		renderDisplayImages();

		//%%%%%%%%%%%% Submit/Display images
		m_setDebugViewFunc(m_iActiveView);

		if ( m_pOvr->m_pHMD ) // submit images
		{
			submitView(LEFT);
			submitView(RIGHT);
		}

		if (m_fMirrorScreenTimer > MIRROR_SCREEN_FRAME_INTERVAL || !m_pOvr->m_pHMD)
		{
			renderToScreen();

			renderGui();

			SDL_GL_SwapWindow( m_pWindow ); // swap buffers
		}
		else
		{
			glFinish(); // just Flush
		}
	}

	void CMainApplication::recompileShaders()
	{
		DEBUGLOG->log("Recompiling Shaders"); DEBUGLOG->indent();
		DEBUGLOG->log("Deleting Shaders");
	
		// delete shaders
		delete m_pRaycastShader;
		delete m_pRaycastLayersShader;
		delete m_pComposeTexArrayShader;
		delete m_pNovelViewWarpShader;
		delete m_pGridWarpShader;

		// reload shader defines
		updateShaderDefines();

		// reload shaders
		loadRaycastingShaders();
	
		// set ShaderProgram References
		m_pRaycast[LEFT]->setShaderProgram(m_pRaycastShader);
		m_pRaycast[RIGHT]->setShaderProgram(m_pRaycastShader);
		m_pRaycast[2 + LEFT]->setShaderProgram(m_pRaycastLayersShader);
		m_pRaycast[2 + RIGHT]->setShaderProgram(m_pRaycastLayersShader);
		m_pComposeTexArray->setShaderProgram(m_pComposeTexArrayShader);
		m_pNovelViewWarp->setShaderProgram(m_pNovelViewWarpShader);
		m_pGridWarp->setShaderProgram(m_pGridWarpShader);

		// update uniforms
		initTextureUniforms();
		initUniforms();

		DEBUGLOG->outdent();
	}

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	void CMainApplication::loop()
	{
		std::string window_header = "Volume Renderer - OpenVR";
		SDL_SetWindowTitle(m_pWindow, window_header.c_str() );
		OPENGLCONTEXT->activeTexture(GL_TEXTURE31);
		

		while (!shouldClose(m_pWindow))
		{
			//////////////////////////////////////////////////////////////////////////////
			pollEvents();
		
			//////////////////////////////////////////////////////////////////////////////
			updateGui();
			
			//////////////////////////////////////////////////////////////////////////////
			//updateModel(); 
			s_model = s_translation * m_turntable.getRotationMatrix() * s_rotation * s_scale * m_volumeScale;
			
			//////////////////////////////////////////////////////////////////////////////
			m_frame.Timings.getBack().timestamp("Frame Begin");
			
			//////////////////////////////////////////////////////////////////////////////
			updateCommonUniforms();
			
			//////////////////////////////////////////////////////////////////////////////			
			handleFrameType();

			//////////////////////////////////////////////////////////////////////////////			
			renderViews();

			//////////////////////////////////////////////////////////////////////////////
			m_frame.Timings.getBack().timestamp("Frame End");
		}
	
		ImGui_ImplSdlGL3_Shutdown();
		m_pOvr->shutdown();
		destroyWindow(m_pWindow);
		SDL_Quit();
	
	}

	void CMainApplication::profileFPS(float fps)
	{
		m_fpsCounter[m_iCurFpsIdx] = fps;
		m_iCurFpsIdx = (m_iCurFpsIdx + 1) % m_fpsCounter.size(); 
	}

	
	// load shader defines from file, set activeDefines accordingly
	void CMainApplication::loadShaderDefines()
	{
		m_shaderDefines.clear();

		DEBUGLOG->log("Loading Shader Definitions"); DEBUGLOG->indent(); 
		std::string definitionsFile = m_executableName + ".defs";

		DEBUGLOG->log("Looking for shader definitions file: " + definitionsFile);

		FileReader fileReader;
		// load data
		if ( fileReader.readFileToBuffer(definitionsFile) )
		{
			std::vector<std::string> fileDefines = fileReader.getLines();
			std::vector<std::string> activeDefines;
			for (auto e: fileDefines)
			{
				if (e.length() >= (2 * sizeof(char)) && e[0] == '/' && e[1] == '/' ) { // "Commented" out
					continue; 
				}
				else
				{
					activeDefines.push_back(e);
				}
			}
			m_shaderDefines.insert(m_shaderDefines.end(), activeDefines.begin(), activeDefines.end());
		}
		else
		{
			DEBUGLOG->log("ERROR: Could not find shader definitions file");
		}

		// iterate shader defines, set corresponding bool for active defines
		for (unsigned int i = 0; i < m_issetDefineStr.size(); i++)
		{
			for (auto e : m_shaderDefines)
			{
				if ( m_issetDefineStr[i] == e )
				{
					m_issetDefine[i] = (int) true;
				}
			}
		}

		DEBUGLOG->outdent();
	}

	void CMainApplication::updateShaderDefines()
	{
		// add missing enabled defines, remove disabled defines
		for (unsigned int i = 0; i < m_issetDefine.size(); i++)
		{
			if( (bool) m_issetDefine[i] ) // define isset
			{
				// if not in it yet, add, else skip
				bool doAdd = true;
				for (auto e : m_shaderDefines)
				{
					if (e == m_issetDefineStr[i]) doAdd = false;
				}
				if (doAdd){ m_shaderDefines.push_back( SHADER_DEFINES[i] ); }
			}
			else
			{
				// if in it before, remove, else skip
				for (int j = 0; j < m_shaderDefines.size(); j++)
				{
					if (m_shaderDefines[j] == m_issetDefineStr[i])
					{
						m_shaderDefines.erase( m_shaderDefines.begin() + j );
					}
				}
			}
		}
	}


	CMainApplication::~CMainApplication()
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
		delete m_pNovelViewWarpShader;
		delete m_pShowTexShader;
		delete m_pDepthToTextureShader;

		for (int i = 0; i < 4; i++)
		{
			delete m_pRaycastFBO[i].getFront();
			delete m_pRaycastFBO[i].getBack();
		}

		for (int i = 0; i < 4; i++)
		{
			delete m_pUvwFBO[i];
			delete m_pUvwFBO[i];
		}

		for (int i = 0; i < 4; i++)
		{
			delete m_pUvwFBO[i];
			delete m_pUvwFBO[i];
			delete m_pRaycastChunked[i];
			delete m_pRaycast[i];
		}

		for (int i = 0; i < 2; i++)
		{
			delete m_pOcclusionFrustumFBO[i];
			delete m_pOcclusionClipFrustumFBO[i];
			delete m_pWarpFBO[i];
			delete m_pSceneDepthFBO[i];
			delete m_pDebugDepthFBO[i];
		}

		delete m_pUvw;
		delete m_pOcclusionFrustum;
		delete m_pOcclusionClipFrustum;
		delete m_pQuadWarp;
		delete m_pGridWarp;
		delete m_pNovelViewWarp;
		delete m_pShowTex;
		delete m_pDebugDepthToTexture;
	}




//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	pMainApplication->loadShaderDefines();

	pMainApplication->initOpenVR();

	pMainApplication->initSceneVariables();

	pMainApplication->handleVolume();

	pMainApplication->loadGeometries();

	pMainApplication->loadShaders();

	pMainApplication->initFramebuffers();

	pMainApplication->initRenderPasses();
	
	pMainApplication->initLayerTexture();

	pMainApplication->initTextureUnits();

	pMainApplication->initTextureUniforms();

	pMainApplication->initUniforms();

	pMainApplication->initGUI();

	pMainApplication->initEventHandlers();

	pMainApplication->loop();

	return 0;
}