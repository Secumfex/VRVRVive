#include "Rendering/PostProcessing.h"

#include <Rendering/FrameBufferObject.h>
#include <Rendering/VertexArrayObjects.h>

#include <UI/imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

#include "Rendering/OpenGLContext.h"

namespace{float log_2( float n )  
{  
    return log( n ) / log( 2 );      // log(n)/log(2) is log_2. 
}}

PostProcessing::BoxBlur::BoxBlur(int width, int height, Quad* quad)
	: m_pushShaderProgram("/screenSpace/fullscreen.vert", "/screenSpace/pushBoxBlur.frag" )
	, m_height(height)
	, m_width(width)
{
	if (quad == nullptr){
		m_quad = new Quad();
		ownQuad = true;
	}else{
		m_quad = quad;
		ownQuad = false;
	}

	glGenTextures(1, &m_mipmapTextureHandle);
	OPENGLCONTEXT->bindTexture(m_mipmapTextureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width,height, 0, GL_RGBA, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE); // does this do anything?

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT); // does this do anything?
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT); 

	glGenerateMipmap(GL_TEXTURE_2D);
	int mipmapNumber = (int) log_2( (float) std::max(width,height) );

	m_mipmapFBOHandles.resize(mipmapNumber);
	glGenFramebuffers(mipmapNumber, &m_mipmapFBOHandles[0]);

	for ( int i = 0; i < mipmapNumber; i++)
	{
		OPENGLCONTEXT->bindFBO(m_mipmapFBOHandles[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_mipmapTextureHandle, i);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
	}

	OPENGLCONTEXT->bindFBO(0);

	m_pushShaderProgram.bindTextureOnUse("tex", m_mipmapTextureHandle);
}

void PostProcessing::BoxBlur::pull()
{
	OPENGLCONTEXT->bindTexture(m_mipmapTextureHandle);
	glGenerateMipmap(GL_TEXTURE_2D);
}

void PostProcessing::BoxBlur::push(int numLevels, int beginLevel)
{
	// check boundaries
	GLboolean depthTestEnableState = OPENGLCONTEXT->isEnabled(GL_DEPTH_TEST);
	if (depthTestEnableState) {OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);}
	m_pushShaderProgram.use();
	for (int level = std::min( (int) m_mipmapFBOHandles.size()-2, beginLevel+numLevels-1); level >= beginLevel; level--)
	{
		OPENGLCONTEXT->bindFBO(m_mipmapFBOHandles[level]);
		m_pushShaderProgram.update("level", level);
		m_quad->draw();
	}
	OPENGLCONTEXT->bindFBO(0);
	if (depthTestEnableState){OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);}
}

PostProcessing::BoxBlur::~BoxBlur()
{
	if (ownQuad) {delete m_quad;}
}

