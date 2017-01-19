/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>

#include "UI/imgui/imgui.h"
#include "UI/imgui_impl_sdl_gl3.h"
#include <UI/imguiTools.h>
#include <UI/Turntable.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

////////////////////// PARAMETERS /////////////////////////////

#include "Parameters.h" //<! static variables

using namespace RaycastingParameters;
using namespace ViewParameters;
using namespace VolumeParameters;

static const char* s_models[] = {"CT Head"};

static std::vector<float> s_fpsCounter = std::vector<float>(120);
static int s_curFPSidx = 0;

const char* SHADER_DEFINES[] = {
	"RANDOM_OFFSET"
};
static std::vector<std::string> s_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES));

const glm::vec2 TEXTURE_RESOLUTION = glm::vec2( 800, 800);
const glm::vec2 WINDOW_RESOLUTION = glm::vec2( 1600, 800);

const int NUM_LAYERS = 1; // lol lets try this

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
	auto window = generateWindow_SDL(WINDOW_RESOLUTION.x,WINDOW_RESOLUTION.y);

	// load data set: CT of a Head	// load into 3d texture
	std::string file = RESOURCES_PATH + std::string( "/volumes/CTHead/CThead");
	VolumeData<float> volumeDataCTHead = Importer::load3DData<float>(file, 256, 256, 113, 2);
	activateVolume<float>(volumeDataCTHead);
	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	DEBUGLOG->log("Loading Volume Data to 3D-Texture.");
	GLuint volumeTextureCT = loadTo3DTexture<float>(volumeDataCTHead, 99, GL_R32F, GL_RED, GL_FLOAT);

	//////////////////////////////////////////////////////////////////////////////
	/////////////////////////////// RENDERING  ///////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	/////////////////////     Scene / View Settings     //////////////////////////
	// create Volume and Quad
	VolumeSubdiv volume(s_volumeSize.x, s_volumeSize.y, s_volumeSize.z, 3);
	Quad quad;

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
	ShaderProgram shaderProgram("/raycast/simpleRaycast.vert", "/raycast/synth_raycastLayer.frag", s_shaderDefines); DEBUGLOG->outdent();
	shaderProgram.update("uStepSize", s_rayStepSize);
	
	// DEBUG
	generateTransferFunction();
	updateTransferFunctionTex();

	DEBUGLOG->log("FrameBufferObject Creation: synth ray casting layers"); DEBUGLOG->indent();
	FrameBufferObject::s_internalFormat = GL_RGBA32F; // allow arbitrary values
	FrameBufferObject synth_raycastLayerFBO(shaderProgram.getOutputInfoMap(), TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	FrameBufferObject::s_internalFormat = GL_RGBA; // default
	DEBUGLOG->outdent(); 

	// bind volume texture, back uvw textures, front uvws
	OPENGLCONTEXT->bindTextureToUnit(volumeTextureCT, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE1, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(s_transferFunction.getTextureHandle(), GL_TEXTURE3, GL_TEXTURE_1D);
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	// generate and bind right view image texture
	//GLuint textureArray = createTextureArray((int) getResolution(window).x/2, (int)getResolution(window).y, NUM_LAYERS, GL_RGBA16F);
	//OPENGLCONTEXT->bindImageTextureToUnit(textureArray,  0, GL_RGBA16F, GL_WRITE_ONLY, 0, GL_TRUE); // layer will be ignored, entire array will be bound
	//OPENGLCONTEXT->bindTextureToUnit(textureArray, GL_TEXTURE6, GL_TEXTURE_2D_ARRAY); // for display

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
	showTex.addEnable(GL_BLEND);

	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE4, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT2), GL_TEXTURE5, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT3), GL_TEXTURE6, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT4), GL_TEXTURE7, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT5), GL_TEXTURE8, GL_TEXTURE_2D); // debugging output
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getDepthTextureHandle(),								  GL_TEXTURE9, GL_TEXTURE_2D); //depth 0
	OPENGLCONTEXT->bindTextureToUnit(synth_raycastLayerFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE10, GL_TEXTURE_2D); //depth 1-4
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	showTexShader.update("tex", 4);

	///////////////////////   DEBUG Recompose Layers Renderpass    //////////////////////////
	ShaderProgram debugRecomposeShader("/screenSpace/fullscreen.vert", "/raycast/debug_synth_recomposeLayers.frag");
	RenderPass debugRecompose(&debugRecomposeShader,0);
	debugRecompose.addRenderable(&quad);
	debugRecompose.setViewport(TEXTURE_RESOLUTION.x,0,TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);

	debugRecomposeShader.update("layer1",4);
	debugRecomposeShader.update("layer2",5);
	debugRecomposeShader.update("layer3",6);
	debugRecomposeShader.update("layer4",7);
	debugRecomposeShader.update("depth0",9);
	debugRecomposeShader.update("depth", 10);

	///////////////////////   novel view synthesis Renderpass    //////////////////////////
	//Grid grid(400,400,0.0025f,0.0025f, false);

	ShaderProgram novelViewShader("/screenSpace/fullscreen.vert", "/raycast/synth_novelView.frag");
	//ShaderProgram novelViewShader("/raycast/synth_volumeMVP.vert", "/raycast/synth_novelView.frag");
	FrameBufferObject FBO_novelView(novelViewShader.getOutputInfoMap(), TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);
	//RenderPass novelView(&novelViewShader, &FBO_novelView);
	RenderPass novelView(&novelViewShader, 0);
	novelView.addRenderable(&quad);
	//novelView.addRenderable(&volume);
	novelView.addEnable(GL_DEPTH_TEST);
	//novelView.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	novelView.setViewport(TEXTURE_RESOLUTION.x,0,TEXTURE_RESOLUTION.x, TEXTURE_RESOLUTION.y);

	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE12, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE13, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(FBO_novelView.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE11, GL_TEXTURE_2D); //depth 1-4
	OPENGLCONTEXT->activeTexture(GL_TEXTURE20);

	novelViewShader.update("layer1",4);
	novelViewShader.update("layer2",5);
	novelViewShader.update("layer3",6);
	novelViewShader.update("layer4",7);
	novelViewShader.update("depth0",9);
	novelViewShader.update("depth", 10);
	
	novelViewShader.update("back_uvw_map_old", 1);
	novelViewShader.update("back_uvw_map",  12);
	novelViewShader.update("front_uvw_map", 13);
	
	novelViewShader.update("uThreshold", 100);

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

	//++++++++++++++ DEBUG
	//++++++++++++++ DEBUG

	float elapsedTime = 0.0;
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

		if (ImGui::CollapsingHeader("Volume Rendering Settings"))
    	{
			ImGui::Columns(2, "TFcontrol", true);
			ImGui::Separator();
			bool changed = false;
			for (unsigned int n = 0; n < s_transferFunction.getValues().size(); n++)
			{
				changed |= ImGui::DragInt(("V" + std::to_string(n)).c_str(), &s_transferFunction.getValues()[n], 1.0f, s_minValue, s_maxValue);
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
            ImGui::Text("Parameters related to volume rendering");
            ImGui::DragFloatRange2("windowing range", &s_windowingMinValue, &s_windowingMaxValue, 5.0f, (float) s_minValue, (float) s_maxValue); // grayscale ramp boundaries
        	ImGui::SliderFloat("ray step size",   &s_rayStepSize,  0.0001f, 0.1f, "%.5f", 2.0f);
        	ImGui::PopItemWidth();
		}
        
		ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume

		static int active_debug_texture = 4;
		if ( ImGui::SliderInt("debug texture", &active_debug_texture, 4, 11) ) // active layer texture
		{
			showTexShader.update("tex", active_debug_texture);
		}

		static int threshold = 4;
		if ( ImGui::SliderInt("threshold", &threshold, 0, 100) ) // active layer texture
		{
			novelViewShader.update("uThreshold", threshold);
		}

		if ( ImGui::SliderFloat("eye dist", &s_eyeDistance, 0.0, 1.0) ) // active layer texture
		{
			ViewParameters::updateView();
		}

		//////////////////////////////////////////////////////////////////////////////

		///////////////////////////// MATRIX UPDATING ///////////////////////////////
		if (s_isRotating) // update view matrix
		{
			s_rotation = glm::rotate(glm::mat4(1.0f), (float) io.DeltaTime, glm::vec3(0.0f, 1.0f, 0.0f) ) * s_rotation;
		}

		ViewParameters::updateView(); // might have changed from SDL Event

		//glm::vec4 warpCenter  = s_center + glm::vec4(sin(elapsedTime*2.0)*0.25f, cos(elapsedTime*2.0)*0.125f, 0.0f, 0.0f);
		glm::vec4 warpCenter  = s_center;
		glm::vec4 warpEye = s_eye + glm::vec4(-sin(elapsedTime*1.0)*0.125f, -cos(elapsedTime*2.0)*0.125f, 0.0f, 0.0f);
		s_view_r = glm::lookAt(glm::vec3(warpEye), glm::vec3(warpCenter), glm::normalize(glm::vec3( sin(elapsedTime)*0.25f, 1.0f, 0.0f)));
	
		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
		// update view related uniforms
		uvwShaderProgram.update("model", s_translation * turntable.getRotationMatrix() * s_rotation * s_scale);

		/************* update color mapping parameters ******************/
		// ray start/end parameters
		shaderProgram.update("uStepSize", s_rayStepSize); 	  // ray step size

		// color mapping parameters
		shaderProgram.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		shaderProgram.update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

		/************* update experimental  parameters ******************/
		shaderProgram.update("uProjection", s_perspective);
		//shaderProgram.update("uViewToTexture", s_modelToTexture * glm::inverse(s_model) * glm::inverse(s_view) );
		shaderProgram.update("uScreenToView", s_screenToView );

		debugRecomposeShader.update("uProjection", s_perspective); // used for depth to distance computation
		novelViewShader.update("uProjection", s_perspective); // used for depth to distance computation
		
		// used to render the volume bbox (entry point)
		//novelViewShader.update("uModel",  s_translation * turntable.getRotationMatrix() * s_rotation * s_scale);
		
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
		//debugRecompose.render();
		
		ImGui::Render();
		SDL_GL_SwapWindow(window); // swap buffers
		//////////////////////////////////////////////////////////////////////////////
	}

	destroyWindow(window);

	return 0;
}