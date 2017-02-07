/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Core/Timer.h>
#include <Core/DoubleBuffer.h>
#include <Core/CSVWriter.h>
#include <Core/FileReader.h>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>
#include <Importing/TextureTools.h>

#include "UI/imgui/imgui.h"
#include <UI/imgui_impl_sdl_gl3.h>
#include <UI/imguiTools.h>
#include <UI/Turntable.h>
#include <UI/Profiler.h>

#include <Volume/TransferFunction.h>
#include <Volume/SyntheticVolume.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <ctime>

#include <Misc/TransferFunctionPresets.h>>
#include <Misc/Parameters.h>

////////////////////// PARAMETERS /////////////////////////////
static const char* s_models[] = {"CT Head", "MRT Brain", "Homogeneous", "Radial Gradient", "MRT Brain Stanford"};

const char* SHADER_DEFINES[] = {
	"FIRST_HIT",
};

const int LEFT = 0;
const int RIGHT = 1;

using namespace ViewParameters;
using namespace VolumeParameters;
using namespace RaycastingParameters;

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
class CMainApplication 
{
private: 
	// SDL bookkeeping
	SDL_Window	 *m_pWindow;
	SDL_GLContext m_pContext;

	// Render objects bookkeeping
	VolumeData<float> m_volumeData[5];
	GLuint m_volumeTexture[5];
	Quad*	m_pQuad;
	Grid*	m_pGrid;
	VertexGrid* m_pVertexGrid;
	VolumeSubdiv* m_pVolume;

	// Shader bookkeeping
	ShaderProgram* m_pUvwShader;
	ShaderProgram* m_pRaycastShader;
	ShaderProgram* m_pRaycastStereoShader;
	ShaderProgram* m_pShowTexShader;
	ShaderProgram* m_pShowLayerShader;
	ShaderProgram* m_pComposeTexArrayShader;

	// m_pRaycastFBO bookkeeping
	FrameBufferObject* m_pUvwFBO;
	FrameBufferObject* m_pUvwFBO_r;
	FrameBufferObject* m_pFBO;
	FrameBufferObject* m_pFBO_r;
	FrameBufferObject* m_pFBO_single;
	FrameBufferObject* m_pFBO_single_r;

	GLuint m_stereoOutputTextureArray;

	// Renderpass bookkeeping
	RenderPass* m_pUvw; 		
	RenderPass* m_pSimpleRaycast;
	RenderPass* m_pStereoRaycast;
	RenderPass* m_pShowTex;
	RenderPass* m_pShowLayer;
	RenderPass* m_pComposeTexArray;

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> m_sdlEventFunc;

	// Frame profiling
	struct Frame{
		Profiler FrameProfiler;
		SimpleDoubleBuffer<OpenGLTimings> Timings;
	} m_frame;

	//========== MISC ================
	std::vector<std::string> m_shaderDefines;

	Turntable m_turntable;

	int m_iActiveModel;

	//int m_iVertexGridWidth;
	//int m_iVertexGridHeight;

	float m_fOldX;
	float m_fOldY;

	std::vector<float> m_fpsCounter;
	int m_iCurFpsIdx;

	float m_fElapsedTime;
	float m_fDeltaTime;

	struct MatrixSet
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 perspective;
	} matrices[2]; // left, right, firsthit, current

	bool m_bShowSingleLayer;
	
	float m_fDisplayedLayer;

	glm::mat4 m_textureToProjection_r;

	std::vector<float> m_texData;
	GLuint m_pboHandle; // PBO 
	GLuint m_atomicsBuffer;

	CSVWriter m_csvWriter;
	int  m_iCsvCounter;
	int  m_iCsvNumFramesToProfile;
	bool m_bCsvDoRun;

	int m_iNumLayers;
    glm::vec2 m_textureResolution;

	std::string m_executableName;

public:

	void profileFPS(float fps);
	void loop();

	void renderViews();
	void renderGui();
	void renderToScreen();
	void renderUVWs(int eye, MatrixSet& matrixSet );
	void updateCommonUniforms();
	void updateMatrices();
	void updateGui();
	void pollEvents();
	void initAtomicsBuffer();
	void initEventHandlers();
	void initGUI();
	void initRenderPasses();
	void initTextureUniforms();
	void initTextureUnits();
	void initFramebuffers();
	void initUniforms();
	void loadShaders();
	void loadGeometries();
	void initSceneVariables();
	void loadVolumes();

	CMainApplication( int argc, char *argv[] );
	virtual ~CMainApplication();

	void clearOutputTexture(GLuint texture);
	void loadShaderDefines();
	void handleCsvProfiling();

	void recompileShaders();
	void rebuildFramebuffers(); 
	void loadRaycastingShaders();
	void initRaycastingFramebuffers();
};

//////////////////////////////////////////////////////////////////////////////
///////////////////////// IMPLEMENTATION  ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