PostProcessing::DepthOfField::DepthOfField(int width, int height, Quad* quad)
	: m_calcCoCShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessCircleOfConfusion.frag")
	, m_dofShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessDOF.frag")
	, m_dofCompShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessDOFCompositing.frag")
	, m_width(width)
	, m_height(height)
	, m_focusPlaneDepths(2.0,4.0,7.0,10.0)
	, m_focusPlaneRadi(10.0f, -5.0f)
	, m_farRadiusRescale(2.0f)
	, m_disable_near_field(false)
	, m_disable_far_field(false)
{
	if (quad == nullptr){
		m_quad = new Quad();
		ownQuad = true;
	}else{
		m_quad = quad;
		ownQuad = false;
	}

	FrameBufferObject::s_internalFormat = GL_RGBA32F;
	m_cocFBO 	 = new FrameBufferObject(m_calcCoCShader.getOutputInfoMap(), width, height);
	m_hDofFBO 	 = new FrameBufferObject(m_dofShader.getOutputInfoMap(), width / 4, height );
	m_vDofFBO 	 = new FrameBufferObject(m_dofShader.getOutputInfoMap(), width / 4, height / 4);
	FrameBufferObject::s_internalFormat = GL_RGBA;
	m_dofCompFBO = new FrameBufferObject(m_dofCompShader.getOutputInfoMap(), width, height );
	
	for ( auto t : m_vDofFBO->getColorAttachments() )
	{
		OPENGLCONTEXT->bindTexture(t.second);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	
	m_dofCompShader.bindTextureOnUse("sharpFocusField", m_cocFBO->getBuffer("fragmentColor"));
	m_dofCompShader.bindTextureOnUse("blurryNearField", m_vDofFBO->getBuffer("nearResult"));
	m_dofCompShader.bindTextureOnUse("blurryFarField" , m_vDofFBO->getBuffer("blurResult"));

	// default settings
	m_calcCoCShader.update("focusPlaneDepths", m_focusPlaneDepths);
	m_calcCoCShader.update("focusPlaneRadi",   m_focusPlaneRadi);

	m_dofShader.update("maxCoCRadiusPixels", (int) m_focusPlaneRadi.x);
	m_dofShader.update("nearBlurRadiusPixels", (int) m_focusPlaneRadi.x);
	m_dofShader.update("invNearBlurRadiusPixels", 1.0f / m_focusPlaneRadi.x);

	m_dofCompShader.update("maxCoCRadiusPixels", m_focusPlaneRadi.x);
	m_dofCompShader.update("farRadiusRescale" , m_farRadiusRescale);
}

PostProcessing::DepthOfField::~DepthOfField()
{
	if (ownQuad) {delete m_quad;}
}

void PostProcessing::DepthOfField::execute(GLuint positionMap, GLuint colorMap)
{
	// setup
	GLboolean depthTestEnableState = glIsEnabled(GL_DEPTH_TEST);
	if (depthTestEnableState) {glDisable(GL_DEPTH_TEST);}

	// compute COC map
	OPENGLCONTEXT->setViewport(0,0,m_width, m_height);
	m_cocFBO->bind();
	m_calcCoCShader.updateAndBindTexture("colorMap", 0, colorMap);
	m_calcCoCShader.updateAndBindTexture("positionMap", 1, positionMap);
	m_calcCoCShader.use();
	m_quad->draw();

	// compute DoF
	// horizontal pass 
	OPENGLCONTEXT->setViewport(0, 0, m_hDofFBO->getWidth(), m_hDofFBO->getHeight());
	m_hDofFBO->bind();
	m_dofShader.use();
	m_dofShader.update("HORIZONTAL", true);
	m_dofShader.updateAndBindTexture("blurSourceBuffer", 1, m_cocFBO->getBuffer("fragmentColor"));
	m_quad->draw();

	// vertical pass
	OPENGLCONTEXT->setViewport(0,0,m_vDofFBO->getWidth(), m_vDofFBO->getHeight());
	m_vDofFBO->bind();
	m_dofShader.update("HORIZONTAL", false);
	m_dofShader.updateAndBindTexture("blurSourceBuffer", 1, m_hDofFBO->getBuffer("blurResult"));
	m_dofShader.updateAndBindTexture("nearSourceBuffer", 2, m_hDofFBO->getBuffer("nearResult"));
	m_quad->draw();

	m_dofCompFBO->bind();
	m_dofCompShader.use();
	m_quad->draw();
} 

void PostProcessing::DepthOfField::imguiInterfaceEditParameters()
{
	// ImGui interface
	ImGui::DragFloat4("depths", glm::value_ptr(m_focusPlaneDepths),0.1f,0.0f);
	// ImGui::SliderFloat2("near/far radi", glm::value_ptr(m_focusPlaneRadi), -10.0f, 10.0f);

	// ImGui::SliderFloat("far radius rescale", &m_farRadiusRescale, 0.0f, 5.0f);
	ImGui::Checkbox("disable near field", &m_disable_near_field);
	ImGui::Checkbox("disable far field", &m_disable_far_field);
}

void PostProcessing::DepthOfField::updateUniforms()
{
	// update uniforms
	m_calcCoCShader.update("focusPlaneDepths", m_focusPlaneDepths);
	m_calcCoCShader.update("focusPlaneRadi",   m_focusPlaneRadi);

	m_dofShader.update("maxCoCRadiusPixels", (int) m_focusPlaneRadi.x);
	m_dofShader.update("nearBlurRadiusPixels", (int) m_focusPlaneRadi.x);
	m_dofShader.update("invNearBlurRadiusPixels", 1.0f / m_focusPlaneRadi.x);

	m_dofCompShader.update("maxCoCRadiusPixels", m_focusPlaneRadi.x);
	m_dofCompShader.update("farRadiusRescale" , m_farRadiusRescale);

	m_dofCompShader.update("disableNearField", m_disable_near_field);
	m_dofCompShader.update("disableFarField" , m_disable_far_field);
}

PostProcessing::SkyboxRendering::SkyboxRendering(std::string fShader, std::string vShader, Renderable* skybox)
	: m_skyboxShader(vShader, fShader)
{
	if (skybox == nullptr)
	{
		m_skybox = new Skybox();
		ownSkybox = true;
	}
	else{
		m_skybox = skybox;
		ownSkybox = false;
	}

}
PostProcessing::SkyboxRendering::~SkyboxRendering()
{
	if (ownSkybox)
	{
		delete m_skybox;
	}
}
void PostProcessing::SkyboxRendering::render(GLuint cubeMapTexture, FrameBufferObject* target)
{
	OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, true);
	glDepthFunc(GL_LEQUAL);
	
	if(target != nullptr) {target->bind();}
	else{ OPENGLCONTEXT->bindFBO(0); }

	m_skyboxShader.use();
	m_skyboxShader.update("skybox", 0);
	OPENGLCONTEXT->bindTextureToUnit(cubeMapTexture, GL_TEXTURE0, GL_TEXTURE_CUBE_MAP);
	m_skybox->draw();
	glDepthFunc(GL_LESS);
	OPENGLCONTEXT->setEnabled(GL_DEPTH_TEST, false);
}

