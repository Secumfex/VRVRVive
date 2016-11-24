/*******************************************
 * **** DESCRIPTION ****
 ****************************************/

#include <iostream>
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
		// upload input texture
		glBindImageTexture(
				0,
				inputTexture,
				0,
				GL_FALSE,
				0,
				GL_READ_ONLY,
				GL_RGBA32F);

		// upload output texture
		glBindImageTexture(1,
				outputTexture,
				0,
				GL_FALSE,
				0,
				GL_WRITE_ONLY,
				GL_RGBA32F);
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
	GLuint atomicsBuffer;
	glGenBuffers(1, &atomicsBuffer);
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicsBuffer);
	glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint) * 3, NULL, GL_DYNAMIC_DRAW); // values are still undefined!
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicsBuffer);
	createVertexGrid(); // setup vbo
	
	GLuint inputTexture = createTexture((int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y, GL_RGBA32F);
	uploadInputData( inputTexture );
	GLuint outputTexture = createTexture((int) WINDOW_RESOLUTION.x,(int) WINDOW_RESOLUTION.y, GL_RGBA32F);

	bindImages(inputTexture, outputTexture);

	// arbitrary matrices
	glm::mat4 model = glm::rotate( glm::radians(45.0f), glm::vec3(0.0f,1.0f,0.0f) );
	glm::mat4 view		= glm::lookAt( glm::vec3(0.0f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 rightView = glm::lookAt( glm::vec3(0.25f,0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) );
	glm::mat4 projection = glm::perspective( glm::radians(45.0f), getRatio(window), 0.1f, 10.0f);

	glm::mat4 inverseView = glm::inverse( view );

	//arbitrary render pass for some geometry
	Volume volume;
	ShaderProgram cubeShader("/modelSpace/modelViewProjection.vert", "/modelSpace/simpleColor.frag");
	FrameBufferObject cubeFbo( cubeShader.getOutputInfoMap(), (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y );
	RenderPass cube( &cubeShader, &cubeFbo );
	cube.addRenderable( &volume );
	cube.addClearBit( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	cubeShader.update( "model", model );
	cubeShader.update( "view", view );
	cubeShader.update( "projection", projection );
	cubeShader.update( "color", glm::vec4(1.0f,1.0f,1.0f,1.0f) );

	// abritrary render pass for the vertex grid
	ShaderProgram vertexGridShader("/modelSpace/vertexGrid.vert", "/modelSpace/vertexGrid.frag");
	FrameBufferObject fbo( vertexGridShader.getOutputInfoMap(), (int) WINDOW_RESOLUTION.x, (int) WINDOW_RESOLUTION.y );
	RenderPass vertexGrid( &vertexGridShader, &fbo );
	vertexGrid.addRenderable( s_vertexGrid );
	vertexGrid.addClearBit( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	vertexGridShader.bindTextureOnUse( "depthTex", cubeFbo.getDepthTextureHandle());
	vertexGridShader.bindTextureOnUse( "posTex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
	vertexGridShader.update("inverseView", inverseView);
	vertexGridShader.update("rightView", rightView);
	vertexGridShader.update("projection", projection);

	// show texture
	Quad quad;
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);
	showTex.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	showTex.setViewport(0,0,WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y);
	showTexShader.bindTextureOnUse( "tex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1) );

	bool swap = true;
	setKeyCallback(window, [&](int k, int s, int a, int m){
		if (a == GLFW_RELEASE) {return;}	
		if (swap){
			showTexShader.bindTextureOnUse("tex", outputTexture);
			swap = false;
		} else {
			showTexShader.bindTextureOnUse("tex", cubeFbo.getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT1));
			swap = true;
		}
		DEBUGLOG->log("swap: ", swap);
	});

	///////////////////////////////////////////////////////////////////////////////
	while (!shouldClose(window))
	{	
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