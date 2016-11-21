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

#include <openvr.h>

static const int TEXTURE_SIZE = 512;
const glm::vec2 WINDOW_RESOLUTION = glm::vec2( TEXTURE_SIZE, TEXTURE_SIZE);


int main()
{
	auto window = generateWindow(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 200, 200);
	DEBUGLOG->setAutoPrint(true);

	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////// multithreading /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	
	// Loading the SteamVR Runtime
	vr::EVRInitError eError = vr::VRInitError_None;
	auto m_pHMD = vr::VR_Init( &eError, vr::VRApplication_Scene );

	if ( eError != vr::VRInitError_None )
	{
		m_pHMD = NULL;
		char buf[1024];
		sprintf_s( buf, sizeof( buf ), "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription( eError ) );
		DEBUGLOG->log("VR_Init Failed!");
	}

	// show texture
	Quad quad;
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	RenderPass showTex(&showTexShader,0);
	showTex.addRenderable(&quad);
	showTex.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	showTex.setViewport(0,0,WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y);

	///////////////////////////////////////////////////////////////////////////////

	int loopsSinceLastUpdate = 0;
	double elapsedTime = 0.0;

	while (!shouldClose(window))
	{	
		// do stuff
		double dt = elapsedTime;
		elapsedTime = glfwGetTime();
		dt = elapsedTime - dt;
		glfwSetWindowTitle(window, DebugLog::to_string( 1.0 / dt ).c_str());

		// render with current texture
		showTex.render();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	return 0;
}