/*******************************************
 * **** DESCRIPTION ****
 ****************************************/

#include <iostream>
#include <Rendering/OpenGLContext.h>
#include <Rendering/GLTools.h>
#include <Rendering/RenderPass.h>
#include <Rendering/ShaderProgram.h>
#include <Rendering/FrameBufferObject.h>
#include <Rendering/VertexArrayObjects.h>
#include <Importing/TextureTools.h>
#include <UI/imgui_impl_glfw_gl3.h>
#include <UI/imgui/imgui.h>
#include <glm/gtx/transform.hpp>

////////////////////// PARAMETERS /////////////////////////////
const int NUM_THREADS = 10;

void* call_from_thread(int tid) 
{
	std::cout << "Launched from thread" << tid <<std::endl;
	return NULL;
}

static const int VERTEX_GRID_SIZE = 512;

static Renderable* s_vertexGrid = nullptr;

const glm::vec2 WINDOW_RESOLUTION = glm::vec2( VERTEX_GRID_SIZE, VERTEX_GRID_SIZE);

void createVertexGrid()
{
	// create VBO
	s_vertexGrid = new VertexGrid(VERTEX_GRID_SIZE, VERTEX_GRID_SIZE, true, VertexGrid::TOP_RIGHT_COLUMNWISE, glm::ivec2(-1));
}

void bindImages(GLuint inputTexture, GLuint outputTexture)
{
	OPENGLCONTEXT->bindImageTextureToUnit(inputTexture,  0, GL_RGBA32F, GL_READ_ONLY);
	OPENGLCONTEXT->bindImageTextureToUnit(outputTexture, 1, GL_RGBA32F, GL_READ_WRITE);
}


static std::vector<float> s_texData((int) WINDOW_RESOLUTION.x * (int) WINDOW_RESOLUTION.y * 4, 0.0f);
static GLuint s_pboHandle = -1; // PBO 

void clearOutputTexture(GLuint texture)
{
	if(s_pboHandle == -1)
	{
		glGenBuffers(1,&s_pboHandle);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(float) * (int) WINDOW_RESOLUTION.x * (int) WINDOW_RESOLUTION.y * 4, &s_texData[0], GL_STATIC_DRAW);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	
	OPENGLCONTEXT->activeTexture(GL_TEXTURE4);
	OPENGLCONTEXT->bindTexture(texture);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s_pboHandle);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y, GL_RGBA, GL_FLOAT, 0); // last param 0 => will read from pbo
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void uploadInputData(GLuint inputTexture)
{
	std::vector<float> inputData( ((int) WINDOW_RESOLUTION.x) * ((int) WINDOW_RESOLUTION.y) * 4, 0.75);
	float t = 0.0;
	for (int i = 0; i < inputData.size(); i++)
	{
		inputData[i] = 0.5f + 0.25f*sin(t); //some random pattern 
		t+= 0.00015f; 
	}

	uploadTextureData<float>(inputTexture, inputData, GL_RGBA, GL_FLOAT);
}

static std::vector<float> s_fpsCounter = std::vector<float>(120);
static int s_curFPSidx = 0;
void profileFPS(float fps)
{
	s_fpsCounter[s_curFPSidx] = fps;
	s_curFPSidx = (s_curFPSidx + 1) % s_fpsCounter.size(); 
}

