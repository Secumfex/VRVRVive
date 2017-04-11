/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>
#include <algorithm>
#include <ctime>

#include <Core/Timer.h>
#include <Core/DoubleBuffer.h>
#include <Core/FileReader.h>

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

const glm::vec2 FRAMEBUFFER_RESOLUTION = glm::vec2( 800, 800);
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
	DisplaySimulation() :m_fRefreshTime(16.0f) {}
	float m_fRefreshTime;
	float m_fTime;
	int   m_iFrame;

	inline void advanceTime(float delta) { m_fTime += delta; m_iFrame = (int) (m_fTime / m_fRefreshTime); }
	inline void setFrame(int idx){ m_iFrame = idx; m_fTime = ((float) m_iFrame) * m_fRefreshTime; }
	inline float getTimeToUpdate() { return ((float) (m_iFrame + 1)) * m_fRefreshTime - m_fTime; } 
	inline float getTimeOfFrame(int idx) { return ((float) (idx)) * m_fRefreshTime; } 
};

class HmdSimulation
{
public:
	bool m_bAnimateView;
	bool m_bAnimateRotation;
	bool m_bAnimateTranslation;
public: 
	HmdSimulation() : m_bAnimateView(true), m_bAnimateRotation(false), m_bAnimateTranslation(true) {}
	glm::mat4 getView(float time, int eye);
	inline glm::mat4 getPerspective(float time, int eye) {return s_perspective;}
};

