/*******************************************
 * **** DESCRIPTION ****
 ****************************************/
#ifdef MINGW_THREADS
 	#include <mingw-std-threads/mingw.thread.h>
#else
 	#include <thread>
#endif
#include <atomic>

#include <windows.h>
#include <mmsystem.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <Importing/AssimpTools.h>

#include <Rendering/GLTools.h>
#include <Rendering/Renderpass.h>
#include <Rendering/VertexArrayObjects.h>
#include <iostream>
#include <SDL.h>
#include <SDL_opengl.h>
#include <UI/imgui/imgui.h>
#include <UI/imgui_impl_glfw_gl3.h>
#include <UI/imgui_impl_sdl_gl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>

const double NANOSECONDS_TO_MILLISECONDS = 1.0 / 1000000.0;

const int NUM_INSTANCES = 1000;

static const int TEXTURE_SIZE = 512;
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;


float randFloat(float min, float max) //!< returns a random number between min and max
{
	return (((float) rand() / (float) RAND_MAX) * (max - min) + min); 
}

class SlowThread
{
public:
	SlowThread(Renderable* renderable, ShaderProgram* shaderProgram, FrameBufferObject* fbo);
	~SlowThread();

	//////  uniforms
	std::vector<glm::mat4> modelMatrices;
	std::vector<glm::vec4> colors;
	void generateModels(float xSize, float zSize, float ySize);

	//////  RENDERING PARAMETERS
	int  curDrawIdx;
	int colorOffset; // to keep things interesting
	int  numDrawsPerCall;
	void render(); //perform (partial) render operation
	Renderable* renderable;
	ShaderProgram* shaderProgram;
	FrameBufferObject* fbo;

	////// PROFILING PARAMETERS
	int curFidx; // current frame index
	std::vector<float> renderTimes;
	float renderTime;
	float targetRenderTime;
	void profileRendertime();
	GLuint m_queryObjects[1]; // for render time

	////// RENDER LOAD ADJUSTMENTS
	float adjustmentFactor;
	void adjustRenderload();

	bool isFinished;
};

void SlowThread::profileRendertime(){
		GLint available = 0;
		GLuint timeElapsed = 0;

		// Wait for all results to become available
		while (!available) {
			glGetQueryObjectiv(m_queryObjects[0], GL_QUERY_RESULT_AVAILABLE, &available);
		}
		glGetQueryObjectuiv(m_queryObjects[0], GL_QUERY_RESULT, &timeElapsed);

		// convert to ms
		renderTime = (float) (NANOSECONDS_TO_MILLISECONDS * (double) timeElapsed);

		// save for profiling
		renderTimes[curFidx] = renderTime;
		curFidx = (curFidx + 1) % renderTimes.size();
	}

void SlowThread::generateModels(float xSize, float zSize, float ySize)
{
	modelMatrices.resize(NUM_INSTANCES);
	colors.resize(NUM_INSTANCES);

	for (int i = 0; i < modelMatrices.size(); i++)
	{
		float r = randFloat(0.1,1.0);
		float g = randFloat(0.1,1.0);
		float b = randFloat(0.1,1.0);
		colors[i] = glm::vec4(r,g,b,1.0);

		// generate random position on x/z plane
		float x = randFloat(-xSize * 0.5f, xSize * 0.5f);
		float z = randFloat(-zSize * 0.5f, zSize * 0.5f);
		float y = randFloat(-ySize* 0.5f, ySize * 0.5f);
		modelMatrices[i] = glm::scale(glm::vec3(1.0,1.0,1.0)); 
		modelMatrices[i] = glm::translate(glm::vec3(x, y, z)) * modelMatrices[i];
	}
}

void SlowThread::render()
{
	glGenQueries(1, m_queryObjects);

	fbo->bind();
	shaderProgram->use();

	int maxIdx = curDrawIdx + numDrawsPerCall;
	bool finishes = false;
	if (maxIdx > NUM_INSTANCES)
	{
		finishes = true;
		maxIdx = NUM_INSTANCES;
	}
	
	glBeginQuery(GL_TIME_ELAPSED, m_queryObjects[0]);
	renderable->bind();
	for(int i = curDrawIdx; i < maxIdx; i++)
	{
		shaderProgram->update("model", modelMatrices[i]);
		shaderProgram->update("color", colors[( i + colorOffset ) % colors.size()]);
		
		renderable->draw();
	}	
	glEndQuery(GL_TIME_ELAPSED);

	if(finishes)
	{
		curDrawIdx = 0;
		colorOffset+=1;
		isFinished = true;
	}
	else
	{
		isFinished = false;
		curDrawIdx = maxIdx;
	}
}