int main(int argc, char *argv[])
{
	auto window = generateWindow(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 200, 200);
    ImGui_ImplGlfwGL3_Init(window, true);
	FrameBufferObject::s_internalFormat = GL_RGBA16F;
	DEBUGLOG->setAutoPrint(true);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////// Sequential Raycasting////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// atomic counters
	GLuint atomicsBuffer = bufferData<GLuint>(std::vector<GLuint>(3,0), GL_ATOMIC_COUNTER_BUFFER, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicsBuffer); // important
	createVertexGrid(); // setup vbo
	
	GLuint inputTexture = createTexture((int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y, GL_RGBA32F);
	GLuint outputTexture = createTexture((int) WINDOW_RESOLUTION.x,(int) WINDOW_RESOLUTION.y, GL_RGBA32F);
	OPENGLCONTEXT->bindTextureToUnit(outputTexture, GL_TEXTURE4);
	uploadInputData( inputTexture );
	clearOutputTexture( outputTexture );

	bindImages(inputTexture, outputTexture);

	// arbitrary matrices
	glm::mat4 model = glm::rotate( glm::radians(45.0f), glm::vec3(1.0f,1.0f,0.0f) );
	glm::mat4 view		= glm::lookAt( glm::vec3(0.0f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 rightView = glm::lookAt( glm::vec3(0.5f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 projection = glm::perspective( glm::radians(45.0f), getRatio(window), 0.1f, 10.0f);

	glm::mat4 inverseView = glm::inverse( view );
	glm::mat4 homography = projection * rightView * inverseView;
	DEBUGLOG->log("homopraphy: ", homography);

	//arbitrary render pass for some geometry
	Volume volume;
	ShaderProgram cubeShader("/modelSpace/modelViewProjection.vert", "/modelSpace/simpleLighting.frag");
	FrameBufferObject cubeFbo( cubeShader.getOutputInfoMap(), (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y );
	RenderPass cube( &cubeShader, &cubeFbo );
	cube.addRenderable( &volume );
	cube.addClearBit( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	cubeShader.update( "model", model );
	cubeShader.update( "view", view );
	cubeShader.update( "projection", projection );
	cubeShader.update( "color", glm::vec4(1.0f,1.0f,1.0f,1.0f) );

	Quad quad;

	// abritrary render pass for the vertex grid
	ShaderProgram quadShader("/screenSpace/fullscreen.vert", "/modelSpace/vertexGrid.frag");
	ShaderProgram vertexGridShader("/screenSpace/fullscreen.vert", "/modelSpace/vertexGrid.frag");
	FrameBufferObject fbo( vertexGridShader.getOutputInfoMap(), (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y );
	RenderPass vertexGrid( &vertexGridShader, &fbo );
	vertexGrid.addRenderable( s_vertexGrid );
	vertexGrid.addClearBit( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	vertexGridShader.bindTextureOnUse( "depthTex", cubeFbo.getDepthTextureHandle());
	vertexGridShader.bindTextureOnUse( "posTex",   cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
	vertexGridShader.bindTextureOnUse( "colorTex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	vertexGridShader.update("inverseView", inverseView);
	vertexGridShader.update("rightView", rightView);
	vertexGridShader.update("projection", projection);
	quadShader.bindTextureOnUse( "depthTex", cubeFbo.getDepthTextureHandle());
	quadShader.bindTextureOnUse( "posTex",   cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
	quadShader.bindTextureOnUse( "colorTex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
	quadShader.update("inverseView", inverseView);
	quadShader.update("rightView", rightView);
	quadShader.update("projection", projection);

	// show texture
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);
	showTex.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	showTex.setViewport(0,0,WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y);
	showTexShader.bindTextureOnUse( "tex", fbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
	
	static std::string activeRenderableStr = "Vertex Grid";
	static std::string activeViewStr = "Original";
	static bool enableIdOverlay = false;
	setKeyCallback(window, [&](int k, int s, int a, int m){
		if (a == GLFW_RELEASE) {return;}
		if (k == GLFW_KEY_SPACE)
		{
			static bool swap = true;
			if (swap){
				showTexShader.bindTextureOnUse("tex", outputTexture);
				activeViewStr = "Warped";
				DEBUGLOG->log("display: Output");
				swap = false;
			} else {
				showTexShader.bindTextureOnUse("tex", fbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
				activeViewStr = "Original";
				DEBUGLOG->log("display: Cube FBO");
				swap = true;
			}
		} 
		if (k == GLFW_KEY_R) 
		{
			static bool swap = true;
			if (swap){
				vertexGrid.clearRenderables();
				vertexGrid.addRenderable(&quad);
				vertexGrid.setShaderProgram(&quadShader);
				DEBUGLOG->log("renderable: Quad");
				activeRenderableStr = "Quad";
				swap = false;
			} else {
				vertexGrid.clearRenderables();
				vertexGrid.addRenderable(s_vertexGrid);
				vertexGrid.setShaderProgram(&vertexGridShader);
				DEBUGLOG->log("renderable: Vertex Grid");
				activeRenderableStr = "Vertex Grid";
				swap = true;
			}
		}
		if (k == GLFW_KEY_I) 
		{
			enableIdOverlay = !enableIdOverlay;
		}
	});

	double old_x, d_x;
	double old_y, d_y;
	glfwGetCursorPos(window, &old_x, &old_y);
	setCursorPosCallback(window, [&](double x, double y)
	{
		d_x = x - old_x;
		d_y = y - old_y;

		old_x = x;
		old_y = y;
	});

	setMouseButtonCallback(window, [&](int b, int a, int m)
	{
		if (b == GLFW_MOUSE_BUTTON_LEFT && a == GLFW_PRESS)
		{
			unsigned char pick_col[15];
			glReadPixels(old_x-2, WINDOW_RESOLUTION.y-old_y, 5, 1, GL_RGB, GL_UNSIGNED_BYTE, pick_col);

			for (int i = 0; i < 15; i += 3)
			{
				DEBUGLOG->log("color: ", glm::vec3(pick_col[i + 0], pick_col[i + 1], pick_col[i+2]));
			}
		}
	});
	///////////////////////////////////////////////////////////////////////////////
	
	render(window, [&](double dt)
	{	
		////////////////////////////////     GUI      ////////////////////////////////
		profileFPS((float) (1.0 / dt));
		ImGuiIO& io = ImGui::GetIO();
		ImGui_ImplGlfwGL3_NewFrame(); // tell ImGui a new frame is being rendered
		ImGui::Value("FPS", ImGui::GetIO().Framerate);
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
		//ImGui::PlotLines("FPS", &s_fpsCounter[0], s_fpsCounter.size(), 0, NULL, 0.0, 65.0, ImVec2(120,60));
		ImGui::Text("Active View:"); ImGui::SameLine(); ImGui::Text(activeViewStr.c_str());
		ImGui::Text("Active Fragment Generator:"); ImGui::SameLine(); ImGui::Text(activeRenderableStr.c_str());
		ImGui::Checkbox("Enable Invocation ID Overlay", &enableIdOverlay);
		vertexGridShader.update("uEnableIdOverlay", enableIdOverlay);
		quadShader.update("uEnableIdOverlay", enableIdOverlay);

		ImGui::PopStyleColor();
		///////////////////////////////////////////////////////////////////////////////
		checkGLError();

		cubeShader.update("model", glm::rotate( (float) glfwGetTime() / 2.0f, glm::vec3(1.0f, 1.0f, 0.0f)));
		cube.render();

		// reset atomic buffers
		GLuint a[3] = {0,0,0};
		glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0 , sizeof(GLuint) * 3, a);

		// reset output texture	
		clearOutputTexture(outputTexture);

		vertexGrid.render();

		showTex.render();

		ImGui::Render();
	});

	destroyWindow(window);

	return 0;
}