CMainApplication::CMainApplication(int argc, char *argv[])
	: m_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES))
	, m_bShowSingleLayer(false)
	, m_fElapsedTime(0.0f)
	, m_fDisplayedLayer(0.0f)
	, m_fpsCounter(120)
	, m_iCurFpsIdx(0)
	, m_iNumLayers(32)
	, m_textureResolution(800, 800)
	, m_pboHandle(-1)
	, m_iCsvCounter(0)
	, m_iCsvNumFramesToProfile(150)
	, m_bCsvDoRun(false)
{
	m_texData.resize((int) m_textureResolution.x * (int) m_textureResolution.y * 4, 0.0f);

	std::string fullExecutableName( argv[0] );
	m_executableName = fullExecutableName.substr( fullExecutableName.rfind("\\") + 1);
	m_executableName = m_executableName.substr(0, m_executableName.find(".exe"));
	DEBUGLOG->log("Executable name: " + m_executableName);

	// create m_pWindow and opengl context
	m_pWindow = generateWindow_SDL(m_textureResolution.x, m_textureResolution.y, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	printOpenGLInfo();
	printSDLRenderDriverInfo();
}

void CMainApplication::clearOutputTexture(GLuint texture)
{
	if(m_pboHandle == -1)
	{
		glGenBuffers(1,&m_pboHandle);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboHandle);
		glBufferStorage(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) m_textureResolution.x * (int) m_textureResolution.y * m_iNumLayers * 4, NULL, GL_DYNAMIC_STORAGE_BIT);
		checkGLError(true);
		for (int i = 0; i < m_iNumLayers; i++)
		{
			glBufferSubData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) m_textureResolution.x * (int) m_textureResolution.y * i * 4, m_texData.size(), &m_texData[0]);
		}
		checkGLError(true);
		//glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) m_textureResolution.x * (int) m_textureResolution.y * m_iNumLayers * 4, &m_texData[0], GL_STATIC_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	
	OPENGLCONTEXT->activeTexture(GL_TEXTURE6);
	OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D_ARRAY);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboHandle);
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, (int) m_textureResolution.x, (int) m_textureResolution.y, m_iNumLayers, GL_RGBA, GL_FLOAT, 0); // last param 0 => will read from pbo
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void updateTransferFunctionTex()
{
	TransferFunctionPresets::s_transferFunction.updateTex();
}

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

void CMainApplication::profileFPS(float fps)
{
	m_fpsCounter[m_iCurFpsIdx] = fps;
	m_iCurFpsIdx = (m_iCurFpsIdx + 1) % m_fpsCounter.size(); 
}

void CMainApplication::loadShaderDefines()
{
	m_shaderDefines.clear();
	m_shaderDefines.insert(m_shaderDefines.end(), SHADER_DEFINES, std::end(SHADER_DEFINES));

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

	DEBUGLOG->outdent();

}

void CMainApplication::loadVolumes()
{
	// load data set: CT of a Head	// load into 3d texture
	std::string file = RESOURCES_PATH;
		
	m_volumeData[0] = Importer::load3DData<float>(file + "/volumes/CTHead/CThead", 256, 256, 113, 2);
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

	m_volumeData[4] = Importer::load3DData<float>(file + "/volumes/MRbrain/MRbrain", 256,256, 109, 2);
	m_volumeTexture[4] =  loadTo3DTexture<float>(m_volumeData[4], 3, GL_R16F, GL_RED, GL_FLOAT);
	m_volumeData[4].data.clear(); // set free	

	activateVolume<float>(m_volumeData[0]);
	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, TransferFunctionPresets::CT_Head);
	checkGLError(true);
}

	/////////////////////     Scene / View Settings     //////////////////////////
void CMainApplication::initSceneVariables()
{
	s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -2.0f));
	s_scale = glm::scale(glm::vec3(1.0f, 1.0f, 1.0f));
	updateModel();

	s_eye = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	s_center = s_translation[3];
	s_aspect = m_textureResolution.x / m_textureResolution.y;
	s_fovY = 45.0f;

	m_textureToProjection_r = s_perspective * s_view_r;

	updateNearHeightWidth();
	updateView();
	updatePerspective();
	updateScreenToViewMatrix();

	s_modelToTexture = glm::mat4( // swap components
		glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), // column 1
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), // column 2
		glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),//column 3
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) //column 4 
		* glm::inverse(glm::scale(s_volumeSize)) // moves origin to front left
		* glm::translate(glm::vec3(s_volumeSize.x * 0.5f, s_volumeSize.y * 0.5f, -s_volumeSize.z * 0.5f));
}

