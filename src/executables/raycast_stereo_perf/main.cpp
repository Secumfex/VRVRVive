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
#include <Rendering/ComputePass.h>
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
#include <Misc/VolumePresets.h>

////////////////////// PARAMETERS /////////////////////////////
static const char* resolutionPresetsStr[]  = {"256", "512", "768", "1024","Vive (1080)"};
static const int resolutionPresets[]  = {256, 512, 768, 1024, 1080};
static const char* numLayersPresetsStr[]  = {"24", "32", "48", "64"};
static const int numLayersPresets[]  = {24, 32, 48, 64};
static const char* fovPresetsStr[]  = {"45", "55", "Vive (110)"};
static const float fovPresets[]  = {45, 55, 110};

const char* SHADER_DEFINES[] = {
	"FIRST_HIT",
	"DSSIM_ERROR",
};

// defines that will be checked when writing config
const char* SHADER_DEFINES_CONFIG[] = {
	"SHADOW_SAMPLING",
	"AMBIENT_OCCLUSION",
	"LEVEL_OF_DETAIL",
	"RANDOM_OFFSET"
};
std::vector<std::string> s_shader_defines_config(SHADER_DEFINES_CONFIG, std::end(SHADER_DEFINES_CONFIG));

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
public: // who cares 
	// SDL bookkeeping
	SDL_Window	 *m_pWindow;
	SDL_GLContext m_pContext;

	// Render objects bookkeeping
	VolumeData<float> m_volumeData;
	GLuint m_volumeTexture;
	Quad*	m_pQuad;
	Grid*	m_pGrid;
	VertexGrid* m_pVertexGrid;
	VolumeSubdiv* m_pVolume;
	GLuint m_cubemapTexture;

	// Shader bookkeeping
	ShaderProgram* m_pUvwShader;
	ShaderProgram* m_pRaycastShader;
	ShaderProgram* m_pRaycastStereoShader;
	ShaderProgram* m_pShowTexShader;
	ShaderProgram* m_pErrorShader;
	ShaderProgram* m_pShowLayerShader;
	ShaderProgram* m_pComposeTexArrayShader;
	ShaderProgram* m_pRaycastStereoComputeShader;
	ShaderProgram* m_pMipmapComputeShader;
	
	// m_pRaycastFBO bookkeeping
	FrameBufferObject* m_pUvwFBO;
	FrameBufferObject* m_pUvwFBO_r;
	FrameBufferObject* m_pFBO;
	FrameBufferObject* m_pFBO_r;
	FrameBufferObject* m_pFBO_single;
	FrameBufferObject* m_pFBO_single_r;
	FrameBufferObject* m_pFBO_error;

	GLuint m_stereoOutputTextureArray;

	// Renderpass bookkeeping
	RenderPass* m_pUvw; 		
	RenderPass* m_pSimpleRaycast;
	RenderPass* m_pStereoRaycast;
	RenderPass* m_pShowTex;
	RenderPass* m_pError;
	RenderPass* m_pShowLayer;
	RenderPass* m_pComposeTexArray;
	ComputePass* m_pStereoRaycastCompute;
	ComputePass* m_pMipmapCompute;

	// Event handler bookkeeping
	std::function<bool(SDL_Event*)> m_sdlEventFunc;

	// Frame profiling
	struct Frame{
		Profiler FrameProfiler;
		SimpleDoubleBuffer<OpenGLTimings> Timings;
		SimpleDoubleBuffer<glm::vec4> AvgError;
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

	bool m_bShowDebugView;
	bool m_bDebugShowLayers;
	
	float m_fDisplayedLayer;

	glm::mat4 m_textureToProjection_r;

	std::vector<float> m_texData;
	GLuint m_pboHandle; // PBO 
	GLuint m_atomicsBuffer;

	struct ConfigHelper
	{
		std::string prefix; // i.e. timestamp or other uid

		CSVWriter<float> timings;
		CSVWriter<std::string> shader;
		CSVWriter<std::string> render;
		CSVWriter<float> averages;

		ConfigHelper();
		void saveImages(CMainApplication* mainApp);
		void copyShaderConfig(CMainApplication* mainApp);
		void copyRenderConfig(CMainApplication* mainApp);
		void writeToFiles();

	} m_configHelper;

	int  m_iCsvCounter;
	int  m_iCsvNumFramesToProfile;
	bool m_bCsvDoRun;

	int m_iNumLayers;
    glm::vec2 m_textureResolution;

	std::string m_executableName;

	bool m_bUseCompute;

	bool m_bAutoTranslate;

	int m_iNumSamples;
	int m_iCsvMaxNumSamples;
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
	void loadVolume();

	CMainApplication( int argc, char *argv[] );
	virtual ~CMainApplication();

	void clearOutputTexture(GLuint texture);
	void loadShaderDefines();
	void handleCsvProfiling();
	void handleCsvProfilingBasic(); // CSV Profiling when envmap sampling is enabled
	void handleCsvProfilingShadingComplexity(); // CSV Profiling when envmap sampling is enabled
	void updateError();

	void handleVolume();
	void handleResolutionPreset(int resolution);
	void handleNumLayersPreset(int numLayers);
	void handleFieldOfViewPreset(float fov);
	float getIdealNearValue(); //!< returns the computed 'near' value

	void recompileShaders();
	void rebuildFramebuffers();
	void rebuildLayerTexture();
	void loadRaycastingShaders();
	void initRaycastingFramebuffers();
	void initLayerTexture();

	void singlePassLayered();
	void singlePassCompute();
};

//////////////////////////////////////////////////////////////////////////////
///////////////////////// IMPLEMENTATION  ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

CMainApplication::CMainApplication(int argc, char *argv[])
	: m_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES))
	, m_bShowDebugView(false)
	, m_bDebugShowLayers(false)
	, m_fElapsedTime(0.0f)
	, m_fDisplayedLayer(0.0f)
	, m_fpsCounter(120)
	, m_iCurFpsIdx(0)
	, m_iNumLayers(32)
	, m_textureResolution(768, 768)
	, m_pboHandle(-1)
	, m_iCsvCounter(0)
	, m_iCsvNumFramesToProfile(50)
	, m_bCsvDoRun(false)
	, m_iActiveModel(0)
	, m_bUseCompute(false)
	, m_bAutoTranslate(false)
	, m_cubemapTexture(0)
	, m_iNumSamples(4) 
	, m_iCsvMaxNumSamples(32)
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
	s_rayStepSize = 1.0f / (2.0f * std::max(volumeData.size_x, std::max(volumeData.size_y, volumeData.size_z))); // this seems a reasonable size
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

void CMainApplication::loadVolume()
{
	int numLevels = 1;
	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
		numLevels = 4;
	}}

	VolumePresets::loadPreset( m_volumeData, (VolumePresets::Preset) m_iActiveModel);
	m_volumeTexture = loadTo3DTexture<float>(m_volumeData, numLevels, GL_R16F, GL_RED, GL_FLOAT);
	m_volumeData.data.clear(); // set free	

	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	checkGLError(true);
}

	/////////////////////     Scene / View Settings     //////////////////////////