PostProcessing::SunOcclusionQuery::SunOcclusionQuery(GLuint depthTexture, glm::vec2 textureSize, Renderable* sun)
	: m_occlusionShader("/screenSpace/postProcessSunOcclusionTest.vert", "/screenSpace/postProcessSunOcclusionTest.frag")
{
	m_occlusionFBO = new FrameBufferObject(m_occlusionShader.getOutputInfoMap(),16,16);
	if (sun == nullptr)
	{
		m_sun = new Quad();
		ownRenderable = true;
	}
	else{
		m_sun = sun;
		ownRenderable = false;
	}

	glGenQueries(1, &mQueryId);

	if (depthTexture != -1)
	{
		m_occlusionShader.bindTextureOnUse("depthTexture", depthTexture);
		m_occlusionShader.update("invTexRes", glm::vec2(1.0f / textureSize.x, 1.0f / textureSize.y));
	}
}

PostProcessing::SunOcclusionQuery::~SunOcclusionQuery()
{
 	glDeleteQueries(1, &mQueryId);
	delete m_occlusionFBO;
}


GLuint PostProcessing::SunOcclusionQuery::performQuery(const glm::vec4& sunScreenPos)
{
	m_occlusionShader.update("lightData", sunScreenPos);

	glBeginQuery(GL_SAMPLES_PASSED, mQueryId);
	
	m_occlusionShader.use();
	m_occlusionFBO->bind();
	glClear(GL_COLOR_BUFFER_BIT);
	m_sun->draw();

	GLuint queryState = GL_FALSE;
	glEndQuery(GL_SAMPLES_PASSED);
	while ( queryState != GL_TRUE)
	{
		glGetQueryObjectuiv(mQueryId, GL_QUERY_RESULT_AVAILABLE, &queryState);
	}
	glGetQueryObjectuiv(mQueryId, GL_QUERY_RESULT, &lastNumVisiblePixels);

	return lastNumVisiblePixels;
}

