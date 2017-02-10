#ifndef COMPUTEPASS_H
#define COMPUTEPASS_H

#include "Rendering/ShaderProgram.h"
#include "Rendering/Uniform.h"

#include <vector>

class ComputePass{
protected:
	ShaderProgram* m_shaderProgram;
	std::vector< Uploadable* > m_uniforms;

	glm::ivec3 m_localGroupSize;
public:
	/**
	 * @param shader ShaderProgram to be used with this RenderPass
	 * @param fbo (optional) leave empty or 0 to render to screen
	 */
	ComputePass(ShaderProgram* shader = 0);
	virtual ~ComputePass();

	virtual void preDispatch(); //!< executed before looping over all added Renderables, virtual method that may be overridden in a derived class  
	virtual void uploadUniforms(); //!< @deprecated calls all Uniform objects omitted using addUniform to upload their values, executed per Renderable. Currently kinda outdated/deprecated, should be revised
	virtual void dispatch(int numGroupsX, int numGroupsY = 1, int numGroupsZ = 1); //!< execute this renderpass
	virtual void postDispatch(); //!< executed after looping over all Renderables, virtual method that may be overridden in a derived class

	void setShaderProgram(ShaderProgram* shaderProgram);

	ShaderProgram* getShaderProgram();

	glm::ivec3 getLocalGroupSize();
	int getLocalGroupNumInvocations();
	void addUniform(Uploadable* uniform); //!< @deprecated add an Uploadable that should be called before drawing a Renderable 
};

#endif