void CMainApplication::initSceneVariables()
{
	// volume radius == 1.0f
	s_volumeSize = glm::vec3( 2.0f * sqrtf( 1.0f / 3.0f ) );
	float radius = sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );

	s_scale = glm::scale(glm::vec3(1.0f, 1.0f, 1.0f));

	s_eye = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	s_center = s_translation[3];
	s_aspect = m_textureResolution.x / m_textureResolution.y;
	s_fovY = 45.0f;
	s_near = getIdealNearValue();

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

	s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -( s_near + radius )));
	updateModel();
}

void CMainApplication::loadGeometries(){
	DEBUGLOG->log("Geometry Creation"); DEBUGLOG->indent();
	m_pVolume = new VolumeSubdiv(0.5f * s_volumeSize.x, 0.5f * s_volumeSize.y, 0.5f * s_volumeSize.z , 6);
	m_pVertexGrid = new VertexGrid(m_textureResolution.x, m_textureResolution.y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(-1));
	m_pQuad = new Quad();
	m_pGrid = new Grid(100, 100, 0.1f, 0.1f);
	DEBUGLOG->outdent();

	{bool hasCubemap = false; for (auto e : m_shaderDefines) { hasCubemap |= (e == "CUBEMAP_SAMPLING"); } if ( hasCubemap ){
	DEBUGLOG->log("Loading Cubemap"); DEBUGLOG->indent();
	//m_cubemapTexture = TextureTools::loadDefaultCubemap();
	std::vector<std::string> cubeMapFiles;
    cubeMapFiles.push_back("cubemap/vendetta-cove_rt.tga");
    cubeMapFiles.push_back("cubemap/vendetta-cove_lf.tga");
    cubeMapFiles.push_back("cubemap/vendetta-cove_up.tga");
    cubeMapFiles.push_back("cubemap/vendetta-cove_dn.tga");
    cubeMapFiles.push_back("cubemap/vendetta-cove_ft.tga");
    cubeMapFiles.push_back("cubemap/vendetta-cove_bk.tga");
	m_cubemapTexture = TextureTools::loadCubemapFromResourceFolder( cubeMapFiles );
	DEBUGLOG->outdent();
	}}
}

void CMainApplication::loadRaycastingShaders()
{
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	m_pRaycastShader = new ShaderProgram("/raycast/simpleRaycast.vert", "/raycast/unified_raycast.frag", m_shaderDefines); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: ray casting shader - single pass stereo"); DEBUGLOG->indent();
	std::vector<std::string> shaderDefinesStereo(m_shaderDefines);
	shaderDefinesStereo.push_back("STEREO_SINGLE_PASS");
	m_pRaycastStereoShader = new ShaderProgram("/raycast/simpleRaycast.vert", "/raycast/unified_raycast.frag", shaderDefinesStereo); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: compose tex array shader"); DEBUGLOG->indent();
	m_pComposeTexArrayShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/composeTextureArray.frag", m_shaderDefines); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: ray casting shader - single pass stereo compute"); DEBUGLOG->indent();
	std::vector<std::string> shaderDefinesStereoCompute(shaderDefinesStereo);
	shaderDefinesStereoCompute.push_back( "STEREO_SINGLE_OUTPUT" );
	shaderDefinesStereoCompute.push_back( "LOCAL_SIZE_Y " + std::to_string( int(m_textureResolution.y) ) );
	m_pRaycastStereoComputeShader = new ShaderProgram("/compute/unified_raycast.glsl", shaderDefinesStereoCompute); DEBUGLOG->outdent();
}


void CMainApplication::loadShaders()
{
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	m_pUvwShader = new ShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag"); DEBUGLOG->outdent();

	loadRaycastingShaders();

	DEBUGLOG->log("Shader Compilation: show layer shader"); DEBUGLOG->indent();
	m_pShowLayerShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", std::vector<std::string>(1,"ARRAY_TEXTURE"));DEBUGLOG->outdent();
	
	DEBUGLOG->log("Shader Compilation: show texture shader"); DEBUGLOG->indent();
	m_pShowTexShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", std::vector<std::string>(1,"LEVEL_OF_DETAIL"));DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: square error shader"); DEBUGLOG->indent();
	m_pErrorShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/error.frag", m_shaderDefines); DEBUGLOG->outdent();

	DEBUGLOG->log("Shader Compilation: mipmap compute"); DEBUGLOG->indent();
	std::vector<std::string> shaderDefinesMipmapCompute(1, "AVERAGE_MIPMAP");
	m_pMipmapComputeShader = new ShaderProgram("/compute/mipmap.glsl", shaderDefinesMipmapCompute); DEBUGLOG->outdent();
}

