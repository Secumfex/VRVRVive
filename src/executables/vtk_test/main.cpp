/*******************************************
 * **** DESCRIPTION ****
 ****************************************/

#include <iostream>
#include <Rendering/GLTools.h>
#include <Rendering/RenderPass.h>
#include <Rendering/VertexArrayObjects.h>
#include <Importing/TextureTools.h>

// ExternalVTKWidget provides an easy way to render VTK objects in an external
// environment using the VTK rendering framework without drawing a new window.
#include "ExternalVTKWidget.h"

#include "vtkSmartPointer.h"
#include "vtkSphereSource.h"
#include "vtkPolyDataMapper.h"
#include "vtkActor.h"
#include <vtkCallbackCommand.h>
#include "vtkExternalOpenGLRenderWindow.h"
#include "vtkProperty.h"
#include "vtkCamera.h"
#include "vtkLight.h"
#include "vtkNew.h"
#include "vtkPolyDataMapper.h"
#include "vtkOpenVRRenderWindow.h"
#include "vtkOpenVRRenderer.h"
#include "vtkOpenVRCamera.h"
#include <vtkCubeSource.h>

static RenderPass* rp = nullptr;
static vtkOpenVRRenderer* ren = nullptr;
static vtkOpenVRCamera* cam = nullptr;
static vtkOpenVRRenderWindow* renWin = nullptr;
static const int TEXTURE_SIZE = 512;
const glm::vec2 WINDOW_RESOLUTION = glm::vec2( TEXTURE_SIZE, TEXTURE_SIZE);

// Global variables used by the glutDisplayFunc and glutIdleFunc
vtkNew<ExternalVTKWidget> externalVTKWidget;
static bool initialized = false;

static void MakeCurrentCallback(vtkObject* vtkNotUsed(caller),
                                long unsigned int vtkNotUsed(eventId),
                                void * vtkNotUsed(clientData),
                                void * vtkNotUsed(callData))
{
  if (initialized)
    {
		glfwMakeContextCurrent( OPENGLCONTEXT->cacheWindow );
    }
}

void handleResize(int w, int h)
{
  //externalVTKWidget->GetRenderWindow()->SetSize(w, h);
  if(rp){rp->setViewport(0,0,w,h);}
}

void renderVTK()
{
	if (!initialized)
    {
		
		//vtkNew<vtkExternalOpenGLRenderWindow> renWin;
		renWin = vtkOpenVRRenderWindow::New();
		renWin->Initialize();

		//externalVTKWidget->SetRenderWindow(renWin.GetPointer());

		// create a callback object and call when makcurrent event is fired or something
		vtkNew<vtkCallbackCommand> callback;
		callback->SetCallback(MakeCurrentCallback);
		renWin->AddObserver(vtkCommand::WindowMakeCurrentEvent,
							callback.GetPointer());

		// setup VTK rendering for the cube 
		vtkNew<vtkPolyDataMapper> mapper;
		vtkNew<vtkActor> actor;
		actor->SetMapper(mapper.GetPointer());
		
		ren = vtkOpenVRRenderer::New(); 	
		renWin->AddRenderer(ren);

		ren->AddActor(actor.GetPointer());
		vtkNew<vtkCubeSource> cs; // the cube
		mapper->SetInputConnection(cs->GetOutputPort()); // where the mapper gets the data
		actor->RotateX(45.0);
		actor->RotateY(45.0);

		cam = vtkOpenVRCamera::New();
		ren->SetActiveCamera(cam);
		ren->ResetCamera();

		initialized = true;
    }
	
	renWin->Start();

}

int main()
{
	renderVTK();
	return 0;

	auto window = generateWindow(WINDOW_RESOLUTION.x, WINDOW_RESOLUTION.y, 200, 200);
	DEBUGLOG->setAutoPrint(true);

	setWindowResizeCallback(window, handleResize);


	//////////////////////////////////////////////////////////////////////////////
	////////////////////////////// VTK RENDERING /////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////

	// load a texture
	TextureTools::TextureInfo texInfo;
	GLuint texHandle = TextureTools::loadTextureFromResourceFolder("Lena.png", &texInfo);

	// show texture
	Quad quad;
	ShaderProgram showTexShader("/screenSpace/fullscreen.vert", "/screenSpace/simpleAlphaTexture.frag");
	RenderPass showTex(&showTexShader,0);
	rp = &showTex;

	showTex.addRenderable(&quad);
	showTex.addDisable(GL_DEPTH_TEST);
	showTex.addClearBit(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	showTexShader.bindTextureOnUse("tex", texHandle);

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

		// render texture
		showTex.render();

		// render vtk object ontop
		renderVTK();

		// invalidate cached values (VTK changes them)
		//OPENGLCONTEXT->cacheVAO = -1; 
		//OPENGLCONTEXT->cacheShader = -1;
		//OPENGLCONTEXT->cacheInt.erase(OPENGLCONTEXT->cacheInt.find(GL_DEPTH_TEST));

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	return 0;
}