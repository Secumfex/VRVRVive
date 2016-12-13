/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>
#include <Volume/ChunkedRenderPass.h>

#include <UI/imgui/imgui.h>
#include <UI/imgui_impl_sdl_gl3.h>
#include <UI/Turntable.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <Volume/TransferFunction.h>

// openvr includes
#include <openvr.h>
#include <VR/OpenVRTools.h>

////////////////////// PARAMETERS /////////////////////////////
static float s_minValue = INT_MIN; // minimal value in data set; to be overwitten after import
static float s_maxValue = INT_MAX;  // maximal value in data set; to be overwitten after import

static bool  s_isRotating = false; 	// initial state for rotating animation
static float s_rayStepSize = 0.1f;  // ray sampling step size; to be overwritten after volume data import

static float s_rayParamEnd  = 1.0f; // parameter of uvw ray start in volume
static float s_rayParamStart= 0.0f; // parameter of uvw ray end   in volume

static const char* s_models[] = {"CT Head"};

static float s_windowingMinValue = -FLT_MAX / 2.0f;
static float s_windowingMaxValue = FLT_MAX / 2.0f;
static float s_windowingRange = FLT_MAX;

static const float MIRROR_SCREEN_FRAME_INTERVAL = 0.1f; // interval time (seconds) to mirror the screen (to avoid wait for vsync stalls)

static const std::vector<std::string> s_shaderDefines;

struct TFPoint{
	int v; // value 
	glm::vec4 col; // mapped color
};

static TransferFunction s_transferFunction;

static std::vector<float> s_fpsCounter = std::vector<float>(120);
static int s_curFPSidx = 0;

float s_near = 0.1f;
float s_far = 30.0f;
float s_fovY = 45.0f;
float s_nearH;
float s_nearW;

// matrices
glm::mat4 s_view;
glm::mat4 s_view_r;
glm::mat4 s_perspective;
glm::mat4 s_perspective_r;

glm::mat4 s_screenToView;   // const
glm::mat4 s_modelToTexture; // const

glm::mat4 s_translation;
glm::mat4 s_rotation;
glm::mat4 s_scale;

glm::vec3 s_volumeSize(1.0f, 0.886f, 1.0);

struct MatrixSet
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 perspective;
} matrices[2][2]; // left, right, firsthit, current
const int FIRST_HIT = 0;
const int CURRENT = 1;
const int LEFT = vr::Eye_Left;
const int RIGHT = vr::Eye_Right;


//////////////////////////////////////////////////////////////////////////////
////////////////////////////// MISC //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


void generateTransferFunction()
{
	s_transferFunction.getValues().clear();
	s_transferFunction.getColors().clear();
	s_transferFunction.getValues().push_back(58);
	s_transferFunction.getColors().push_back(glm::vec4(0.0/255.0f, 0.0/255.0f, 0.0/255.0f, 0.0/255.0f));
	s_transferFunction.getValues().push_back(539);
	s_transferFunction.getColors().push_back(glm::vec4(255.0/255.0f, 0.0/255.0f, 0.0/255.0f, 231.0/255.0f));
	s_transferFunction.getValues().push_back(572);
	s_transferFunction.getColors().push_back(glm::vec4(0.0 /255.0f, 74.0 /255.0f, 118.0 /255.0f, 64.0 /255.0f));
	s_transferFunction.getValues().push_back(1356);
	s_transferFunction.getColors().push_back(glm::vec4(0/255.0f, 11.0/255.0f, 112.0/255.0f, 0.0 /255.0f));
	s_transferFunction.getValues().push_back(1500);
	s_transferFunction.getColors().push_back(glm::vec4( 242.0/ 255.0, 212.0/ 255.0, 255.0/ 255.0, 255.0 /255.0f));
}