void CMainApplication::initRaycastingFramebuffers()
{
	DEBUGLOG->log("FrameBufferObject Creation: raycasting results"); DEBUGLOG->indent();
	m_pFBO = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	m_pFBO_r = new FrameBufferObject(m_pRaycastShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	m_pFBO_single = new FrameBufferObject(m_pRaycastStereoShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	DEBUGLOG->outdent();

	DEBUGLOG->log("FrameBufferObject Creation: single-pass stereo compositing result"); DEBUGLOG->indent();
	m_pFBO_single_r = new FrameBufferObject(m_pComposeTexArrayShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	DEBUGLOG->outdent();
}

void CMainApplication::initLayerTexture()
{
	DEBUGLOG->log("FrameBufferObject Creation: single-pass stereo output texture array"); DEBUGLOG->indent();
	m_stereoOutputTextureArray = createTextureArray((int)m_textureResolution.x, (int)m_textureResolution.y, m_iNumLayers, GL_RGBA16F);
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

	initLayerTexture();

	DEBUGLOG->log("FrameBufferObject Creation: error evaluation"); DEBUGLOG->indent();
	int numMipmaps = (int) (std::log( std::max(m_textureResolution.x, m_textureResolution.y) ) / std::log( 2.0f ) );
	FrameBufferObject::s_useTexStorage2D = true;
	FrameBufferObject::s_numLevels = numMipmaps + 1;
	FrameBufferObject::s_internalFormat = GL_RGBA16F;

	m_pFBO_error = new FrameBufferObject(m_pErrorShader->getOutputInfoMap(), (int)m_textureResolution.x, (int)m_textureResolution.y);
	OPENGLCONTEXT->bindTexture( m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // or else texturelod doesn't work
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	OPENGLCONTEXT->bindTexture(0);
	FrameBufferObject::s_internalFormat = GL_RGBA8;
	FrameBufferObject::s_useTexStorage2D = false;
	FrameBufferObject::s_numLevels = 1;

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
	m_pShowTex->addEnable(GL_BLEND);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: square error"); DEBUGLOG->indent();
	m_pError = new RenderPass(m_pErrorShader, m_pFBO_error);
	m_pError->addRenderable(m_pQuad);	
	m_pError->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	DEBUGLOG->outdent();
	
	DEBUGLOG->log("RenderPass Creation: mip map compute"); DEBUGLOG->indent();
	m_pMipmapCompute = new ComputePass(m_pMipmapComputeShader);
	DEBUGLOG->outdent();

	DEBUGLOG->log("RenderPass Creation: show texture"); DEBUGLOG->indent();
	m_pStereoRaycastCompute = new ComputePass(m_pRaycastStereoComputeShader);
	DEBUGLOG->outdent();

}


void CMainApplication::initTextureUnits()
{
	OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(TransferFunctionPresets::s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D);

	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
	OPENGLCONTEXT->bindTextureToUnit(m_pUvwFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

	OPENGLCONTEXT->bindImageTextureToUnit(m_stereoOutputTextureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
	OPENGLCONTEXT->bindTextureToUnit(m_stereoOutputTextureArray, GL_TEXTURE6, GL_TEXTURE_2D_ARRAY); // for display
	
	OPENGLCONTEXT->bindTextureToUnit(m_pFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE8, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D);

	OPENGLCONTEXT->bindTextureToUnit(m_cubemapTexture, GL_TEXTURE12, GL_TEXTURE_CUBE_MAP);
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

	m_pRaycastStereoComputeShader->update("volume_texture", 0);
	m_pRaycastStereoComputeShader->update("transferFunctionTex", 1);
	m_pRaycastStereoComputeShader->update("back_uvw_map", 2);
	m_pRaycastStereoComputeShader->update("front_uvw_map", 4);
	
	if (m_cubemapTexture != 0)
	{
		m_pRaycastShader->update("cube_map", 12);
		m_pRaycastStereoShader->update("cube_map", 12);
		m_pRaycastStereoComputeShader->update("cube_map", 12);
	}


	m_pComposeTexArrayShader->update( "tex", 6);

	m_pShowLayerShader->update("tex", 6);

	m_pErrorShader->update("tex1", 10);
	m_pErrorShader->update("tex2", 8);
}

void CMainApplication::initUniforms()
{
	m_pUvwShader->update("model", s_translation * s_rotation * s_scale);
	m_pUvwShader->update("view", s_view);

	m_pRaycastShader->update("uStepSize", s_rayStepSize);
	m_pRaycastStereoShader->update("uStepSize", s_rayStepSize);
	m_pRaycastStereoComputeShader->update("uStepSize", s_rayStepSize);
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

void CMainApplication::handleVolume()
{
	glDeleteTextures(1, &m_volumeTexture);
	OPENGLCONTEXT->bindTextureToUnit(0, GL_TEXTURE0, GL_TEXTURE_3D);
	
	loadVolume();

	activateVolume(m_volumeData);

	// adjust scale
	s_scale = VolumePresets::getScalation((VolumePresets::Preset) m_iActiveModel);
	s_rotation = VolumePresets::getRotation((VolumePresets::Preset) m_iActiveModel);

	OPENGLCONTEXT->bindTextureToUnit(m_volumeTexture, GL_TEXTURE0, GL_TEXTURE_3D);
	TransferFunctionPresets::loadPreset(TransferFunctionPresets::s_transferFunction, (VolumePresets::Preset) m_iActiveModel );
}

float CMainApplication::getIdealNearValue()
{
	// what we have right now
	float b = s_eyeDistance;
	float w = m_textureResolution.x;
	float d_p = (float) m_iNumLayers + 1; // plus for rounding to bigger range
	float alpha = glm::radians(s_fovY * 0.5f);
	float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));

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

void CMainApplication::handleNumLayersPreset(int numLayers)
{
	// adjust num layers
	m_iNumLayers = numLayers;

	// rebuild fbos
	rebuildLayerTexture();

	s_near = getIdealNearValue();
	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
		s_lodBegin = s_near;
		s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
	}}	
	
	float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));
	if ( m_bAutoTranslate) s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -( s_near + radius )));
	updateModel();

	updateNearHeightWidth();
	updatePerspective();
	updateScreenToViewMatrix();
}

void CMainApplication::handleResolutionPreset(int resolution)
{
	// adjust resolution
	m_textureResolution = glm::vec2(resolution, resolution);

	// rebuild fbos
	rebuildFramebuffers();

	//  compute ideal model position
	s_near = getIdealNearValue();

	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
		s_lodBegin = s_near;
		s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
	}}	


	float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));
	if ( m_bAutoTranslate) s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -( s_near + radius )));
	updateModel();

	updateNearHeightWidth();
	updatePerspective();
	updateScreenToViewMatrix();
}

