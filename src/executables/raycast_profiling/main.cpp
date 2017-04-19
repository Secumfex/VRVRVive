/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>
#include <algorithm>
#include <ctime>

#include <Core/Timer.h>
#include <Core/DoubleBuffer.h>
#include <Core/FileReader.h>
#include <Core/CSVWriter.h>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>
#include <Volume/ChunkedRenderPass.h>
#include <Importing/TextureTools.h>

#include "UI/imgui/imgui.h"
#include "UI/imgui_impl_sdl_gl3.h"
#include <UI/imguiTools.h>
#include <UI/Turntable.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

////////////////////// PARAMETERS /////////////////////////////

#include <Misc/Parameters.h> //<! static variables
#include <Misc/TransferFunctionPresets.h> //<! static variables

using namespace RaycastingParameters;
using namespace ViewParameters;
using namespace VolumeParameters;

const glm::vec2 FRAMEBUFFER_RESOLUTION = glm::vec2( 768, 768);
const glm::vec2 WINDOW_RESOLUTION = glm::vec2( 1600, 800);

const int LEFT = 0;
const int RIGHT = 1;

const char* SHADER_DEFINES[] = {
	//"AMBIENT_OCCLUSION",
	//"CUBEMAP_SAMPLING",
	//"CULL_PLANES",
	//"EMISSION_ABSORPTION_RAW",
	//"FIRST_HIT",
	//"LEVEL_OF_DETAIL",
	//"OCCLUSION_MAP",
	"RANDOM_OFFSET",
	//"SCENE_DEPTH",
	//"SHADOW_SAMPLING",
	//"STEREO_SINGLE_PASS",
};

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

template <class T>
void activateVolume(VolumeData<T>& volumeData ) // set static variables
{
	// set volume specific parameters
	s_minValue = volumeData.min;
	s_maxValue = volumeData.max;
	s_rayStepSize = 1.0f / (2.0f * volumeData.size_x); // this seems a reasonable size
	s_windowingMinValue = (float) volumeData.min;
	s_windowingMaxValue = (float) volumeData.max;
	s_windowingRange = s_windowingMaxValue - s_windowingMinValue;
}

class Warp
{
public:
	Warp(RenderPass* rp) { m_pWarp = rp; }
	RenderPass* m_pWarp; 
	std::function<void()> m_updateFunc; // set from outside
};

class Difference
{
public:
	Difference(RenderPass* rp) { m_pDiff = rp; }
	RenderPass* m_pDiff; 
	void diff(GLuint tex1, GLuint tex2);
};

class DisplaySimulation
{
public:
	DisplaySimulation() 
		: m_fRefreshTime(1.0f / 60.0f) 
		, m_fTime(0.0f)
		, m_iFrame(0)
		{}
	float m_fRefreshTime;
	float m_fTime;
	int   m_iFrame;

	inline void advanceTime(float delta) { m_fTime += delta; m_iFrame = (int) (m_fTime / m_fRefreshTime); }
	inline void setFrame(int idx){ m_iFrame = idx; m_fTime = ((float) m_iFrame) * m_fRefreshTime; }
	inline float getTimeToUpdate() { return ((float) (m_iFrame + 1)) * m_fRefreshTime - m_fTime; } 
	inline float getTimeToUpdateFromTime( float time ) { return ( m_fRefreshTime - fmodf(time, m_fRefreshTime) ); } 
	inline float getTimeOfFrame(int idx) { return ((float) (idx)) * m_fRefreshTime; } 
};

class HmdSimulation
{
public:
	enum Mode {BASIC, ROTATE, TRANSLATE, PIVOT, NUM_ANIMATIONMODES};
	int m_mode;
	bool m_bAnimateView;
	bool m_bAnimateRotation;
	bool m_bAnimateTranslation;
	bool m_bClampTime;

	float m_fStartValue; //!< start value
	float m_fValue;      //!< animation value
	float m_fDuration;   //!< duration for translation of angle
	float m_fDistance;   //!< distance of view to center
	float m_fNeckOffsetZ; //!< distance to the neck (axis of rotation)
public: 
	HmdSimulation() 
		: m_bAnimateView(true)
		, m_bAnimateRotation(false)
		, m_bAnimateTranslation(true) 
		, m_bClampTime(false)
		, m_mode(TRANSLATE)
		, m_fStartValue(0.f)
		, m_fValue(0.f)
		, m_fDuration(1.f)
		, m_fDistance(1.5f)
		, m_fNeckOffsetZ(0.07)
		{}

	glm::mat4 getView(float timeParam, int eye);
	inline glm::mat4 getPerspective(float time, int eye) {return s_perspective;}
};

