#ifndef MISC_TRANSFERFUNCTIONPRESETS_H
#define MISC_TRANSFERFUNCTIONPRESETS_H

#include <Volume/TransferFunction.h>

namespace TransferFunctionPresets
{
	static TransferFunction s_transferFunction;

	enum Preset {CT_Head, MRT_Brain};
	
	void loadPreset(TransferFunction& transferFunction, Preset preset);

	inline void generateTransferFunction()
	{
		TransferFunctionPresets::loadPreset(s_transferFunction, TransferFunctionPresets::CT_Head);
	}

	inline void updateTransferFunctionTex()
	{
		s_transferFunction.updateTex();
	}
}


////// IMPLEMENTATION
void TransferFunctionPresets::loadPreset(TransferFunction& transferFunction, Preset preset)
{
	transferFunction.getValues().clear();
	transferFunction.getColors().clear();
	
	if ( preset == Preset::CT_Head )
	{
		transferFunction.getValues().push_back((58.0f - (-128.0f)) / (3327.0f));
		transferFunction.getColors().push_back(glm::vec4(0.0/255.0f, 0.0/255.0f, 0.0/255.0f, 0.0/255.0f));
		transferFunction.getValues().push_back((539.0f - (-128.0f))/ (3327.0f));
		transferFunction.getColors().push_back(glm::vec4(255.0/255.0f, 0.0/255.0f, 0.0/255.0f, 231.0/255.0f));
		transferFunction.getValues().push_back((572.0f - (-128.0f))/ (3327.0f));
		transferFunction.getColors().push_back(glm::vec4(0.0 /255.0f, 74.0 /255.0f, 118.0 /255.0f, 64.0 /255.0f));
		transferFunction.getValues().push_back((1356.0f - (-128.0f)) / (3327.0f));
		transferFunction.getColors().push_back(glm::vec4(0/255.0f, 11.0/255.0f, 112.0/255.0f, 0.0 /255.0f));
		transferFunction.getValues().push_back((1500.0f - (-128.0f))/ (3327.0f));
		transferFunction.getColors().push_back(glm::vec4( 242.0/ 255.0, 212.0/ 255.0, 255.0/ 255.0, 255.0 /255.0f));
	}
	else if( preset == Preset::MRT_Brain )
	{
		transferFunction.getValues().push_back(0.0f / (5972.0f));
		transferFunction.getColors().push_back(glm::vec4(255.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 32.0f/255.0f));
		transferFunction.getValues().push_back(2655.0f / (5972.0f));
		transferFunction.getColors().push_back(glm::vec4(0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 80.0f/255.0f));
		transferFunction.getValues().push_back(2729.0f / (5972.0f));
		transferFunction.getColors().push_back(glm::vec4(126.0f /255.0f, 156.0f /255.0f, 213.0f /255.0f, 80.0f /255.0f));
		transferFunction.getValues().push_back(2821.0f / (5972.0f));
		transferFunction.getColors().push_back(glm::vec4(255.0f/255.0f, 120.0f/255.0f, 0.0f/255.0f, 49.0f /255.0f));
		transferFunction.getValues().push_back(2933.0f / (5972.0f));
		transferFunction.getColors().push_back(glm::vec4(117.0f/255.0f, 119.0f/255.0, 255.0f/255.0f, 7.0f/255.0f));
	}
	
	transferFunction.updateTex(); // values are all in range 0..1
}

#endif