#include <Importing/stb_image.h>
//#include <Rendering/GLTools.h>
GLuint PostProcessing::LensFlare::loadLensColorTexture(std::string resourcesPath)
{
	std::string fileName = resourcesPath +  "/lenscolor.png";
	std::string fileString = std::string(fileName);
	fileString = fileString.substr(fileString.find_last_of("/"));

	int width, height, bytesPerPixel;
    unsigned char *data = stbi_load(fileName.c_str(), &width, &height, &bytesPerPixel, 0);

    if(data == NULL){
    	DEBUGLOG->log("ERROR : Unable to open image " + fileString);
    	  return -1;}

    //create new texture
    GLuint textureHandle;
    glGenTextures(1, &textureHandle);
 
    //bind the texture
	OPENGLCONTEXT->bindTexture(textureHandle,GL_TEXTURE_1D );

    //send image data to the new texture
    if (bytesPerPixel < 3) {
    	DEBUGLOG->log("ERROR : Unable to open image " + fileString);
        return -1;
    } else if (bytesPerPixel == 3){
        glTexImage1D(GL_TEXTURE_1D, 0,GL_RGB, width, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    } else if (bytesPerPixel == 4) {
        glTexImage1D(GL_TEXTURE_1D, 0,GL_RGBA, width, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    } else {
    	DEBUGLOG->log("Unknown format for bytes per pixel... Changed to \"4\"");
        glTexImage1D(GL_TEXTURE_1D, 0,GL_RGBA, width, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }

    //texture settings
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_1D);

    stbi_image_free(data);
    DEBUGLOG->log( "SUCCESS: image loaded from " + fileString );

	return textureHandle;
}

#include <Importing/TextureTools.h>

PostProcessing::LensFlare::LensFlare(int width, int height)
	: m_downSampleShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessLFDownSampling.frag")
	, m_ghostingShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessLFGhosting.frag")
	, m_upscaleBlendShader("/screenSpace/fullscreen.vert", "/screenSpace/postProcessLFUpscaleBlend.frag")
	,m_scale(1.1f)
	,m_bias(-0.58f)
	,m_num_ghosts(3)
	,m_blur_strength(3)
	,m_ghost_dispersal(0.6f)
	,m_halo_width(0.25f)
	,m_distortion(5.0f)
	,m_strength(2.5f)
{
	m_downSampleFBO = new FrameBufferObject(m_downSampleShader.getOutputInfoMap(),width,height);
	m_featuresFBO = new FrameBufferObject(m_ghostingShader.getOutputInfoMap(),width,height);

	// change texture filtering parameters
	OPENGLCONTEXT->bindTexture(m_downSampleFBO->getBuffer("fResult"));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	OPENGLCONTEXT->bindTexture(0);
	
	// 1D texture
	m_lensColorTexture = loadLensColorTexture();
	// 2D textures
	m_lensDirtTexture = TextureTools::loadTextureFromResourceFolder("lensdirt.png");
	m_lensStarTexture = TextureTools::loadTextureFromResourceFolder("lensstar.png");
	
	// box blur
	m_boxBlur = new BoxBlur(width / 2, height / 2, &m_quad);

	// default values
	m_downSampleShader.update("uScale", glm::vec4(m_scale));
	m_downSampleShader.update("uBias",  glm::vec4(m_bias));
	m_ghostingShader.update("uGhosts", m_num_ghosts);
	m_ghostingShader.update("uGhostDispersal",m_ghost_dispersal);
	m_ghostingShader.update("uHaloWidth", m_halo_width);
	m_ghostingShader.update("uDistortion", m_distortion);
	m_upscaleBlendShader.update("strength", m_strength);
	m_upscaleBlendShader.update("uLensStarMatrix", glm::mat3(1.0f));

	// default texture bindings
	m_ghostingShader.bindTextureOnUse("uInputTex", m_downSampleFBO->getBuffer("fResult"));
	m_upscaleBlendShader.bindTextureOnUse("uLensFlareTex", m_boxBlur->m_mipmapTextureHandle);
	m_upscaleBlendShader.bindTextureOnUse("uLensDirtTex", m_lensDirtTexture);
	m_upscaleBlendShader.bindTextureOnUse("uLensStarTex", m_lensStarTexture);
}

PostProcessing::LensFlare::~LensFlare()
{
	delete m_downSampleFBO;
	delete m_featuresFBO;
}

void PostProcessing::LensFlare::renderLensFlare(GLuint sourceTexture, FrameBufferObject* target)
{
	GLint temp_viewport[4];
	if ( target == 0)
	{
		glGetIntegerv( GL_VIEWPORT, temp_viewport );
	}
	// downsample
	m_downSampleFBO->bind();
	glClear(GL_COLOR_BUFFER_BIT);
	m_downSampleShader.updateAndBindTexture("uInputTex", 0, sourceTexture);
	m_downSampleShader.use();
	m_quad.draw();

	// produce features
	m_featuresFBO->bind();
	glClear(GL_COLOR_BUFFER_BIT);
	m_ghostingShader.updateAndBindTexture("uLensColor", 1, m_lensColorTexture, GL_TEXTURE_1D);
	m_ghostingShader.use();
	m_quad.draw();

	// copy content to box blur fbo
	OPENGLCONTEXT->bindFBO(m_featuresFBO->getFramebufferHandle(), GL_READ_FRAMEBUFFER);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	OPENGLCONTEXT->bindFBO(m_boxBlur->m_mipmapFBOHandles[0], GL_DRAW_FRAMEBUFFER);
	glBlitFramebuffer(0,0,m_featuresFBO->getWidth(), m_featuresFBO->getHeight(), 0,0, m_boxBlur->m_width, m_boxBlur->m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	OPENGLCONTEXT->bindFBO(0);

	// blur
	m_boxBlur->pull();
	m_boxBlur->push( m_blur_strength );

	// render to target fbo
	if ( target != nullptr )
	{
		target->bind();
	}
	else{
		OPENGLCONTEXT->bindFBO(0);
		OPENGLCONTEXT->setViewport( temp_viewport[0], temp_viewport[1], temp_viewport[2], temp_viewport[3]);
	}
	m_upscaleBlendShader.updateAndBindTexture("uInputTex", m_upscaleBlendShader.getTextureMap()->size(), sourceTexture);
	m_upscaleBlendShader.use();
	m_quad.draw();
}

#include <glm/gtx/transform.hpp>
glm::mat3 PostProcessing::LensFlare::updateLensStarMatrix(const glm::mat3& view)
{
	glm::vec3 camx = - view[0]; // camera x (left) vector
	glm::vec3 camz = - view[2]; // camera z (forward) vector
	float camrot = glm::dot(camx, glm::vec3(0,0,1)) + glm::dot(camz, glm::vec3(0,1,0));

	glm::mat3 scaleBias1(
		2.0f,   0.0f, 0.0f,
		0.0f,   2.0f,  -0.0f,
		-1.0f,   -1.0f,   1.0f
	);
	glm::mat3 rotation(
		cos(camrot), sin(camrot), 0.0f,
		-sin(camrot), cos(camrot),  0.0f,
		0.0f,        0.0f,         1.0f
	);
	glm::mat3 scaleBias2(
		0.5f,   0.0f, 0.0f,
		0.0f,   0.5f,  0.0f,
		 0.5f,   0.5f ,   1.0f
	);

	glm::mat3 uLensStarMatrix = scaleBias2 * rotation * scaleBias1;

	m_upscaleBlendShader.update("uLensStarMatrix", uLensStarMatrix);
		
	return uLensStarMatrix;
}

glm::mat3 PostProcessing::LensFlare::updateLensStarMatrix(const glm::mat4& view)
{
	return updateLensStarMatrix(glm::mat3(view));
}

void PostProcessing::LensFlare::imguiInterfaceEditParameters()
{
	ImGui::SliderFloat("bias",			 &m_bias, -1.0f, 0.0f);
	ImGui::SliderFloat("scaling factor", &m_scale, 0.0f, 20.0f);
	ImGui::SliderFloat("halo width",	 &m_halo_width, 0.0f, 5.0f);
	ImGui::SliderFloat("chrom. distort", &m_distortion, 0.0f, 10.0f);
	ImGui::SliderInt("num ghosts",		 &m_num_ghosts, 0, 10);
	ImGui::SliderInt("blur strength",	 &m_blur_strength, 0, 7);
	ImGui::SliderFloat("ghost dispersal",&m_ghost_dispersal, 0.0f, 3.0f);
	ImGui::SliderFloat("add strength",	 &m_strength, 0.0f, 10.0f);
}

void PostProcessing::LensFlare::updateUniforms()
{
	m_downSampleShader.update("uScale", glm::vec4(m_scale));
	m_downSampleShader.update("uBias",  glm::vec4(m_bias));
	m_ghostingShader.update("uGhosts", m_num_ghosts);
	m_ghostingShader.update("uGhostDispersal", m_ghost_dispersal);
	m_ghostingShader.update("uHaloWidth",  m_halo_width);
	m_ghostingShader.update("uDistortion",  m_distortion);
	m_upscaleBlendShader.update("strength", m_strength);
}