void SlowThread::adjustRenderload()
{
	float avgRenderTime = (renderTimes[(curFidx-1)%renderTimes.size()] + renderTimes[(curFidx-2)%renderTimes.size()] + renderTimes[(curFidx-3)%renderTimes.size()]) / 3.0f;

	float avgTDelta = (targetRenderTime - renderTimes[(curFidx-1)%renderTimes.size()] 
		+ targetRenderTime - renderTimes[(curFidx-2)%renderTimes.size()] 
		+ targetRenderTime - renderTimes[(curFidx-3)%renderTimes.size()]) 
		/ 3.0f; // spare (or over) time
	
	float tDraw = avgRenderTime / (float) numDrawsPerCall; //aprox render time per draw call

	int numCalls = (int) (avgTDelta / tDraw) * adjustmentFactor; // how many draws fit into tDelta?

	// add or remove calls
	if (numCalls != 0)
	{
		numDrawsPerCall = numDrawsPerCall + numCalls;
	}
	else // fractional result
	{
		if (avgTDelta < 0.0f) // should remove calls
		{
			numDrawsPerCall--;
		}
		else
		{
			numDrawsPerCall++;
		}
	}
}

SlowThread::SlowThread(Renderable* renderable, ShaderProgram* shaderProgram, FrameBufferObject* fbo)
	: renderTimes(256,0.0f),
	curFidx(0),
	curDrawIdx(0),
	numDrawsPerCall(20),
	renderable(renderable),
	shaderProgram(shaderProgram),
	fbo(fbo),
	isFinished(false),
	colorOffset(0),
	targetRenderTime(16.0f),
	renderTime(10.0f),
	adjustmentFactor(0.5f)
{
	generateModels(3.0f, 3.0f, 3.0f);
}

SlowThread::~SlowThread()
{

}


class CMainApplication
{
private: // SDL bookkeeping
	SDL_Window	 *m_pWindow;
	SDL_GLContext m_pContext;
	GLFWwindow	 *m_pGLFWwindow;

	RenderPass*	m_pRenderThread;
	SlowThread*		m_pSlowThread;
	
	Renderable*			m_pRenderable;
	Quad*				m_pQuad;
	ShaderProgram*		m_pSlowShader;
	ShaderProgram*		m_pFastShader;
	FrameBufferObject*	m_pFbo;
	FrameBufferObject*	m_pFboDisplay[2];
	int					m_activeDisplayFBO;
public:
	CMainApplication( int argc, char *argv[] ):
		m_activeDisplayFBO(0)
	{
		DEBUGLOG->setAutoPrint(true);

		Uint32 unWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
		m_pWindow = generateWindow_SDL(SCREEN_WIDTH, SCREEN_HEIGHT, 100, 100, unWindowFlags );
		
		printOpenGLInfo();

		printSDLRenderDriverInfo();

		// init imgui
		ImGui_ImplSdlGL3_Init(m_pWindow);
	}

	void loadObject()
	{
		DEBUGLOG->log("loading object");
		std::string file = ADDITIONAL_RESOURCES_PATH "/Stanford/bunny/blender_bunny.dae";

		// import using ASSIMP and check for errors
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile( file, aiProcessPreset_TargetRealtime_MaxQuality);
		if (scene == NULL)
		{
			DEBUGLOG->log("ERROR: could not load model");
		}

		// print some asset info
		DEBUGLOG->log("Asset (scene) info: ");	DEBUGLOG->indent();
			DEBUGLOG->log("has meshes: "	 , scene->HasMeshes());
			DEBUGLOG->log("num meshes: "     , scene->mNumMeshes); DEBUGLOG->indent();
			for ( unsigned int i = 0; i < scene->mNumMeshes ; i++ )
			{
				aiMesh* m = scene->mMeshes[i];
				DEBUGLOG->log(std::string("mesh ") + DebugLog::to_string(i) + std::string(": ") + std::string( m->mName.C_Str() ));
			}
			for ( unsigned int i = 0; i < scene->mNumMaterials; i++ )
			{
				DEBUGLOG->log(std::string("material ") + DebugLog::to_string(i) + std::string(": "));
				auto matInfo = AssimpTools::getMaterialInfo(scene, i);
				DEBUGLOG->indent();
					AssimpTools::printMaterialInfo(matInfo);
				DEBUGLOG->outdent();
			}
			DEBUGLOG->outdent();
		DEBUGLOG->outdent();

		auto vertexData  = AssimpTools::createVertexDataInstancesFromScene(scene);
		auto renderables = AssimpTools::createSimpleRenderablesFromVertexDataInstances(vertexData);
		
		m_pRenderable = renderables[0];
		m_pQuad = new Quad();
	}