glm::mat4 HmdSimulation::getView(float timeParam, int eye)
{
	glm::mat4 view = s_view;
	glm::mat4 view_r = s_view_r;

	float time = 0.0f;
	if (m_bAnimateView)
	{
		time = timeParam;

		if (m_bClampTime)
		{
			time = max(0.0f, min(m_fDuration, timeParam));
		}
	}

	switch (m_mode)
	{
	case BASIC:
	{
		glm::vec4 center = glm::vec4(0.f, 0.f, 0.f, 1.0f);
		glm::vec4 eyePos = glm::vec4(0.f, 0.f, m_fDistance, 1.0f);
		glm::vec3 up = glm::vec3(s_up);

		if (m_bAnimateRotation)
		{
			center  = glm::vec4(sin(time * 2.0f)*0.25f, cos( time * 2.0f)*0.125f, 0.0f, 1.0f);
			up = glm::normalize(glm::vec3( sin( time ) * 0.25f, 1.0f, 0.0f));
		}
		if (m_bAnimateTranslation) 
		{
			eyePos = eyePos + glm::vec4(-sin( time * 1.0f)*0.125f, -cos(time * 2.0f) * 0.125f, 0.0f, 1.0f);
		}

		view   = glm::lookAt(glm::vec3(eyePos), glm::vec3(center), up);
		view_r = glm::lookAt(glm::vec3(eyePos) + glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(center) + glm::vec3(s_eyeDistance,0.0,0.0), up);
		break;
	}
	case ROTATE:
	{
		glm::vec3 rotAxis = glm::vec3(s_up);
		glm::vec3 up = glm::vec3(s_up); // TODO animate aswell?

		glm::vec4 headPos = glm::vec4(0.f, 0.f, m_fDistance, 1.0f);
		glm::vec4 center = glm::vec4(0.f, 0.f, 0.f, 1.0f);
		glm::mat4 initialRotation = glm::rotate(glm::radians(m_fStartValue), rotAxis );

		// current
		float t = -0.5f * ( cos( ((time) * glm::pi<float>()) / m_fDuration ) ) + 0.5f; // parametric 0..1
		glm::mat4 rotation = glm::rotate( t * glm::radians(m_fValue), rotAxis ) * initialRotation; 
		
		//resulting view
		glm::mat4 translate = glm::translate( glm::vec3(-s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		glm::mat4 translate_r = glm::translate( glm::vec3(s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		view = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		view = glm::inverse(rotation) * view;
		view = glm::inverse(translate) * view; 
		
		view_r = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		view_r = glm::inverse(rotation) * view_r;
		view_r = glm::inverse(translate_r) * view_r; 
		break;
	}
	case TRANSLATE:
	{
		// initial
		glm::vec4 center = glm::vec4(0.f, 0.f, 0.f, 1.0f);
		glm::vec4 headPos = glm::vec4(0.f, 0.f, m_fDistance, 1.0f);
		glm::vec3 up = glm::vec3(s_up); // TODO animate aswell?
		glm::mat4 initialTranslation = glm::translate(glm::vec3(0.01f * m_fStartValue,0.0f,0.0f));

		// current
		float t = -0.5f * ( cos( ((time) * glm::pi<float>()) / m_fDuration ) ) + 0.5f; // parametric 0..1
		glm::mat4 translation = glm::translate( t * glm::vec3(0.01f * m_fValue,0.0f,0.0f) ); 
		headPos = translation * initialTranslation  * headPos;
		center  = translation * initialTranslation  * center;

		//resulting view
		glm::mat4 translate = glm::translate( glm::vec3(-s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		glm::mat4 translate_r = glm::translate( glm::vec3(s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		view = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		view = glm::inverse(translate) * view; 
		
		view_r = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		view_r = glm::inverse(translate_r) * view_r; 
		break;
	}
	case PIVOT:
	{
		// initial
		glm::vec4 center = glm::vec4(0.f, 0.f, 0.f, 1.0f);
		glm::vec3 rotAxis = glm::vec3(0.f,1.f,0.f);
		glm::vec4 headPos = glm::vec4(0.f, 0.f, m_fDistance, 1.0f);
		glm::vec3 up = glm::vec3(s_up); // TODO animate aswell?
		glm::mat4 initialRotation = glm::rotate(glm::radians(m_fStartValue), rotAxis );

		// current
		float t = -0.5f * ( cos( ((time) * glm::pi<float>()) / m_fDuration ) ) + 0.5f; // parametric 0..1
		glm::mat4 rotation = glm::rotate( t * glm::radians(m_fValue), rotAxis ) * initialRotation; 
		headPos = rotation * headPos;

		//resulting view
		glm::mat4 translate = glm::translate( glm::vec3(-s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		glm::mat4 translate_r = glm::translate( glm::vec3(s_eyeDistance * 0.5f, 0.0f, -m_fNeckOffsetZ) );
		view = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		//view = glm::inverse(rotation) * view;
		view = glm::inverse(translate) * view; 
		
		view_r = glm::lookAt(glm::vec3(headPos), glm::vec3(center), up);
		//view_r = glm::inverse(rotation) * view_r;
		view_r = glm::inverse(translate_r) * view_r; 
		break;
	}
	}

	return (eye == LEFT) ? view : view_r;
}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
class CMainApplication 
{
public:
	//struct Config
	//{
	//	glm::vec2 res;
	//	float dur;
	//} m_config;

private:
	// SDL bookkeeping
	SDL_Window	 *m_pWindow;
	SDL_GLContext m_pContext;

	// Render objects bookkeeping
	int m_iActiveModel;
	VolumeData<float> m_volumeData;
	VolumeSubdiv* m_pVolume;
	GLuint m_volumeTexture;
	Quad*	m_pQuad;
	Grid*	m_pGrid;
	
	// Shader/FBO/RenderPass bookkeeping
public:
	enum WarpingTechniques{
	REFERENCE,
	NONE,
	QUAD,
	GRID,
	NOVELVIEW,
	NUM_WARPTECHNIQUES // auxiliary
	}; 
	int m_iActiveWarpingTechnique;
private:	
	ShaderProgram* m_pUvwShader[NUM_WARPTECHNIQUES];
	ShaderProgram* m_pWarpShader[NUM_WARPTECHNIQUES];
	ShaderProgram* m_pRaycastShader[NUM_WARPTECHNIQUES];
	ShaderProgram* m_pDiffShader[NUM_WARPTECHNIQUES];
	ShaderProgram* m_pShowTexShader;

	RenderPass*	   m_pShowTex;
	RenderPass*    m_pUvw[NUM_WARPTECHNIQUES];
	RenderPass*    m_pWarp[NUM_WARPTECHNIQUES];
	RenderPass*    m_pRaycast[NUM_WARPTECHNIQUES][2];
	RenderPass*    m_pDiff[NUM_WARPTECHNIQUES];
	ChunkedAdaptiveRenderPass* m_pChunkedRaycast[NUM_WARPTECHNIQUES][2];

	FrameBufferObject* m_pUvwFBO[NUM_WARPTECHNIQUES][2]; // Left + Right
	FrameBufferObject* m_pWarpFBO[NUM_WARPTECHNIQUES][2]; // Left + Right
	SimpleDoubleBuffer<FrameBufferObject*> m_pSimFBO[NUM_WARPTECHNIQUES][2]; //Left + Right, front/back
	FrameBufferObject* m_pDiffFBO[NUM_WARPTECHNIQUES][2]; //Left + Right
	FrameBufferObject* m_pNovelViewUvwFBO[2]; // Left + Right

	// Simulation
	DisplaySimulation m_displaySimulation;
	HmdSimulation m_hmdSimulation;

	float m_fSimLastTime[NUM_WARPTECHNIQUES][2];
	float m_fSimRenderTime[NUM_WARPTECHNIQUES][2];
	float m_fWarpTime[NUM_WARPTECHNIQUES][2];

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> m_sdlEventFunc;

	//========== MISC ================
	std::vector<std::string> m_shaderDefines;  // defines in shader
	std::vector<std::string> m_issetDefineStr; // define strings that can be set in GUI
	std::vector<int> m_issetDefine;            // define booleans (as int) settable by GUI

	Turntable m_turntable;
	glm::mat4 m_volumeScale;

	std::vector<float> m_fpsCounter;
	int m_iCurFpsIdx;

	bool m_bHasLod;
	bool m_bHasShadow;

	float m_fOldX;
	float m_fOldY;

	int m_iNumNovelViewSamples;

	int m_iSaveImageIdxMod;

	int m_iNumShadowSamples;
	glm::vec3 m_shadowDir;
	float m_fShadowAngles[2];

	OpenGLTimings m_timings[NUM_WARPTECHNIQUES][2]; // for each warp method
	float m_fTotalFinishTimeBuffer[NUM_WARPTECHNIQUES][2]; // for each warp method
	float m_fTotalFinishTimestampBuffer[NUM_WARPTECHNIQUES][2]; // for each warp method

public:
	CMainApplication(int argc, char *argv[]);
	~CMainApplication();

	std::string m_executableName;

	// once
	void loop();
	void loadShaderDefines();
	void loadShaders();
	void initSceneVariables();
	void loadGeometries();
	void initFramebuffers();
	void initRenderPasses();
	void initEventHandlers();
	void initGui();

	// on event
	void handleVolume();
	void loadVolume();
	void updateShaderDefines();
	float getPredictionTimeOffset(float simTime, int idx, int eye); 
	void updatePredictionTimes();

	// per frame
	void updateGui();
	void updateWarpShader(float lastTime, float warpTime, int idx, int eye);
	void updateDiffShader(int idx, int eye, GLuint tex1, GLuint tex2);
	void updateRaycastShader(float simTime, int idx, int eye);
	void updateUvwShader(float simTime, int idx, int eye);
	void renderGui();
	void renderView(float simTime, int idx, int eye, bool doPrediction = true);
	void renderViews(float simTime, int idx, bool doPrediction = true);
	void renderRef(float simTime);
	void renderVolume(float renderTime, int idx, int eye);
	void renderWarp(float lastTime, float warpTime, int idx, int eye);
	void renderDiff(int idx, int eye); 
	void renderDiffs(int idx); 
	void renderToScreen(GLuint left, GLuint right);
	void profileFPS(float fps);

	// profiling
	std::vector<float> getVanillaTimes(); // return the timestamps at wich a new raycasting frame is issued
	float getAvgDssim(int idx, int eye);
	
	// aux
	void printProgress(float progress, std::string msg = "");
};

int main(int argc, char *argv[])
{
	DEBUGLOG->setAutoPrint(true);

	CMainApplication *pMainApplication = new CMainApplication( argc, argv );
		
	pMainApplication->loadShaderDefines();
	
	pMainApplication->loadShaders();
	
	pMainApplication->initSceneVariables();

	pMainApplication->handleVolume();

	pMainApplication->loadGeometries();

	pMainApplication->initFramebuffers();

	pMainApplication->initRenderPasses();

	pMainApplication->initEventHandlers();

	pMainApplication->initGui();

	pMainApplication->loop();

	return 0;
}

CMainApplication::CMainApplication(int argc, char *argv[])
	: m_issetDefineStr(SHADER_DEFINES, std::end(SHADER_DEFINES)) // all possible defines
	, m_issetDefine(m_issetDefineStr.size(), (int) false) // define states
	, m_shaderDefines(0) // currently set defines
	, m_fpsCounter(120)
	, m_iCurFpsIdx(0)
	, m_iActiveModel(VolumePresets::CT_Head)
	, m_bHasLod(false)
	, m_bHasShadow(false)
	, m_fOldX(0.0f)
	, m_fOldY(0.0f)
	, m_iNumNovelViewSamples(12)
	, m_iActiveWarpingTechnique(0)
	, m_iNumShadowSamples(12)
	, m_iSaveImageIdxMod(10)
	, m_shadowDir( glm::normalize( glm::vec3( std::cos( -glm::half_pi<float>() ) * std::cos( -glm::quarter_pi<float>() ), std::sin( -glm::half_pi<float>() ) * std::cos( -glm::quarter_pi<float>() ), std::tan( -glm::quarter_pi<float>() ) ) ) )
{
	DEBUGLOG->setAutoPrint(true);
	m_fShadowAngles[0] = -0.5f;
	m_fShadowAngles[1] = -0.5f;
	
	for (auto e : m_fTotalFinishTimeBuffer) { e[0] = 0.016f; e[1] = 0.016f; }

	std::string fullExecutableName( argv[0] );
	m_executableName = fullExecutableName.substr( fullExecutableName.rfind("\\") + 1);
	m_executableName = m_executableName.substr(0, m_executableName.find(".exe"));
	DEBUGLOG->log("Executable name: " + m_executableName);
		
	// create m_pWindow and opengl context
	m_pWindow = generateWindow_SDL(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	printOpenGLInfo();
	printSDLRenderDriverInfo();
}

// auxiliary
void CMainApplication::printProgress(float progress, std::string msg)
{
	ImGui_ImplSdlGL3_NewFrame(m_pWindow);
	ImGui::OpenPopup("Progress");
	if (ImGui::BeginPopupModal("Progress", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		static float _progress = progress;
		_progress = progress;
		ImGui::SliderFloat("##progress", &_progress, 0.0, 100.0);
		if (msg != "") { ImGui::Text(msg.c_str()); }
		ImGui::EndPopup();
	}
	renderToScreen(
	m_pWarpFBO[m_iActiveWarpingTechnique][LEFT]->getBuffer("fragColor"), 
	m_pWarpFBO[m_iActiveWarpingTechnique][RIGHT]->getBuffer("fragColor")
	);
	renderGui();
	SDL_GL_SwapWindow( m_pWindow ); // swap buffers
}

void CMainApplication::updatePredictionTimes()
{
	for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
	{
	for (int j = LEFT; j <= RIGHT; j++)
	{
		m_fTotalFinishTimeBuffer[i][j] = m_pChunkedRaycast[i][j]->getLastFinishTime() / 1000.0f;
	}
	}
}


void CMainApplication::loop()
{
	std::string window_header = "Volume Renderer - Warp Profiling";
	SDL_SetWindowTitle(m_pWindow, window_header.c_str() );
	while (!shouldClose(m_pWindow))
	{
		enum Modes{SETUP, PROFILE}; 
		static int mode = SETUP;	
		
		pollSDLEvents(m_pWindow, m_sdlEventFunc);
		profileFPS((float) (ImGui::GetIO().Framerate));

		if(mode == SETUP)
		{
			updateGui();

			if ( ImGui::Button("Begin Profiling") )
			{
				mode = PROFILE;
			}

			s_model = s_translation * m_turntable.getRotationMatrix() * s_rotation * s_scale * m_volumeScale;

			if ( m_iActiveWarpingTechnique == REFERENCE )
			{
				renderRef(ImGui::GetTime());
			}
			else
			{
				renderViews(ImGui::GetTime(), m_iActiveWarpingTechnique);
				updatePredictionTimes();
			}

			renderToScreen(
				m_pWarpFBO[m_iActiveWarpingTechnique][LEFT]->getBuffer("fragColor"), 
				m_pWarpFBO[m_iActiveWarpingTechnique][RIGHT]->getBuffer("fragColor")
				);

			renderGui();

			SDL_GL_SwapWindow( m_pWindow ); // swap buffers
		}

		if(mode == PROFILE)
		{
			// auxiliary
			auto lastTimeBefore = [](std::vector<float>& times, float time)
			{
				float last = 0.0f;	for ( auto t: times) if( t > time ){ return last; }else{ last = t; } return last;
			};
			std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";

			//----------- CSV Writer -------------
			CSVWriter<float> csvWriter;
			std::vector<std::string> headers;
			headers.push_back("Frame");
			headers.push_back("Sim Time");
			headers.push_back("DSSIM NONE (L)");
			headers.push_back("DSSIM QUAD (L)");
			headers.push_back("DSSIM GRID (L)");
			headers.push_back("DSSIM NOVELVIEW (L)");
			headers.push_back("Rt NONE (L)");
			headers.push_back("Rt QUAD (L)");
			headers.push_back("Rt GRID (L)");
			headers.push_back("Rt NOVELVIEW (L)");
			headers.push_back("Ref t (L/R)");
			headers.push_back("Lt NONE (L)");
			headers.push_back("Lt QUAD (L)");
			headers.push_back("Lt GRID (L)");
			headers.push_back("Lt NOVELVIEW (L)");
			headers.push_back("Wt NONE (L)");
			headers.push_back("Wt QUAD (L)");
			headers.push_back("Wt GRID (L)");
			headers.push_back("Wt NOVELVIEW (L)");
			csvWriter.setHeaders(headers);

			//----------- VANILLA TIMES -------------
			std::vector<float> vanillaTimes = getVanillaTimes(); // [s]
			int numFrames = (int) ( (m_hmdSimulation.m_fDuration) / m_displaySimulation.m_fRefreshTime ) + 1; // [s]
			
			//----------- SETUP/RESET/INITIALIZE -------------
			// temporarily disable animation --> hmd only returns values for time = 0.0
			bool tmpClampTime = m_hmdSimulation.m_bClampTime;
			m_hmdSimulation.m_bClampTime = true;
			m_hmdSimulation.m_bAnimateView = false;
			for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
			{
			for (int j = LEFT; j <= RIGHT; j++)
			{
				//+++++++++++++ DEBUG ++++++++++++++
				printProgress(((float) ((i-1) * 2 + j) / ((float) (NUM_WARPTECHNIQUES - 1) * 2)) * 100.0f, "Setup...");
				//++++++++++++++++++++++++++++++++++

				// reset sim and warp times
				m_fSimLastTime[i][j]   = -1.0f;
				m_fSimRenderTime[i][j] = -1.0f;
				m_fWarpTime[i][j]      = -1.0f;

				// render t=0 views / warps
				// reset ChunkedRenderPasses
				m_pChunkedRaycast[i][j]->reset();
				int numSetupFrames = 5;
				for (int k = 0; k < numSetupFrames; k++){ // actually, do it a couple of times to make sure grid render times are available
				do {
					renderView(0.0f, i, j, false);
					glFinish();
				} while (!m_pChunkedRaycast[i][j]->isFinished());}

				// reset sim and warp times
				m_fSimLastTime[i][j]   = 0.0f;
				m_fSimRenderTime[i][j] = 0.0f;
				m_fWarpTime[i][j]      = 0.0f;
				m_fTotalFinishTimestampBuffer[i][j] = 0.0f;
			}
			}
			// enable animation
			m_hmdSimulation.m_bAnimateView = true;

			//----------- PROFILING -------------
			for (int i = 0; i < numFrames; i++)
			{
				//+++++++++++++ DEBUG ++++++++++++++
				printProgress(((float) i / (float) numFrames) * 100.0f, "Profiling...");
				//++++++++++++++++++++++++++++++++++

				m_displaySimulation.setFrame(i);
				float simTime = m_displaySimulation.getTimeOfFrame(i); // [s]
				
				// REFERENCE
				float referenceTime = simTime + m_displaySimulation.getTimeToUpdate();
				m_fSimLastTime[REFERENCE][LEFT]    = referenceTime;
				m_fSimLastTime[REFERENCE][RIGHT]   = referenceTime;
				m_fSimRenderTime[REFERENCE][LEFT]  = referenceTime;
				m_fSimRenderTime[REFERENCE][RIGHT] = referenceTime;
				renderRef( referenceTime ); // i.e.: what should be visible at the time the display flashes
				
				// NONE
				float lastVanillaRenderTime = lastTimeBefore( vanillaTimes, simTime );
				renderViews(lastVanillaRenderTime, NONE, false);
				
				// WARP ITERATIONS 
				renderViews(simTime, QUAD );
				renderViews(simTime, GRID );
				renderViews(simTime, NOVELVIEW );
				glFinish();

				//timestamp queries, calculate 'render times'
				for (int i = QUAD; i < NUM_WARPTECHNIQUES; i++)
				{ for (int j = LEFT; j <= RIGHT; j++)
				{
					if (m_pChunkedRaycast[i][j]->isFinished())
					{
						float lastTime = m_fTotalFinishTimestampBuffer[i][j];
						m_fTotalFinishTimeBuffer[i][j] = simTime - lastTime;
						m_fTotalFinishTimestampBuffer[i][j] = simTime;
					}
				}}

				// DIFFERENCES
				renderDiffs(NONE);
				renderDiffs(QUAD);
				renderDiffs(GRID);
				renderDiffs(NOVELVIEW);
				glFinish();
				
				//+++++++++++++ DEBUG ++++++++++++++
				if (m_iSaveImageIdxMod >= 1 && i % m_iSaveImageIdxMod == 0){
					//+++++++++++++ DEBUG ++++++++++++++
					printProgress(((float) 0.0 / (float) NUM_WARPTECHNIQUES) * 100.0f, "Save Images...");
					//++++++++++++++++++++++++++++++++++
					TextureTools::saveTexture(prefix + "REF_"    + std::to_string(i) + ".png" ,m_pSimFBO[REFERENCE][LEFT].getFront()->getBuffer("fragColor") );
					for (int j = NONE; j < NUM_WARPTECHNIQUES; j++){
						//+++++++++++++ DEBUG ++++++++++++++
						printProgress(((float) j / (float) NUM_WARPTECHNIQUES) * 100.0f, "Save Images...");
						//++++++++++++++++++++++++++++++++++
						std::string prefix_ = prefix + std::to_string(j) + "_" ;
						TextureTools::saveTexture(prefix_ + "WRP_"    + std::to_string(i) + ".png", m_pWarpFBO[j][LEFT]->getBuffer("fragColor") );
						TextureTools::saveTexture(prefix_ + "DSSIM_"  + std::to_string(i) + ".png", m_pDiffFBO[j][LEFT]->getBuffer("fragColor") );
					}
				}
				glFinish();
				//++++++++++++++++++++++++++++++++++

				std::vector<float> row;
				row.push_back((float) i);
				row.push_back(simTime);
				for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
				{
					row.push_back( getAvgDssim(i,LEFT) );
				}
				for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
				{
					row.push_back( m_fSimRenderTime[i][LEFT] );
				}
				row.push_back(referenceTime);
				for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
				{
					row.push_back( m_fSimLastTime[i][LEFT] );
				}
				for (int i = NONE; i < NUM_WARPTECHNIQUES; i++)
				{
					row.push_back( m_fWarpTime[i][LEFT] );
				}
				csvWriter.addRow(row);
			}

			csvWriter.writeToFile(prefix + "PROFILING.csv" );

			// reset
			m_hmdSimulation.m_bClampTime = tmpClampTime;
			mode = SETUP;
		}
	}

	ImGui_ImplSdlGL3_Shutdown();
	destroyWindow(m_pWindow);
	SDL_Quit();
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

	{ m_bHasLod = false; for (auto e : m_shaderDefines) { m_bHasLod  |= (e == "LEVEL_OF_DETAIL"); } }
	{ m_bHasShadow = false; for (auto e : m_shaderDefines) { m_bHasShadow  |= (e == "SHADOW_SAMPLING"); } }

	DEBUGLOG->outdent();
}

void CMainApplication::loadShaders()
{
	DEBUGLOG->log("Shader Compilation: shaders"); DEBUGLOG->indent();
	for (int i = 0; i< NUM_WARPTECHNIQUES; i++)
	{
		m_pUvwShader[i] = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag", m_shaderDefines);	
		m_pDiffShader[i] = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/error.frag", m_shaderDefines); DEBUGLOG->outdent();
	}

	m_pRaycastShader[REFERENCE] = new ShaderProgram("/raycast/simpleRaycast.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[NONE] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[QUAD] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[GRID] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[NOVELVIEW] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/synth_raycastLayer_simple.frag", m_shaderDefines);

	m_pWarpShader[REFERENCE] = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", m_shaderDefines);
	m_pWarpShader[NONE] = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", m_shaderDefines);
	m_pWarpShader[QUAD] = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleWarp.frag", m_shaderDefines);
	m_pWarpShader[GRID] = new ShaderProgram("/raycast/gridWarp.vert", "/raycast/gridWarp.frag", m_shaderDefines);
	m_pWarpShader[NOVELVIEW] = new ShaderProgram("/screenSpace/fullscreen.vert",  "/raycast/synth_novelView_simple.frag", m_shaderDefines);
	
	m_pShowTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	DEBUGLOG->outdent();
}

void CMainApplication::initFramebuffers()
{
	DEBUGLOG->log("FrameBufferObject Creation"); DEBUGLOG->indent();
	for (int i = 0; i < NUM_WARPTECHNIQUES; i++) 
	{ for (int j = 0; j < 2; j++) {
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pUvwFBO[i][j] = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pUvwFBO[i][j]->addColorAttachments(2); // front UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
	
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pSimFBO[i][j].getFront() = new FrameBufferObject(m_pRaycastShader[i]->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		m_pSimFBO[i][j].getBack()  = new FrameBufferObject(m_pRaycastShader[i]->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		FrameBufferObject::s_internalFormat = GL_RGBA;

		m_pWarpFBO[i][j] = new FrameBufferObject(m_pWarpShader[i]->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		
		// Diff FBOs
		int numMipmaps = (int) (std::log( std::max(FRAMEBUFFER_RESOLUTION.x, FRAMEBUFFER_RESOLUTION.y) ) / std::log( 2.0f ) );
		FrameBufferObject::s_useTexStorage2D = true;
		FrameBufferObject::s_numLevels = numMipmaps + 1;
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pDiffFBO[i][j] = new FrameBufferObject(m_pDiffShader[i]->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
		OPENGLCONTEXT->bindTexture( m_pDiffFBO[i][j]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // or else texturelod doesn't work
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		OPENGLCONTEXT->bindTexture(m_pDiffFBO[i][j]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // or else texturelod doesn't work
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		OPENGLCONTEXT->bindTexture(0);
		FrameBufferObject::s_internalFormat = GL_RGBA8;
		FrameBufferObject::s_useTexStorage2D = false;
		FrameBufferObject::s_numLevels = 1;
	}}
	DEBUGLOG->outdent();
	
	// redirect REFERENCE WarpFBO to SimFBO --> no warp technique
	for (int i = 0; i < 2; i++)
	{
		delete m_pWarpFBO[REFERENCE][i]; // dont need this one
		//delete m_pSimFBO[REFERENCE][i].getBack(); // dont need this one either
		m_pWarpFBO[REFERENCE][i] = m_pSimFBO[REFERENCE][i].getFront();
	}

	for (int i = 0; i < 2; i++)
	{
		FrameBufferObject::s_internalFormat = GL_RGBA16F;
		m_pNovelViewUvwFBO[i] = new FrameBufferObject((int) FRAMEBUFFER_RESOLUTION.x,(int) FRAMEBUFFER_RESOLUTION.y);
		m_pNovelViewUvwFBO[i]->addColorAttachments(2); // front UVRs and back UVRs
		FrameBufferObject::s_internalFormat = GL_RGBA;
	}
}

void CMainApplication::initRenderPasses()
{
	DEBUGLOG->log("RenderPass Creation"); DEBUGLOG->indent();
	for (int i = 0; i < NUM_WARPTECHNIQUES; i++)
	{
		m_pUvw[i] = new RenderPass(m_pUvwShader[i], m_pUvwFBO[i][LEFT]);
		m_pUvw[i]->setClearColor(0,0,0,0);
		m_pUvw[i]->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		m_pUvw[i]->addRenderable(m_pVolume);
		m_pUvw[i]->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
		m_pUvw[i]->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded

		for (int j = LEFT; j <= RIGHT; j++)
		{
			m_pRaycast[i][j] = new RenderPass( m_pRaycastShader[i], m_pSimFBO[i][LEFT].getBack());
			m_pRaycast[i][j]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			m_pRaycast[i][j]->addRenderable( m_pQuad );
			m_pRaycast[i][j]->addEnable(GL_DEPTH_TEST);
			m_pRaycast[i][j]->addDisable(GL_BLEND);

			glm::ivec2 viewportSize = glm::ivec2(FRAMEBUFFER_RESOLUTION);
			glm::ivec2 chunkSize = glm::ivec2(96,96);
			float targetRenderTime = (m_displaySimulation.m_fRefreshTime * 1000.0f) / 2.0f;
			if (i == NONE || i == REFERENCE ) {
				targetRenderTime = 999.0f;
				chunkSize = glm::ivec2(FRAMEBUFFER_RESOLUTION);
			}
		
			m_pChunkedRaycast[i][j]  = new ChunkedAdaptiveRenderPass(m_pRaycast[i][j], viewportSize, chunkSize, 8, targetRenderTime, 1.0f);
		}

		m_pDiff[i] = new RenderPass(m_pDiffShader[i], m_pDiffFBO[i][LEFT]);
		m_pDiff[i]->addRenderable(m_pQuad);
		m_pDiff[i]->addDisable(GL_DEPTH_TEST);
	}
	
	m_pWarp[REFERENCE] = new RenderPass(m_pWarpShader[REFERENCE], m_pWarpFBO[REFERENCE][LEFT]);
	m_pWarp[REFERENCE]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pWarp[REFERENCE]->addDisable(GL_DEPTH_TEST);
	m_pWarp[REFERENCE]->addRenderable(m_pQuad);
	m_pWarp[NONE] = new RenderPass(m_pWarpShader[NONE], m_pWarpFBO[NONE][LEFT]);
	m_pWarp[NONE]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pWarp[NONE]->addDisable(GL_DEPTH_TEST);
	m_pWarp[NONE]->addRenderable(m_pQuad);
	m_pWarp[QUAD] = new RenderPass(m_pWarpShader[QUAD], m_pWarpFBO[QUAD][LEFT]);
	m_pWarp[QUAD]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pWarp[QUAD]->addDisable(GL_DEPTH_TEST);
	m_pWarp[QUAD]->addRenderable(m_pQuad);
	m_pWarp[GRID] = new RenderPass(m_pWarpShader[GRID], m_pWarpFBO[GRID][LEFT]);
	m_pWarp[GRID]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pWarp[GRID]->addEnable(GL_DEPTH_TEST);
	m_pWarp[GRID]->addRenderable(m_pGrid);
	m_pWarp[NOVELVIEW] = new RenderPass(m_pWarpShader[NOVELVIEW], m_pWarpFBO[NOVELVIEW][LEFT]);
	m_pWarp[NOVELVIEW]->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pWarp[NOVELVIEW]->addRenderable(m_pQuad);
	m_pWarp[NOVELVIEW]->addEnable(GL_DEPTH_TEST);
	m_pWarp[NOVELVIEW]->addEnable(GL_BLEND);

	// 'disable' adaptive chunked raycasting behaviour for 'none' raycaster
	for(int i = 0; i< 2; i++)
	{
		m_pChunkedRaycast[NONE][i]->setAutoAdjustRenderTime(false);
		m_pChunkedRaycast[NONE][i]->setChunkSize( glm::ivec2(FRAMEBUFFER_RESOLUTION) );
		m_pChunkedRaycast[NONE][i]->setTargetRenderTime( 999.0f );
		m_pChunkedRaycast[NONE][i]->setRenderTimeBias( 1.0f );
		m_pChunkedRaycast[NONE][i]->setPrintDebug(false);
	}
	// 'disable' adaptive chunked raycasting behaviour for 'reference' raycaster
	for(int i = 0; i< 2; i++)
	{
		m_pChunkedRaycast[REFERENCE][i]->setAutoAdjustRenderTime(false);
		m_pChunkedRaycast[REFERENCE][i]->setChunkSize( glm::ivec2(FRAMEBUFFER_RESOLUTION) );
		m_pChunkedRaycast[REFERENCE][i]->setTargetRenderTime( 999.0f );
		m_pChunkedRaycast[REFERENCE][i]->setRenderTimeBias( 1.0f );
		m_pChunkedRaycast[REFERENCE][i]->setPrintDebug(false);
	}
	m_pShowTex = new RenderPass( m_pShowTexShader, 0);
	m_pShowTex->addRenderable(m_pQuad);
	m_pShowTex->addDisable(GL_DEPTH_TEST);
	m_pShowTex->addDisable(GL_BLEND);

	DEBUGLOG->outdent();
}

void CMainApplication::initSceneVariables()
{
	/////////////////////     Scene / View Settings     //////////////////////////
	s_volumeSize = glm::vec3(1.0f);
	updateModelToTexture();

	s_translation = glm::translate(glm::vec3(0.0f,0.0f,0.0f));
	s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
	s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));

	s_fovY = 65.f;

	updateNearHeightWidth();
	updatePerspective();
	updateScreenToViewMatrix();
}

void CMainApplication::loadGeometries()
{
	m_pQuad = new Quad();
	m_pVolume = new VolumeSubdiv(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 4);
	m_pGrid = new Grid(400, 400, 0.0025f, 0.0025f, false);
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
						m_iActiveWarpingTechnique = (m_iActiveWarpingTechnique + 1) % NUM_WARPTECHNIQUES;
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
}

void CMainApplication::initGui()
{
	// Setup ImGui binding
	ImGui_ImplSdlGL3_Init(m_pWindow);
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

	{ m_bHasLod = false; for (auto e : m_shaderDefines) { m_bHasLod  |= (e == "LEVEL_OF_DETAIL"); } }
	{ m_bHasShadow = false; for (auto e : m_shaderDefines) { m_bHasShadow  |= (e == "SHADOW_SAMPLING"); } }
}

void CMainApplication::loadVolume()
{
	int numLevels = 1;
	{if ( m_bHasLod ){
		numLevels = 4;
	}}

	VolumePresets::loadPreset( m_volumeData, (VolumePresets::Preset) m_iActiveModel);
	m_volumeTexture = loadTo3DTexture<float>(m_volumeData, numLevels, GL_R16F, GL_RED, GL_FLOAT);
	m_volumeData.data.clear(); // set free	

	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	checkGLError(true);
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

void CMainApplication::updateGui()
{
		ImGui_ImplSdlGL3_NewFrame(m_pWindow);
		ImGuiIO& io = ImGui::GetIO();
		profileFPS(ImGui::GetIO().Framerate);

		ImGui::Value("FPS", io.Framerate);

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
		}
        		
		ImGui::Separator();

		{if (m_bHasLod){
		if (ImGui::CollapsingHeader("Level of Detail Settings"))
		{
			ImGui::DragFloat("Lod Max Level", &s_lodMaxLevel, 0.1f, 0.0f, 8.0f);
			ImGui::DragFloat("Lod Begin", &s_lodBegin, 0.01f, 0.0f, s_far);
			ImGui::DragFloat("Lod Range", &s_lodRange, 0.01f, 0.0f, std::max(0.1f, s_far - s_lodBegin));
		}
		}}

		//if (ImGui::ColorEdit4("Clear Color", &m_clearColor[0]))
		//{
		//	m_pGridWarpShader->update("color", m_clearColor);
		//}

		if (ImGui::Button("Save Current Images"))
		{		
			std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";
			TextureTools::saveTexture(prefix + "UVW_F.png", m_pUvwFBO[m_iActiveWarpingTechnique][LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1) );
			TextureTools::saveTexture(prefix + "UVW_B.png", m_pUvwFBO[m_iActiveWarpingTechnique][LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
			TextureTools::saveTexture(prefix + "SIM.png", m_pSimFBO[m_iActiveWarpingTechnique][LEFT].getFront()->getBuffer("fragColor") );
			TextureTools::saveTexture(prefix + "WRP.png", m_pWarpFBO[m_iActiveWarpingTechnique][LEFT]->getBuffer("fragColor") );
		}


		ImGui::Separator();
		{
			static float scale = s_scale[0][0];
			if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 100.0f))
			{
				s_scale = glm::scale(glm::vec3(scale));
			}
		}

		ImGui::SliderInt("Active Warpin Technique", &m_iActiveWarpingTechnique, 0, NUM_WARPTECHNIQUES - 1);

		ImGui::SliderInt("Num Novel-View Samples", &m_iNumNovelViewSamples, 4, 96);

		{if ( m_bHasShadow ){
			float alpha = m_fShadowAngles[0] * glm::pi<float>();
			float beta  = m_fShadowAngles[1] * glm::half_pi<float>();
			if (ImGui::CollapsingHeader("Shadow Properties"))
			{	
				if (ImGui::SliderFloat2("Shadow Dir", m_fShadowAngles, -0.999f, 0.999f) )
				{
					m_shadowDir = glm::normalize( glm::vec3(std::cos( alpha ) * std::cos(beta), std::sin( alpha ) * std::cos( beta ), std::tan( beta ) ) );
				}
				ImGui::SliderInt("Num Steps", &m_iNumShadowSamples, 0, 32);
			}
		}}

		if ( ImGui::CollapsingHeader("Animation Settings") )
		{
			ImGui::SliderInt("Save Image Idx Mod", &m_iSaveImageIdxMod, 0, 100); if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 == disabled");
			ImGui::SliderInt("Active Animation Mode", &m_hmdSimulation.m_mode, 0, HmdSimulation::NUM_ANIMATIONMODES - 1);
			ImGui::SliderFloat("Start Value", &m_hmdSimulation.m_fStartValue, -90.0f, 90.0f);
			ImGui::SliderFloat("Value", &m_hmdSimulation.m_fValue, -90.0f, 90.0f);
			ImGui::SliderFloat("Duration", &m_hmdSimulation.m_fDuration, 0.0f, 5.0f);
			ImGui::SliderFloat("Distance", &m_hmdSimulation.m_fDistance, 0.0f, 2.0f);
			switch (m_hmdSimulation.m_mode)
			{
			case HmdSimulation::TRANSLATE:
				ImGui::Value("Length [m]", m_hmdSimulation.m_fValue / 100.0f); ImGui::SameLine(); ImGui::Value("Speed [m/s]", m_hmdSimulation.m_fValue / (100.0f * m_hmdSimulation.m_fDuration) );
				break;
			case HmdSimulation::ROTATE:
				ImGui::Value("Angle  [deg]", m_hmdSimulation.m_fValue); ImGui::SameLine(); ImGui::Value("Speed [deg/s]", m_hmdSimulation.m_fValue / m_hmdSimulation.m_fDuration);
				break;
			case HmdSimulation::PIVOT:
				float arcLength = glm::radians(m_hmdSimulation.m_fValue) * m_hmdSimulation.m_fDistance;
				ImGui::Value("Length [m]", arcLength); ImGui::SameLine(); ImGui::Value("Speed [m/s]", arcLength / m_hmdSimulation.m_fDuration);
				break;
			}
		}

		/**
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
			if (m_frame.Timings.getFront().m_timestamps.find("Swap Time LEFT") != m_frame.Timings.getFront().m_timestamps.end())
			{
				float t = m_frame.Timings.getFront().m_timestamps.at("Swap Time RIGHT").lastTime;
				if (t > lastSwapTimestampLeft) { lastSwapTimeLeft = t - lastSwapTimestampLeft; lastSwapTimestampLeft = t; }
				ImGui::Value("Swap Time Left", lastSwapTimeLeft);
			}
			if (m_frame.Timings.getFront().m_timestamps.find("Swap Time RIGHT") != m_frame.Timings.getFront().m_timestamps.end())
			{
				float t = m_frame.Timings.getFront().m_timestamps.at("Swap Time RIGHT").lastTime;
				if (t > lastSwapTimestampRight) { lastSwapTimeRight = t - lastSwapTimestampRight; lastSwapTimestampRight = t; }
				ImGui::Value("Swap Time Right", lastSwapTimeRight);
			}
			ImGui::Separator();
			}
			//+++++++++++++++++++++++++++++++++

		}
		if(!pause_frame_profiler) m_frame.Timings.swap();
		*/
}

void CMainApplication::updateWarpShader(float lastTime, float warpTime, int idx, int eye)
{
	switch (idx)
	{
	default:
	case REFERENCE:
	case NONE:
		m_pWarpShader[idx]->updateAndBindTexture( "tex", 2, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor") ); // last result
		break;
	case QUAD:
		m_pWarpShader[QUAD]->updateAndBindTexture( "tex", 2, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor") ); // last result
		m_pWarpShader[QUAD]->update( "blendColor", 1.0f );
		m_pWarpShader[QUAD]->update( "oldView",     m_hmdSimulation.getView(lastTime, eye) ); // update with old view
		m_pWarpShader[QUAD]->update( "newView",     m_hmdSimulation.getView(warpTime, eye) ); // most current view
		m_pWarpShader[QUAD]->update( "projection",  m_hmdSimulation.getPerspective(lastTime, eye) ); 
		break;
	case GRID:
		m_pWarpShader[GRID]->updateAndBindTexture( "tex", 2, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor") );
		m_pWarpShader[GRID]->updateAndBindTexture( "depth_map", 3, m_pSimFBO[idx][eye].getFront()->getDepthTextureHandle() ); // last first hit map
		//m_pWarpShader[GRID]->update("color", m_clearColor);
		m_pWarpShader[GRID]->update( "uViewOld",    m_hmdSimulation.getView(lastTime, eye) ); // update with old view
		m_pWarpShader[GRID]->update( "uViewNew",    m_hmdSimulation.getView(warpTime, eye) ); // most current view
		m_pWarpShader[GRID]->update( "uProjection", m_hmdSimulation.getPerspective(lastTime, eye) ); 
		break;
	case NOVELVIEW:
		m_pUvw[NOVELVIEW]->setFrameBufferObject( m_pNovelViewUvwFBO[eye] );
		m_pUvwShader[idx]->update("model", s_model);
		m_pUvwShader[idx]->update("view", m_hmdSimulation.getView(warpTime, eye));
		m_pUvwShader[idx]->update("projection", m_hmdSimulation.getPerspective(warpTime, eye));
		m_pUvw[NOVELVIEW]->render();

		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("layer0", 2, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor1"));
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("layer1", 3, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor2"));
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("layer2", 4, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor3"));
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("layer3", 5, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor4"));
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("depth",  6, m_pSimFBO[idx][eye].getFront()->getBuffer("fragDepth"));
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("back_uvw_map", 7, m_pNovelViewUvwFBO[eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		m_pWarpShader[NOVELVIEW]->updateAndBindTexture("front_uvw_map", 8, m_pNovelViewUvwFBO[eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
		m_pWarpShader[NOVELVIEW]->update("uThreshold",   m_iNumNovelViewSamples);
		m_pWarpShader[NOVELVIEW]->update("uProjection",  m_hmdSimulation.getPerspective(lastTime, eye) );
		m_pWarpShader[NOVELVIEW]->update("uViewOld",     m_hmdSimulation.getView(lastTime, eye) );
		m_pWarpShader[NOVELVIEW]->update("uViewNovel",   m_hmdSimulation.getView(warpTime, eye));	
		break;
	}
}

void CMainApplication::updateDiffShader(int idx, int eye, GLuint tex1, GLuint tex2)
{
	m_pDiffShader[idx]->updateAndBindTexture("tex1", 2, tex1);
	m_pDiffShader[idx]->updateAndBindTexture("tex2", 3, tex2);
}


void CMainApplication::updateUvwShader(float simTime, int idx, int eye)
{
	m_pUvwShader[idx]->update("model", s_model);
	m_pUvwShader[idx]->update("view", m_hmdSimulation.getView(simTime, eye));
	m_pUvwShader[idx]->update("projection", m_hmdSimulation.getPerspective(simTime, eye));
}

void CMainApplication::updateRaycastShader(float simTime, int idx, int eye)
{
	switch (idx)
	{
	case NOVELVIEW:
		//m_pRaycastShader[NOVELVIEW]->update( "uScreenToView", s_screenToView );
	default:
	case REFERENCE:
	case NONE:
	case QUAD:
	case GRID:
		m_pRaycastShader[idx]->updateAndBindTexture("volume_texture", 0, m_volumeTexture, GL_TEXTURE_3D); // m_pVolume texture
		m_pRaycastShader[idx]->updateAndBindTexture("transferFunctionTex", 1, TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE_1D);
		m_pRaycastShader[idx]->updateAndBindTexture("back_uvw_map",  2, m_pUvwFBO[idx][eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		m_pRaycastShader[idx]->updateAndBindTexture("front_uvw_map", 3, m_pUvwFBO[idx][eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1) );
		m_pRaycastShader[idx]->update("uStepSize", s_rayStepSize); 	  // ray step size
		m_pRaycastShader[idx]->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		m_pRaycastShader[idx]->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		//m_pRaycastShader[idx]->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(m_hmdSimulation.getView(simTime, eye)) * s_screenToView );
		m_pRaycastShader[idx]->update("uProjection", m_hmdSimulation.getPerspective(simTime, eye) );
		m_pRaycastShader[idx]->update( "uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse( m_hmdSimulation.getView(simTime, eye)) );
		
		if ( m_bHasShadow ){
			m_pRaycastShader[idx]->update("uShadowRayDirection", m_shadowDir); // full range of values in window
			m_pRaycastShader[idx]->update("uShadowRayNumSteps", m_iNumShadowSamples); 	  // lower grayscale ramp boundary		//////////////////////////////////////////////////////////////////////////////
		}
		break;
	}
}

void CMainApplication::renderGui()
{
	ImGui::Render();
	OPENGLCONTEXT->setEnabled(GL_BLEND, false);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
}

float CMainApplication::getPredictionTimeOffset(float simTime, int idx, int eye)
{
	float renderTime = m_fTotalFinishTimeBuffer[idx][eye];
	float timeToNextUpdateThen = m_displaySimulation.getTimeToUpdateFromTime( simTime + renderTime );
	return renderTime + timeToNextUpdateThen;

	// acutally, this doesnt work, because finish time is based on GPU time and is agnostic to what is happening around it
	// return (m_pChunkedRaycast[idx][eye]->getLastFinishTime() / 1000.0) + m_displaySimulation.getTimeToUpdate();
}

void CMainApplication::renderView(float simTime, int idx, int eye, bool doPrediction)
{
	if (m_pChunkedRaycast[idx][eye]->isFinished())
	{
		m_pSimFBO[idx][eye].swap();
		m_pChunkedRaycast[idx][eye]->getRenderPass()->setFrameBufferObject( m_pSimFBO[idx][eye].getBack() );

		m_fSimLastTime[idx][eye] = m_fSimRenderTime[idx][eye];
		if (doPrediction) {
			float predictionTimeOffset = getPredictionTimeOffset(simTime, idx, eye);
			m_fSimRenderTime[idx][eye] = simTime + predictionTimeOffset;
		}
		else
		{
			m_fSimRenderTime[idx][eye] = simTime;
		}
	}

	if (doPrediction) {
		float predictTimeOffset = m_displaySimulation.getTimeToUpdate(); //'time until display flashes'
		m_fWarpTime[idx][eye] = simTime + predictTimeOffset;
	}
	else
	{
		m_fWarpTime[idx][eye] = simTime;
	}
	renderVolume(m_fSimRenderTime[idx][eye], idx, eye);
	renderWarp(  m_fSimLastTime[idx][eye],   m_fWarpTime[idx][eye], idx, eye);
}

void CMainApplication::renderViews(float simTime, int idx, bool doPrediction)
{
	for (int i = 0; i < 2; i++)
	{
		renderView(simTime, idx, i);
	}
}

void CMainApplication::renderRef(float simTime)
{
	for (int i = 0; i < 2; i++)
	{
		updateUvwShader(simTime, REFERENCE, i);
		m_pUvw[REFERENCE]->setFrameBufferObject( m_pUvwFBO[REFERENCE][i] );
		m_pUvw[REFERENCE]->render();

		updateRaycastShader(simTime, REFERENCE, i);
		m_pRaycast[REFERENCE][i]->setFrameBufferObject( m_pSimFBO[REFERENCE][i].getFront() );
		m_pRaycast[REFERENCE][i]->render();
	}
}

void CMainApplication::renderVolume(float renderTime, int idx, int eye)
{
	if (m_pChunkedRaycast[idx][eye]->isFinished())
	{
		updateUvwShader(renderTime, idx, eye);
		m_pUvw[idx]->setFrameBufferObject( m_pUvwFBO[idx][eye] );
		m_pUvw[idx]->render();
	}

	updateRaycastShader(renderTime, idx, eye);

	m_pChunkedRaycast[idx][eye]->render();
}

void CMainApplication::renderWarp(float lastTime, float warpTime, int idx, int eye)
{
	updateWarpShader(lastTime, warpTime, idx, eye);
	m_pWarp[idx]->setFrameBufferObject( m_pWarpFBO[idx][eye] );
	m_pWarp[idx]->render();
}

void CMainApplication::renderDiffs(int idx)
{
	for (int i = LEFT; i <= RIGHT; i++)
	{
		renderDiff(idx, i);
	}
}
void CMainApplication::renderDiff(int idx, int eye)
{
	updateDiffShader(
		idx,
		eye,
		m_pSimFBO[REFERENCE][eye].getFront()->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0),
		m_pWarpFBO[idx][eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0)
		);
	m_pDiff[idx]->setFrameBufferObject( m_pDiffFBO[idx][eye] );
	m_pDiff[idx]->render();
	
	OPENGLCONTEXT->bindTextureToUnit( m_pDiffFBO[idx][eye]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2 );
	glGenerateMipmap(GL_TEXTURE_2D);
}

void CMainApplication::renderToScreen(GLuint left, GLuint right)
{
	OPENGLCONTEXT->bindFBO(0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	{
		m_pShowTexShader->updateAndBindTexture("tex", 2, left);
		m_pShowTex->setViewport(0, 0, (int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), (int)std::min( getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f ));
		m_pShowTex->render();
	}
	{
		m_pShowTexShader->updateAndBindTexture("tex", 3, right);
		m_pShowTex->setViewport((int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), 0, (int) std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f), (int)std::min( getResolution(m_pWindow).y, getResolution(m_pWindow).x / 2.0f ));
		m_pShowTex->render();
	}
}

void CMainApplication::profileFPS(float fps)
{
	m_fpsCounter[m_iCurFpsIdx] = fps;
	m_iCurFpsIdx = (m_iCurFpsIdx + 1) % m_fpsCounter.size(); 
}


std::vector<float> CMainApplication::getVanillaTimes()
{
	std::vector<float> result;

	OpenGLTimings timings;
	m_displaySimulation.setFrame(0);
	bool tmpClampTime = m_hmdSimulation.m_bClampTime;
	m_hmdSimulation.m_bClampTime = true;

	// setup
	for (int j = LEFT; j <= RIGHT; j++)
	{
		// reset sim and warp times
		m_fSimLastTime[NONE][j]   = -1.0f;
		m_fSimRenderTime[NONE][j] = -1.0f;
		m_fWarpTime[NONE][j]      = -1.0f;

		// reset ChunkedRenderPasses
		m_pChunkedRaycast[NONE][j]->reset();
		int numSetupFrames = 5;
		for (int k = 0; k < numSetupFrames; k++){ // actually, do it a couple of times to make sure grid render times are available
		//+++++++++++++ DEBUG ++++++++++++++
		printProgress( ((float) (j * 2 + k)/((float) (2 * numSetupFrames))) * 100.0f, "Setup...");
		//++++++++++++++++++++++++++++++++++
			do {
			renderView(0.0f, NONE, j, false);
			glFinish();
		} while (!m_pChunkedRaycast[NONE][j]->isFinished());}

		// reset sim and warp times
		m_fSimLastTime[NONE][j]   = 0.0f;
		m_fSimRenderTime[NONE][j] = 0.0f;
		m_fWarpTime[NONE][j]      = 0.0f;
	}

	int maxNumFrames = (m_hmdSimulation.m_fDuration / m_displaySimulation.m_fRefreshTime) + 1;

	int i = 0;
	result.push_back(m_displaySimulation.m_fTime);
	while(m_displaySimulation.m_fTime <= m_hmdSimulation.m_fDuration && i <= maxNumFrames)
	{
		//+++++++++++++ DEBUG ++++++++++++++
		printProgress((m_displaySimulation.m_fTime/ m_hmdSimulation.m_fDuration) * 100.0f, "Vanilla Times...");
		//++++++++++++++++++++++++++++++++++
		
		// render view and retrieve time
		timings.beginTimer(std::to_string(i));
			renderViews(m_displaySimulation.m_fTime, NONE, true);
		timings.stopTimer(std::to_string(i));
		glFinish();

		// retrieve render time and advance display simulation
		OpenGLTimings::Timer timer = timings.waitForTimerResult(std::to_string(i));
		m_displaySimulation.advanceTime( timer.lastTiming / 1000.0f );
		result.push_back(m_displaySimulation.m_fTime);

		// advance until next 'VSync'
		m_displaySimulation.advanceTime( m_displaySimulation.getTimeToUpdate() );
		i++;
	}

	m_hmdSimulation.m_bClampTime = tmpClampTime;

	return result;
}

static float avg[CMainApplication::NUM_WARPTECHNIQUES * 4];
float CMainApplication::getAvgDssim(int idx, int eye)
{
	int numMipmaps = (int) (std::log( std::max( (float) m_pDiffFBO[idx][eye]->getWidth(), (float) m_pDiffFBO[idx][eye]->getHeight()) ) / std::log( 2.0f ) );

	OPENGLCONTEXT->bindTextureToUnit( m_pDiffFBO[idx][eye]->getBuffer("fragColor"), GL_TEXTURE2 );
	glGetTexImage(GL_TEXTURE_2D, numMipmaps, GL_RGBA, GL_FLOAT, &avg[idx * 4]);
	
	return (avg[idx * 4] + avg[idx * 4 + 1] + avg[idx * 4 + 2] + avg[idx * 4 + 3])/4.0f;
}