void updateTransferFunctionTex()
{
	s_transferFunction.updateTex(s_minValue, s_maxValue);
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

void profileFPS(float fps)
{
	s_fpsCounter[s_curFPSidx] = fps;
	s_curFPSidx = (s_curFPSidx + 1) % s_fpsCounter.size(); 
}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
	DEBUGLOG->setAutoPrint(true);

	// create window and opengl context
	auto window = generateWindow_SDL(1400,700);
	SDL_DisplayMode currentDisplayMode;
	SDL_GetWindowDisplayMode(window, &currentDisplayMode);

	DEBUGLOG->log("SDL Display Mode");
	{ DEBUGLOG->indent();
	DEBUGLOG->log("w: ", currentDisplayMode.w);
	DEBUGLOG->log("h: ", currentDisplayMode.h);
	DEBUGLOG->log("refresh rate: ", currentDisplayMode.refresh_rate);
	} DEBUGLOG ->outdent();

	// set refresh rate higher
	currentDisplayMode.refresh_rate = 90;
	SDL_SetWindowDisplayMode(window, &currentDisplayMode);

	// check
	SDL_GetWindowDisplayMode(window, &currentDisplayMode);
	DEBUGLOG->log("updated refresh rate: ", currentDisplayMode.refresh_rate);

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// INITIALIZE OPENVR   //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	OpenVRSystem ovr(s_near, s_far);
	
	if ( ovr.initialize() )
	{
		DEBUGLOG->log("Alright! OpenVR up and running!");
		ovr.initializeHMDMatrices();

		ovr.CreateShaders();
		ovr.SetupRenderModels();

		s_fovY = ovr.getFovY();
	}

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// VOLUME DATA LOADING //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// load data set: CT of a Head	// load into 3d texture
	std::string file = RESOURCES_PATH;
	file += std::string( "/volumes/CTHead/CThead");
	VolumeData<float> volumeData = Importer::load3DData<float>(file, 256, 256, 113, 2);
	GLuint volumeTextureCT = loadTo3DTexture<float>(volumeData, 8, GL_R16F, GL_RED, GL_FLOAT);

	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	DEBUGLOG->log("Loading Volume Data to 3D-Texture.");

	activateVolume<float>(volumeData);

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////////////// RENDERING  ///////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	/////////////////////     Scene / View Settings     //////////////////////////
	if (ovr.m_pHMD)
	{
		s_translation = glm::translate(glm::vec3(0.0f,1.0f,0.0f));
		s_scale = glm::scale(glm::vec3(0.5f,0.5f,0.5f));
	}
	else
	{	
		s_translation = glm::translate(glm::vec3(0.0f,0.0f,-3.0f));
		s_scale = glm::scale(glm::vec3(1.0f,1.0f,1.0f));
	}
	s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));


	//glm::mat4 model(1.0f)
	glm::vec4 eye(0.0f, 0.0f, 3.0f, 1.0f);
	glm::vec4 center(0.0f,0.0f,0.0f,1.0f);

	// use waitgetPoses to update matrices
	s_view = glm::lookAt(glm::vec3(eye), glm::vec3(center), glm::vec3(0.0f, 1.0f, 0.0f));
	s_view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(center), glm::vec3(0.0f, 1.0f, 0.0f));
	if (!ovr.m_pHMD)
	{
		s_perspective = glm::perspective(glm::radians(45.f), getResolution(window).x / (2.0f * getResolution(window).y), s_near, 10.f);
		s_perspective_r = glm::perspective(glm::radians(45.f), getResolution(window).x / (2.0f * getResolution(window).y), s_near, 10.f);
	}
	else
	{
		s_view = ovr.m_mat4eyePosLeft * s_view;
		s_view_r = ovr.m_mat4eyePosRight * s_view_r;
		s_perspective = ovr.m_mat4ProjectionLeft; 
		s_perspective_r = ovr.m_mat4ProjectionRight;
	}
	
	s_nearH = s_near * std::tanf( glm::radians(s_fovY/2.0f) );
	s_nearW = s_nearH * getResolution(window).x / (2.0f * getResolution(window).y);

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

	// create Volume and VertexGrid
	VolumeSubdiv volume(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 3);
	Quad quad;

	///////////////////////     UVW Map Renderpass     ///////////////////////////
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	ShaderProgram uvwShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag", s_shaderDefines); DEBUGLOG->outdent();
	uvwShaderProgram.update("model", s_translation * s_rotation * s_scale);
	uvwShaderProgram.update("view", s_view);
	uvwShaderProgram.update("projection", s_perspective);

	DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
	FrameBufferObject uvwFBO(getResolution(window).x/2, getResolution(window).y);
	uvwFBO.addColorAttachments(2); // front UVRs and back UVRs
	FrameBufferObject uvwFBO_r(getResolution(window).x/2, getResolution(window).y);
	uvwFBO_r.addColorAttachments(2); // front UVRs and back UVRs
	DEBUGLOG->outdent();
	
	RenderPass uvwRenderPass(&uvwShaderProgram, &uvwFBO);
	uvwRenderPass.addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	uvwRenderPass.addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
	uvwRenderPass.addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
	uvwRenderPass.addRenderable(&volume);

	///////////////////////   Ray-Casting Renderpass    //////////////////////////
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	ShaderProgram shaderProgram("/raycast/simpleRaycastChunked.vert", "/raycast/simpleRaycastLodDepthOcclusion.frag", s_shaderDefines); DEBUGLOG->outdent();
	shaderProgram.update("uStepSize", s_rayStepSize);
	shaderProgram.update("uViewport", glm::vec4(0,0,getResolution(window).x/2, getResolution(window).y));	
	shaderProgram.update("uResolution", glm::vec4(getResolution(window).x/2, getResolution(window).y,0,0));
	shaderProgram.update("uScreenToView", s_screenToView);

	// DEBUG
	generateTransferFunction();
	updateTransferFunctionTex();

	DEBUGLOG->log("FrameBufferObject Creation: ray casting"); DEBUGLOG->indent();
	//FrameBufferObject::s_internalFormat = GL_RGBA16F;
	FrameBufferObject FBO(shaderProgram.getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject FBO_r(shaderProgram.getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject FBO_front(shaderProgram.getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject FBO_front_r(shaderProgram.getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject::s_internalFormat = GL_RGBA;
	DEBUGLOG->outdent();

	// bind volume texture, back uvw textures, front uvws
	OPENGLCONTEXT->bindTextureToUnit(volumeTextureCT, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(s_transferFunction.getTextureHandle()						   , GL_TEXTURE1, GL_TEXTURE_1D); // transfer function

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(  GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

	OPENGLCONTEXT->bindTextureToUnit(FBO.getColorAttachmentTextureHandle(	  GL_COLOR_ATTACHMENT1), GL_TEXTURE6, GL_TEXTURE_2D); // left first hit map
	OPENGLCONTEXT->bindTextureToUnit(FBO_r.getColorAttachmentTextureHandle(	  GL_COLOR_ATTACHMENT1), GL_TEXTURE7, GL_TEXTURE_2D); // right first hit map

	OPENGLCONTEXT->bindTextureToUnit(FBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D); // left  raycasting result
	OPENGLCONTEXT->bindTextureToUnit(FBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE11, GL_TEXTURE_2D);// right raycasting result
	
	OPENGLCONTEXT->bindTextureToUnit(FBO_front.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE12, GL_TEXTURE_2D); // left  raycasting result (for display)
	OPENGLCONTEXT->bindTextureToUnit(FBO_front_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE13, GL_TEXTURE_2D);// right raycasting result (for display)

	shaderProgram.update("volume_texture", 0); // volume texture
	shaderProgram.update("transferFunctionTex", 1);

	RenderPass renderPass(&shaderProgram, &FBO);
	renderPass.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	//renderPass.setClearColor(0.1f,0.12f,0.15f,0.0f);
	renderPass.addRenderable(&quad);
	renderPass.addEnable(GL_DEPTH_TEST);
	renderPass.addDisable(GL_BLEND);

	RenderPass renderPass_r(&shaderProgram, &FBO_r);
	renderPass_r.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	//renderPass_r.setClearColor(0.1f,0.12f,0.15f,0.0f);
	renderPass_r.addRenderable(&quad);
	renderPass_r.addEnable(GL_DEPTH_TEST);
	renderPass_r.addDisable(GL_BLEND);
	
	///////////////////////   Chunked RenderPasses    //////////////////////////
	glm::ivec2 viewportSize = glm::ivec2(getResolution(window).x / 2, getResolution(window).y);
	glm::ivec2 chunkSize = glm::ivec2(96,96);
	ChunkedAdaptiveRenderPass chunkedRenderPass(
		&renderPass,
		viewportSize,
		chunkSize,
		8,
		6.0f
		);
	ChunkedAdaptiveRenderPass chunkedRenderPass_r(
		&renderPass_r,
		viewportSize,
		chunkSize,
		8,
		6.0f
		);

	//DEBUG
	//chunkedRenderPass.activateClearbits();
	//chunkedRenderPass_r.activateClearbits();

	///////////////////////   Occlusion Frustum Renderpass    //////////////////////////
	int occlusionBlockSize = 6;
	int vertexGridWidth  = (int) getResolution(window).x/2 / occlusionBlockSize;
	int vertexGridHeight = (int) getResolution(window).y   / occlusionBlockSize;
	VertexGrid vertexGrid(vertexGridWidth, vertexGridHeight, false, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(96, 96)); //dunno what is a good group size?
	ShaderProgram occlusionFrustumShader("/raycast/occlusionFrustum.vert", "/raycast/occlusionFrustum.frag", "/raycast/occlusionFrustum.geom", s_shaderDefines);
	//FrameBufferObject::s_internalFormat = GL_RGBA16F;
	FrameBufferObject occlusionFrustumFBO(   occlusionFrustumShader.getOutputInfoMap(), uvwFBO.getWidth(),   uvwFBO.getHeight() );
	FrameBufferObject occlusionFrustumFBO_r( occlusionFrustumShader.getOutputInfoMap(), uvwFBO_r.getWidth(), uvwFBO_r.getHeight() );
	FrameBufferObject::s_internalFormat = GL_RGBA;
	RenderPass occlusionFrustum(&occlusionFrustumShader, &occlusionFrustumFBO);
	occlusionFrustum.addRenderable(&vertexGrid);
	occlusionFrustum.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	occlusionFrustum.addEnable(GL_DEPTH_TEST);
	occlusionFrustum.addDisable(GL_BLEND);
	occlusionFrustumShader.update("uOcclusionBlockSize", occlusionBlockSize);
	occlusionFrustumShader.update("uGridSize", glm::vec4(vertexGridWidth, vertexGridHeight, 1.0f / (float) vertexGridWidth, 1.0f / vertexGridHeight));
	occlusionFrustumShader.update("uScreenToView", s_screenToView);

	OPENGLCONTEXT->bindTextureToUnit(occlusionFrustumFBO.getColorAttachmentTextureHandle(	  GL_COLOR_ATTACHMENT0), GL_TEXTURE8, GL_TEXTURE_2D); // left occlusion map
	OPENGLCONTEXT->bindTextureToUnit(occlusionFrustumFBO_r.getColorAttachmentTextureHandle(	  GL_COLOR_ATTACHMENT0), GL_TEXTURE9, GL_TEXTURE_2D); // right occlusion map
	
	DEBUGLOG->log("Render Configuration: Warp Rendering"); DEBUGLOG->indent();
	auto m_pWarpingShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/simpleWarp.frag");
	m_pWarpingShader->update( "blendColor", 1.0f );

	FrameBufferObject FBO_warp(m_pWarpingShader->getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject FBO_warp_r(m_pWarpingShader->getOutputInfoMap(), getResolution(window).x/2, getResolution(window).y);

	auto m_pWarpingThread = new RenderPass(m_pWarpingShader, &FBO_warp);
	m_pWarpingThread->addRenderable(&quad);
	m_pWarpingThread->addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	DEBUGLOG->outdent();

	OPENGLCONTEXT->bindTextureToUnit(FBO_warp.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE14, GL_TEXTURE_2D); // left  raycasting result (for display)
	OPENGLCONTEXT->bindTextureToUnit(FBO_warp_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE15, GL_TEXTURE_2D);// right raycasting result (for display)

	///////////////////////   Show Texture Renderpass    //////////////////////////
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);

	//////////////////////////////////////////////////////////////////////////////
	///////////////////////    GUI / USER INPUT   ////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// Setup ImGui binding
	ImGui_ImplSdlGL3_Init(window);
    bool show_test_window = true;

	Turntable turntable;
	double old_x;
    double old_y;

	//handles all the sdl events
	auto sdlEventHandler = [&](SDL_Event *event)
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

				double d_x = event->motion.x - old_x;
				double d_y = event->motion.y - old_y;

				if ( turntable.getDragActive() )
				{
					turntable.dragBy(d_x, d_y, s_view);
				}

				old_x = event->motion.x;
				old_y = event->motion.y;
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
					unsigned char pick_col[15];
					glReadPixels(old_x-2, getResolution(window).y-old_y, 5, 1, GL_RGB, GL_UNSIGNED_BYTE, pick_col);

					for (int i = 0; i < 15; i += 3)
					{
						DEBUGLOG->log("color: ", glm::vec3(pick_col[i + 0], pick_col[i + 1], pick_col[i+2]));
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

	static int  leftDebugView = 10;
	static int rightDebugView = 11;

	auto vrEventHandler = [&](const vr::VREvent_t & event)
	{
		switch( event.eventType )
		{
			case vr::VREvent_ButtonPress:
			{
				DEBUGLOG->log("button pressed dude");
				leftDebugView = leftDebugView - (leftDebugView % 2 );
				leftDebugView = max((leftDebugView + 2) % 16, 2);
				rightDebugView = leftDebugView + 1;
				break;
			}
		}
		return false;
	};

	std::string window_header = "Volume Renderer - OpenVR";
	SDL_SetWindowTitle(window, window_header.c_str() );

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);
	float elapsedTime = 0.0;
	float mirrorScreenTimer = 0.0f;
	while (!shouldClose(window))
	{
		////////////////////////////////    EVENTS    ////////////////////////////////
		pollSDLEvents(window, sdlEventHandler);
		ovr.PollVREvents(vrEventHandler);

		//++++++++++++++ DEBUG 
		// Process SteamVR controller state
		//for (vr::TrackedDeviceIndex_t unDevice = 0; unDevice < vr::k_unMaxTrackedDeviceCount; unDevice++)
		//{
		//	vr::VRControllerState_t state;
		//	if (ovr.m_pHMD && ovr.m_pHMD->GetControllerState(unDevice, &state))
		//	{
		//		static auto lastPacketNum = state.unPacketNum;
		//		if (state.ulButtonPressed != 0 && lastPacketNum !=state.unPacketNum) //is pressed
		//		{
		//			leftDebugView = leftDebugView - (leftDebugView % 2 );
		//			leftDebugView = max((leftDebugView + 2) % 16, 2);
		//			rightDebugView = leftDebugView + 1;
		//		}
		//		lastPacketNum = state.unPacketNum;
		//	}
		//}
		//++++++++++++++

		////////////////////////////////     GUI      ////////////////////////////////
        ImGuiIO& io = ImGui::GetIO();
		profileFPS(ImGui::GetIO().Framerate);
		ImGui_ImplSdlGL3_NewFrame(window);
	
		ImGui::Value("FPS", io.Framerate);
		mirrorScreenTimer += io.DeltaTime;
		elapsedTime += io.DeltaTime;

		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
		ImGui::PlotLines("FPS", &s_fpsCounter[0], s_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
		ImGui::PopStyleColor();
	
		ImGui::Columns(2, "mycolumns2", true);
        ImGui::Separator();
		bool changed = false;
		for (unsigned int n = 0; n < s_transferFunction.getValues().size(); n++)
        {
			changed |= ImGui::DragInt(("V" + std::to_string(n)).c_str(), &s_transferFunction.getValues()[n], 1.0, s_minValue, s_maxValue);
			ImGui::NextColumn();
			changed |= ImGui::ColorEdit4(("C" + std::to_string(n)).c_str(), &s_transferFunction.getColors()[n][0]);
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
        
		ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume
		ImGui::PopItemWidth();
		
		ImGui::Separator();

		static float lodScale = 2.0f;
		static float lodBias  = 0.25f;
		ImGui::DragFloat("Lod Scale", &lodScale, 0.1f,0.0f,20.0f);
		ImGui::DragFloat("Lod Bias", &lodBias, 0.01f,0.0f,1.2f);
		
		ImGui::Separator();
		ImGui::Columns(2);
		static bool profiler_visible, profiler_visible_r = false;
		ImGui::Checkbox("Chunk Perf Profiler Left", &profiler_visible);
		if (profiler_visible) { chunkedRenderPass.imguiInterface(&profiler_visible); };
		ImGui::NextColumn();
		ImGui::Checkbox("Chunk Perf Profiler Right", &profiler_visible_r);
		if (profiler_visible_r) { chunkedRenderPass_r.imguiInterface(&profiler_visible_r); };
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
		//static bool pauseFirstHitUpdates = false;
		static bool useOcclusionMap = true;
		//ImGui::Checkbox("Pause First Hit Updated", &pauseFirstHitUpdates);
		ImGui::Checkbox("Use Occlusion Map", &useOcclusionMap);
		

        //////////////////////////////////////////////////////////////////////////////

		///////////////////////////// MATRIX UPDATING ///////////////////////////////
		if (s_isRotating) // update s_view matrix
		{
			s_rotation = glm::rotate(glm::mat4(1.0f), (float) io.DeltaTime, glm::vec3(0.0f, 1.0f, 0.0f) ) * s_rotation;
		}

		// use waitgetPoses to update matrices, or just use regular stuff
		if (!ovr.m_pHMD)
		{
			s_view = glm::lookAt(glm::vec3(eye), glm::vec3(center), glm::vec3(0.0f, 1.0f, 0.0f));
			s_view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(center), glm::vec3(0.0f, 1.0f, 0.0f));
		}
		else
		{
			ovr.updateTrackedDevicePoses();
			s_view = ovr.m_mat4eyePosLeft * ovr.m_mat4HMDPose;
			s_view_r = ovr.m_mat4eyePosRight * ovr.m_mat4HMDPose;
		}

		// Update Matrices
		// compute current auxiliary matrices
		glm::mat4 model = s_translation * turntable.getRotationMatrix() * s_rotation * s_scale;

		//++++++++++++++ DEBUG
		static bool animateView = false;
		ImGui::Checkbox("Animate View", &animateView);
		if (animateView)
		{
			glm::vec4 warpCenter  = glm::vec4(sin(elapsedTime*2.0)*0.25f, cos(elapsedTime*2.0)*0.125f, 0.0f, 1.0f);
			glm::vec4 warpEye  = eye + glm::vec4(-sin(elapsedTime*1.0)*0.125f, -cos(elapsedTime*2.0)*0.125f, 0.0f, 1.0f);
			s_view   = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
			s_view_r = glm::lookAt(glm::vec3(warpEye) +  glm::vec3(0.15,0.0,0.0), glm::vec3(warpCenter), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
		}
		//++++++++++++++ DEBUG

		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////

		/************* update color mapping parameters ******************/
		// ray start/end parameters
		shaderProgram.update("uStepSize", s_rayStepSize); 	  // ray step size
		shaderProgram.update("uLodBias", lodBias);
		shaderProgram.update("uLodDepthScale", lodScale);  

		// color mapping parameters
		shaderProgram.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		shaderProgram.update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window
		//////////////////////////////////////////////////////////////////////////////
		
		////////////////////////////////  RENDERING //// /////////////////////////////
		glDisable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
		// check for finished left/right images, copy to Front FBOs
		if (chunkedRenderPass.isFinished())
		{
			copyFBOContent(&FBO, &FBO_front, GL_COLOR_BUFFER_BIT);
		}
		if (chunkedRenderPass_r.isFinished())
		{
			copyFBOContent(&FBO_r, &FBO_front_r, GL_COLOR_BUFFER_BIT);
		}

		//%%%%%%%%%%%% render left image
		if (chunkedRenderPass.isFinished())
		{
			matrices[LEFT][FIRST_HIT] = matrices[LEFT][CURRENT]; // first hit map was rendered with last "current" matrices
			matrices[LEFT][CURRENT].model = model; // overwrite with current  matrices
			matrices[LEFT][CURRENT].view = s_view;
			matrices[LEFT][CURRENT].perspective = s_perspective; 

			MatrixSet& firstHit = matrices[LEFT][FIRST_HIT]; /// convenient access
			MatrixSet& current = matrices[LEFT][CURRENT];
			
			//update raycasting matrices for next iteration	// for occlusion frustum
			glm::mat4 firstHitViewToCurrentView  = current.view * (current.model * glm::inverse(firstHit.model)) * glm::inverse(firstHit.view);
			glm::mat4 firstHitViewToTexture = s_modelToTexture * glm::inverse(firstHit.model) * glm::inverse(firstHit.view);
			
			// uvw maps
			uvwRenderPass.setFrameBufferObject( &uvwFBO );
			uvwShaderProgram.update("view", current.view);
			uvwShaderProgram.update("model", current.model);
			uvwShaderProgram.update("projection", current.perspective);
			uvwRenderPass.render();

			// occlusion maps 
			occlusionFrustum.setFrameBufferObject( &occlusionFrustumFBO );
			occlusionFrustumShader.update("first_hit_map", 6); // left first hit map
			occlusionFrustumShader.update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
			occlusionFrustumShader.update("uFirstHitViewToTexture", firstHitViewToTexture);
			occlusionFrustumShader.update("uProjection", current.perspective);
			occlusionFrustum.render();
		}

		// raycasting (chunked)
		shaderProgram.update( "uScreenToTexture", s_modelToTexture * glm::inverse( matrices[LEFT][CURRENT].model ) * glm::inverse( matrices[LEFT][CURRENT].view ) * s_screenToView );
		shaderProgram.update( "uViewToTexture", s_modelToTexture * glm::inverse(matrices[LEFT][CURRENT].model) * glm::inverse(matrices[LEFT][CURRENT].view) );
		shaderProgram.update( "uProjection", matrices[LEFT][CURRENT].perspective);
		shaderProgram.update( "back_uvw_map",  2 );
		shaderProgram.update( "front_uvw_map", 4 );
		shaderProgram.update( "occlusion_map", (useOcclusionMap) ? 8 : 2 );

		chunkedRenderPass.render(); 
		
		//+++++++++ DEBUG  +++++++++++++++++++++++++++++++++++++++++++ 
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		
		//%%%%%%%%%%%% render right image
		// uvw maps
		if (chunkedRenderPass_r.isFinished())
		{
			matrices[RIGHT][FIRST_HIT] = matrices[RIGHT][CURRENT]; // first hit map was rendered with last "current" matrices
			matrices[RIGHT][CURRENT].model = model; // overwrite with current  matrices
			matrices[RIGHT][CURRENT].view = s_view_r;
			matrices[RIGHT][CURRENT].perspective = s_perspective_r; 

			MatrixSet& firstHit = matrices[RIGHT][FIRST_HIT]; /// convenient access
			MatrixSet& current = matrices[RIGHT][CURRENT];

			//update raycasting matrices for next iteration	// for occlusion frustum
			glm::mat4 firstHitViewToCurrentView  = current.view * (current.model * glm::inverse(firstHit.model)) * glm::inverse(firstHit.view);
			glm::mat4 firstHitViewToTexture = s_modelToTexture * glm::inverse(firstHit.model) * glm::inverse(firstHit.view);

			uvwRenderPass.setFrameBufferObject( &uvwFBO_r );
			uvwShaderProgram.update( "view", current.view );
			uvwShaderProgram.update( "model", current.model );
			uvwShaderProgram.update( "projection", current.perspective );
			uvwRenderPass.render();
		
			// occlusion maps 
			occlusionFrustum.setFrameBufferObject( &occlusionFrustumFBO_r );
			occlusionFrustumShader.update("first_hit_map", 7); // right first hit map
			occlusionFrustumShader.update("uFirstHitViewToCurrentView", firstHitViewToCurrentView);
			occlusionFrustumShader.update("uFirstHitViewToTexture", firstHitViewToTexture);
			occlusionFrustumShader.update("uProjection", current.perspective);
			occlusionFrustum.render();
		}

		// raycasting (chunked)
		shaderProgram.update("uScreenToTexture", s_modelToTexture * glm::inverse( matrices[RIGHT][CURRENT].model ) * glm::inverse( matrices[RIGHT][CURRENT].view ) * s_screenToView );
		shaderProgram.update("uViewToTexture", s_modelToTexture * glm::inverse(matrices[RIGHT][CURRENT].model) * glm::inverse(matrices[RIGHT][CURRENT].view) );
		shaderProgram.update("uProjection", matrices[RIGHT][CURRENT].perspective);
		shaderProgram.update("back_uvw_map",  3);
		shaderProgram.update("front_uvw_map", 5);
		shaderProgram.update("occlusion_map", (useOcclusionMap) ? 9 : 5);

		//renderPass_r.render();
		chunkedRenderPass_r.render();

		//%%%%%%%%%%%% Image Warping
		// warp left
		m_pWarpingThread->setFrameBufferObject( &FBO_warp );
		m_pWarpingShader->update( "tex", 12 ); // last result left
		m_pWarpingShader->update( "oldView", matrices[LEFT][FIRST_HIT].view ); // update with old view
		m_pWarpingShader->update( "newView", s_view ); // most current view
		m_pWarpingShader->update( "projection",  matrices[LEFT][FIRST_HIT].perspective ); 
		m_pWarpingThread->render();
		
		// warp right
		m_pWarpingThread->setFrameBufferObject( &FBO_warp_r );
		m_pWarpingShader->update( "tex", 13 ); // last result right
		m_pWarpingShader->update( "oldView", matrices[RIGHT][FIRST_HIT].view ); // update with old view
		m_pWarpingShader->update( "newView", s_view_r); // most current view
		m_pWarpingShader->update( "projection",  matrices[RIGHT][FIRST_HIT].perspective ); 
		m_pWarpingThread->render();

		//%%%%%%%%%%%% Submit/Display images
		if ( ovr.m_pHMD ) // submit images only when finished
		{
			if ( chunkedRenderPass.isFinished() )
			{
				FBO_front.bind();
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				//ovr.renderModels(vr::Eye_Left); //TODO find out what dirty states are caused
			}

			if ( chunkedRenderPass_r.isFinished() )
			{
				FBO_front_r.bind();
				OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
				//ovr.renderModels(vr::Eye_Right);
			}
			OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);

			ovr.submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + leftDebugView], vr::Eye_Left);
			ovr.submitImage( OPENGLCONTEXT->cacheTextures[GL_TEXTURE0 + rightDebugView], vr::Eye_Right);
		}
		
		if (mirrorScreenTimer > MIRROR_SCREEN_FRAME_INTERVAL || !ovr.m_pHMD)
		{
			//if (chunkedRenderPass.isFinished())
			{
				showTexShader.update("tex", leftDebugView);
				showTex.setViewport(0,0,(int) getResolution(window).x/2, (int) getResolution(window).y);
				showTex.render();
			}
			//if (chunkedRenderPass_r.isFinished())
			{
				showTexShader.update("tex", rightDebugView);
				showTex.setViewport((int) getResolution(window).x/2,0,(int) getResolution(window).x/2, (int) getResolution(window).y);
				showTex.render();
			}
			//////////////////////////////////////////////////////////////////////////////
			ImGui::Render();
			SDL_GL_SwapWindow( window ); // swap buffers

			mirrorScreenTimer = 0.0f;
		}
		else
		{
			glFlush(); // just Flush
		}
	}
	
	ImGui_ImplSdlGL3_Shutdown();
	ovr.shutdown();
	destroyWindow(window);
	SDL_Quit();

	return 0;
}