glm::mat4 HmdSimulation::getView(float time, int eye)
{
	glm::vec4 warpCenter = s_center;
	glm::vec4 warpEye = s_eye;
	glm::vec3 warpUp = glm::vec3(s_up);

	if (m_bAnimateView)
	{
		if (m_bAnimateRotation)
		{
			warpCenter  = glm::vec4(sin(time * 2.0f)*0.25f, cos( time * 2.0f)*0.125f, 0.0f, 1.0f);
			warpUp = glm::normalize(glm::vec3( sin( time ) * 0.25f, 1.0f, 0.0f));
		}
		if (m_bAnimateTranslation) 
		{
			warpEye = s_eye + glm::vec4(-sin( time * 1.0f)*0.125f, -cos(time * 2.0f) * 0.125f, 0.0f, 1.0f);
		}
	}

	glm::mat4 view   = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), warpUp);
	glm::mat4 view_r = glm::lookAt(glm::vec3(warpEye) + glm::vec3(s_eyeDistance,0.0,0.0), glm::vec3(warpCenter) + glm::vec3(s_eyeDistance,0.0,0.0), warpUp);

	return (eye == LEFT) ? view : view_r; // DEBUG
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
	enum WarpingTechniques{
	NONE,
	QUAD,
	GRID,
	NOVELVIEW,
	NUM_WARPTECHNIQUES // auxiliary
	}; 
	int m_iActiveWarpingTechnique;
	
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
	FrameBufferObject* m_pRefFBO[2]; // Left + Right
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

	int m_iNumShadowSamples;
	glm::vec3 m_shadowDir;
	float m_fShadowAngles[2];

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

	// per frame
	void updateGui();
	void updateWarpShader(float lastTime, float warpTime, int idx, int eye);
	void updateRaycastShader(float simTime, int idx, int eye);
	void updateUvwShader(float simTime, int idx, int eye);
	void renderGui();
	void renderViews(float simTime, int idx);
	void renderVolume(float renderTime, float nextRenderTime, int idx, int eye);
	void renderWarp(float lastTime, float warpTime, int idx, int eye);
	void renderToScreen(GLuint left, GLuint right);
	void profileFPS(float fps);

	// profiling
	std::vector<float> getVanillaTimes();
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

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////////////// RENDERING  ///////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	/**
	///////////////////////     UVW Map Renderpass     ///////////////////////////
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	ShaderProgram uvwShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag"); DEBUGLOG->outdent();
	uvwShaderProgram.update("model", s_model);
	uvwShaderProgram.update("view", s_view);
	uvwShaderProgram.update("projection", s_perspective);

	DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
	FrameBufferObject uvwFBO(TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	FrameBufferObject uvwFBO_novelView(TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	FrameBufferObject::s_internalFormat = GL_RGBA32F; // allow arbitrary values
	uvwFBO.addColorAttachments(2);
	uvwFBO_novelView.addColorAttachments(2);
	FrameBufferObject::s_internalFormat = GL_RGBA; // default
	DEBUGLOG->outdent(); 
	
	RenderPass uvwRenderPass(&uvwShaderProgram, &uvwFBO);
	uvwRenderPass.addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	uvwRenderPass.addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
	uvwRenderPass.addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
	uvwRenderPass.addRenderable(&volume);

	
	///////////////////////   Ray-Casting Renderpass    //////////////////////////
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	ShaderProgram shaderProgram("/raycast/simpleRaycast.vert", "/raycast/synth_raycastLayer_simple.frag", m_shaderDefines); DEBUGLOG->outdent();
	shaderProgram.update("uStepSize", s_rayStepSize);
		
	if ( m_bHasShadow ){
	shaderProgram.update("uShadowRayDirection", glm::normalize(glm::vec3(0.0f,-0.5f,-1.0f))); // full range of values in window
	shaderProgram.update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary		//////////////////////////////////////////////////////////////////////////////
	}

	// DEBUG
	TransferFunctionPresets::generateTransferFunction();
	TransferFunctionPresets::updateTransferFunctionTex();

	DEBUGLOG->log("FrameBufferObject Creation: synth ray casting layers"); DEBUGLOG->indent();
	FrameBufferObject::s_internalFormat = GL_RGBA32F; // allow arbitrary values
	FrameBufferObject synth_raycastLayerFBO(shaderProgram.getOutputInfoMap(), TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	FrameBufferObject::s_internalFormat = GL_RGBA; // default
	DEBUGLOG->outdent(); 

	// bind volume texture, back uvw textures, front uvws
	OPENGLCONTEXT->bindTextureToUnit(volumeTextureCT, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE1, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE3, GL_TEXTURE_1D);
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	shaderProgram.update("volume_texture", 0); // volume texture
	shaderProgram.update("back_uvw_map",  1);
	shaderProgram.update("front_uvw_map", 2);
	shaderProgram.update("transferFunctionTex", 3);

	// ray casting render pass
	RenderPass renderPass(&shaderProgram, &synth_raycastLayerFBO);
	renderPass.setClearColor(0.0f,0.0f,0.0f,1.0f);
	renderPass.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	renderPass.addRenderable(&quad);
	renderPass.addEnable(GL_DEPTH_TEST);
	renderPass.addDisable(GL_BLEND);
	
	///////////////////////   Show Texture Renderpass    //////////////////////////
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", s_shaderDefines);
	RenderPass showTex(&showTexShader,0);
	showTex.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	showTex.addRenderable(&quad);
	showTex.setViewport(0,0,TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	showTex.addDisable(GL_BLEND);

	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT2), GL_TEXTURE5, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT3), GL_TEXTURE6, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT4), GL_TEXTURE7, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT5), GL_TEXTURE8, GL_TEXTURE_2D); // debugging output
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getDepthTextureHandle(),								  GL_TEXTURE9, GL_TEXTURE_2D); //depth 0
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D); //depth 1-4
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	showTexShader.update("tex", 4);

	///////////////////////   novel view synthesis Renderpass    //////////////////////////
	//Grid grid(400,400,0.0025f,0.0025f, false);

	ShaderProgram novelViewShader("/screenSpace/fullscreen.vert", "/raycast/synth_novelView_simple.frag", s_shaderDefines);
	FrameBufferObject FBO_novelView(novelViewShader.getOutputInfoMap(), TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	RenderPass novelView(&novelViewShader, 0);
	novelView.addRenderable(&quad);
	novelView.addEnable(GL_DEPTH_TEST);
	novelView.setViewport(TEXTURE_RESOLUTION.x,0,TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE12, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE13, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(FBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE11, GL_TEXTURE_2D); //depth 1-4
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	novelViewShader.update("layer0",4);
	novelViewShader.update("layer1",5);
	novelViewShader.update("layer2",6);
	novelViewShader.update("layer3",7);
	novelViewShader.update("depth", 10);
	
	novelViewShader.update("back_uvw_map",  12);
	novelViewShader.update("front_uvw_map", 13);
	
	novelViewShader.update("uThreshold", 32);

	//////////////////////////////////////////////////////////////////////////////
	///////////////////////    GUI / USER INPUT   ////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// Setup ImGui binding
	ImGui_ImplSdlGL3_Init(window);

	Turntable turntable;
	double old_x;
    double old_y;
	
	auto sdlEventHandler = [&](SDL_Event *event)
	{
		bool imguiHandlesEvent = ImGui_ImplSdlGL3_ProcessEvent(event);

		switch (event->type)
		{
		case SDL_KEYDOWN:
		{
			int k = event->key.keysym.sym;
			switch (k)
			{
			case SDLK_w:
				s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(0.0f, 0.0f, -0.1f, 0.0f))) * s_translation;
				break;
			case SDLK_a:
				s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(-0.1f, 0.0f, 0.0f, 0.0f))) * s_translation;
				break;
			case SDLK_s:
				s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(0.0f, 0.0f, 0.1f, 0.0f))) * s_translation;
				break;
			case SDLK_d:
				s_translation = glm::translate(glm::vec3(glm::inverse(s_view) * glm::vec4(0.1f, 0.0f, 0.0f, 0.0f))) * s_translation;
				break;
			}
			break;
		}
		case SDL_MOUSEMOTION:
		{
			ImGuiIO& io = ImGui::GetIO();
			if (io.WantCaptureMouse)
			{
				break;
			} // ImGUI is handling this

			float d_x = event->motion.x - old_x;
			float d_y = event->motion.y - old_y;

			if (turntable.getDragActive())
			{
				turntable.dragBy(d_x, d_y, s_view);
			}

			old_x = (float)event->motion.x;
			old_y = (float)event->motion.y;
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				turntable.setDragActive(true);
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

	std::string window_header = "Novel View Synthesis";
	SDL_SetWindowTitle(window, window_header.c_str() );

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	float elapsedTime = 0.0;
	while (!shouldClose(window))
	{
		////////////////////////////////    EVENTS    ////////////////////////////////
		pollSDLEvents(window, sdlEventHandler);
		profileFPS((float) (ImGui::GetIO().Framerate));

		////////////////////////////////     GUI      ////////////////////////////////
	
		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
		// update view related uniforms
		uvwShaderProgram.update("model", s_translation * turntable.getRotationMatrix() * s_rotation * s_scale);

		// ray start/end parameters
		shaderProgram.update("uStepSize", s_rayStepSize); 	  // ray step size

		// color mapping parameters
		shaderProgram.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		shaderProgram.update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

		shaderProgram.update("uProjection", s_perspective);
		shaderProgram.update("uScreenToView", s_screenToView );

		novelViewShader.update("uProjection", s_perspective); // used for depth to distance computation
		
		// used for reprojection
		novelViewShader.update("uViewOld", s_view); // used for depth to distance computation
		novelViewShader.update("uViewNovel", s_view_r); // used for depth to distance computation
		//////////////////////////////////////////////////////////////////////////////
		
		////////////////////////////////  RENDERING //// ///////////////////////////// 
		glDisable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
		// render left image
		uvwShaderProgram.update("view", s_view);
		uvwRenderPass.setFrameBufferObject(&uvwFBO);
		uvwRenderPass.render();		
		renderPass.setViewport(0, 0, getResolution(window).x / 2, getResolution(window).y);
		renderPass.render();
		
		showTex.render();

		uvwShaderProgram.update("view", s_view_r);
		uvwRenderPass.setFrameBufferObject(&uvwFBO_novelView);
		uvwRenderPass.render();

		novelView.render();
		
		glActiveTexture(GL_TEXTURE21);
		ImGui::Render();
		SDL_GL_SwapWindow(window); // swap buffers
		//////////////////////////////////////////////////////////////////////////////
	}

	destroyWindow(window);
	
**/

	return 0;
}

