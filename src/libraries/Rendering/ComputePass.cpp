#include "ComputePass.h"

#include "Rendering/OpenGLContext.h"

#include <glm/gtc/type_ptr.hpp>

ComputePass::ComputePass(ShaderProgram* shaderProgram)
{
	m_shaderProgram = shaderProgram;

	GLint* params = glm::value_ptr(m_localGroupSize);
	glGetProgramiv( m_shaderProgram->getShaderProgramHandle(), GL_COMPUTE_WORK_GROUP_SIZE, params );
}

ComputePass::~ComputePass()
{
	
}

void ComputePass::setShaderProgram(ShaderProgram* shaderProgram)
{
	m_shaderProgram = shaderProgram;

	GLint* params = glm::value_ptr(m_localGroupSize);
	glGetProgramiv( m_shaderProgram->getShaderProgramHandle(), GL_COMPUTE_WORK_GROUP_SIZE, params );
}

int ComputePass::getLocalGroupNumInvocations()
{
	return m_localGroupSize.x * m_localGroupSize.y * m_localGroupSize.z;
}


ShaderProgram* ComputePass::getShaderProgram()
{
	return m_shaderProgram;
}

void ComputePass::preDispatch()
{

}

void ComputePass::postDispatch()
{

}

void ComputePass::uploadUniforms()
{
	for(unsigned int i = 0; i < m_uniforms.size(); i++)
	{
		m_uniforms[i]->uploadUniform( m_shaderProgram );
	}
}

// static glm::vec4 temp_viewport;
void ComputePass::dispatch(int numGroupsX, int numGroupsY, int numGroupsZ)
{
	m_shaderProgram->use();

	preDispatch();
	uploadUniforms();

	// dispatch
	glDispatchCompute( numGroupsX, numGroupsY, numGroupsZ );

	postDispatch();
}

void ComputePass::addUniform(Uploadable* uniform) {
	m_uniforms.push_back( uniform );
}

glm::ivec3 ComputePass::getLocalGroupSize() 
{
	return m_localGroupSize;
}
