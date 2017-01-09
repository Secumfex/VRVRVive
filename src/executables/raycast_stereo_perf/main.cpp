/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>

#include "UI/imgui/imgui.h"
#include <UI/imgui_impl_sdl_gl3.h>
#include <UI/imguiTools.h>
#include <UI/Turntable.h>

#include <Volume/TransferFunction.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <algorithm>

////////////////////// PARAMETERS /////////////////////////////
static float s_minValue = (float) INT_MIN; // minimal value in data set; to be overwitten after import
static float s_maxValue = (float) INT_MAX;  // maximal value in data set; to be overwitten after import

static bool  s_isRotating = false; 	// initial state for rotating animation
static float s_rayStepSize = 0.1f;  // ray sampling step size; to be overwritten after volume data import

static float s_rayParamEnd  = 1.0f; // parameter of uvw ray start in volume
static float s_rayParamStart= 0.0f; // parameter of uvw ray end   in volume

static float s_eyeDistance = 0.065f;

static const char* s_models[] = {"CT Head"};

static float s_windowingMinValue = -FLT_MAX / 2.0f;
static float s_windowingMaxValue = FLT_MAX / 2.0f;
static float s_windowingRange = FLT_MAX;

static TransferFunction s_transferFunction;

static float s_lodMaxLevel = 4.0f;
static float s_lodBegin = 0.3f;
static float s_lodRange = 4.0f;

static std::vector<float> s_fpsCounter = std::vector<float>(120);
static int s_curFPSidx = 0;

const char* SHADER_DEFINES[] = {
	"RANDOM_OFFSET",
	//"ARRAY_TEXTURE"
};
static std::vector<std::string> s_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES));

static const int NUM_LAYERS = 16;
const glm::vec2 TEXTURE_RESOLUTION = glm::vec2(800, 800);

static std::vector<float> s_texData((int) TEXTURE_RESOLUTION.x * (int) TEXTURE_RESOLUTION.y * NUM_LAYERS * 4, 0.0f);
static GLuint s_pboHandle = -1; // PBO 

float s_near = 0.1f;
float s_far = 30.0f;
float s_fovY = 45.0f;
float s_nearH;
float s_nearW;

glm::mat4 s_view;
glm::mat4 s_view_r;
glm::mat4 s_perspective;
//glm::mat4 s_perspective_r;

glm::mat4 s_screenToView;   // const
glm::mat4 s_modelToTexture; // const

glm::mat4 s_translation;
glm::mat4 s_rotation;
glm::mat4 s_scale;

glm::vec3 s_volumeSize(1.0f, 0.886f, 1.0);

const int LEFT = 0;
const int RIGHT = 1;

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

void clearOutputTexture(GLuint texture)
{
	if(s_pboHandle == -1)
	{
		glGenBuffers(1,&s_pboHandle);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) TEXTURE_RESOLUTION.x * (int) TEXTURE_RESOLUTION.y * NUM_LAYERS * 4, &s_texData[0], GL_STATIC_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	
	OPENGLCONTEXT->activeTexture(GL_TEXTURE6);
	OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D_ARRAY);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, (int) TEXTURE_RESOLUTION.x, (int) TEXTURE_RESOLUTION.y, NUM_LAYERS, GL_RGBA, GL_FLOAT, 0); // last param 0 => will read from pbo
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void generateTransferFunction()
{
	s_transferFunction.getValues().clear();
	s_transferFunction.getColors().clear();
	s_transferFunction.getValues().push_back(58);
	s_transferFunction.getColors().push_back(glm::vec4(0.0 / 255.0f, 0.0 / 255.0f, 0.0 / 255.0f, 0.0 / 255.0f));
	s_transferFunction.getValues().push_back(539);
	s_transferFunction.getColors().push_back(glm::vec4(255.0 / 255.0f, 0.0 / 255.0f, 0.0 / 255.0f, 231.0 / 255.0f));
	s_transferFunction.getValues().push_back(572);
	s_transferFunction.getColors().push_back(glm::vec4(0.0 / 255.0f, 74.0 / 255.0f, 118.0 / 255.0f, 64.0 / 255.0f));
	s_transferFunction.getValues().push_back(1356);
	s_transferFunction.getColors().push_back(glm::vec4(0 / 255.0f, 11.0 / 255.0f, 112.0 / 255.0f, 0.0 / 255.0f));
	s_transferFunction.getValues().push_back(1500);
	s_transferFunction.getColors().push_back(glm::vec4(242.0 / 255.0, 212.0 / 255.0, 255.0 / 255.0, 255.0 / 255.0f));
}

