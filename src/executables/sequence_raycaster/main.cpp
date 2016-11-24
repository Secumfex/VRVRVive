/*******************************************
 * **** DESCRIPTION ****
 ****************************************/

#include <iostream>
#include <Rendering/GLTools.h>
#include <Rendering/RenderPass.h>
#include <Rendering/VertexArrayObjects.h>
 
#ifdef MINGW_THREADS
 	#include <mingw-std-threads/mingw.thread.h>
#else
 	#include <thread>
#endif
#include <atomic>

#include <windows.h>
#include <mmsystem.h>

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

int main(int argc, char *argv[])
{
	auto window = generateWindow(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 200, 200);
	DEBUGLOG->setAutoPrint(true);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// multithreading /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	createVertexGrid(); // setup vbo
	
	// show texture
	ShaderProgram renderpassShader("/modelSpace/vertexGrid.vert", "/modelSpace/vertexGrid.frag");
	RenderPass renderpass(&renderpassShader,0);
	renderpass.addRenderable(s_vertexGrid);
	renderpass.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderpass.setViewport(0,0,WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y);

	///////////////////////////////////////////////////////////////////////////////
	while (!shouldClose(window))
	{	
		renderpass.render();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	destroyWindow(window);

	return 0;
}