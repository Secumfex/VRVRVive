#ifndef TRANSFERFUNCTION_H_
#define TRANSFERFUNCTION_H_

#include <Rendering/GLTools.h>

class TransferFunction {
protected:
	GLuint m_textureHandle;
	std::vector<int> m_values;
	std::vector<glm::vec4> m_colors;
	std::vector<float> m_transferFunctionTexData;
	int m_size;

public:
	TransferFunction();
	virtual ~TransferFunction();

	inline void setTexResolution(int size) { m_transferFunctionTexData.resize(4*size); }

	void updateTex(int minValue, int maxValue);

	GLuint getTextureHandle();
	inline std::vector<glm::vec4>& getColors(){ return m_colors; }
	inline std::vector<int>& getValues(){ return m_values; }

	enum Preset {CT_Head, MRT_Brain};
	
	void loadPreset(Preset preset, int s_minValue, int s_maxValue);
};

#endif