void CMainApplication::handleFieldOfViewPreset(float fov)
{
	// adjust fov
	s_fovY = fov;

	// compute ideal model position
	s_near = getIdealNearValue();

	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
		s_lodBegin = s_near;
		s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
	}}	


	float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));
	if ( m_bAutoTranslate) s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -( s_near + radius )));
	updateModel();

	updateNearHeightWidth();
	updatePerspective();
	updateScreenToViewMatrix();

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
	ImGui::PlotLines("FPS", &m_fpsCounter[0], m_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2( ImGui::GetContentRegionAvailWidth() ,60));
	ImGui::PopStyleColor();

	ImGui::DragFloat("eye distance", &s_eyeDistance, 0.01f, 0.0f, 2.0f);
	
	ImGui::Text("Active Model");
	ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());
	if (ImGui::ListBox("##activemodel", &m_iActiveModel, VolumePresets::s_models, (int)(sizeof(VolumePresets::s_models)/sizeof(*VolumePresets::s_models)), 5))
    {
		handleVolume();
    }
	ImGui::PopItemWidth();

	if (ImGui::CollapsingHeader("Transfer Function"))
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
		updateTransferFunctionTex();
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
		{bool hasCullPlanes = false; for (auto e : m_shaderDefines) { hasCullPlanes |= (e == "CULL_PLANES"); } if ( hasCullPlanes ){
			ImGui::SliderFloat3("Cull Max", glm::value_ptr(s_cullMax),0.0f, 1.0f);
			ImGui::SliderFloat3("Cull Min", glm::value_ptr(s_cullMin),0.0f, 1.0f);
		}}

    }
	ImGui::PopItemWidth();

	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
	if (ImGui::CollapsingHeader("Level of Detail Settings"))
    {
	ImGui::DragFloat("Lod Max Level", &s_lodMaxLevel, 0.1f, 0.0f, 8.0f);
	ImGui::DragFloat("Lod Begin", &s_lodBegin, 0.01f, 0.0f, s_far);
	ImGui::DragFloat("Lod Range", &s_lodRange, 0.01f, 0.0f, std::max(0.1f, s_far - s_lodBegin));
	}
	}}

	if (ImGui::Button("Recompile Shaders"))
	{
		recompileShaders();
	}
	
	if (ImGui::Button("Save Current"))
	{
		std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";
		TextureTools::saveTexture(prefix + "LEFT.png", m_pFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		TextureTools::saveTexture(prefix + "RIGHT.png", m_pFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		TextureTools::saveTexture(prefix + "RIGHT_SINGLE.png", m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		TextureTools::saveTexture(prefix + "DSSIM.png", m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		CSVWriter<float> writer;
		writer.setHeaders(std::vector<std::string>(1, "DSSIM" ));
		writer.setData(std::vector<float>(1, glm::dot(m_frame.AvgError.getFront(), glm::vec4(1.0f / 4.0f))));
		writer.writeToFile(prefix + "DSSIM.csv");

		if (m_bShowDebugView && m_bDebugShowLayers) // also output the layer
		{
			TextureTools::saveTextureArrayLayer(prefix + "LAYER_" + std::to_string(int(m_fDisplayedLayer)) + ".png", m_stereoOutputTextureArray, int(m_fDisplayedLayer));
		}

	}

	if (ImGui::CollapsingHeader("Preset Settings"))
	{
	if (ImGui::SliderFloat("Texture Size", &m_textureResolution.x, 256.0f, 2160.0f, "%.f", 2.0f)){ m_textureResolution.y = m_textureResolution.x; }
	ImGui::SliderInt("Num Layers", &m_iNumLayers, 1, 128);
	if (ImGui::Button("Rebuild Framebuffers"))
	{
		rebuildFramebuffers();

		//  compute ideal model position
		s_near = getIdealNearValue();

		{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
			s_lodBegin = s_near;
			s_lodRange = 2.0f * sqrtf( powf(s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f) );
		}}	

		float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));
		if ( m_bAutoTranslate) s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -( s_near + radius )));
		updateModel();

		updateNearHeightWidth();
		updatePerspective();
		updateScreenToViewMatrix();
	}

	///////////////////// DEBUG ///////////////////////////
	{
	static CSVWriter<float> writer;
	if (ImGui::Button("Push Overhead Times"))
	{
		if (writer.getHeaders().empty()) {
			const char* headers[] = {"Resolution", "Layers", "Clear", "Compose"};
			writer.setHeaders( std::vector<std::string>(headers, std::end(headers)) );
		}
		std::vector<float> row;
		row.push_back( m_textureResolution.x );
		row.push_back( (float) m_iNumLayers );
		row.push_back( m_frame.Timings.getFront().m_timersElapsed.at("Clear Array").lastTiming);
		row.push_back( m_frame.Timings.getFront().m_timersElapsed.at("Compose").lastTiming);
		writer.addRow(row);
	}
	ImGui::SameLine();
	if (ImGui::Button("Write Overhead Times"))
	{
		std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";
		writer.writeToFile(prefix + "OVERHEAD_TIMES.csv");
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Overhead Times"))
	{
		writer.clearData();
	}
	}

	ImGui::PushItemWidth( ImGui::GetContentRegionAvailWidth() / 3.f );
	{static int selectedPreset = 2;
	ImGui::BeginGroup();
	ImGui::Text("Resolution");
	if (ImGui::ListBox("##resolutionpresets", &selectedPreset, resolutionPresetsStr, (int)(sizeof(resolutionPresetsStr)/sizeof(*resolutionPresetsStr)),4))
    {
		handleResolutionPreset( resolutionPresets[ selectedPreset ]);
	}}
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::Text("Num Layers");
	{static int selectedPreset = 1;
	if (ImGui::ListBox("##numlayerpresets", &selectedPreset, numLayersPresetsStr, (int)(sizeof(numLayersPresetsStr)/sizeof(*numLayersPresetsStr)),4))
	{
		handleNumLayersPreset(numLayersPresets[ selectedPreset ]);
	}}
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::Text("Field of View");
	{static int selectedPreset = 0;
	if (ImGui::ListBox("##fovpresets", &selectedPreset, fovPresetsStr, (int)(sizeof(fovPresetsStr)/sizeof(*fovPresetsStr)),4))
	{
		handleFieldOfViewPreset( fovPresets[ selectedPreset ]);
	}}
	ImGui::EndGroup();

	ImGui::PopItemWidth();
	}
	
	{
		ImGui::PushItemWidth( ImGui::GetContentRegionAvailWidth() / 3.f );
		ImGui::SliderFloat("Translation Z", &s_translation[3][2], -2.5f, 2.5f); 
		ImGui::SameLine();
		ImGui::Checkbox("Auto-Translate", &m_bAutoTranslate);
		ImGui::PopItemWidth();
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
		OpenGLTimings& front = m_frame.Timings.getFront();

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

		// DEBUG see full ranges
		//if ( front.m_timestamps.find("Raycast Left") != front.m_timestamps.end() && front.m_timersElapsed.find("Raycast_R") != front.m_timersElapsed.end())
		//{
		//	m_frame.FrameProfiler.setRangeByTag("Total Stereo", front.m_timestamps.at("Raycast Left").lastTime, front.m_timersElapsed.at("Raycast_R").lastTime + front.m_timersElapsed.at("Raycast_R").lastTiming, "", 1);
		//}
		//if ( front.m_timestamps.find("Raycast Stereo") != front.m_timestamps.end() && front.m_timestamps.find("Finished") != front.m_timestamps.end())
		//{
		//	m_frame.FrameProfiler.setRangeByTag("Total Single", front.m_timestamps.at("Raycast Stereo").lastTime, front.m_timestamps.at("Finished").lastTime, "", 1);
		//}


		m_frame.FrameProfiler.imguiInterface(frame_begin, frame_end, &frame_profiler_visible);
	}

		
	//DEBUG Output to profiler file
	if (!pause_frame_profiler) { m_frame.Timings.swap(); m_frame.AvgError.swap(); }
	m_frame.Timings.getBack().timestamp("Frame Begin");

    // DSSIM
	ImGui::Separator();
	float avgErrorArr[4] = {m_frame.AvgError.getFront().x, m_frame.AvgError.getFront().y, m_frame.AvgError.getFront().z, m_frame.AvgError.getFront().w};
	ImGui::InputFloat4("Avg DSSIM Channels", avgErrorArr, 3);
	ImGui::Value("AVG DSSIM", ((avgErrorArr[0] + avgErrorArr[1] + avgErrorArr[2] + avgErrorArr[3]) / 4.0f));
	ImGui::Separator();

	//////////////////////////////////////////////////////////////////////////////

	ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume
	ImGui::Checkbox("Use Compute", &m_bUseCompute);
	ImGui::Checkbox("Show Debug View", &m_bShowDebugView);
	ImGui::Checkbox("Debug Show Layers", &m_bDebugShowLayers);
	if (m_bDebugShowLayers)
	{
		ImGui::SliderFloat("Displayed Layer", &m_fDisplayedLayer, 0.0f, m_iNumLayers - 1);
		if (ImGui::Button("Save Layer"))
		{
			std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";
			TextureTools::saveTextureArrayLayer(prefix + "LAYER_" + std::to_string(int(m_fDisplayedLayer)) + ".png", m_stereoOutputTextureArray, int(m_fDisplayedLayer));
		}
		ImGui::SameLine();
		if ( ImGui::Button("Save Texture Array") )
		{
			std::string prefix = std::to_string( (std::time(0) / 6) % 10000) + "_";
			TextureTools::saveTextureArray(prefix + "LAYER", m_stereoOutputTextureArray);
		}
	}
}

void CMainApplication::updateError()
{
	// compute dssim
	m_pError->render();

	// compute average dssim by mipmapping
	// option 1: use OpenGL mipmapping func
	OPENGLCONTEXT->activeTexture( GL_TEXTURE11 );
	OPENGLCONTEXT->bindTexture( m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE_2D);
	glGenerateMipmap(GL_TEXTURE_2D);

	int numMipmaps = (int) (std::log( std::max( (float) m_pFBO_error->getWidth(), (float) m_pFBO_error->getHeight()) ) / std::log( 2.0f ) );

	// option 2: use compute shader
	//int mipmapBase = 0;
	//int mipmapTarget = 1;
	//glm::ivec2 curRes(m_pFBO_error->getWidth(), m_pFBO_error->getHeight());
	//for (int i = 0; i < numMipmaps; i++)
	//{
	//	curRes /= 2;
	//	OPENGLCONTEXT->bindImageTextureToUnit(m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), 0, GL_RGBA16F, GL_READ_ONLY, mipmapBase + i, GL_FALSE);
	//	OPENGLCONTEXT->bindImageTextureToUnit(m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), 1, GL_RGBA16F, GL_WRITE_ONLY, mipmapTarget + i, GL_FALSE);
	//	m_pMipmapCompute->dispatch( curRes.x / m_pMipmapCompute->getLocalGroupSize().x + 1, curRes.y / m_pMipmapCompute->getLocalGroupSize().y + 1);
	//}

	glGetTexImage(GL_TEXTURE_2D, numMipmaps, GL_RGBA, GL_FLOAT, glm::value_ptr( m_frame.AvgError.getBack() ));
}