void CMainApplication::loadGeometries(){
	DEBUGLOG->log("Geometry Creation"); DEBUGLOG->indent();
	m_pVolume = new VolumeSubdiv(0.5f * s_volumeSize.x, 0.5f * s_volumeSize.y, 0.5f * s_volumeSize.z , 6);
	m_pVertexGrid = new VertexGrid(m_textureResolution.x, m_textureResolution.y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(-1));
	m_pQuad = new Quad();;
	m_pGrid = new Grid(100, 100, 0.1f, 0.1f);
	DEBUGLOG->outdent();
}

void CMainApplication::loadRaycastingShaders()
{
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	m_pRaycastShader = new ShaderProgram("/raycast/simpleRaycast.vert", "/raycast/unified_raycast.frag", m_shaderDefines); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: ray casting shader - single pass stereo"); DEBUGLOG->indent();
	std::vector<std::string> shaderDefinesStereo(m_shaderDefines);
	shaderDefinesStereo.push_back("STEREO_SINGLE_PASS");
	m_pRaycastStereoShader = new ShaderProgram("/raycast/simpleRaycast.vert", "/raycast/unified_raycast.frag", shaderDefinesStereo); DEBUGLOG->outdent();
}


void CMainApplication::loadShaders()
{
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	m_pUvwShader = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag"); DEBUGLOG->outdent();

	loadRaycastingShaders();

	DEBUGLOG->log("Shader Compilation: compose tex array shader"); DEBUGLOG->indent();
	m_pComposeTexArrayShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/composeTextureArray.frag", m_shaderDefines); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: show layer shader"); DEBUGLOG->indent();
	m_pShowLayerShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", std::vector<std::string>(1,"ARRAY_TEXTURE"));DEBUGLOG->outdent();
	
	DEBUGLOG->log("Shader Compilation: show texture shader"); DEBUGLOG->indent();
	m_pShowTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", m_shaderDefines);DEBUGLOG->outdent();

}