CMainApplication::CMainApplication(int argc, char *argv[])
	: m_issetDefineStr(SHADER_DEFINES, std::end(SHADER_DEFINES)) // all possible defines
	, m_issetDefine(m_issetDefineStr.size(), (int) false) // define states
	, m_shaderDefines(0) // currently set defines
	, m_fpsCounter(120)
	, m_iCurFpsIdx(0)
	, m_iActiveModel(0)
	, m_bHasLod(false)
	, m_bHasShadow(false)
	, m_fOldX(0.0f)
	, m_fOldY(0.0f)
	, m_iNumNovelViewSamples(12)
	, m_iActiveWarpingTechnique(0)
	, m_iNumShadowSamples(12)
	, m_shadowDir( glm::normalize( glm::vec3( std::cos( -glm::half_pi<float>() ) * std::cos( -glm::quarter_pi<float>() ), std::sin( -glm::half_pi<float>() ) * std::cos( -glm::quarter_pi<float>() ), std::tan( -glm::quarter_pi<float>() ) ) ) )
{
	DEBUGLOG->setAutoPrint(true);
	m_fShadowAngles[0] = -0.5f;
	m_fShadowAngles[1] = -0.5f;

	std::string fullExecutableName( argv[0] );
	m_executableName = fullExecutableName.substr( fullExecutableName.rfind("\\") + 1);
	m_executableName = m_executableName.substr(0, m_executableName.find(".exe"));
	DEBUGLOG->log("Executable name: " + m_executableName);
		
	// create m_pWindow and opengl context
	m_pWindow = generateWindow_SDL(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	printOpenGLInfo();
	printSDLRenderDriverInfo();
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

			s_model = s_translation * m_turntable.getRotationMatrix() * s_rotation * s_scale * m_volumeScale;

			renderViews(ImGui::GetTime(), m_iActiveWarpingTechnique);

			renderToScreen(
				m_pWarpFBO[m_iActiveWarpingTechnique][LEFT]->getBuffer("fragColor"), 
				m_pWarpFBO[m_iActiveWarpingTechnique][RIGHT]->getBuffer("fragColor")
				);


			renderGui();

			SDL_GL_SwapWindow( m_pWindow ); // swap buffers
		}

		if(mode == PROFILE)
		{
			//std::vector<float> vanillaTimes = getVanillaTimes();
			//for (int i = 0; i< numFrames; i++)
			//{
			//	ref = renderRefForFrame( i );
			//	vanilla = renderLastFrameBeforeFrame( i );
			//	warp1 = renderIterationWarp1();
			//	warp2 = renderIterationWarp2();
			//	warp3 = renderIterationWarp3();
			//	diffVanilla = diff( ref, vanilla );
			//	diffWarp1 = diff( ref, warp1 );
			//	diffWarp2 = diff( ref, warp2 );
			//	diffWarp3 = diff( ref, warp3 );
			//	addCsvRow( i, diffVanilla.DSSIMavg, diffWarp1.DSSIMavg, diffWarp2.DSSIMavg, diffWarp3.DSSIMavg);
			//}
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

	m_pRaycastShader[NONE] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[QUAD] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[GRID] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/unified_raycast.frag", m_shaderDefines);
	m_pRaycastShader[NOVELVIEW] = new ShaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/synth_raycastLayer_simple.frag", m_shaderDefines);

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
		
		m_pDiffFBO[i][j] = new FrameBufferObject(m_pDiffShader[i]->getOutputInfoMap(), (int) FRAMEBUFFER_RESOLUTION.x, (int) FRAMEBUFFER_RESOLUTION.y);
	}}
	DEBUGLOG->outdent();
		
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
			float targetRenderTime = m_displaySimulation.m_fRefreshTime/2.0f;
			if (i == NONE) {
				targetRenderTime = 999.0f;
				chunkSize = glm::ivec2(FRAMEBUFFER_RESOLUTION);
			}
		
			m_pChunkedRaycast[i][j]  = new ChunkedAdaptiveRenderPass(m_pRaycast[i][j], viewportSize, chunkSize, 8, targetRenderTime, 1.0f);
			m_pChunkedRaycast[i][j]->setPrintDebug(false);
		}

		m_pDiff[i] = new RenderPass(m_pDiffShader[i], m_pDiffFBO[i][LEFT]);
		OPENGLCONTEXT->bindTexture( m_pDiffFBO[i][LEFT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // or else texturelod doesn't work
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		OPENGLCONTEXT->bindTexture( m_pDiffFBO[i][RIGHT]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // or else texturelod doesn't work
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	
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
		m_pChunkedRaycast[NONE][i]->setTargetRenderTime( 30.0f );
		m_pChunkedRaycast[NONE][i]->setRenderTimeBias( 0.001f );
	}
	m_pShowTex = new RenderPass( m_pShowTexShader, 0);
	m_pShowTex->addRenderable(m_pQuad);
	m_pShowTex->addDisable(GL_DEPTH_TEST);
	m_pShowTex->addEnable(GL_BLEND);

	DEBUGLOG->outdent();
}

void CMainApplication::initSceneVariables()
{
	/////////////////////     Scene / View Settings     //////////////////////////
	s_volumeSize = glm::vec3(1.0f);
	updateModelToTexture();

	s_translation = glm::translate(glm::vec3(0.0f,0.0f,-3.0f));
	s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
	s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));

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
	case NONE:
		m_pWarpShader[NONE]->updateAndBindTexture( "tex", 2, m_pSimFBO[idx][eye].getFront()->getBuffer("fragColor") ); // last result
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
		m_pRaycastShader[NOVELVIEW]->update( "uScreenToView", s_screenToView );
	default:
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
		m_pRaycastShader[idx]->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(m_hmdSimulation.getView(simTime, eye)) * s_screenToView );
		m_pRaycastShader[idx]->update("uProjection", m_hmdSimulation.getPerspective(simTime, eye) );
		if (idx!=NOVELVIEW)
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