static enum ProfilingStates 
{
	DONE,
	//NEXTVOLUME,
	NEXTCOMPLEX,
	PROFILING,
	PUSHING,
	WRITING,
	PREPARESCREENSHOTS,
	PREPARESCREENSHOTSWAIT,
	SCREENSHOTS
} profilerstate = DONE;

void CMainApplication::handleCsvProfilingShadingComplexity()
{
	ImGui::SliderInt("Max Shading Samples", &m_iCsvMaxNumSamples, 1, 128);
	ImGui::Separator();
	
	// STATE MACHINE
	std::vector<std::string> headers;
	std::vector<float> row;
	std::vector<float> data;
	std::vector<float> averagesRow;

	static glm::mat4 tmp_rotation;

	if ( m_bCsvDoRun )
	{
		switch (profilerstate)
		{
		case DONE: // Setup data structures
		{
			m_configHelper.prefix = "prf_" + std::to_string( (std::time(0) / 6) % 10000) 
				+ "_" + std::to_string((int) m_textureResolution.x) 
				+ "_" + std::to_string(m_iNumLayers) 
				+ "_" + VolumePresets::s_models[m_iActiveModel] 
				+ "_";
			if (m_bUseCompute) { m_configHelper.prefix += "GPGPU_"; }

			for (auto e : m_frame.Timings.getFront().m_timersElapsed)
			{
				headers.push_back(e.first);
			}

			// additional headers
			headers.push_back("Num Samples");
			headers.push_back("Total Stereo");
			headers.push_back("Total Single");
			headers.push_back("Time-Saving");
			headers.push_back("Average DSSIM");

			m_configHelper.timings.setHeaders(headers);

			// averages
			m_configHelper.averages.setHeaders(headers); // same headers

			m_configHelper.copyRenderConfig( this );
			m_configHelper.copyShaderConfig( this );

			// clear timings
			m_configHelper.timings.clearData();
			m_configHelper.averages.clearData();
			
			m_iNumSamples = 0;
			tmp_rotation = m_turntable.getRotationMatrix();
			profilerstate = PROFILING;
		}
		break;
		case PROFILING:
		{
			float totalStereo = 0.0f;
			float totalSingle = 0.0f;
			float totalLeft = 0.0f;
			float totalRight = 0.0f;
		
			float rotationStepSize = (1.0f / (float) m_iCsvNumFramesToProfile) * glm::two_pi<float>();
			m_turntable.setRotationMatrix( glm::rotate(glm::mat4(1.0f), m_iCsvCounter * rotationStepSize, glm::vec3(0.0f, 1.0f, 0.0f)) * tmp_rotation );
		
			int i = 0;
			for (auto e : m_frame.Timings.getFront().m_timersElapsed )
			{
				row.push_back( e.second.lastTiming );

				if ( e.first.find("_R") != e.first.npos || e.first.find("_L") != e.first.npos)
				{
					if( e.first.find("_R") != e.first.npos )
					{
						totalRight =+ e.second.lastTiming;
					}
					if( e.first.find("_L") != e.first.npos) // contains an L or R suffix --> from stereo rendering
					{
						totalLeft += e.second.lastTiming;
					}
					totalStereo += e.second.lastTiming;
				}
				else
				{
					totalSingle += e.second.lastTiming;
				}
			}
		
			float speedup = 1.0f - ((totalSingle - totalLeft) / totalRight);

			// additional values
			row.push_back( m_iNumSamples );
			row.push_back( totalStereo ); // total stereo rendering time
			row.push_back( totalSingle ); // total single pass time
			row.push_back( speedup ); // time-saving of total single pass to total stereo passes time
			row.push_back( (m_frame.AvgError.getFront().x + m_frame.AvgError.getFront().y +m_frame.AvgError.getFront().z +m_frame.AvgError.getFront().w) / 4.0f ); // time-saving of total single pass to total stereo passes time

			m_configHelper.timings.addRow(row);

			if ( m_iCsvCounter > m_iCsvNumFramesToProfile )
			{
				m_iCsvCounter = 0;
				m_turntable.setRotationMatrix( tmp_rotation );
				profilerstate = PUSHING;
			}
			else
			{
				m_iCsvCounter ++;
				profilerstate = PROFILING;
			}
		}
		break;
		
		case PUSHING:
		{
			// compute averages
			data = m_configHelper.timings.getData();
			const int rowLength = m_configHelper.timings.getHeaders().size();
			const int numRows = (data.size() / rowLength);
			averagesRow.resize(rowLength, 0.0f); 

			for ( int row = 0; row < numRows; row++ )
			{
				int idx = row * rowLength;
				for (int col = 0; col < rowLength; col++)
				{
					averagesRow[col] += data[idx + col];
				}
			}
			for (int col = 0; col < rowLength; col++)
			{
				averagesRow[col] /= (float) numRows;
			}
			m_configHelper.averages.addRow(averagesRow);

			// clear profiling times for next iteration
			m_configHelper.timings.clearData();
			
			profilerstate = NEXTCOMPLEX;
		}
		break;
		case NEXTCOMPLEX:
		{
			// next complexity state
			if ( m_iNumSamples >= m_iCsvMaxNumSamples )
			{
				m_iNumSamples = 0;
				profilerstate = PREPARESCREENSHOTS;
			}
			else
			{
				if (m_iNumSamples < 4) 
				{ 
					m_iNumSamples++; 
				}
				else
				{
					m_iNumSamples += 4;
				}
				profilerstate = PROFILING;
			}
		}
		break;
		case PREPARESCREENSHOTS:
		{
			m_iNumSamples = m_iCsvMaxNumSamples;
			profilerstate = PREPARESCREENSHOTSWAIT;
		}
		break;
		case PREPARESCREENSHOTSWAIT: // to render back buffer
		{
			profilerstate = SCREENSHOTS; 
		}
		break;
		case SCREENSHOTS:
		{
			m_configHelper.saveImages(this);
			m_iNumSamples = 0;

			profilerstate = WRITING;
		}
		break;
		case WRITING:
		{
			m_configHelper.writeToFiles();

			// reset
			m_configHelper.averages.clearData();
			m_iCsvCounter = 0;
			m_bCsvDoRun = false;
			profilerstate = DONE;

		}
		break;
		}
	}
}		


void CMainApplication::handleCsvProfiling()
{
	ImGui::Separator();
	if (ImGui::Button("Run CSV Profiling") && !m_bCsvDoRun)
	{
		m_bCsvDoRun = true;
	}
	ImGui::SameLine(); ImGui::Value("Running", m_bCsvDoRun);
	ImGui::SameLine(); ImGui::Value("Frame", m_iCsvCounter);
	ImGui::SliderInt("Num Frames To Profile", &m_iCsvNumFramesToProfile, 1, 1000);
	ImGui::Separator();

	{bool hasCubemap = false; for (auto e : m_shaderDefines) { hasCubemap |= (e == "CUBEMAP_SAMPLING"); } if (hasCubemap) {
		handleCsvProfilingShadingComplexity();
	}
	else
	{
		handleCsvProfilingBasic();
	}}
}