void CMainApplication::initRaycastingFramebuffers()
{
	DEBUGLOG->log("FrameBufferObject Creation: raycasting results"); DEBUGLOG->indent();
	m_pFBO = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	m_pFBO_r = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	m_pFBO_single = new FrameBufferObject(m_pRaycastStereoShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	DEBUGLOG->outdent();
}


void CMainApplication::initFramebuffers()
{
	DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
	FrameBufferObject::s_internalFormat = GL_RGBA16F;
	m_pUvwFBO = new FrameBufferObject(m_textureResolution.x, m_textureResolution.y);
	m_pUvwFBO_r = new FrameBufferObject(m_textureResolution.x, m_textureResolution.y);
	m_pUvwFBO->addColorAttachments(2); // front UVRs and back UVRs
	m_pUvwFBO_r->addColorAttachments(2); // front UVRs and back UVRs
	FrameBufferObject::s_internalFormat = GL_RGBA;
	DEBUGLOG->outdent();

	initRaycastingFramebuffers();
	
	DEBUGLOG->log("FrameBufferObject Creation: single-pass stereo compositing result"); DEBUGLOG->indent();
	m_pFBO_single_r = new FrameBufferObject(m_pComposeTexArrayShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	DEBUGLOG->outdent();

	DEBUGLOG->log("FrameBufferObject Creation: single-pass stereo output texture array"); DEBUGLOG->indent();
	m_stereoOutputTextureArray = createTextureArray((int)m_textureResolution.x, (int)m_textureResolution.y, m_iNumLayers, GL_RGBA16F);
	DEBUGLOG->outdent();
}


void CMainApplication::initRenderPasses()
{
	DEBUGLOG->log("RenderPass Creation: uvw coords"); DEBUGLOG->indent();
	m_pUvw = new RenderPass(m_pUvwShader, m_pUvwFBO);
	m_pUvw->addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	m_pUvw->addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
	m_pUvw->addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
	m_pUvw->addRenderable(m_pVolume);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: simple raycast"); DEBUGLOG->indent();
	m_pSimpleRaycast = new RenderPass(m_pRaycastShader, m_pFBO);
	m_pSimpleRaycast->addRenderable(m_pQuad);
	m_pSimpleRaycast->addDisable(GL_BLEND);
	m_pSimpleRaycast->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pSimpleRaycast->addEnable(GL_DEPTH_TEST);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: stereo raycast"); DEBUGLOG->indent();
	m_pStereoRaycast = new RenderPass(m_pRaycastStereoShader, m_pFBO_single);
	m_pStereoRaycast->addRenderable(m_pGrid);
	m_pStereoRaycast->addDisable(GL_BLEND);
	m_pStereoRaycast->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pStereoRaycast->addEnable(GL_DEPTH_TEST);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: compose texture array"); DEBUGLOG->indent();
	m_pComposeTexArray = new RenderPass(m_pComposeTexArrayShader, m_pFBO_single_r);
	m_pComposeTexArray->addRenderable(m_pGrid);
	m_pComposeTexArray->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	DEBUGLOG->outdent();
	
	DEBUGLOG->log("RenderPass Creation: show layer"); DEBUGLOG->indent();
	m_pShowLayer = new RenderPass(m_pShowLayerShader, 0);
	m_pShowLayer->setViewport(getResolution(m_pWindow).x / 2, getResolution(m_pWindow).y / 2, getResolution(m_pWindow).x / 2, getResolution(m_pWindow).y / 2);
	m_pShowLayer->addRenderable(m_pQuad);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: show texture"); DEBUGLOG->indent();
	m_pShowTex = new RenderPass(m_pShowTexShader, 0);
	m_pShowTex->addRenderable(m_pQuad);
	DEBUGLOG->outdent();
}


void CMainApplication::initTextureUnits()
{
	OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture[0], GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D);

	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

	OPENGLCONTEXT->bindImageTextureToUnit(m_stereoOutputTextureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
	OPENGLCONTEXT->bindTextureToUnit(m_stereoOutputTextureArray, GL_TEXTURE6, GL_TEXTURE_2D_ARRAY); // for display
	
	OPENGLCONTEXT->bindTextureToUnit(m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D);
}

void CMainApplication::initTextureUniforms()
{
	m_pRaycastShader->update("volume_texture", 0);
	m_pRaycastShader->update("transferFunctionTex", 1);
	m_pRaycastShader->update("back_uvw_map", 2);
	m_pRaycastShader->update("front_uvw_map", 4);

	m_pRaycastStereoShader->update("volume_texture", 0);
	m_pRaycastStereoShader->update("transferFunctionTex", 1);
	m_pRaycastStereoShader->update("back_uvw_map", 2);
	m_pRaycastStereoShader->update("front_uvw_map", 4);

	m_pComposeTexArrayShader->update( "tex", 6);

	m_pShowLayerShader->update("tex", 6);
}

void CMainApplication::initUniforms()
{
	m_pUvwShader->update("model", s_translation * s_rotation * s_scale);
	m_pUvwShader->update("view", s_view);
	m_pUvwShader->update("projection", s_perspective);

	m_pRaycastShader->update("uStepSize", s_rayStepSize);
	m_pRaycastStereoShader->update("uStepSize", s_rayStepSize);
}

void CMainApplication::initGUI()
{
	// Setup ImGui binding
	ImGui_ImplSdlGL3_Init(m_pWindow);
}

void CMainApplication::initAtomicsBuffer()
{
	m_atomicsBuffer = bufferData<GLuint>(std::vector<GLuint>(3,0), GL_ATOMIC_COUNTER_BUFFER, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicsBuffer); // important
}

void CMainApplication::initEventHandlers()
{
	//////////////////////////////////////////////////////////////////////////////
	///////////////////////    GUI / USER INPUT   ////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	m_sdlEventFunc = [&](SDL_Event *event)
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
			case SDLK_r:
				static int activeRenderable = 2;
				m_pSimpleRaycast->clearRenderables();
				activeRenderable = (activeRenderable + 1) % 3;
				switch (activeRenderable)
				{
				case 0: // Quad
					m_pSimpleRaycast->addRenderable(m_pQuad);
					DEBUGLOG->log("renderable: Quad");
					break;
				case 1: // Vertex Grid
					m_pSimpleRaycast->addRenderable(m_pVertexGrid);
					DEBUGLOG->log("renderable: Vertex Grid");
					break;
				case 2: // m_pGrid
					m_pSimpleRaycast->addRenderable(m_pGrid);
					DEBUGLOG->log("renderable: Grid");
					break;
				}
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

			float d_x = event->motion.x - m_fOldX;
			float d_y = event->motion.y - m_fOldY;

			if (m_turntable.getDragActive())
			{
				m_turntable.dragBy(d_x, d_y, s_view);
			}

			m_fOldX = (float)event->motion.x;
			m_fOldY = (float)event->motion.y;
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				m_turntable.setDragActive(true);
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

////////////////////////////////    EVENTS    ////////////////////////////////
void CMainApplication::pollEvents()
{
	pollSDLEvents(m_pWindow, m_sdlEventFunc);
}

////////////////////////////////     GUI      ////////////////////////////////
void CMainApplication::updateGui()
{
    ImGui_ImplSdlGL3_NewFrame(m_pWindow); // tell ImGui a new frame is being rendered
	ImGuiIO& io = ImGui::GetIO();
		
	profileFPS((float) (ImGui::GetIO().Framerate));
	
	ImGui::Value("FPS", (float) (io.Framerate));
	m_fElapsedTime += io.DeltaTime;
	m_fDeltaTime = io.DeltaTime;

	ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
	ImGui::PlotLines("FPS", &m_fpsCounter[0], m_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
	ImGui::PopStyleColor();

	ImGui::DragFloat("eye distance", &s_eyeDistance, 0.01f, 0.0f, 2.0f);
	
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
		updateTransferFunctionTex();
	}
    ImGui::Columns(1);
    ImGui::Separator();

	ImGui::PushItemWidth(-100);
	if (ImGui::CollapsingHeader("Volume Rendering Settings"))
    {
        ImGui::Text("Parameters related to volume rendering");
        ImGui::DragFloatRange2("windowing range", &s_windowingMinValue, &s_windowingMaxValue, 5.0f, (float) s_minValue, (float) s_maxValue); // grayscale ramp boundaries
        ImGui::SliderFloat("ray step size",   &s_rayStepSize,  0.0001f, 0.1f, "%.5f", 2.0f);
    }
	ImGui::PopItemWidth();

	ImGui::DragFloat("Lod Max Level", &s_lodMaxLevel, 0.1f, 0.0f, 8.0f);
	ImGui::DragFloat("Lod Begin", &s_lodBegin, 0.01f, 0.0f, s_far);
	ImGui::DragFloat("Lod Range", &s_lodRange, 0.01f, 0.0f, std::max(0.1f, s_far - s_lodBegin));

	ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume

	static bool s_writeStereo = true;
	ImGui::Checkbox("write stereo", &s_writeStereo); // enable/disable single pass stereo
	
	ImGui::Checkbox("Show Single Layer", &m_bShowSingleLayer);
	ImGui::SliderFloat("Displayed Layer", &m_fDisplayedLayer, 0.0f, m_iNumLayers-1);

	
	if (ImGui::Button("Recompile Shaders"))
	{
		recompileShaders();
	}

	if (ImGui::SliderFloat("Texture Size", &m_textureResolution.x, 256.0f, 2160.0f, "%.f", 2.0f)){ m_textureResolution.y = m_textureResolution.x; }
	ImGui::SliderInt("Num Layers", &m_iNumLayers, 1, 128);
	if (ImGui::Button("Rebuild Framebuffers"))
	{
		rebuildFramebuffers();
	}

	if (ImGui::Button("Save Image"))
	{
		TextureTools::saveTexture("test.png", m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	}

	/////////// PROFILING /////////////////////
	static bool frame_profiler_visible = false;
	static bool pause_frame_profiler = false;

	ImGui::Checkbox("Perf Profiler", &frame_profiler_visible);
	ImGui::Checkbox("Pause Frame Profiler", &pause_frame_profiler);
	m_frame.Timings.getFront().setEnabled(!pause_frame_profiler);
	m_frame.Timings.getBack().setEnabled(!pause_frame_profiler);
	m_frame.Timings.getFront().updateReadyTimings();
	m_frame.Timings.getBack().updateReadyTimings();

	double frame_begin = 0.0;
	double frame_end = 17.0;
	if (m_frame.Timings.getFront().m_timestamps.find("Frame Begin") != m_frame.Timings.getFront().m_timestamps.end())
	{
		frame_begin = m_frame.Timings.getFront().m_timestamps.at("Frame Begin").lastTime;
	}
	if (m_frame.Timings.getFront().m_timestamps.find("Frame End") != m_frame.Timings.getFront().m_timestamps.end())
	{
		frame_end = m_frame.Timings.getFront().m_timestamps.at("Frame End").lastTime;
	}

	if (frame_profiler_visible)
	{
		for (auto e : m_frame.Timings.getFront().m_timers)
		{
			m_frame.FrameProfiler.setRangeByTag(e.first, e.second.lastTime, e.second.lastTime + e.second.lastTiming);
		}
		for (auto e : m_frame.Timings.getFront().m_timersElapsed)
		{
			m_frame.FrameProfiler.setRangeByTag(e.first, e.second.lastTime, e.second.lastTime + e.second.lastTiming);
		}
		for (auto e : m_frame.Timings.getFront().m_timestamps)
		{
			m_frame.FrameProfiler.setMarkerByTag(e.first, e.second.lastTime);
		}

		m_frame.FrameProfiler.imguiInterface(frame_begin, frame_end, &frame_profiler_visible);
	}

		
	//DEBUG Output to profiler file
	if (!pause_frame_profiler) m_frame.Timings.swap();
	m_frame.Timings.getBack().timestamp("Frame Begin");

    //////////////////////////////////////////////////////////////////////////////
}

void CMainApplication::handleCsvProfiling()
{
	ImGui::Separator();
	if (ImGui::Button("Run CSV Profiling") && !m_bCsvDoRun)
	{
		m_bCsvDoRun = true;
	}
	ImGui::SameLine(); ImGui::Value("Running", m_bCsvDoRun);
	ImGui::SameLine(); ImGui::Value("Frame",   m_iCsvCounter);
	ImGui::SliderInt("Num Frames To Profile", &m_iCsvNumFramesToProfile, 1, 1000);
	ImGui::Separator();

	if ( m_bCsvDoRun && m_iCsvCounter == 0) // just not frame one, okay?
	{
		std::vector<std::string> headers;
		for (auto e : m_frame.Timings.getFront().m_timersElapsed)
		{
			headers.push_back(e.first);
		}

		// additional headers
		headers.push_back("Total Stereo");
		headers.push_back("Total Single");

		m_csvWriter.setHeaders(headers);
	}

	if ( m_bCsvDoRun &&  m_iCsvCounter >= 0 && m_iCsvCounter < m_iCsvNumFramesToProfile ) // going to profile 1000 frames
	{
		std::vector<std::string> row;
		float totalStereo = 0.0f;
		float totalSingle = 0.0f;
		for (auto e : m_frame.Timings.getFront().m_timersElapsed )
		{
			row.push_back( std::to_string( e.second.lastTiming ) );

			if( e.first.find("_R") != e.first.npos || e.first.find("_L") != e.first.npos) // contains an L or R suffix --> from stereo rendering
			{
				totalStereo += e.second.lastTiming;
			}
			else
			{
				totalSingle += e.second.lastTiming;
			}

		}
			
		// additional values
		row.push_back(std::to_string(totalStereo)); // total stereo rendering time
		row.push_back(std::to_string(totalSingle)); // total single pass time

		m_csvWriter.addRow(row);
	}

	if ( m_bCsvDoRun && m_iCsvCounter >= m_iCsvNumFramesToProfile ) // write when finished
	{
		m_csvWriter.writeToFile( "profile_" + std::to_string( (std::time(0) / 6) % 10000) + ".csv" );
		m_iCsvCounter = 0;
		m_bCsvDoRun = false;
	}
		
	if( m_bCsvDoRun )
	{
		m_iCsvCounter++;
	}
}


void CMainApplication::updateCommonUniforms()
{
	m_pUvwShader->update("model", s_model);

	m_pRaycastStereoShader->update( "uTextureToProjection_r", m_textureToProjection_r ); //since position map contains s_view space coords

	/************* update color mapping parameters ******************/
	// ray start/end parameters
	m_pRaycastStereoShader->update("uStepSize", s_rayStepSize); 	  // ray step size
	m_pRaycastStereoShader->update("uLodMaxLevel", s_lodMaxLevel);
	m_pRaycastStereoShader->update("uLodBegin", s_lodBegin);
	m_pRaycastStereoShader->update("uLodRange", s_lodRange);

	m_pRaycastShader->update("uStepSize", s_rayStepSize); 	  // ray step size
	m_pRaycastShader->update("uLodMaxLevel", s_lodMaxLevel);
	m_pRaycastShader->update("uLodBegin", s_lodBegin);
	m_pRaycastShader->update("uLodRange", s_lodRange);

	// color mapping parameters
	m_pRaycastStereoShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
	m_pRaycastStereoShader->update("uWindowingRange", s_windowingMaxValue - s_windowingMinValue); // full range of values in m_pWindow

	m_pRaycastShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
	m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in m_pWindow

	/************* update experimental  parameters ******************/
	//m_pRaycastStereoShader->update("uWriteStereo", s_writeStereo);

	static glm::vec3 shadowDir(0.0f,-0.5f,-1.0f);
	ImGui::SliderFloat3("Shadow Dir", glm::value_ptr(shadowDir), -1.0f, 1.0f);
	m_pRaycastStereoShader->update("uShadowRayDirection", glm::normalize(shadowDir)); // full range of values in m_pWindow
	m_pRaycastStereoShader->update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary
	m_pRaycastShader->update("uShadowRayDirection", glm::normalize(shadowDir)); // full range of values in m_pWindow
	m_pRaycastShader->update("uShadowRayNumSteps", 8); 	  // lower grayscale ramp boundary

	float s_zRayEnd   = abs(s_translation[3].z) + sqrt(2.0)*0.5f;
	float s_zRayStart = abs(s_translation[3].z) - sqrt(2.0)*0.5f;
	float e = s_eyeDistance;
	float w = m_textureResolution.x;
	float t_near = (s_zRayStart) / s_near;
	float t_far  = (s_zRayEnd)  / s_near;
	float nW = s_nearW;
	float pixelOffsetFar  = (1.0f / t_far)  * (e * w) / (nW * 2.0f); // pixel offset between points at zRayEnd distance to image planes
	float pixelOffsetNear = (1.0f / t_near) * (e * w) / (nW * 2.0f); // pixel offset between points at zRayStart distance to image planes
		
	m_pComposeTexArrayShader->update("uPixelOffsetFar",  pixelOffsetFar);
	m_pComposeTexArrayShader->update("uPixelOffsetNear", pixelOffsetNear);
		
	ImGui::Value("Approx Distance to Ray Start", s_zRayStart);
	ImGui::Value("Approx Distance to Ray End", s_zRayEnd);
	ImGui::Value("Pixel Offset at Ray Start", pixelOffsetNear);
	ImGui::Value("Pixel Offset at Ray End", pixelOffsetFar);
	ImGui::Value("Pixel Range of a Ray", pixelOffsetNear - pixelOffsetFar);
	//////////////////////////////////////////////////////////////////////////////
}

void CMainApplication::updateMatrices()
{
	if (s_isRotating) // update s_view matrix
	{
		s_rotation = glm::rotate(glm::mat4(1.0f), (float) m_fDeltaTime, glm::vec3(0.0f, 1.0f, 0.0f)) * s_rotation;
	}

	updateView();
	
	s_model = s_translation * m_turntable.getRotationMatrix() * s_rotation * s_scale; // auxiliary
	m_textureToProjection_r = s_perspective * s_view_r * s_model * glm::inverse( s_modelToTexture ); // texture to model
}

void CMainApplication::renderViews()
{
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
	OPENGLCONTEXT->bindFBO(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// reset atomic buffers
	GLuint a[3] = {0,0,0};
	glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0 , sizeof(GLuint) * 3, a);

	// render left image
	m_frame.Timings.getBack().timestamp("Raycast Left");
	m_frame.Timings.getBack().beginTimerElapsed("UVW_L");
	m_pUvwShader->update("view", s_view);
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pUvw->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().beginTimerElapsed("Raycast_L");
	m_pRaycastShader->update("back_uvw_map", 2);
	m_pRaycastShader->update("front_uvw_map", 4);
	m_pRaycastShader->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view) * s_screenToView);
	m_pRaycastShader->update("uViewToTexture",   s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view));
	m_pRaycastShader->update("uProjection", s_perspective);
	m_pSimpleRaycast->setFrameBufferObject(m_pFBO);
	m_pSimpleRaycast->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().timestamp("Raycast Right");
	// render right image
	m_frame.Timings.getBack().beginTimerElapsed("UVW_R");
	m_pUvwShader->update("view", s_view_r);
	m_pUvw->setFrameBufferObject(m_pUvwFBO_r);
	m_pUvw->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().beginTimerElapsed("Raycast_R");
	m_pRaycastShader->update("back_uvw_map", 3);
	m_pRaycastShader->update("front_uvw_map", 5);
	m_pRaycastShader->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view_r) * s_screenToView);
	m_pRaycastShader->update("uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view_r));
	m_pRaycastShader->update("uProjection", s_perspective);
	m_pSimpleRaycast->setFrameBufferObject(m_pFBO_r);
	m_pSimpleRaycast->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	// clear output texture
	m_frame.Timings.getBack().beginTimerElapsed("Clear Array");
	clearOutputTexture( m_stereoOutputTextureArray );
	m_frame.Timings.getBack().stopTimerElapsed();

	// render stereo images in a single pass		
	m_frame.Timings.getBack().timestamp("Raycast Stereo");
	m_frame.Timings.getBack().beginTimerElapsed("UVW");
	m_pUvwShader->update("view", s_view);
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pUvw->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().beginTimerElapsed("RaycastAndWrite");
	m_pRaycastStereoShader->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view) * s_screenToView);
	m_pRaycastStereoShader->update("uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view));
	m_pRaycastStereoShader->update("uProjection", s_perspective);
	m_pStereoRaycast->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	// compose right image from single pass output
	m_frame.Timings.getBack().beginTimerElapsed("Compose");
	m_pComposeTexArray->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().timestamp("Finished");
	// display fbo contents
	m_pShowTexShader->updateAndBindTexture("tex", 7, m_pFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)0, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 8, m_pFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)getResolution(m_pWindow).x / 2, 0, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 9, m_pFBO_single->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)0, (int)getResolution(m_pWindow).y / 2, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 10, m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y / 2, (int)getResolution(m_pWindow).x / 2, (int)getResolution(m_pWindow).y / 2);
	if (!m_bShowSingleLayer)
	{
		m_pShowTex->render();
	}
	else
	{
		m_pShowLayer->setViewport(getResolution(m_pWindow).x / 2, getResolution(m_pWindow).y / 2, getResolution(m_pWindow).x / 2, getResolution(m_pWindow).y / 2);
		m_pShowLayerShader->update("layer", m_fDisplayedLayer);
		m_pShowLayer->render();
	}

	m_frame.Timings.getBack().timestamp("Frame End");
	ImGui::Render();
		
	glFinish();
	SDL_GL_SwapWindow(m_pWindow); // swap buffers
}

