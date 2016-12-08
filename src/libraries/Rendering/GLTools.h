#ifndef GLTOOLS_H
#define GLTOOLS_H

#include "Core/DebugLog.h"
#include "Rendering/OpenGLContext.h"
#include <Importing/Importer.h>

#include <GL/glew.h>
#include <SDL.h>
#include <GLFW/glfw3.h>

#include <functional>
#include <glm/glm.hpp>

class FrameBufferObject;
//struct SDL_Window;

void initSDL();
void initGLFW();
void initOpenGL();

GLFWwindow* generateWindow(int width = 1280, int height = 720, int posX = 100, int posY = 100); //!< initialize OpenGL (if not yet initialized) and create a GLFW window
SDL_Window* generateWindow_SDL(int width = 1280, int height = 720, int posX = 100, int posY = 100, Uint32 unWindowFlags = (SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN)); //!< initialize OpenGL (if not yet initialized) and create a SDL window
bool shouldClose(GLFWwindow* window);
bool shouldClose(SDL_Window* window);
void swapBuffers(GLFWwindow* window);
void swapBuffers(SDL_Window* window);
void destroyWindow(GLFWwindow* window);
void destroyWindow(SDL_Window* window);
void render(GLFWwindow* window, std::function<void (double)> loop); //!< keep executing the provided loop function until the window is closed, swapping buffers and computing frame time (passed as argument to loop function)
GLenum checkGLError(bool printIfNoError = false); //!< check for OpenGL errors and also print it to the console (optionally even if no error occured)
std::string decodeGLError(GLenum error); //!< return string corresponding to an OpenGL error code (use with checkGLError)
void printOpenGLInfo();
/** print some information from the GPU about compute shader related stuff */
void printComputeShaderInfo();

void pollSDLEvents(SDL_Window* window, std::function<bool(SDL_Event*)> ui_eventHandler = [](){return false;}); //!< poll events and send to event handler, also send to ui_eventHandler

void setKeyCallback(GLFWwindow* window, std::function<void (int, int, int, int)> func); //!< set callback function called when a key is pressed
void setMouseButtonCallback(GLFWwindow* window, std::function<void (int, int, int)> func); //!< set callback function called when a mouse button is pressed
void setCharCallback(GLFWwindow* window, std::function<void (unsigned int)> func); //!< set callback function called when a unicode character is put in
void setCursorPosCallback(GLFWwindow* window, std::function<void (double, double)> func); //!< set callback function called when cursor position changes
void setScrollCallback(GLFWwindow* window, std::function<void (double, double)> func); //!< set callback function called when scrolling
void setCursorEnterCallback(GLFWwindow* window, std::function<void (int)> func); //!< set callback function called when cursor enters window
void setWindowResizeCallback(GLFWwindow* window, std::function<void (int, int)> func); //!< set callback function called when cursor enters window

void printSDLRenderDriverInfo();

SDL_GLContext getCurrentSDLGLContext();
//TODO SDL Event handler

glm::vec2 getMainWindowResolution(); //!< returns width and height of the main window (if it exists)
glm::vec2 getResolution(GLFWwindow* window);
glm::vec2 getResolution(SDL_Window* window);
float getRatio(GLFWwindow* window); //!< returns (width / height) of the window
float getRatio(SDL_Window* window); //!< returns (width / height) of the window

/**
* @brief copies the content of one frame buffer to another, either a color attachment or the depth buffer etc
* @param bitField defines the content to copy, e.g. GL_COLOR_BUFFER_BIT or GL_DEPTH_BUFFER_BIT
* @param readBuffer (optional) can be set if bitField is GL_COLOR_BUFFER_BIT, if GL_NONE is provided, the default is used (GL_COLOR_ATTACHMENT0) or (GL_BACK), if source is 0 (window)
* @param filter (optional) can be set if bitField is GL_COLOR_BUFFER_BIT, if GL_NONE is provided, GL_NEAREST is used
* @param defaultFBOSize (optional) size of 0 framebuffer object, will be read from mainWindowResolution if omitted
*/
void copyFBOContent(FrameBufferObject* source, FrameBufferObject* target, GLbitfield bitField, GLenum readBuffer = GL_NONE, GLenum filter = GL_NONE , glm::vec2 defaultFBOSize = glm::vec2(-1.0f, -1.0f));
void copyFBOContent(GLuint source, GLuint target, glm::vec2 sourceResolution, glm::vec2 targetResolution, GLenum bitField, GLenum readBuffer = GL_NONE, GLenum filter = GL_NONE); //!< like above, but without FBO class


GLuint createTexture(int width, int height, GLenum internalFormat = GL_RGBA8, GLsizei levels = 1);

template <class T>
void uploadTextureData(GLuint texture, const std::vector<T>& content, GLenum format = GL_RGB, GLenum type = GL_FLOAT, int width = -1, int height = -1, int x = 0, int y = 0, int level=0){
	OPENGLCONTEXT->bindTexture(texture);
	GLint _width = width;
	GLint _height = height;
	if ( width  == -1 ) { glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH,  &_width ); }
	if ( height == -1 ) { glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &_height); }
	glTexSubImage2D(GL_TEXTURE_2D, level, x,y, _width, _height, format, type, &content[0] );
	OPENGLCONTEXT->bindTexture(0);
}

template <class T>
GLuint bufferData(const std::vector<T>& content, GLenum target = GL_ARRAY_BUFFER, GLenum drawType = GL_STATIC_DRAW)
{
    GLuint vbo = 0;
	if ( content.size() != 0 )// && content.size() % dimensions == 0 )
	{
		glGenBuffers(1, &vbo);
		glBindBuffer(target, vbo);
		glBufferData(target, content.size() * sizeof(T), &content[0], drawType);
	}
    return vbo;
}

namespace {
double log_2( double n )  
{  
    return log( n ) / log( 2 );      // log(n)/log(2) is log_2. 
}}

/** upload the provided volume data to a 3D OpenGL texture object, i.e. CT-Data*/
template <typename T>
GLuint loadTo3DTexture(VolumeData<T>& volumeData, int levels = 1, GLenum internalFormat = GL_R16I, GLenum format = GL_RED_INTEGER, GLenum type = GL_SHORT)
{
	GLuint volumeTexture;

	glEnable(GL_TEXTURE_3D);
	OPENGLCONTEXT->activeTexture(GL_TEXTURE0);
	glGenTextures(1, &volumeTexture);
	OPENGLCONTEXT->bindTexture(volumeTexture, GL_TEXTURE_3D);

	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP);

	int numMipmaps = (int) log_2( (float) std::max(std::max(volumeData.size_x,volumeData.size_y),volumeData.size_z)); //max number of additional mipmap levels

	// allocate GPU memory
	glTexStorage3D(GL_TEXTURE_3D
		, std::min(levels, numMipmaps+1)
		, internalFormat
		, volumeData.size_x
		, volumeData.size_y
		, volumeData.size_z
	);

	// upload data
	glTexSubImage3D(GL_TEXTURE_3D
		, 0
		, 0
		, 0
		, 0
		, volumeData.size_x
		, volumeData.size_y
		, volumeData.size_z
		, format
		, type
		, &(volumeData.data[0])
	);

	if (levels > 1)
	{
		glGenerateMipmap(GL_TEXTURE_3D);
	}

	return volumeTexture;
}

#endif