void CMainApplication::handleCsvProfilingBasic()
{
	if ( m_bCsvDoRun && m_iCsvCounter == 0) // just not frame one, okay?
	{
		m_configHelper.prefix = "prf_" + std::to_string( (std::time(0) / 6) % 10000) + "_" + std::to_string((int) m_textureResolution.x) + "_" + std::to_string(m_iNumLayers) + "_" + VolumePresets::s_models[m_iActiveModel] + "_";
		if (m_bUseCompute) { m_configHelper.prefix += "GPGPU_"; }

		std::vector<std::string> headers;
		for (auto e : m_frame.Timings.getFront().m_timersElapsed)
		{
			headers.push_back(e.first);
		}

		// additional headers
		headers.push_back("Total Stereo");
		headers.push_back("Total Single");
		headers.push_back("Time-Saving");
		headers.push_back("Average DSSIM");

		m_configHelper.timings.setHeaders(headers);

		// averages
		m_configHelper.averages.setHeaders(headers); // same headers

		m_configHelper.copyRenderConfig( this );
		m_configHelper.copyShaderConfig( this );
		m_configHelper.saveImages( this );

		// clear timings
		m_configHelper.timings.clearData();
		m_configHelper.averages.clearData();
	}

	if ( m_bCsvDoRun &&  m_iCsvCounter > 2 && m_iCsvCounter <= m_iCsvNumFramesToProfile + 2 ) // profiling running
	{
		std::vector<float> row;
		float totalStereo = 0.0f;
		float totalSingle = 0.0f;
		float totalLeft = 0.0f;
		float totalRight = 0.0f;
		
		float rotationStepSize = (1.0f / (float) m_iCsvNumFramesToProfile) * glm::two_pi<float>();
		m_turntable.setRotationMatrix( glm::rotate(glm::mat4(1.0f), rotationStepSize, glm::vec3(0.0f, 1.0f, 0.0f)) * m_turntable.getRotationMatrix() );
		
		int i = 0;
		for (auto e : m_frame.Timings.getFront().m_timersElapsed )
		{
			row.push_back( e.second.lastTiming );

			if ( e.first.find("_R") != e.first.npos || e.first.find("_L") != e.first.npos)
			{
				if( e.first.find("_R") != e.first.npos )
				{
					totalRight =+ e.second.lastTiming;
				}
				if( e.first.find("_L") != e.first.npos) // contains an L or R suffix --> from stereo rendering
				{
					totalLeft += e.second.lastTiming;
				}
				totalStereo += e.second.lastTiming;
			}
			else
			{
				totalSingle += e.second.lastTiming;
			}
		}
		
		float speedup = 1.0f - ((totalSingle - totalLeft) / totalRight);

		// additional values
		row.push_back(totalStereo); // total stereo rendering time
		row.push_back(totalSingle); // total single pass time
		row.push_back(speedup ); // time-saving of total single pass to total stereo passes time
		row.push_back( (m_frame.AvgError.getFront().x + m_frame.AvgError.getFront().y +m_frame.AvgError.getFront().z +m_frame.AvgError.getFront().w) / 4.0f ); // time-saving of total single pass to total stereo passes time

		m_configHelper.timings.addRow(row);
	}

	if ( m_bCsvDoRun && m_iCsvCounter > m_iCsvNumFramesToProfile + 2) // write when finished
	{
		////////////////////
		// compute averages
		auto data = m_configHelper.timings.getData();
		const int rowLength = m_configHelper.timings.getHeaders().size();
		const int numRows = (data.size() / rowLength);
		std::vector<float> averagesRow(rowLength, 0.0f); 

		for ( int row = 0; row < numRows; row++ )
		{
			int idx = row * rowLength;
			for (int col = 0; col < rowLength; col++)
			{
				averagesRow[col] += data[idx + col];
			}
		}
		for (int col = 0; col < rowLength; col++)
		{
			averagesRow[col] /= (float) numRows;
		}
		m_configHelper.averages.addRow(averagesRow);
		///////////////////

		m_configHelper.writeToFiles();
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
	m_pUvwShader->update("projection", s_perspective);

	m_pRaycastStereoShader->update( "uTextureToProjection_r", m_textureToProjection_r ); //since position map contains s_view space coords
	m_pRaycastStereoComputeShader->update( "uTextureToProjection_r", m_textureToProjection_r );

	/************* update color mapping parameters ******************/
	// ray start/end parameters
	m_pRaycastStereoShader->update("uStepSize", s_rayStepSize); 	  // ray step size
	m_pRaycastShader->update("uStepSize", s_rayStepSize); 	  // ray step size


	// ray start/end parameters
	
	{bool hasLod = false; for (auto e : m_shaderDefines) { hasLod |= (e == "LEVEL_OF_DETAIL"); } if ( hasLod){
	m_pRaycastShader->update("uLodMaxLevel", s_lodMaxLevel);
	m_pRaycastShader->update("uLodBegin", s_lodBegin);
	m_pRaycastShader->update("uLodRange", s_lodRange);
	m_pRaycastStereoShader->update("uLodMaxLevel", s_lodMaxLevel);
	m_pRaycastStereoShader->update("uLodBegin", s_lodBegin);
	m_pRaycastStereoShader->update("uLodRange", s_lodRange);
	m_pRaycastStereoComputeShader->update("uStepSize", s_rayStepSize); 	  // ray step size
	m_pRaycastStereoComputeShader->update("uLodMaxLevel", s_lodMaxLevel);
	m_pRaycastStereoComputeShader->update("uLodBegin", s_lodBegin);
	m_pRaycastStereoComputeShader->update("uLodRange", s_lodRange);
	}}

	{bool hasCullPlanes = false; for (auto e : m_shaderDefines) { hasCullPlanes |= (e == "CULL_PLANES"); } if ( hasCullPlanes ){
		m_pRaycastShader->update("uCullMin", s_cullMin);
		m_pRaycastShader->update("uCullMax", s_cullMax);
		m_pRaycastStereoShader->update("uCullMin", s_cullMin);
		m_pRaycastStereoShader->update("uCullMax", s_cullMax);
		m_pRaycastStereoComputeShader->update("uCullMin", s_cullMin);
		m_pRaycastStereoComputeShader->update("uCullMax", s_cullMax);
	}}

	// color mapping parameters
	m_pRaycastStereoShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
	m_pRaycastStereoShader->update("uWindowingRange", s_windowingMaxValue - s_windowingMinValue); // full range of values in m_pWindow

	m_pRaycastShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
	m_pRaycastShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in m_pWindow

	m_pRaycastStereoComputeShader->update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
	m_pRaycastStereoComputeShader->update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in m_pWindow

	/************* update experimental  parameters ******************/
	//m_pRaycastStereoShader->update("uWriteStereo", s_writeStereo);
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
	m_pRaycastStereoShader->update("uShadowRayDirection", glm::normalize(shadowDir)); // full range of values in m_pWindow
	m_pRaycastStereoShader->update("uShadowRayNumSteps", numSteps); 	  // lower grayscale ramp boundary
	m_pRaycastShader->update("uShadowRayDirection", glm::normalize(shadowDir)); // full range of values in m_pWindow
	m_pRaycastShader->update("uShadowRayNumSteps", numSteps); 	  // lower grayscale ramp boundary
	m_pRaycastStereoComputeShader->update("uShadowRayDirection", glm::normalize(shadowDir)); // full range of values in m_pWindow
	m_pRaycastStereoComputeShader->update("uShadowRayNumSteps", numSteps); 	  // lower grayscale ramp boundary
	}}

	{bool hasCubemap = false; for (auto e : m_shaderDefines) { hasCubemap |= (e == "CUBEMAP_SAMPLING"); } if ( hasCubemap ){
		ImGui::SliderInt("Num Samples", &m_iNumSamples, 0, 128);
		m_pRaycastStereoShader->update("uNumSamples", m_iNumSamples); 	  // lower grayscale ramp boundary
		m_pRaycastShader->update("uNumSamples", m_iNumSamples); 	  // lower grayscale ramp boundary
		m_pRaycastStereoComputeShader->update("uNumSamples", m_iNumSamples); 	  // lower grayscale ramp boundary
	}}

	float radius = sqrtf( powf( s_volumeSize.x * 0.5f, 2.0f) + powf(s_volumeSize.y * 0.5f, 2.0f) + powf(s_volumeSize.z * 0.5f, 2.0f));
	float s_zRayEnd   = max(s_near, -(s_translation[3].z - radius));
	float s_zRayStart = max(s_near, -(s_translation[3].z + radius));

	float t_near = (s_zRayStart) / s_near;
	float t_far  = (s_zRayEnd)  / s_near;
	float nW = s_nearW;

	//float pixelOffsetFar  = (1.0f / t_far)  * (b * w) / (nW * 2.0f); // pixel offset between points at zRayEnd distance to image planes
	//float pixelOffsetNear = (1.0f / t_near) * (b * w) / (nW * 2.0f); // pixel offset between points at zRayStart distance to image planes

	float b = s_eyeDistance;
	float w = m_textureResolution.x;
	float alpha = glm::radians(s_fovY * 0.5f);
	// variant 2: ray from near to outer bounds
	float s = w / (2.0f * s_near * tanf(alpha) );
	float imageOffset =  b * s;
	float pixelOffsetNear = ((b * s_near) / s_zRayStart) * s;
	float pixelOffsetFar  = ((b * s_near) / s_zRayEnd) * s;
	m_pComposeTexArrayShader->update("uPixelOffsetFar",  pixelOffsetFar);
	m_pComposeTexArrayShader->update("uPixelOffsetNear", pixelOffsetNear);
	//m_pComposeTexArrayShader->update("uImageOffset", imageOffset);
	
	if (ImGui::CollapsingHeader("Epipolar Info"))
    {
	ImGui::Value("Approx Distance to Ray Start", s_zRayStart);
	ImGui::Value("Approx Distance to Ray End", s_zRayEnd);
	ImGui::Value("Pixel Offset of Images", imageOffset);
	ImGui::Value("Pixel Offset at Ray Start", pixelOffsetNear);
	ImGui::Value("Pixel Offset at Ray End", pixelOffsetFar);
	ImGui::Value("Pixel Range of a Ray", pixelOffsetNear - pixelOffsetFar);
	}

	{bool hasDebugLayerDefine = false;bool hasDebugIdxDefine = false; for (auto e : m_shaderDefines) { hasDebugIdxDefine |= (e == "DEBUG_IDX"); hasDebugLayerDefine |= (e == "DEBUG_LAYER") ; } if ( hasDebugLayerDefine || hasDebugIdxDefine){
	static int debugIdx = -1;
	ImGui::SliderInt( "DEBUG LAYER", &debugIdx, -1, m_iNumLayers - 1 ); 
	if(hasDebugLayerDefine) {m_pComposeTexArrayShader->update("uDebugLayer", debugIdx);}
	if(hasDebugIdxDefine) {m_pComposeTexArrayShader->update("uDebugIdx", debugIdx);}
	}}
	
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

