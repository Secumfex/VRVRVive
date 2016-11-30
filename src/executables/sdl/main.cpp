/*******************************************
 * **** DESCRIPTION ****
 ****************************************/

#include <iostream>
#include <Rendering/GLTools.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <UI/imgui/imgui.h>
#include <UI/imgui_impl_glfw_gl3.h>
#include <UI/imgui_impl_sdl_gl3.h>


static const int TEXTURE_SIZE = 512;
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

class CMainApplication
{
private: // SDL bookkeeping
	SDL_Window *m_pWindow;
	SDL_GLContext m_pContext;
	GLFWwindow *m_pGLFWwindow;
public:
	CMainApplication( int argc, char *argv[] ){
		DEBUGLOG->setAutoPrint(true);

		Uint32 unWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
		m_pWindow = generateWindow_SDL(SCREEN_WIDTH, SCREEN_HEIGHT, 100, 100, unWindowFlags );
		
		printOpenGLInfo();

		printSDLRenderDriverInfo();

		// init imgui
		ImGui_ImplSdlGL3_Init(m_pWindow);
	}

	void loop(){
	    bool show_test_window = true;
		ImVec4 clear_color = ImColor(114, 144, 154);

		while ( !shouldClose(m_pWindow) )
		{
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplSdlGL3_NewFrame(m_pWindow);
			pollSDLEvents(m_pWindow, ImGui_ImplSdlGL3_ProcessEvent);

			{
				static float f = 30.0f;
				static float pos_x = 30.0f;
				static float pos_x2 = 30.0f;
				ImGui::SliderFloat("spacing", &f, 0.0f, 100.0f);
				ImGui::SliderFloat("pos_x", &pos_x, 0.0f, 100.0f);
				ImGui::SliderFloat("pos_x2", &pos_x2, 0.0f, 100.0f);
				ImGui::ColorEdit3("clear color", (float*)&clear_color);
				glClearColor(clear_color.x,clear_color.y,clear_color.z,0.0);
				if (ImGui::Button("Test Window")) show_test_window ^= 1;
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

				
				ImGui::Columns(4, "mycolumns"); // 4-ways, with border
				ImGui::SetColumnOffset(0,0);
				ImGui::SetColumnOffset(1,100);
				ImGui::SetColumnOffset(2,200);
				ImGui::SetColumnOffset(3,300);
			   
				// column 0
					ImGui::BeginGroup();
						ImGui::ProgressBar(0.1f, ImVec2(30.0f,0.0));
						ImGui::ProgressBar(0.2f, ImVec2(20.0f,0.0));
					ImGui::EndGroup();
				ImGui::NextColumn();
				
				//column 1
				ImGui::BeginGroup();
						ImGui::ProgressBar(0.1f, ImVec2(30.0f,0.0));
						ImGui::ProgressBar(0.2f, ImVec2(20.0f,0.0));
					ImGui::EndGroup();
				ImGui::SameLine(0.0f,10.0f);
				ImGui::ProgressBar(1.0f, ImVec2(10.0f,0.0));
				ImGui::NextColumn();
				
				//column 2
				ImGui::Text("Colum 2");
				ImGui::NextColumn();
				
				//column 3
				ImGui::Text("Colum 3");
				ImGui::NextColumn();

			}

			ImGui::Render();
			SDL_GL_SwapWindow( m_pWindow );
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
	CMainApplication *pMainApplication = new CMainApplication( argc, argv );

	pMainApplication->loop();

	return 0;
}