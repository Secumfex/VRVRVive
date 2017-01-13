/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#include <iostream>

#include <Rendering/GLTools.h>
#include <Rendering/VertexArrayObjects.h>
#include <Rendering/RenderPass.h>

#include "UI/imgui/imgui.h"
#include "UI/imgui/imgui_color_picker.cpp"
#include <UI/imguiTools.h>
#include <UI/Turntable.h>

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

struct TFPoint{
	int v; // value 
	glm::vec4 col; // mapped color
};

static std::vector<float> s_transferFunctionValues;
static std::vector<glm::vec4> s_transferFunctionColors;
static std::vector<float> s_transferFunctionTexData = std::vector<float>(512*4);
GLuint s_transferFunctionTex = -1;

static std::vector<float> s_fpsCounter = std::vector<float>(120);
static int s_curFPSidx = 0;

const char* SHADER_DEFINES[] = {
	"NOTHING"
};
static std::vector<std::string> s_shaderDefines(SHADER_DEFINES, std::end(SHADER_DEFINES));

const glm::vec2 TEXTURE_RESOLUTION = glm::vec2( 800, 800);

static std::vector<float> s_texData((int) TEXTURE_RESOLUTION.x * (int) TEXTURE_RESOLUTION.y * 4, 0.0f);
static GLuint s_pboHandle = -1; // PBO 

static std::string activeRenderableStr = "Triangle Grid";

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// MAIN ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

void clearOutputTexture(GLuint texture)
{
	if(s_pboHandle == -1)
	{
		glGenBuffers(1,&s_pboHandle);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) TEXTURE_RESOLUTION.x * (int) TEXTURE_RESOLUTION.y * 4, &s_texData[0], GL_STATIC_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	
	OPENGLCONTEXT->activeTexture(GL_TEXTURE6);
	OPENGLCONTEXT->bindTexture(texture, GL_TEXTURE_2D);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (int) TEXTURE_RESOLUTION.x, (int) TEXTURE_RESOLUTION.y, GL_RGBA, GL_FLOAT, 0); // last param 0 => will read from pbo
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void generateTransferFunction()
{
	s_transferFunctionValues.clear();
	s_transferFunctionColors.clear();
	s_transferFunctionValues.push_back(164);
	s_transferFunctionColors.push_back(glm::vec4(0.0, 0.0, 0.0, 0.0));
	s_transferFunctionValues.push_back(312);
	s_transferFunctionColors.push_back(glm::vec4(1.0, 0.07, 0.07, 0.6));
	s_transferFunctionValues.push_back(872);
	s_transferFunctionColors.push_back(glm::vec4(0.0, 0.5, 1.0, 0.3));
	s_transferFunctionValues.push_back(1142);
	s_transferFunctionColors.push_back(glm::vec4(0.4, 0.3, 0.8, 0.0));
	s_transferFunctionValues.push_back(2500);
	s_transferFunctionColors.push_back(glm::vec4(0.95, 0.83, 1.0, 1.0));
}

