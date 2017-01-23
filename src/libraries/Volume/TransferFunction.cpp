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

	// see whether texture is already bound somewhere
	for (auto t : OPENGLCONTEXT->cacheTextures){ if (t.second == m_textureHandle) { OPENGLCONTEXT->activeTexture(t.first); } }
	
	// submit data // TODO use PBO instead
	OPENGLCONTEXT->bindTexture(m_textureHandle, GL_TEXTURE_1D);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, m_transferFunctionTexData.size() / 4, GL_RGBA, GL_FLOAT, &m_transferFunctionTexData[0]);
	OPENGLCONTEXT->bindTexture(0);
}

void TransferFunction::loadPreset(Preset preset, int s_minValue, int s_maxValue)
{
	m_values.clear();
	m_colors.clear();
	
	if ( preset == Preset::CT_Head )
	{
		m_values.push_back(58);
		m_colors.push_back(glm::vec4(0.0/255.0f, 0.0/255.0f, 0.0/255.0f, 0.0/255.0f));
		m_values.push_back(539);
		m_colors.push_back(glm::vec4(255.0/255.0f, 0.0/255.0f, 0.0/255.0f, 231.0/255.0f));
		m_values.push_back(572);
		m_colors.push_back(glm::vec4(0.0 /255.0f, 74.0 /255.0f, 118.0 /255.0f, 64.0 /255.0f));
		m_values.push_back(1356);
		m_colors.push_back(glm::vec4(0/255.0f, 11.0/255.0f, 112.0/255.0f, 0.0 /255.0f));
		m_values.push_back(1500);
		m_colors.push_back(glm::vec4( 242.0/ 255.0, 212.0/ 255.0, 255.0/ 255.0, 255.0 /255.0f));
	}
	else if( preset == Preset::MRT_Brain )
	{
		m_values.push_back(0);
		m_colors.push_back(glm::vec4(255.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 32.0f/255.0f));
		m_values.push_back(2655);
		m_colors.push_back(glm::vec4(0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 80.0f/255.0f));
		m_values.push_back(2729);
		m_colors.push_back(glm::vec4(126.0f /255.0f, 156.0f /255.0f, 213.0f /255.0f, 80.0f /255.0f));
		m_values.push_back(2821);
		m_colors.push_back(glm::vec4(255.0f/255.0f, 120.0f/255.0f, 0.0f/255.0f, 49.0f /255.0f));
		m_values.push_back(2933);
		m_colors.push_back(glm::vec4(117.0f/255.0f, 119.0f/255.0, 255.0f/255.0f, 7.0f/255.0f));
	}
	
	updateTex(s_minValue, s_maxValue);
}