void CMainApplication::singlePassLayered()
{
	// clear output texture
	m_frame.Timings.getBack().beginTimerElapsed("Clear Array");
	clearOutputTexture( m_stereoOutputTextureArray );
	m_frame.Timings.getBack().stopTimerElapsed();

	// render stereo images in a single pass		
	m_frame.Timings.getBack().beginTimerElapsed("UVW");
	m_pUvwShader->update("view", s_view);
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pUvw->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().beginTimerElapsed("RaycastAndWrite");
	OPENGLCONTEXT->bindImageTextureToUnit(m_stereoOutputTextureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
	m_pRaycastStereoShader->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view) * s_screenToView);
	m_pRaycastStereoShader->update("uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view));
	m_pRaycastStereoShader->update("uProjection", s_perspective);
	m_pStereoRaycast->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	// put barrier
	glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT );

	// compose
	m_frame.Timings.getBack().beginTimerElapsed("Compose");
	m_pComposeTexArray->render();
	m_frame.Timings.getBack().stopTimerElapsed();
}

void CMainApplication::singlePassCompute()
{
	m_frame.Timings.getBack().beginTimerElapsed("UVW");
	m_pUvwShader->update("view", s_view);
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pUvw->render();
	m_frame.Timings.getBack().stopTimerElapsed();

	m_frame.Timings.getBack().beginTimerElapsed("RaycastAndWrite");
	m_pFBO_single->bind();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	m_pFBO_single_r->bind();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	OPENGLCONTEXT->bindImageTextureToUnit( m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), 0, GL_RGBA8, GL_READ_WRITE, 0, GL_FALSE); // stereo output
	OPENGLCONTEXT->bindImageTextureToUnit( m_pFBO_single->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), 1 , GL_RGBA8, GL_WRITE_ONLY); // color output
	OPENGLCONTEXT->bindImageTextureToUnit( m_pFBO_single->getDepthTextureHandle(), 2 , GL_RGBA8, GL_WRITE_ONLY);		 // first hit output

	glm::ivec3 localGroupSize = m_pStereoRaycastCompute->getLocalGroupSize();
	m_pRaycastStereoComputeShader->update("uScreenToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view) * s_screenToView);
	m_pRaycastStereoComputeShader->update("uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view));
	m_pRaycastStereoComputeShader->update("uProjection", s_perspective);

	int numGroupsX = ceil(m_textureResolution.x / localGroupSize.x);
	int numGroupsY = ceil(m_textureResolution.y / localGroupSize.y);

	m_pStereoRaycastCompute->dispatch( numGroupsX, numGroupsY );
	m_frame.Timings.getBack().stopTimerElapsed();
}


void CMainApplication::renderViews()
{
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
	OPENGLCONTEXT->bindFBO(0);
	glClearColor(1.0,1.0,1.0,0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0,0.0,0.0,0.0);

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

	m_frame.Timings.getBack().timestamp("Raycast Stereo");
	if (m_bUseCompute)
	{
		singlePassCompute();
	}
	else
	{
		singlePassLayered();
	}
	m_frame.Timings.getBack().timestamp("Finished");

	////////////////////////////////  Update DSSIM ///////////////////////////////// 
	updateError();

	// display fbo contents
	m_pShowTexShader->updateAndBindTexture("tex", 7, m_pFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)0, 0, (int) std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 8, m_pFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, 0, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 9, m_pFBO_single->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)0, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2);
	m_pShowTex->render();

	m_pShowTexShader->updateAndBindTexture("tex", 10, m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	m_pShowTex->setViewport((int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2);
	if (!m_bShowDebugView)
	{
		m_pShowTex->render();
	}
	else
	{
		m_pShowTexShader->updateAndBindTexture( "tex", 11, m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );

		if (!m_bDebugShowLayers)
		{
			static float level = 0.0;
			int numMipmaps = (int)(std::log(std::max(m_textureResolution.x, m_textureResolution.y)) / std::log(2.0f));

			ImGui::SliderFloat("Debug level", &level, 0.0f, (float)numMipmaps);
			m_pShowTexShader->update("level", level);
			m_pShowTex->setViewport((int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y,getResolution(m_pWindow).x) / 2);
			m_pShowTex->render();
			m_pShowTexShader->update("level", 0.0f);
		}
		else
		{
			m_pShowLayer->setViewport((int)std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x) / 2, (int)std::min(getResolution(m_pWindow).y, getResolution(m_pWindow).x) / 2);
			m_pShowLayerShader->update("layer", m_fDisplayedLayer);
			m_pShowLayer->render();
		}
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
	delete m_pRaycastStereoComputeShader;

	// reload shader defines
	loadShaderDefines();

	// reload shaders
	loadRaycastingShaders();
	
	// set ShaderProgram References
	m_pSimpleRaycast->setShaderProgram(m_pRaycastShader);
	m_pStereoRaycast->setShaderProgram(m_pRaycastStereoShader);
	m_pStereoRaycastCompute->setShaderProgram(m_pRaycastStereoComputeShader);
	m_pComposeTexArray->setShaderProgram(m_pComposeTexArrayShader);

	// update uniforms
	initTextureUniforms();
	initUniforms();

	DEBUGLOG->outdent();
}

void CMainApplication::rebuildLayerTexture()
{
	DEBUGLOG->log("Rebuilding Layer Texture"); DEBUGLOG->indent();
	// delete texture array
	std::vector<GLuint> textures;
	textures.push_back(m_stereoOutputTextureArray);
	glDeleteTextures(textures.size(), &textures[0]);

	// delete pixel buffer object
	glDeleteBuffers(1,&m_pboHandle);
	m_pboHandle = -1;
	m_texData.resize((int) m_textureResolution.x * (int) m_textureResolution.y * 4, 0.0f);

	OPENGLCONTEXT->updateCurrentTextureCache();
	
	initLayerTexture();

	// bind textures
	initTextureUnits();

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
	delete m_pFBO_error;

	// delete texture array
	std::vector<GLuint> textures;
	textures.push_back(m_stereoOutputTextureArray);
	glDeleteTextures(textures.size(), &textures[0]);

	// delete pixel buffer object
	glDeleteBuffers(1,&m_pboHandle);
	m_pboHandle = -1;
	m_texData.resize((int) m_textureResolution.x * (int) m_textureResolution.y * 4, 0.0f);
	
	OPENGLCONTEXT->updateCurrentTextureCache();

	initFramebuffers();
	
	// set Framebuffer References
	m_pUvw->setFrameBufferObject(m_pUvwFBO);
	m_pSimpleRaycast->setFrameBufferObject(m_pFBO);
	m_pStereoRaycast->setFrameBufferObject(m_pFBO_single);
	m_pComposeTexArray->setFrameBufferObject(m_pFBO_single_r);
	m_pError->setFrameBufferObject(m_pFBO_error);

	m_pUvw->setViewport(0,0, m_pUvwFBO->getWidth(),m_pUvwFBO->getHeight());
	m_pSimpleRaycast->setViewport(0,0, m_pFBO->getWidth(),m_pFBO->getHeight());
	m_pStereoRaycast->setViewport(0,0, m_pFBO_single->getWidth(),m_pFBO_single->getHeight());
	m_pComposeTexArray->setViewport(0,0, m_pFBO_single_r->getWidth(),m_pFBO_single_r->getHeight());
	m_pError->setViewport(0,0, m_pFBO_error->getWidth(),m_pFBO_error->getHeight());

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

		//////////////////////////////////////////////////////////////////////////////
		handleCsvProfiling();

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
	glDeleteTextures( 1, &m_volumeTexture);
	
	glDeleteTextures( 1, &m_cubemapTexture);
}

////////////////////////////// CONFIG HELPER ///////////////////////////////////////
CMainApplication::ConfigHelper::ConfigHelper(){
	shader.setHeaders(s_shader_defines_config);
}

void CMainApplication::ConfigHelper::copyShaderConfig(CMainApplication* mainApp)
{
	std::vector<std::string> row(s_shader_defines_config.size(), "0");
	for (unsigned int i = 0; i < s_shader_defines_config.size(); i++)
	{
		for( auto s : mainApp->m_shaderDefines )
		{
			if ( s_shader_defines_config[i] == s)
			{
				row[i] = "1";
			}
		}
	}
	shader.setData(row);
}

void CMainApplication::ConfigHelper::copyRenderConfig(CMainApplication* mainApp)
{
	std::vector<std::string> headers;
	std::vector<std::string> row;
	headers.push_back( "Num Layers" );
	row.push_back(std::to_string(mainApp->m_iNumLayers));
		
	headers.push_back( "Texture Size" );
	row.push_back(std::to_string((int)mainApp->m_textureResolution.x));
		
	headers.push_back( "Field of View" );
	row.push_back(std::to_string((int)s_fovY));
		
	headers.push_back( "Near" );
	row.push_back(std::to_string(s_near));

	headers.push_back( "Volume Name" );
	row.push_back( VolumePresets::s_models[mainApp->m_iActiveModel] );
	headers.push_back( "Volume Res X" );
	row.push_back(std::to_string( mainApp->m_volumeData.size_x ));
	headers.push_back( "Volume Res Y" );
	row.push_back(std::to_string( mainApp->m_volumeData.size_y ));
	headers.push_back( "Volume Res Z" );
	row.push_back(std::to_string( mainApp->m_volumeData.size_z ));

	render.setHeaders(headers);
	render.setData(row);
}

void CMainApplication::ConfigHelper::saveImages(CMainApplication* mainApp)
{
	TextureTools::saveTexture( prefix  + "L.png",		 mainApp->m_pFBO->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	TextureTools::saveTexture( prefix  + "R.png",		 mainApp->m_pFBO_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	TextureTools::saveTexture( prefix  + "R_SINGLE.png", mainApp->m_pFBO_single_r->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	TextureTools::saveTexture( prefix  + "DSSIM.png",    mainApp->m_pFBO_error->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
}

void CMainApplication::ConfigHelper::writeToFiles()
{
	render.writeToFile(  prefix + "RENDER"  + ".csv" );
	shader.writeToFile(  prefix + "SHADER"  + ".csv" );
	timings.writeToFile( prefix + "TIMINGS" + ".csv" );
	averages.writeToFile( prefix + "AVERAGES" + ".csv" );
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char *argv[])
{
	DEBUGLOG->setAutoPrint(true);

	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	pMainApplication->loadShaderDefines();

	pMainApplication->initSceneVariables();

	pMainApplication->handleVolume();

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