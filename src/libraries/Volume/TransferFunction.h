#ifndef TRANSFERFUNCTION_H_
#define TRANSFERFUNCTION_H_

#include <Rendering/GLTools.h>

class TransferFunction {
protected:
	GLuint m_textureHandle;
	std::vector<float> m_values;
	std::vector<glm::vec4> m_colors;
	std::vector<float> m_transferFunctionTexData;
	int m_size;

public:
	TransferFunction();
	virtual ~TransferFunction();

	inline void setTexResolution(int size) { m_transferFunctionTexData.resize(4*size); }

	void updateTex(float minValue = 0.0f, float maxValue = 1.0f);

	GLuint getTextureHandle();
	inline std::vector<glm::vec4>& getColors(){ return m_colors; }
	inline std::vector<float>& getValues(){ return m_values; }
};

#endif
