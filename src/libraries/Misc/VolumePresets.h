#ifndef MISC_VOLUMEPRESETS_H
#define MISC_VOLUMEPRESETS_H

#include <Importing/Importer.h>
#include <Volume/SyntheticVolume.h>

namespace VolumePresets
{
	enum Preset {
		CT_Head, 
		MRT_Brain, 
		Homogeneous, 
		Radial_Gradient, 
		MRT_Brain_Stanford, 
		Bucky_Ball, 
		SolidBox, 
		Foot, 
		Engine, 
		Bonsai3, 
		Clouds, 
		CTA_Brain, 
		CTChest, 
		Lobster, 
		SheepHeart, 
		Tooth, 
		VisMale, 
		XMasTree,
		PiggyBank
	};

	static const char* s_models[]  = {
		"CT-Head",
		"MRT-Brain",
		"Homogeneous",
		"Radial-Gradient",
		"MRT Brain Stanford",
		"Bucky-Ball",
		"Solid-Box",
		"Foot",
		"Engine", 
		"Bonsai3", 
		"Clouds", 
		"CTA-Brain", 
		"CT-Chest", 
		"Lobster", 
		"Sheep-Heart", 
		"Tooth", 
		"Visible Male", 
		"XMasTree",
		"Piggy Bank"
	};

	std::string getPath(Preset preset);
	glm::mat4 getScalation(Preset preset);
	glm::mat4 getRotation(Preset preset);

	template<class T>
	void loadPreset(VolumeData<T>& volData, Preset preset);
}


////// IMPLEMENTATION
std::string VolumePresets::getPath(Preset preset)
{
	switch (preset)
	{
		case Preset::CT_Head: return "/volumes/CTHead/CThead";
		case Preset::MRT_Brain_Stanford: return "/volumes/MRbrain/MRbrain";
		case Preset::Bucky_Ball: return "/volumes/BuckyBall/Bucky.pvm";
		case Preset::SolidBox: return "/volumes/SolidBox/Box.pvm";
		case Preset::Foot: return "/volumes/Foot/Foot.pvm";
		case Preset::Engine: return "/volumes/Engine/Engine.pvm";
		case Preset::Bonsai3: return "/volumes/Bonsai3/Bonsai3-LO.pvm";
		case Preset::Clouds: return "/volumes/Clouds/Clouds.pvm";
		case Preset::CTA_Brain: return "/volumes/CTA-Brain/CTA-Brain.pvm";
		case Preset::CTChest: return "/volumes/CTChest/CT-Chest.pvm";
		case Preset::Lobster: return "/volumes/Lobster/Lobster.pvm";
		case Preset::SheepHeart: return "/volumes/SheepHeart/Sheep.pvm";
		case Preset::Tooth: return "/volumes/Tooth/Tooth.pvm";
		case Preset::VisMale: return "/volumes/VisMale/VisMale.pvm";
		case Preset::XMasTree: return "/volumes/XMasTree/XMasTree-LO.pvm";
		case Preset::PiggyBank:return "/volumes/PiggyBank/Pig.pvm";
		default: return "";
	}
}

template<class T>
void VolumePresets::loadPreset(VolumeData<T>& volData, Preset preset)
{
	std::string file = RESOURCES_PATH;
	
	switch(preset)
	{
		case CT_Head:
			volData = Importer::load3DData<T>(file + getPath(preset), 256, 256, 113, 2);
			break;
		case MRT_Brain_Stanford:
			volData = Importer::load3DData<T>(file + getPath(preset), 256, 256, 109, 2);
			break;
		case MRT_Brain:
			volData = Importer::loadBruder<T>();
			break;
		case Homogeneous:
			volData = SyntheticVolume::generateHomogeneousVolume<T>(64,64,64,1.0f);
			break;
		case Radial_Gradient:
			volData = SyntheticVolume::generateRadialGradientVolume<T>(64,64,64,1.0f,0.0f);
			break;
		default:
			volData = Importer::load3DDataPVM<T>(file + getPath(preset));
		break;
	}
}


#include <glm/gtx/transform.hpp>
glm::mat4 VolumePresets::getRotation(Preset preset)
{
	switch (preset)
	{
		case Preset::CT_Head: return glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,0.0f,1.0f));
		case Preset::MRT_Brain: return glm::rotate(glm::radians(180.0f), glm::vec3(0.0f,1.0f,0.0f));
		case Preset::MRT_Brain_Stanford: return glm::rotate(glm::radians(90.0f), glm::vec3(0.0f,1.0f,0.0f)) * glm::rotate(glm::radians(-90.0f), glm::vec3(1.0f,0.0f,0.0f));
		case Preset::Bucky_Ball: return glm::mat4(1.0f);
		case Preset::SolidBox: return glm::mat4(1.0f);
		// case Preset::Foot: return 
		case Preset::Engine: return glm::rotate(glm::radians(-90.0f), glm::vec3(1.0f,0.0f,0.0f));
		// case Preset::Bonsai3: return 
		// case Preset::Clouds: return 
		// case Preset::CTA_Brain: return 
		// case Preset::CTChest: return 
		// case Preset::Lobster: return 
		// case Preset::SheepHeart: return 
		// case Preset::Tooth: return 
		// case Preset::VisMale: return 
		// case Preset::XMasTree: return 
		default: return glm::rotate(glm::radians(-90.0f), glm::vec3(1.0f,0.0f,0.0f));
	}
	return glm::mat4(1.0f);
}

glm::mat4 VolumePresets::getScalation(Preset preset)
{
	switch (preset)
	{
		case Preset::CT_Head: return glm::scale(glm::vec3(1.0f, 0.8828f, 1.0f));
		case Preset::MRT_Brain: return glm::scale(glm::vec3(1.0f, 0.79166, 1.0f));
		case Preset::MRT_Brain_Stanford: return glm::scale(glm::vec3(1.0f, 1.5f * 0.42578125f, 1.0f));
		case Preset::Bucky_Ball: return glm::scale(glm::vec3(1.0f));
		case Preset::SolidBox: return glm::scale(glm::vec3(1.0f));
		case Preset::Foot: return glm::scale(glm::vec3(1.0f));
		case Preset::Engine: return glm::scale(glm::vec3(1.0f));
		case Preset::Bonsai3: return glm::scale(glm::vec3(1.0f, 0.747654f, 1.0f));
		case Preset::Clouds: return glm::scale(glm::vec3(1.0f,0.0625f,1.0f));
		case Preset::CTA_Brain: return glm::scale(glm::vec3(1.0f,0.54545f, 1.0f));
		case Preset::CTChest: return glm::scale(glm::vec3(1.0f,0.625f,1.0f));
		case Preset::Lobster: return glm::scale(glm::vec3(0.9292f, 0.24197f, 1.0f ));
		case Preset::SheepHeart: return glm::scale(glm::vec3(1.0f,0.7272f, 1.0f));
		case Preset::Tooth: return glm::scale(glm::vec3(1.0f,0.62890f, 1.0f));
		case Preset::VisMale: return glm::scale(glm::vec3(0.7825181f, 1.0f, 0.9872f ));
		case Preset::XMasTree: return glm::scale(glm::vec3(0.95390f, 1.0f, 0.953907f));
		case Preset::PiggyBank: return glm::scale(glm::vec3(1.0f, 0.705262f, 1.0f));
		default: return glm::mat4(1.0f);
	}
	return glm::mat4(1.0f);
}


#endif