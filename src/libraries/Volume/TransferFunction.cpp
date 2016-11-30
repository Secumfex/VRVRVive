#include "TransferFunction.h"

TransferFunction::TransferFunction()
:m_textureHandle(-1),
m_size(512),
m_transferFunctionTexData(512*4){
}

TransferFunction::~TransferFunction() {
}

GLuint TransferFunction::getTextureHandle()
{
	return m_textureHandle;
}

void TransferFunction::updateTex(int minValue, int maxValue)
{
	int currentMin = minValue;
	int currentMax = minValue+1;
	glm::vec4 currentMinCol( 0.0f, 0.0f, 0.0f, 0.0f );
	glm::vec4 currentMaxCol( 0.0f, 0.0f, 0.0f, 0.0f );

	int currentPoint = -1;
	for (unsigned int i = 0; i < m_transferFunctionTexData.size() / 4; i++)
	{
		glm::vec4 c( 0.0f, 0.0f, 0.0f, 0.0f );
		float relVal = (float) i / (float) (m_transferFunctionTexData.size() / 4);
		int v = relVal * (maxValue - minValue) + minValue;

		if (currentMax < v)
		{
			currentPoint++;
			if (currentPoint < m_values.size())
			{
				currentMin = currentMax;
				currentMinCol = currentMaxCol;

				currentMax = (int) m_values[currentPoint];
				currentMaxCol = m_colors[currentPoint];
			}
			else {
				currentMin = currentMax;
				currentMinCol = currentMaxCol;

				currentMax = maxValue;
			}
		}

		float mixParam = (float) (v - currentMin) / (float) (currentMax - currentMin);
		c = (1.0f - mixParam) * currentMinCol  + mixParam * currentMaxCol;

		m_transferFunctionTexData[i * 4 +0] = c[0];
		m_transferFunctionTexData[i * 4 +1] = c[1];
		m_transferFunctionTexData[i * 4 +2] = c[2];
		m_transferFunctionTexData[i * 4 +3] = c[3];
	}

	// Upload to texture
	if (m_textureHandle == -1)
	{
		OPENGLCONTEXT->activeTexture(GL_TEXTURE0);
		glGenTextures(1, &m_textureHandle);
		OPENGLCONTEXT->bindTexture(m_textureHandle, GL_TEXTURE_1D);

		glTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA8, m_transferFunctionTexData.size() / 4);

		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

		OPENGLCONTEXT->bindTexture(0, GL_TEXTURE_1D);
	}

	// submit data // TODO use PBO instead
	OPENGLCONTEXT->bindTexture(m_textureHandle, GL_TEXTURE_1D);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, m_transferFunctionTexData.size() / 4, GL_RGBA, GL_FLOAT, &m_transferFunctionTexData[0]);
	OPENGLCONTEXT->bindTexture(0);
}