void CMainApplication::recompileShaders()
{
	DEBUGLOG->log("Recompiling Shaders"); DEBUGLOG->indent();
	DEBUGLOG->log("Deleting Shaders");
	
	// delete shaders
	delete m_pRaycastShader;
	delete m_pRaycastStereoShader;

	// reload shader defines
	loadShaderDefines();

	// reload shaders
	loadRaycastingShaders();
	
	// set ShaderProgram References
	m_pSimpleRaycast->setShaderProgram(m_pRaycastShader);
	m_pStereoRaycast->setShaderProgram(m_pRaycastStereoShader);

	// update uniforms
	initTextureUniforms();
	initUniforms();

	DEBUGLOG->outdent();
}

void CMainApplication::rebuildFramebuffers()
{
	DEBUGLOG->log("Rebuilding Framebuffers"); DEBUGLOG->indent();
	DEBUGLOG->log("Deleting Framebuffers");
	delete m_pUvwFBO;
	delete m_pUvwFBO_r;
	delete m_pFBO;
	delete m_pFBO_r;
	delete m_pFBO_single;
	delete m_pFBO_single_r;

	// delete texture array
	std::vector<GLuint> textures;
	textures.push_back(m_stereoOutputTextureArray);
	glDeleteTextures(textures.size(), &textures[0]);

	// delete pixel buffer object
	glDeleteBuffers(1,&m_pboHandle);
	m_pboHandle = -1;
	m_texData.resize((int) m_textureResolution.x * (int) m_textureResolution.y * 4, 0.0f);

	initFramebuffers();
	
	// set Framebuffer References
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pSimpleRaycast->setFrameBufferObject(m_pFBO);
	m_pStereoRaycast->setFrameBufferObject(m_pFBO_single);
	m_pComposeTexArray->setFrameBufferObject(m_pFBO_single_r);

	m_pUvw->setViewport(0,0, m_pUvwFBO->getWidth(),m_pUvwFBO->getHeight());
	m_pSimpleRaycast->setViewport(0,0, m_pFBO->getWidth(),m_pFBO->getHeight());
	m_pStereoRaycast->setViewport(0,0, m_pFBO_single->getWidth(),m_pFBO_single->getHeight());
	m_pComposeTexArray->setViewport(0,0, m_pFBO_single_r->getWidth(),m_pFBO_single_r->getHeight());

	// bind textures
	initTextureUnits();

	DEBUGLOG->outdent();

}