void updateTransferFunctionTex()
{
	s_transferFunction.updateTex((int)s_minValue, (int)s_maxValue);
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

int main(int argc, char *argv[])
{
	DEBUGLOG->setAutoPrint(true);

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////// VOLUME DATA LOADING //////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// create window and opengl context
	auto window = generateWindow_SDL(TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);

	// load data set: CT of a Head	// load into 3d texture
	std::string file = RESOURCES_PATH + std::string( "/volumes/CTHead/CThead");
	VolumeData<float> volumeDataCTHead = Importer::load3DData<float>(file, 256, 256, 113, 2);
	activateVolume<float>(volumeDataCTHead);
	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	DEBUGLOG->log("Loading Volume Data to 3D-Texture.");
	GLuint volumeTextureCT = loadTo3DTexture<float>(volumeDataCTHead, 99, GL_R16F, GL_RED, GL_FLOAT);
	checkGLError(true);

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////////////// RENDERING  ///////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	/////////////////////     Scene / View Settings     //////////////////////////
	s_nearH = s_near * std::tanf(glm::radians(s_fovY / 2.0f));
	s_nearW = s_nearH * TEXTURE_RESOLUTION.x / (TEXTURE_RESOLUTION.y);

	s_translation = glm::translate(glm::vec3(0.0f, 0.0f, -3.0f));
	s_rotation = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));

	glm::vec4 eye(0.0f, 0.0f, 3.0f, 1.0f);
	glm::vec4 center(0.0f,0.0f,0.0f,1.0f);
	s_view   = glm::lookAt(glm::vec3(eye) - glm::vec3(s_eyeDistance/2.0f,0.0f,0.0f), glm::vec3(center) - glm::vec3(s_eyeDistance / 2.0f, 0.0f, 0.0f), glm::vec3(0,1,0));
	s_view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(s_eyeDistance/2.0f,0.0f,0.0f), glm::vec3(center) + glm::vec3(s_eyeDistance / 2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	s_perspective = glm::perspective(glm::radians(45.f), TEXTURE_RESOLUTION.x / TEXTURE_RESOLUTION.y, s_near, 10.f);
	glm::mat4 textureToProjection_r = s_perspective * s_view_r;

	static float s_nearH = s_near  * std::tanf( s_fovY );
	static float s_nearW = s_nearH * TEXTURE_RESOLUTION.x / (TEXTURE_RESOLUTION.y);

	// constant
	s_screenToView = glm::scale(glm::vec3(s_nearW, s_nearH, s_near)) *
		glm::inverse(
			glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) *
			glm::scale(glm::vec3(0.5f, 0.5f, 0.5f))
			);

	// constant
	s_modelToTexture = glm::mat4( // swap components
		glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), // column 1
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), // column 2
		glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),//column 3
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) //column 4 
		* glm::inverse(glm::scale(2.0f * s_volumeSize)) // moves origin to front left
		* glm::translate(glm::vec3(s_volumeSize.x, s_volumeSize.y, -s_volumeSize.z));

	// create Volume and VertexGrid
	VolumeSubdiv volume(1.0f, 0.886f, 1.0f, 3);
	VertexGrid vertexGrid(getResolution(window).x/2, getResolution(window).y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(-1));
	VertexGrid vertexGrid_coarse(getResolution(window).x/2, getResolution(window).y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(16,16));
	Quad quad;
	Grid grid(100, 100, 0.1f, 0.1f);

	///////////////////////     UVW Map Renderpass     ///////////////////////////
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	ShaderProgram uvwShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag"); DEBUGLOG->outdent();
	uvwShaderProgram.update("model", s_translation * s_rotation * s_scale);
	uvwShaderProgram.update("view", s_view);
	uvwShaderProgram.update("projection", s_perspective);

	DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
	FrameBufferObject uvwFBO(TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	uvwFBO.addColorAttachments(2); // front UVRs and back UVRs
	DEBUGLOG->outdent(); 

	FrameBufferObject uvwFBO_r(TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	uvwFBO_r.addColorAttachments(2); DEBUGLOG->outdent(); // front UVRs and back UVRs

	RenderPass uvwRenderPass(&uvwShaderProgram, &uvwFBO);
	uvwRenderPass.addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	uvwRenderPass.addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
	uvwRenderPass.addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
	uvwRenderPass.addRenderable(&volume);

	// generate Transferfunction
	generateTransferFunction();
	updateTransferFunctionTex();

	///////////////////////   Simple Ray-Casting Renderpass    //////////////////////////
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	ShaderProgram simpleRaycastShader("/raycast/simpleRaycast.vert", "/raycast/simpleRaycastLodDepth.frag"); DEBUGLOG->outdent();
	simpleRaycastShader.update("uStepSize", s_rayStepSize);
		
	DEBUGLOG->log("Shader Compilation: ray casting shader - single pass stereo"); DEBUGLOG->indent();
	ShaderProgram stereoRaycastShader("/raycast/simpleRaycast.vert", "/raycast/simpleRaycastLodDepthStereo.frag"); DEBUGLOG->outdent();
	stereoRaycastShader.update("uStepSize", s_rayStepSize);
	stereoRaycastShader.update("uBlockWidth", NUM_LAYERS);

	//DEBUGLOG->log("FrameBufferObject Creation: ray casting"); DEBUGLOG->indent();
	FrameBufferObject FBO(simpleRaycastShader.getOutputInfoMap(), (int)TEXTURE_RESOLUTION.x, (int)TEXTURE_RESOLUTION.y);
	FrameBufferObject FBO_r(simpleRaycastShader.getOutputInfoMap(), (int)TEXTURE_RESOLUTION.x, (int)TEXTURE_RESOLUTION.y);
	FrameBufferObject FBO_single(stereoRaycastShader.getOutputInfoMap(), (int)TEXTURE_RESOLUTION.x, (int)TEXTURE_RESOLUTION.y);
	//DEBUGLOG->outdent();

	// generate and bind right s_view image texture
	GLuint stereoOutputTextureArray = createTextureArray((int)TEXTURE_RESOLUTION.x, (int)TEXTURE_RESOLUTION.y, NUM_LAYERS, GL_RGBA16F);

	// bind volume texture, back uvw textures, front uvws
	OPENGLCONTEXT->bindTextureToUnit(volumeTextureCT, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(s_transferFunction.getTextureHandle(), GL_TEXTURE1, GL_TEXTURE_1D);

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE2, GL_TEXTURE_2D); // left uvw back
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D); // left uvw front

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE3, GL_TEXTURE_2D); // right uvw back
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE5, GL_TEXTURE_2D); // right uvw front

	OPENGLCONTEXT->bindImageTextureToUnit(stereoOutputTextureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
	OPENGLCONTEXT->bindTextureToUnit(stereoOutputTextureArray, GL_TEXTURE6, GL_TEXTURE_2D_ARRAY); // for display

	// DEBUG
	clearOutputTexture( stereoOutputTextureArray );

	// atomic counters
	GLuint atomicsBuffer = bufferData<GLuint>(std::vector<GLuint>(3,0), GL_ATOMIC_COUNTER_BUFFER, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicsBuffer); // important

	simpleRaycastShader.update("volume_texture", 0); // volume texture
	simpleRaycastShader.update("transferFunctionTex", 1);
	simpleRaycastShader.update("back_uvw_map", 2);
	simpleRaycastShader.update("front_uvw_map", 4);

	stereoRaycastShader.update("volume_texture", 0); // volume texture
	stereoRaycastShader.update("transferFunctionTex", 1);
	stereoRaycastShader.update("back_uvw_map", 2);
	stereoRaycastShader.update("front_uvw_map", 4);

	RenderPass stereoRaycast(&stereoRaycastShader, &FBO_single);
	stereoRaycast.addRenderable(&quad);
	stereoRaycast.addDisable(GL_BLEND);
	stereoRaycast.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	stereoRaycast.addEnable(GL_DEPTH_TEST);

	// ray casting render pass
	RenderPass simpleRaycast(&simpleRaycastShader, &FBO);
	simpleRaycast.addRenderable(&quad);
	simpleRaycast.addDisable(GL_BLEND);
	simpleRaycast.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	simpleRaycast.addEnable(GL_DEPTH_TEST);

	///////////////////////   Back-To-Front Compose Texture Array Renderpass    //////////////////////////
	ShaderProgram composeTexArrayShader("/screenSpace/fullscreen.vert", "/screenSpace/composeTextureArray.frag", s_shaderDefines);
	FrameBufferObject FBO_single_r(composeTexArrayShader.getOutputInfoMap(), (int)TEXTURE_RESOLUTION.x, (int)TEXTURE_RESOLUTION.y);
	RenderPass composeTexArray(&composeTexArrayShader, &FBO_single_r);
	composeTexArray.addRenderable(&quad);
	composeTexArray.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	composeTexArrayShader.update( "tex", 6); // output texture
	composeTexArrayShader.update( "uBlockWidth", NUM_LAYERS ); // output texture
	
	///////////////////////   Show Texture Renderpass    //////////////////////////
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", s_shaderDefines);
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);

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
			case SDLK_r:
				static int activeRenderable = 3;
				simpleRaycast.clearRenderables();
				activeRenderable = (activeRenderable + 1) % 4;
				switch (activeRenderable)
				{
				case 0: // Quad
					simpleRaycast.addRenderable(&quad);
					DEBUGLOG->log("renderable: Quad");
					break;
				case 1: // Vertex Grid
					simpleRaycast.addRenderable(&vertexGrid);
					DEBUGLOG->log("renderable: Vertex Grid");
					break;
				case 2: // Vertex Grid (Coarse)
					simpleRaycast.addRenderable(&vertexGrid_coarse);
					DEBUGLOG->log("renderable: Vertex Grid (Coarse)");
					break;
				case 3: // grid
					simpleRaycast.addRenderable(&grid);
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

	std::string window_header = "Stereo Volume Renderer - Performance Tests";
	SDL_SetWindowTitle(window, window_header.c_str() );
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	//++++++++++++++ DEBUG
	//++++++++++++++ DEBUG
	
	double elapsedTime = 0.0;
	while (!shouldClose(window))
	{
		////////////////////////////////    EVENTS    ////////////////////////////////
		pollSDLEvents(window, sdlEventHandler);
		profileFPS((float) (ImGui::GetIO().Framerate));

		////////////////////////////////     GUI      ////////////////////////////////
        ImGuiIO& io = ImGui::GetIO();
		ImGui_ImplSdlGL3_NewFrame(window); // tell ImGui a new frame is being rendered

		ImGui::Value("FPS", (float) (io.Framerate));
		elapsedTime += io.DeltaTime;

		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
		ImGui::PlotLines("FPS", &s_fpsCounter[0], s_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
		ImGui::PopStyleColor();

		ImGui::DragFloat("eye distance", &s_eyeDistance, 0.01f, 0.0f, 2.0f);
	
		ImGui::Columns(2, "mycolumns2", true);
        ImGui::Separator();
		bool changed = false;
		for (unsigned int n = 0; n < s_transferFunction.getValues().size(); n++)
		{
			changed |= ImGui::DragInt(("V" + std::to_string(n)).c_str(), &s_transferFunction.getValues()[n], 1.0f, (int)s_minValue, (int)s_maxValue);
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
        
		ImGui::DragFloat("Lod Max Level", &s_lodMaxLevel, 0.1f, 0.0f, 8.0f);
		ImGui::DragFloat("Lod Begin", &s_lodBegin, 0.01f, 0.0f, s_far);
		ImGui::DragFloat("Lod Range", &s_lodRange, 0.01f, 0.0f, std::max(0.1f, s_far - s_lodBegin));

		ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume

		static bool s_writeStereo = true;
		ImGui::Checkbox("write stereo", &s_writeStereo); // enable/disable single pass stereo
	
		static bool s_showSingleLayer = false;
		ImGui::Checkbox("Show Single Layer", &s_showSingleLayer);
		static float s_displayedLayer = 0.0f;
		ImGui::SliderFloat("Displayed Layer", &s_displayedLayer, 0.0f, NUM_LAYERS-1);
		
		ImGui::PopItemWidth();
        //////////////////////////////////////////////////////////////////////////////

		///////////////////////////// MATRIX UPDATING ///////////////////////////////
		if (s_isRotating) // update s_view matrix
		{
			s_rotation = glm::rotate(glm::mat4(1.0f), (float)io.DeltaTime, glm::vec3(0.0f, 1.0f, 0.0f)) * s_rotation;
		}

		s_view = glm::lookAt(glm::vec3(eye) - glm::vec3(s_eyeDistance/2.0,0.0,0.0), glm::vec3(center) - glm::vec3(s_eyeDistance / 2.0, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		s_view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(s_eyeDistance/2.0,0.0f,0.0f), glm::vec3(center) + glm::vec3(s_eyeDistance / 2.0, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		glm::mat4 model = s_translation * turntable.getRotationMatrix() * s_rotation * s_scale; // auxiliary
		textureToProjection_r = s_perspective * s_view_r * model * glm::inverse( s_modelToTexture ); // texture to model
		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
		// update s_view related uniforms
		uvwShaderProgram.update("model", model);

		stereoRaycastShader.update( "uTextureToProjection_r", textureToProjection_r ); //since position map contains s_view space coords

		/************* update color mapping parameters ******************/
		// ray start/end parameters
		stereoRaycastShader.update("uStepSize", s_rayStepSize); 	  // ray step size
		stereoRaycastShader.update("uLodMaxLevel", s_lodMaxLevel);
		stereoRaycastShader.update("uLodBegin", s_lodBegin);
		stereoRaycastShader.update("uLodRange", s_lodRange);

		simpleRaycastShader.update("uStepSize", s_rayStepSize); 	  // ray step size
		simpleRaycastShader.update("uLodMaxLevel", s_lodMaxLevel);
		simpleRaycastShader.update("uLodBegin", s_lodBegin);
		simpleRaycastShader.update("uLodRange", s_lodRange);

		// color mapping parameters
		stereoRaycastShader.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		stereoRaycastShader.update("uWindowingRange", s_windowingMaxValue - s_windowingMinValue); // full range of values in window

		simpleRaycastShader.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		simpleRaycastShader.update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

		/************* update experimental  parameters ******************/
		stereoRaycastShader.update("uWriteStereo", s_writeStereo); 	  // lower grayscale ramp boundary

		float s_zRayEnd  = abs(eye.z) + sqrt(2.0);
		float s_zRayStart = abs(eye.z) - sqrt(2.0);
		float e = s_eyeDistance;
		float w = TEXTURE_RESOLUTION.x;
		float t_near = (s_zRayStart) / s_near;
		float t_far  = (s_zRayEnd)  / s_near;
		float nW = s_nearW;
		float pixelOffsetFar  = (1.0f / t_far)  * (e * w) / (nW * 2.0f); // pixel offset between points at zRayEnd distance to image planes
		float pixelOffsetNear = (1.0f / t_near) * (e * w) / (nW * 2.0f); // pixel offset between points at zRayStart distance to image planes
		
		composeTexArrayShader.update("uPixelOffsetFar",  pixelOffsetFar);
		composeTexArrayShader.update("uPixelOffsetNear", pixelOffsetNear);
		
		ImGui::Value("Approx Distance to Ray Start", s_zRayStart);
		ImGui::Value("Approx Distance to Ray End", s_zRayEnd);
		ImGui::Value("Pixel Offset at Ray Start", pixelOffsetNear);
		ImGui::Value("Pixel Offset at Ray End", pixelOffsetFar);
		ImGui::Value("Pixel Range of a Ray", pixelOffsetNear - pixelOffsetFar);
		//////////////////////////////////////////////////////////////////////////////
		
		////////////////////////////////  RENDERING //// ///////////////////////////// 
		glDisable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
		OPENGLCONTEXT->bindFBO(0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// clear output texture
		//clearOutputTexture( stereoOutputTextureArray );

		// reset atomic buffers
		GLuint a[3] = {0,0,0};
		glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0 , sizeof(GLuint) * 3, a);

		// render left image
		uvwShaderProgram.update("view", s_view);
		uvwRenderPass.setFrameBufferObject(&uvwFBO);
		uvwRenderPass.render();

		simpleRaycastShader.update("back_uvw_map", 2);
		simpleRaycastShader.update("front_uvw_map", 4);
		simpleRaycastShader.update("uScreenToTexture", s_modelToTexture * glm::inverse(model) * glm::inverse(s_view) * s_screenToView);
		simpleRaycastShader.update("uViewToTexture",   s_modelToTexture * glm::inverse(model) * glm::inverse(s_view));
		simpleRaycastShader.update("uProjection", s_perspective);
		simpleRaycast.setFrameBufferObject(&FBO);
		simpleRaycast.render();

		// render right image
		uvwShaderProgram.update("view", s_view_r);
		uvwRenderPass.setFrameBufferObject(&uvwFBO_r);
		uvwRenderPass.render();

		simpleRaycastShader.update("back_uvw_map", 3);
		simpleRaycastShader.update("front_uvw_map", 5);
		simpleRaycastShader.update("uScreenToTexture", s_modelToTexture * glm::inverse(model) * glm::inverse(s_view_r) * s_screenToView);
		simpleRaycastShader.update("uViewToTexture", s_modelToTexture * glm::inverse(model) * glm::inverse(s_view_r));
		simpleRaycastShader.update("uProjection", s_perspective);
		simpleRaycast.setFrameBufferObject(&FBO_r);
		simpleRaycast.render();

		// render stereo images in a single pass
		uvwShaderProgram.update("view", s_view);
		uvwRenderPass.setFrameBufferObject(&uvwFBO);
		uvwRenderPass.render();

		stereoRaycastShader.update("uScreenToTexture", s_modelToTexture * glm::inverse(model) * glm::inverse(s_view) * s_screenToView);
		stereoRaycastShader.update("uViewToTexture", s_modelToTexture * glm::inverse(model) * glm::inverse(s_view));
		stereoRaycastShader.update("uProjection", s_perspective);
		stereoRaycast.render();

		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		// compose right image from single pass output
		composeTexArray.render();

		// display fbo contents
		showTexShader.updateAndBindTexture("tex", 7, FBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
		showTex.setViewport((int)0, 0, (int)TEXTURE_RESOLUTION.x / 2, (int)TEXTURE_RESOLUTION.y / 2);
		showTex.render();

		showTexShader.updateAndBindTexture("tex", 8, FBO_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
		showTex.setViewport((int)TEXTURE_RESOLUTION.x / 2, 0, (int)TEXTURE_RESOLUTION.x / 2, (int)TEXTURE_RESOLUTION.y / 2);
		showTex.render();

		showTexShader.updateAndBindTexture("tex", 9, FBO_single.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
		showTex.setViewport((int)0, (int)TEXTURE_RESOLUTION.y / 2, (int)TEXTURE_RESOLUTION.x / 2, (int)TEXTURE_RESOLUTION.y / 2);
		showTex.render();

		showTexShader.updateAndBindTexture("tex", 10, FBO_single_r.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
		showTex.setViewport((int)TEXTURE_RESOLUTION.x / 2, (int)TEXTURE_RESOLUTION.y / 2, (int)TEXTURE_RESOLUTION.x / 2, (int)TEXTURE_RESOLUTION.y / 2);
		showTex.render();

		ImGui::Render();
		SDL_GL_SwapWindow(window); // swap buffers
		//////////////////////////////////////////////////////////////////////////////
	}

	destroyWindow(window);

	return 0;
}