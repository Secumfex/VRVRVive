#ifndef MISC_TRANSFERFUNCTIONPRESETS_H
#define MISC_TRANSFERFUNCTIONPRESETS_H

#include <Volume/TransferFunction.h>
#include <Misc/VolumePresets.h>

namespace TransferFunctionPresets
{
	static TransferFunction s_transferFunction;
	
	void loadPreset(TransferFunction& transferFunction, VolumePresets::Preset preset);

	inline void generateTransferFunction()
	{
		TransferFunctionPresets::loadPreset(s_transferFunction, VolumePresets::Preset::CT_Head);
	}

	inline void updateTransferFunctionTex()
	{
		s_transferFunction.updateTex();
	}
}


////// IMPLEMENTATION
void TransferFunctionPresets::loadPreset(TransferFunction& transferFunction, VolumePresets::Preset preset)
{
	transferFunction.getValues().clear();
	transferFunction.getColors().clear();
	
	if ( preset == VolumePresets::Preset::CT_Head )
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
	else if( preset == VolumePresets::Preset::MRT_Brain )
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
	else if( preset == VolumePresets::Preset::Homogeneous)
	{
		transferFunction.getValues().push_back(0.0f);
		transferFunction.getColors().push_back(glm::vec4(0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f));
		transferFunction.getValues().push_back(1.0f);
		transferFunction.getColors().push_back(glm::vec4(117.0f/255.0f, 119.0f/255.0, 255.0f/255.0f, 25.0f/255.0f));
	}
	else if( preset == VolumePresets::Preset::Radial_Gradient)
	{
		transferFunction.getValues().push_back(0.28f);
		transferFunction.getColors().push_back(glm::vec4(126.0f/255.0f, 213.0f/255.0f, 255.0f/255.0f, 0.0f/255.0f));
		transferFunction.getValues().push_back(0.68f);
		transferFunction.getColors().push_back(glm::vec4(0.0f/255.0f, 255.0f/255.0f, 212.0f/255.0f, 20.0f/255.0f));
		transferFunction.getValues().push_back(0.71f);
		transferFunction.getColors().push_back(glm::vec4(255.0f/255.0f, 0.0f/255.0, 167.0f/255.0f, 174.0f/255.0f));
	}
	else if( preset == VolumePresets::Preset::MRT_Brain_Stanford)
	{
		transferFunction.getValues().push_back(0.069f);
		transferFunction.getColors().push_back(glm::vec4(208.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f));
		transferFunction.getValues().push_back(0.139f);
		transferFunction.getColors().push_back(glm::vec4(14.0f/255.0f, 10.0f/255.0f, 10.0f/255.0f, 190.0f/255.0f));
		transferFunction.getValues().push_back(0.174f);
		transferFunction.getColors().push_back(glm::vec4(0.0f /255.0f, 158.0f /255.0f, 255.0f /255.0f, 22.0f /255.0f));
		transferFunction.getValues().push_back(0.403f);
		transferFunction.getColors().push_back(glm::vec4(255.0f/255.0f, 134.0f/255.0f, 0.0f/255.0f, 68.0f /255.0f));
		transferFunction.getValues().push_back(0.611f);
		transferFunction.getColors().push_back(glm::vec4(255.0f/255.0f, 255.0f/255.0, 255.0f/255.0f, 255.0f/255.0f));
	}
	else if( preset == VolumePresets::Preset::Bucky_Ball)
	{
		transferFunction.getValues().push_back(0.1f);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		transferFunction.getValues().push_back(0.5f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 1.0f, 0.85f, 0.12f));
		transferFunction.getValues().push_back(0.6f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.15f));
	}
	else if( preset == VolumePresets::Preset::SolidBox)
	{
		transferFunction.getValues().push_back(0.1f);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		transferFunction.getValues().push_back(0.5f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 1.0f, 0.85f, 0.12f));
		transferFunction.getValues().push_back(0.6f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.15f));
		transferFunction.getValues().push_back(0.75f);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 1.0f, 0.0f, 0.15f));
		transferFunction.getValues().push_back(1.0f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	}
	else if( preset == VolumePresets::Preset::Foot)
	{
		transferFunction.getValues().push_back(0.0f);
		transferFunction.getColors().push_back(glm::vec4(120.0f/255.0f, 227.0f/255.0f, 135.0f/255.0f, 17.0f/255.0f));
		transferFunction.getValues().push_back(0.268f);
		transferFunction.getColors().push_back(glm::vec4(192.0f/255.0f, 86.0f/255.0f, 0.0f, 28.0f/255.0f));
		transferFunction.getValues().push_back(0.354f);
		transferFunction.getColors().push_back(glm::vec4(155.0f/255.0f, 11.0f/255.0f, 157.0f/255.0f, 1.0f));
		transferFunction.getValues().push_back(0.5f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 185.0f/255.0f, 255.0f/255.0f, 36.0f/255.0f));
	}
	else if( preset == VolumePresets::Preset::Engine)
	{
		transferFunction.getValues().push_back(0.36f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f/255.0f));
		transferFunction.getValues().push_back(0.385f);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 1.0f, 1.0f, 35.0f/255.0f));
		transferFunction.getValues().push_back(0.606f);
		transferFunction.getColors().push_back(glm::vec4(173.0f/255.0f, 60.0f/255.0f, 0.02f, 8.0f/255.0f));
		transferFunction.getValues().push_back(0.660f);
		transferFunction.getColors().push_back(glm::vec4(0.0f,0.0f,0.0f, 1.0f));
		transferFunction.getValues().push_back(0.769f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 113.0f/255.0f, 185.0f/255.0f, 1.0f));
	}
	else if( preset == VolumePresets::Preset::CTA_Brain)
	{
		transferFunction.getValues().push_back(0.296);
		transferFunction.getColors().push_back(glm::vec4(97.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f, 0.0f/255.0f));
		transferFunction.getValues().push_back(0.308f);
		transferFunction.getColors().push_back(glm::vec4(202.0f/255.0f, 0.0f/255.0f, 0.0f, 255.0f/255.0f));
		transferFunction.getValues().push_back(0.432f);
		transferFunction.getColors().push_back(glm::vec4(69.0f/255.0f,0.0f,0.0f, 63.0f/255.0f));
		transferFunction.getValues().push_back(0.579f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f/255.0f, 0.0f/255.0f, 172.0f/255.0f));
		transferFunction.getValues().push_back(0.865f);
		transferFunction.getColors().push_back(glm::vec4(144.0f/255.0f, 184.0f/255.0f, 255.0f/255.0f, 3.0f/255.0f));
	}
	else // default
	{
		transferFunction.getValues().push_back(0.0);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		transferFunction.getValues().push_back(0.25f);
		transferFunction.getColors().push_back(glm::vec4(1.0f, 0.0f, 0.0f, 0.25f));
		transferFunction.getValues().push_back(0.5f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 1.0f, 0.0f, 0.25f));
		transferFunction.getValues().push_back(0.75f);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.25f));
		transferFunction.getValues().push_back(1.0);
		transferFunction.getColors().push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
	}

	transferFunction.updateTex(); // values are all in range 0..1
}

#endif