void CMainApplication::loop()
{
	clearOutputTexture( m_stereoOutputTextureArray );

	std::string window_header = "Stereo Volume Renderer - Performance Tests";
	SDL_SetWindowTitle(m_pWindow, window_header.c_str() );
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	while (!shouldClose(m_pWindow))
	{
		//////////////////////////////////////////////////////////////////////////////
		pollEvents();
		
		//////////////////////////////////////////////////////////////////////////////
		updateGui();

		///////////////////////////// MATRIX UPDATING ///////////////////////////////
		updateMatrices();
		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
		updateCommonUniforms();

		////////////////////////////////  RENDERING //// ///////////////////////////// 
		renderViews();
	}
	
	ImGui_ImplSdlGL3_Shutdown();
	destroyWindow(m_pWindow);
	SDL_Quit();
}

CMainApplication::~CMainApplication()
{
	delete m_pQuad;
	delete m_pGrid;
	delete m_pVertexGrid;
	delete m_pVolume;

	// Shader bookkeeping
	delete m_pUvwShader;
	delete m_pRaycastShader;
	delete m_pRaycastStereoShader;
	delete m_pShowTexShader;
	delete m_pShowLayerShader;
	delete m_pComposeTexArrayShader;

	// m_pRaycastFBO bookkeeping
	delete m_pUvwFBO;
	delete m_pUvwFBO_r;
	delete m_pFBO;
	delete m_pFBO_r;
	delete m_pFBO_single;
	delete m_pFBO_single_r;

	// Renderpass bookkeeping
	delete m_pUvw; 		
	delete m_pSimpleRaycast;
	delete m_pStereoRaycast;
	delete m_pShowTex;
	delete m_pShowLayer;
	delete m_pComposeTexArray;

	std::vector<GLuint> textures;
	textures.push_back(m_stereoOutputTextureArray);
	glDeleteTextures(textures.size(), &textures[0]);

	// volume textures
	glDeleteTextures( sizeof(m_volumeTexture)/sizeof(GLuint), m_volumeTexture);
}

int main(int argc, char *argv[])
{
	DEBUGLOG->setAutoPrint(true);
	
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );
	
	pMainApplication->loadShaderDefines();

	pMainApplication->initSceneVariables();

	pMainApplication->loadVolumes();

	pMainApplication->loadGeometries();

	pMainApplication->loadShaders();

	pMainApplication->initFramebuffers();

	pMainApplication->initRenderPasses();
	
	pMainApplication->initTextureUnits();

	pMainApplication->initTextureUniforms();

	pMainApplication->initUniforms();

	pMainApplication->initGUI();
	
	pMainApplication->initAtomicsBuffer();

	pMainApplication->initEventHandlers();

	pMainApplication->loop();

	return 0;
}