void CMainApplication::renderViews(float simTime, int idx)
{
	for (int i = 0; i < 2; i++)
	{
		if (m_pChunkedRaycast[idx][i]->isFinished())
		{
			//m_frame.Timings.getBack().timestamp("Swap Time" + STR_SUFFIX[LEFT]);
			m_pSimFBO[idx][i].swap();
			m_pChunkedRaycast[idx][i]->getRenderPass()->setFrameBufferObject( m_pSimFBO[idx][i].getBack() );

			m_fSimLastTime[idx][i] = m_fSimRenderTime[idx][i];
			m_fSimRenderTime[idx][i] = simTime;
		}

		m_fWarpTime[idx][i] = simTime;

		renderVolume(m_fSimRenderTime[idx][i], m_fWarpTime[idx][i], idx, i);
		renderWarp(  m_fSimLastTime[idx][i],   m_fWarpTime[idx][i], idx, i);
	}
}

void CMainApplication::renderVolume(float renderTime, float simTime, int idx, int eye)
{
	if (m_pChunkedRaycast[idx][eye]->isFinished())
	{
		updateUvwShader(simTime, idx, eye);
		m_pUvw[idx]->setFrameBufferObject( m_pUvwFBO[idx][eye] );
		m_pUvw[idx]->render();
	}

	updateRaycastShader(simTime, idx, eye);

	m_pChunkedRaycast[idx][eye]->render();
}

void CMainApplication::renderWarp(float lastTime, float warpTime, int idx, int eye)
{
	updateWarpShader(lastTime, warpTime, idx, eye);
	m_pWarp[idx]->setFrameBufferObject( m_pWarpFBO[idx][eye] );
	m_pWarp[idx]->render();
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