	void loadShaders()
	{
		glm::vec4 eye(0.0f, 0.0f, 5.0f, 1.0f);
		glm::vec4 center(0.0f,0.0f,0.0f,1.0f);
		glm::mat4 view = glm::lookAt(glm::vec3(eye), glm::vec3(center), glm::vec3(0,1,0));
		glm::mat4 perspective = glm::perspective(glm::radians(65.f), ((float)(SCREEN_WIDTH/2)/(float)SCREEN_HEIGHT), 0.1f, 10.f);

		DEBUGLOG->log("Render Configuration: Slow Rendering"); DEBUGLOG->indent();
		m_pSlowShader = new ShaderProgram("/modelSpace/modelViewProjection.vert", "/modelSpace/simpleLighting.frag");
		m_pFastShader = new ShaderProgram("/screenSpace/fullscreen.vert", "/modelSpace/simpleColor.frag");
		m_pFbo		  = new FrameBufferObject(m_pSlowShader->getOutputInfoMap(), SCREEN_WIDTH/2, SCREEN_HEIGHT);
		m_pFboDisplay[0] = new FrameBufferObject(m_pFastShader->getOutputInfoMap(), SCREEN_WIDTH/2, SCREEN_HEIGHT);
		m_pFboDisplay[1] = new FrameBufferObject(m_pFastShader->getOutputInfoMap(), SCREEN_WIDTH/2, SCREEN_HEIGHT);

		m_pSlowShader->update("view", view);
		m_pSlowShader->update("projection", perspective);
		m_pSlowShader->update("color", glm::vec4(1.0));

		m_pFastShader->bindTextureOnUse("tex", m_pFboDisplay[0]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		m_pFastShader->update("blendColor", 0.5f);
		DEBUGLOG->outdent();
	}


	void initThreads()
	{
		m_pSlowThread = new SlowThread(m_pRenderable, m_pSlowShader, m_pFbo);
		m_pRenderThread = new RenderPass(m_pFastShader, 0);
		m_pRenderThread->setViewport(SCREEN_WIDTH/2,0,SCREEN_WIDTH/2,SCREEN_HEIGHT);
		m_pRenderThread->addRenderable(m_pQuad);
		m_pRenderThread->addClearBit(GL_DEPTH_BUFFER_BIT);
	}

	void updateFastRenderThread()
	{
		// copy
		copyFBOContent(m_pFbo->getFramebufferHandle(), m_pFboDisplay[(m_activeDisplayFBO+1)%2]->getFramebufferHandle(), glm::vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT), glm::vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT), GL_COLOR_BUFFER_BIT);

		// then clear
		m_pFbo->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//switch active display fbo
		m_pFastShader->bindTextureOnUse("tex", m_pFboDisplay[(m_activeDisplayFBO+1)%2]->getColorAttachmentTextureHandle(GL_COLOR_ATTACHMENT0) );
		m_activeDisplayFBO = (m_activeDisplayFBO+1)%2;

	}


	void loop(){

		ImVec4 clear_color = ImColor(114, 144, 154);

		while ( !shouldClose(m_pWindow) )
		{
			ImGui_ImplSdlGL3_NewFrame(m_pWindow);
			pollSDLEvents(m_pWindow, ImGui_ImplSdlGL3_ProcessEvent);
			
			static bool autoCorrect = false;
			{
				static float f = 10.0f;
				ImGui::Checkbox("auto adjust render time", &autoCorrect);
				ImGui::ColorEdit3("clear color", (float*)&clear_color);
				glClearColor(clear_color.x,clear_color.y,clear_color.z,0.0);
				ImGui::SliderInt("Num Draws per Call",&m_pSlowThread->numDrawsPerCall, 1,NUM_INSTANCES);
				ImGui::SliderFloat("Target Rendertime",&m_pSlowThread->targetRenderTime, 1.0, 30.0f);
				ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2,0.8,0.2,1.0) );
				ImGui::PlotLines("FPS", &m_pSlowThread->renderTimes[0], m_pSlowThread->renderTimes.size(), 0, NULL, 0.0, 30.0, ImVec2(120,60));
				ImGui::PopStyleColor();
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

				float r = sin( ImGui::GetTime() * 4.0f ) * 0.25 + 0.75f;
				float g = cos( ImGui::GetTime() * 4.0f ) * 0.25 + 0.75f;
				float b = cos( ImGui::GetTime() * 4.0f + 1.42f ) * 0.25 + 0.75f;
				m_pFastShader->update( "color", glm::vec4(r,g,b,1.0) );
			}

			
			m_pSlowThread->render();

			copyFBOContent(m_pFbo->getFramebufferHandle(), 0, glm::vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT), glm::vec2(SCREEN_WIDTH/2, SCREEN_HEIGHT), GL_COLOR_BUFFER_BIT);
			m_pRenderThread->render();

			ImGui::Render();
			SDL_GL_SwapWindow( m_pWindow );

			m_pSlowThread->profileRendertime();
			if (m_pSlowThread->isFinished)
			{
				updateFastRenderThread();
			}
			else
			{
				if (autoCorrect)
				{
					m_pSlowThread->adjustRenderload( );
				}
			}

		}

		ImGui_ImplSdlGL3_Shutdown();
		destroyWindow(m_pWindow);
		SDL_Quit();
	}

	virtual ~CMainApplication(){}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	srand(time(NULL));
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	pMainApplication->loadObject();

	pMainApplication->loadShaders();

	pMainApplication->initThreads();

	pMainApplication->loop();

	return 0;
}