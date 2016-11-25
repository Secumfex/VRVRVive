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

static std::vector<float> s_vertexGridData;

const glm::vec2 WINDOW_RESOLUTION = glm::vec2( VERTEX_GRID_SIZE, VERTEX_GRID_SIZE);

void createVertexGrid()
{
	// create Data
	s_vertexGridData.resize(VERTEX_GRID_SIZE * VERTEX_GRID_SIZE * 3);
	int vIdx = 0;
	for (int j = (int)(WINDOW_RESOLUTION.x) - 1; j >= 0; j--) // from right
	{
		for (int i = (int) (WINDOW_RESOLUTION.y) - 1; i >= 0; i--) // from top 
		{
			s_vertexGridData[vIdx+0] = j; // x
			s_vertexGridData[vIdx+1] = i; // y
			s_vertexGridData[vIdx+2] = 0; // z

			//DEBUGLOG->log("x: ", s_vertexGridData[vIdx+0]);
			//DEBUGLOG->log("y: ", s_vertexGridData[vIdx+1]);
			//DEBUGLOG->log("z: ", s_vertexGridData[vIdx+2]);

			vIdx += 3;
		}
	}

	// create VBO
	s_vertexGrid = new Renderable();
	glGenVertexArrays(1, &(s_vertexGrid->m_vao));
	OPENGLCONTEXT->bindVAO(s_vertexGrid->m_vao);
	s_vertexGrid->createVbo(s_vertexGridData, 3, 0);
	s_vertexGrid->setDrawMode(GL_POINTS);
	s_vertexGrid->m_positions.m_size = (VERTEX_GRID_SIZE * VERTEX_GRID_SIZE);
}

void bindImages(GLuint inputTexture, GLuint outputTexture)
{
	OPENGLCONTEXT->bindImageTextureToUnit(inputTexture,  0, GL_RGBA32F, GL_READ_ONLY);
	OPENGLCONTEXT->bindImageTextureToUnit(outputTexture, 1, GL_RGBA32F, GL_WRITE_ONLY);
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


int main(int argc, char *argv[])
{
	auto window = generateWindow(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 200, 200);
	FrameBufferObject::s_internalFormat = GL_RGBA16F;
	DEBUGLOG->setAutoPrint(true);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////// Sequential Raycasting////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// atomic counters
	GLuint atomicsBuffer = bufferData<GLuint>(std::vector<GLuint>(3,0), GL_ATOMIC_COUNTER_BUFFER, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicsBuffer);
	createVertexGrid(); // setup vbo
	
	GLuint inputTexture = createTexture((int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y, GL_RGBA32F);
	uploadInputData( inputTexture );
	GLuint outputTexture = createTexture((int) WINDOW_RESOLUTION.x,(int) WINDOW_RESOLUTION.y, GL_RGBA32F);

	bindImages(inputTexture, outputTexture);

	// arbitrary matrices
	glm::mat4 model = glm::rotate( glm::radians(45.0f), glm::vec3(1.0f,1.0f,0.0f) );
	glm::mat4 view		= glm::lookAt( glm::vec3(0.0f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 rightView = glm::lookAt( glm::vec3(0.5f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 projection = glm::perspective( glm::radians(45.0f), getRatio(window), 0.1f, 10.0f);

	glm::mat4 inverseView = glm::inverse( view );

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
	ShaderProgram vertexGridShader("/modelSpace/vertexGrid.vert", "/modelSpace/vertexGrid.frag");
	FrameBufferObject fbo( vertexGridShader.getOutputInfoMap(), (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y );
	RenderPass vertexGrid( &vertexGridShader, &fbo );
	//vertexGrid.addRenderable( s_vertexGrid );
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
	showTexShader.bindTextureOnUse( "tex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );

	setKeyCallback(window, [&](int k, int s, int a, int m){
		if (a == GLFW_RELEASE) {return;}
		if (k == GLFW_KEY_SPACE)
		{
			static bool swap = true;
			if (swap){
				showTexShader.bindTextureOnUse("tex", outputTexture);
				DEBUGLOG->log("display: Output");
				swap = false;
			} else {
				showTexShader.bindTextureOnUse("tex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0));
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
				swap = false;
			} else {
				vertexGrid.clearRenderables();
				vertexGrid.addRenderable(s_vertexGrid);
				vertexGrid.setShaderProgram(&vertexGridShader);
				DEBUGLOG->log("renderable: Vertex Grid");
				swap = true;
			}
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

	while (!shouldClose(window))
	{	
		cubeShader.update("model", glm::rotate( (float) glfwGetTime() / 2.0f, glm::vec3(1.0f, 1.0f, 0.0f)));
		cube.render();

		// reset atomic buffers
		GLuint a[3] = {0,0,0};
		glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0 , sizeof(GLuint) * 3, a);

		vertexGrid.render();

		showTex.render();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	destroyWindow(window);

	return 0;
}