void updateTransferFunctionTex()
{
	int currentMin = s_minValue;
	int currentMax = s_minValue+1;
	glm::vec4 currentMinCol( 0.0f, 0.0f, 0.0f, 0.0f );
	glm::vec4 currentMaxCol( 0.0f, 0.0f, 0.0f, 0.0f );

	int currentPoint = -1;
	for (unsigned int i = 0; i < s_transferFunctionTexData.size() / 4; i++)
	{
		glm::vec4 c( 0.0f, 0.0f, 0.0f, 0.0f );
		float relVal = (float) i / (float) (s_transferFunctionTexData.size() / 4);
		int v = relVal * (s_maxValue - s_minValue) + s_minValue;

		if (currentMax < v)
		{
			currentPoint++;
			if (currentPoint < s_transferFunctionValues.size())
			{
				currentMin = currentMax;
				currentMinCol = currentMaxCol;

				currentMax = (int) s_transferFunctionValues[currentPoint];
				currentMaxCol = s_transferFunctionColors[currentPoint];
			}
			else {
				currentMin = currentMax;
				currentMinCol = currentMaxCol;

				currentMax = s_maxValue;
			}
		}

		float mixParam = (float) (v - currentMin) / (float) (currentMax - currentMin);
		c = (1.0f - mixParam) * currentMinCol  + mixParam * currentMaxCol;

		s_transferFunctionTexData[i * 4 +0] = c[0];
		s_transferFunctionTexData[i * 4 +1] = c[1];
		s_transferFunctionTexData[i * 4 +2] = c[2];
		s_transferFunctionTexData[i * 4 +3] = c[3];
	}

	// Upload to texture
	if (s_transferFunctionTex == -1)
	{
		OPENGLCONTEXT->activeTexture(GL_TEXTURE0);
		glGenTextures(1, &s_transferFunctionTex);
		OPENGLCONTEXT->bindTexture(s_transferFunctionTex, GL_TEXTURE_1D);

		glTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA8, s_transferFunctionTexData.size() / 4);

		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

		OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_1D);
	}

	OPENGLCONTEXT->bindTexture(s_transferFunctionTex, GL_TEXTURE_1D);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, s_transferFunctionTexData.size() / 4, GL_RGBA, GL_FLOAT, &s_transferFunctionTexData[0]);
	OPENGLCONTEXT->bindTexture(0);
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
	auto window = generateWindow(1600,800);

	// load data set: CT of a Head	// load into 3d texture
	std::string file = RESOURCES_PATH + std::string( "/volumes/CTHead/CThead");
	VolumeData<float> volumeDataCTHead = Importer::load3DData<float>(file, 256, 256, 113, 2);
	activateVolume<float>(volumeDataCTHead);
	DEBUGLOG->log("Initial ray sampling step size: ", s_rayStepSize);
	DEBUGLOG->log("Loading Volume Data to 3D-Texture.");
	GLuint volumeTextureCT = loadTo3DTexture<float>(volumeDataCTHead, 99, GL_R32F, GL_RED, GL_FLOAT);
	checkGLError(true);
	//////////////////////////////////////////////////////////////////////////////
	/////////////////////////////// RENDERING  ///////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	/////////////////////     Scene / View Settings     //////////////////////////
	static float s_zNear = 0.1f;
	static float s_zFar = 10.0f;
	static float s_fovY = glm::radians(45.0f);

	glm::mat4 model = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));
	glm::vec4 eye(0.0f, 0.0f, 3.0f, 1.0f);
	glm::vec4 center(0.0f,0.0f,0.0f,1.0f);
	glm::mat4 view   = glm::lookAt(glm::vec3(eye) - glm::vec3(s_eyeDistance/2.0f,0.0f,0.0f), glm::vec3(center) - glm::vec3(s_eyeDistance / 2.0f, 0.0f, 0.0f), glm::vec3(0,1,0));
	glm::mat4 view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(s_eyeDistance/2.0f,0.0f,0.0f), glm::vec3(center) + glm::vec3(s_eyeDistance / 2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 perspective = glm::perspective( s_fovY, 1.0f, s_zNear, 10.f);
	//glm::mat4 perspective = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.f);
	glm::mat4 textureToProjection_r = perspective * view_r;

	glm::vec3 s_volumeSize(1.0f, 0.886f, 1.0);
	glm::mat4 s_modelToTexture = glm::mat4( // swap components
		glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), // column 1
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), // column 2
		glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),//column 3
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) //column 4 
		* glm::inverse(glm::scale(2.0f * s_volumeSize)) // moves origin to front left
		* glm::translate(glm::vec3(s_volumeSize.x, s_volumeSize.y, -s_volumeSize.z));

	static float s_nearH = s_zNear  * std::tanf( s_fovY / 2.0f );
	static float s_nearW = s_nearH * TEXTURE_RESOLUTION.x / (TEXTURE_RESOLUTION.y);

	// create Volume and VertexGrid
	VolumeSubdiv volume(1.0f, 0.886f, 1.0f, 3);
	VertexGrid vertexGrid(getResolution(window).x/2, getResolution(window).y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(-1));
	VertexGrid vertexGrid_coarse(getResolution(window).x/2, getResolution(window).y, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(16,16));
	Quad quad;
	Grid grid(100, 100, 0.1f, 0.1f);

	///////////////////////     UVW Map Renderpass     ///////////////////////////
	DEBUGLOG->log("Shader Compilation: volume uvw coords"); DEBUGLOG->indent();
	ShaderProgram uvwShaderProgram("/modelSpace/volumeMVP.vert", "/modelSpace/volumeUVW.frag"); DEBUGLOG->outdent();
	uvwShaderProgram.update("model", model);
	uvwShaderProgram.update("view", view);
	uvwShaderProgram.update("projection", perspective);

	DEBUGLOG->log("FrameBufferObject Creation: volume uvw coords"); DEBUGLOG->indent();
	FrameBufferObject uvwFBO(getResolution(window).x/2, getResolution(window).y);
	FrameBufferObject::s_internalFormat = GL_RGBA16F; // allow arbitrary values
	uvwFBO.addColorAttachments(2); // front UVRs and back UVRs
	FrameBufferObject::s_internalFormat = GL_RGBA; // default
	DEBUGLOG->outdent(); 

	FrameBufferObject uvwFBO_r(getResolution(window).x/2, getResolution(window).y);
	uvwFBO_r.addColorAttachments(2); DEBUGLOG->outdent(); // front UVRs and back UVRs
	uvwFBO_r.addColorAttachments(2); // front positions and back positions
	
	RenderPass uvwRenderPass(&uvwShaderProgram, &uvwFBO);
	uvwRenderPass.addClearBit(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	uvwRenderPass.addDisable(GL_DEPTH_TEST); // to prevent back fragments from being discarded
	uvwRenderPass.addEnable(GL_BLEND); // to prevent vec4(0.0) outputs from overwriting previous results
	uvwRenderPass.addRenderable(&volume);

	///////////////////////   Ray-Casting Renderpass    //////////////////////////
	DEBUGLOG->log("Shader Compilation: ray casting shader"); DEBUGLOG->indent();
	ShaderProgram shaderProgram("/screenSpace/volumeStereo.vert", "/screenSpace/volumeStereo_old.frag"); DEBUGLOG->outdent();
	shaderProgram.update("uStepSize", s_rayStepSize);
		
	// DEBUG
	generateTransferFunction();
	updateTransferFunctionTex();

	// bind volume texture, back uvw textures, front uvws
	OPENGLCONTEXT->bindTextureToUnit(volumeTextureCT, GL_TEXTURE0, GL_TEXTURE_3D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE1, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2, GL_TEXTURE_2D);
	OPENGLCONTEXT->bindTextureToUnit(s_transferFunctionTex, GL_TEXTURE3, GL_TEXTURE_1D);
	OPENGLCONTEXT->activeTexture(GL_TEXTURE0);

	// generate and bind right view image texture
	GLuint outputTexture = createTexture((int) getResolution(window).x/2, (int)getResolution(window).y, GL_RGBA16F);
	OPENGLCONTEXT->bindImageTextureToUnit(outputTexture,  0, GL_RGBA16F, GL_READ_WRITE, 0, GL_FALSE);
	OPENGLCONTEXT->bindTextureToUnit(outputTexture, GL_TEXTURE6, GL_TEXTURE_2D); // for display

	// DEBUG
	clearOutputTexture( outputTexture );

	// atomic counters
	GLuint atomicsBuffer = bufferData<GLuint>(std::vector<GLuint>(3,0), GL_ATOMIC_COUNTER_BUFFER, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicsBuffer); // important

	shaderProgram.update("volume_texture", 0); // volume texture
	shaderProgram.update("back_uvw_map",  1);
	shaderProgram.update("front_uvw_map", 2);
	shaderProgram.update("transferFunctionTex", 3);

	// ray casting render pass
	RenderPass renderPass(&shaderProgram);
	renderPass.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	renderPass.addRenderable(&grid);
	renderPass.addEnable(GL_DEPTH_TEST);
	renderPass.addDisable(GL_BLEND);
	
	///////////////////////   Show Texture Renderpass    //////////////////////////
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag", s_shaderDefines);
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);
	showTex.setViewport((int) getResolution(window).x/2,0,(int) getResolution(window).x/2, (int) getResolution(window).y);
	showTexShader.update( "tex", 6); // output texture

	//////////////////////////////////////////////////////////////////////////////
	///////////////////////    GUI / USER INPUT   ////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// Setup ImGui binding
    ImGui_ImplGlfwGL3_Init(window, true);
    bool show_test_window = true;

	Turntable turntable;
	double old_x;
    double old_y;
	glfwGetCursorPos(window, &old_x, &old_y);
	
	auto cursorPosCB = [&](double x, double y)
	{
		ImGuiIO& io = ImGui::GetIO();
		if ( io.WantCaptureMouse )
		{ return; } // ImGUI is handling this

		double d_x = x - old_x;
		double d_y = y - old_y;

		if ( turntable.getDragActive() )
		{
			turntable.dragBy(d_x, d_y, view);
		}

		old_x = x;
		old_y = y;
	};

	auto mouseButtonCB = [&](int b, int a, int m)
	{
		if (b == GLFW_MOUSE_BUTTON_LEFT && a == GLFW_PRESS)
		{
			turntable.setDragActive(true);
		}
		if (b == GLFW_MOUSE_BUTTON_LEFT && a == GLFW_RELEASE)
		{
			turntable.setDragActive(false);
		}

		ImGui_ImplGlfwGL3_MouseButtonCallback(window, b, a, m);
	};

	auto keyboardCB = [&](int k, int s, int a, int m)
	{
		if (a == GLFW_RELEASE) {return;} 
		switch (k)
		{
			case GLFW_KEY_W:
				eye += glm::inverse(view)    * glm::vec4(0.0f,0.0f,-0.1f,0.0f);
				center += glm::inverse(view) * glm::vec4(0.0f,0.0f,-0.1f,0.0f);
				break;
			case GLFW_KEY_A:
				eye += glm::inverse(view)	 * glm::vec4(-0.1f,0.0f,0.0f,0.0f);
				center += glm::inverse(view) * glm::vec4(-0.1f,0.0f,0.0f,0.0f);
				break;
			case GLFW_KEY_S:
				eye += glm::inverse(view)    * glm::vec4(0.0f,0.0f,0.1f,0.0f);
				center += glm::inverse(view) * glm::vec4(0.0f,0.0f,0.1f,0.0f);
				break;
			case GLFW_KEY_D:
				eye += glm::inverse(view)    * glm::vec4(0.1f,0.0f,0.0f,0.0f);
				center += glm::inverse(view) * glm::vec4(0.1f,0.0f,0.0f,0.0f);
				break;
			default:
				break;
			case GLFW_KEY_R:
				static int activeRenderable = 3;
				renderPass.clearRenderables();
				activeRenderable = (activeRenderable + 1) % 4;
				switch(activeRenderable)
				{
				case 0: // Quad
					renderPass.addRenderable(&quad);
					DEBUGLOG->log("renderable: Quad");
					activeRenderableStr = "Quad";
					break;
				case 1: // Vertex Grid
					renderPass.addRenderable(&vertexGrid);
					DEBUGLOG->log("renderable: Vertex Grid");
					activeRenderableStr = "Vertex Grid";
					break;
				case 2: // Vertex Grid (Coarse)
					renderPass.addRenderable(&vertexGrid_coarse);
					DEBUGLOG->log("renderable: Vertex Grid (Coarse)");
					activeRenderableStr = "Vertex Grid (Coarse)";
					break;
				case 3: // grid
					renderPass.addRenderable(&grid);
					DEBUGLOG->log("renderable: Grid");
					activeRenderableStr = "Triangle Grid";
					break;
				}
			break;
		}
		ImGui_ImplGlfwGL3_KeyCallback(window,k,s,a,m);
	};

	setCursorPosCallback(window, cursorPosCB);
	setMouseButtonCallback(window, mouseButtonCB);
	setKeyCallback(window, keyboardCB);

	std::string window_header = "Volume Renderer";
	glfwSetWindowTitle(window, window_header.c_str() );

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// RENDER LOOP /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	//++++++++++++++ DEBUG
	{
		glm::mat4 m = 
			(perspective * // -1 .. -1
				view_r * glm::inverse(view) * // from one view to the other
			glm::inverse(perspective) ) * // world with non-one w
		
			glm::translate(glm::vec3(-1.f, -1.f, -1.f)) * //-1..1 
			glm::scale(glm::vec3(2.0f,2.0f,2.0f)) // 0..2
			;
	
		DEBUGLOG->log("m", m);

		glm::vec4 p1(0.0f,0.0f,0.0f,1.0f); // bottom left on near plane
		glm::vec4 p2(0.5f,0.5f,1.0f,1.0f); // center on far plane
		glm::vec4 p3(0.5f,0.5f,0.0f,1.0f); // center on near plane

		DEBUGLOG->log("bottom left: ", m * p1);
		DEBUGLOG->log("center far : ", m * p2);
		DEBUGLOG->log("center near: ", m * p3);

		glm::vec4 p1n =  (m * p1) / (m * p1).w; // bottom left on near plane
		glm::vec4 p2n =  (m * p2) / (m * p2).w; // center on far plane
		glm::vec4 p3n =  (m * p3) / (m * p3).w; // center on near plane

		DEBUGLOG->log("bottom left {normalized}: ", p1n);
		DEBUGLOG->log("center far  {normalized}: ", p2n);
		DEBUGLOG->log("center near {normalized}: ", p3n);

		glm::mat4 bias = glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * glm::scale(glm::vec3(0.5f,0.5f,0.5f));
	
		DEBUGLOG->log("bottom left : ", bias * p1n);
		DEBUGLOG->log("center far  : ", bias * p2n);
		DEBUGLOG->log("center near : ", bias * p3n);
	}
	{
		float z_near = -1.0f;
		float z_far = -10.0f;

		float resolution = TEXTURE_RESOLUTION.x;
		glm::vec4 p( 0.5f, 0.5f, 0.0f, 1.0f); // gl_FragCoord (bottom leftmost pixel)
		glm::vec4 p_c = glm::vec4( floor(p.x) / resolution, p.y / resolution, p.z, p.w); // uv of pixel corner 
		glm::vec4 p_v = glm::inverse(perspective) * // -w .. w
						glm::translate(glm::vec3(-1.f, -1.f, -1.f)) * //-1..1 
						glm::scale(glm::vec3(2.0f,2.0f,2.0f)) * p_c;// 0..2
		p_v = p_v / p_v.w; // divide by w
		
		float t = z_far / z_near; // constant for t
		glm::vec4 p_f = glm::vec4( t * p_v.x, p_v.y, t * p_v.z, p_v.w ); // corner of pixel (z becomes z_far)
		glm::vec4 p_v0 = view * glm::inverse(view_r) * p_f; // point in left view
		glm::vec4 p_p0 = perspective * p_v0;
		p_p0 = p_p0 / p_p0.w;

		glm::vec4 p_c0 = glm::translate(glm::vec3(0.5f, 0.5f, 0.5f)) * // 0..1
						glm::scale(glm::vec3(0.5f,0.5f,0.5f)) * p_p0; // -0.5..0.5
		glm::vec4 p_0 = glm::scale(glm::vec3(resolution, resolution, 1.0f)) * p_c0; // texture relative coordinates
		int p_l0 = 	(16 - ( (int) p_0.x % 16) - 1 );
						
		DEBUGLOG->log("view coord for far point: ", p_f);
		DEBUGLOG->log("left view coord for far point: ", p_v0);
		DEBUGLOG->log("left uv coord for far point: ", p_c0);
		DEBUGLOG->log("left layer idx: ", p_l0);

		
		float e = s_eyeDistance;
		float w = resolution;
		// float t = z_far / z_near; // defined above
		float nW = s_nearW;
		float offSet = (e * w) / (t * nW * 2.0f);

		float pLeft_0 = floor( 0.5f ) + offSet;
		float pLeft_1 = floor( resolution - 0.5f ) + offSet;
		float pLeft_2 = floor( (resolution / 2.0f) - 0.5f ) + offSet;
		DEBUGLOG->log("pLeft_0 ", pLeft_0);
	}


	//++++++++++++++ DEBUG


	
	double elapsedTime = 0.0;
	render(window, [&](double dt)
	{
		profileFPS((float) (1.0 / dt));

		////////////////////////////////     GUI      ////////////////////////////////
        ImGuiIO& io = ImGui::GetIO();
		ImGui_ImplGlfwGL3_NewFrame(); // tell ImGui a new frame is being rendered

		ImGui::Value("FPS", (float) (1.0 / dt));

		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
		ImGui::PlotLines("FPS", &s_fpsCounter[0], s_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
		ImGui::PopStyleColor();

		ImGui::DragFloat("eye distance", &s_eyeDistance, 0.01f, 0.0f, 2.0f);
		
		if (ImGui::CollapsingHeader("Transfer Function"))
    	{
		ImGui::Columns(2, "mycolumns2", true);
        ImGui::Separator();
		bool changed = false;
		for (unsigned int n = 0; n < s_transferFunctionValues.size(); n++)
        {
			changed |= ImGui::DragFloat(("V" + std::to_string(n)).c_str(), &s_transferFunctionValues[n], 1.0f, s_minValue, s_maxValue);
			ImGui::NextColumn();
			changed |= ImGui::ColorEdit4(("C" + std::to_string(n)).c_str(), &s_transferFunctionColors[n][0]);
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
        }
        
		ImGui::Checkbox("auto-rotate", &s_isRotating); // enable/disable rotating volume

		static bool s_writeStereo = true;
		ImGui::Checkbox("write stereo", &s_writeStereo); // enable/disable single pass stereo
	
		ImGui::PopItemWidth();
        //////////////////////////////////////////////////////////////////////////////

		///////////////////////////// MATRIX UPDATING ///////////////////////////////
		if (s_isRotating) // update view matrix
		{
			model = glm::rotate(glm::mat4(1.0f), (float) dt, glm::vec3(0.0f, 1.0f, 0.0f) ) * model;
		}

		view = glm::lookAt(glm::vec3(eye) - glm::vec3(s_eyeDistance/2.0,0.0,0.0), glm::vec3(center) - glm::vec3(s_eyeDistance / 2.0, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		view_r = glm::lookAt(glm::vec3(eye) +  glm::vec3(s_eyeDistance/2.0,0.0f,0.0f), glm::vec3(center) + glm::vec3(s_eyeDistance / 2.0, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		
		textureToProjection_r = perspective * view_r * turntable.getRotationMatrix() * model * glm::inverse( s_modelToTexture ); // texture to model
		//////////////////////////////////////////////////////////////////////////////
				
		////////////////////////  SHADER / UNIFORM UPDATING //////////////////////////
		// update view related uniforms
		uvwShaderProgram.update("model", turntable.getRotationMatrix() * model);
		uvwShaderProgram.update("view", view);

		shaderProgram.update( "uTextureToProjection_r", textureToProjection_r ); //since position map contains view space coords

		/************* update color mapping parameters ******************/
		// ray start/end parameters
		shaderProgram.update("uStepSize", s_rayStepSize); 	  // ray step size

		// color mapping parameters
		shaderProgram.update("uWindowingMinVal", s_windowingMinValue); 	  // lower grayscale ramp boundary
		shaderProgram.update("uWindowingRange",  s_windowingMaxValue - s_windowingMinValue); // full range of values in window

		/************* update experimental  parameters ******************/
		shaderProgram.update("uWriteStereo", s_writeStereo); 	  // lower grayscale ramp boundary

		float s_zRayEnd  = abs(eye.z) + sqrt(2.0);
		float s_zRayStart = abs(eye.z) - sqrt(2.0);
		float e = s_eyeDistance;
		float w = TEXTURE_RESOLUTION.x;
		float t_near = (s_zRayStart) / s_zNear;
		float t_far  = (s_zRayEnd)  / s_zNear;
		float nW = s_nearW;
		float pixelOffsetFar  = (1.0f / t_far)  * (e * w) / (nW * 2.0f); // pixel offset between points at zRayEnd distance to image planes
		float pixelOffsetNear = (1.0f / t_near) * (e * w) / (nW * 2.0f); // pixel offset between points at zRayStart distance to image planes
		
		ImGui::Text("Active Fragment Generator: "); ImGui::SameLine(); ImGui::Text(activeRenderableStr.c_str());
		ImGui::Separator();

		ImGui::Value("Approx Distance to Ray Start", s_zRayStart);
		ImGui::Value("Approx Distance to Ray End", s_zRayEnd);
		ImGui::Value("Pixel Offset at Ray Start", pixelOffsetNear);
		ImGui::Value("Pixel Offset at Ray End", pixelOffsetFar);
		ImGui::Value("Pixel Range of a Ray", pixelOffsetNear - pixelOffsetFar);
		//////////////////////////////////////////////////////////////////////////////
		
		////////////////////////////////  RENDERING //// ///////////////////////////// 
		glDisable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // this is altered by ImGui::Render(), so set it every frame
		
		// clear output texture
		clearOutputTexture( outputTexture );

		// reset atomic buffers
		GLuint a[3] = {0,0,0};
		glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0 , sizeof(GLuint) * 3, a);

		// render left image
		uvwRenderPass.render();
		
		OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0), GL_TEXTURE1, GL_TEXTURE_2D);
		OPENGLCONTEXT->bindTextureToUnit(uvwFBO.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1), GL_TEXTURE2, GL_TEXTURE_2D);
		renderPass.setViewport(0, 0, getResolution(window).x / 2, getResolution(window).y);
		renderPass.render();
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		// display right image
		showTex.render();
		
		ImGui::Render();
		//////////////////////////////////////////////////////////////////////////////
	});

	destroyWindow(window);

	return 0;
}