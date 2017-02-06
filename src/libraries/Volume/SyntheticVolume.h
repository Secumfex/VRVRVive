#ifndef VOLUME_SYNTHETICVOLUME_H_
#define VOLUME_SYNTHETICVOLUME_H_

#include <glm/glm.hpp>
#include <Core/VolumeData.h>
//#include <Rendering/GLTools.h>

namespace SyntheticVolume
{
	template<typename T> 
	static VolumeData<T> generateHomogeneousVolume(unsigned int width, unsigned int height, unsigned int depth, T value);

	template<typename T> 
	static VolumeData<T> generateRadialGradientVolume(unsigned int width, unsigned int height, unsigned int depth, T centerValue, T outerValue);

	template<typename T> 
	static VolumeData<T> generateNoiseVolume(unsigned int width, unsigned int height, unsigned int depth, T avgValue, T maxDeviation);
}

////// IMPLEMENTATION
#include <Core/DebugLog.h>
template<typename T>
VolumeData<T> SyntheticVolume::generateHomogeneousVolume(unsigned int width, unsigned int height, unsigned int depth, T value)
{
	VolumeData<T> result;

	// arbitrary offsets
	result.min = 0.0;
	result.max = value + (1.0f / value);

	result.size_x = width;
	result.size_y = depth;
	result.size_z = height;

	result.real_size_x = 1.0f / (float) width;
	result.real_size_y = 1.0f / (float) depth;
	result.real_size_z = 1.0f / (float) height;
	
	DEBUGLOG->log("Filling homogeneous volume data...");
	result.data = std::vector<T>(); // reset
	result.data.resize(width * height * depth, value);
	return result;
}

template<typename T>
VolumeData<T> SyntheticVolume::generateRadialGradientVolume( unsigned int width, unsigned int height, unsigned int depth, T centerValue, T outerValue)
{
	VolumeData<T> result;
	result.min = std::min<T>(centerValue, outerValue);
	result.max = std::max<T>(centerValue, outerValue);

	result.size_x = width;
	result.size_y = depth;
	result.size_z = height;

	result.real_size_x = 1.0f / (float) width;
	result.real_size_y = 1.0f / (float) depth;
	result.real_size_z = 1.0f / (float) height;

	glm::vec3 center(
		0.5f * (float) result.size_x * result.real_size_x,
		0.5f * (float) result.size_y * result.real_size_y,
		0.5f * (float) result.size_z * result.real_size_z);

	float maxRadius = length(center);
	
	DEBUGLOG->log("Filling radial gradient volume data...");
	result.data = std::vector<T>(); // reset
	result.data.resize( result.size_x * result.size_y * result.size_z);

	for (unsigned int k = 0; k < result.size_z; k ++ ) // slice
	{
	std::cout << ".";
	for (unsigned int j = 0; j < result.size_y; j ++ ) // row
	{
	for (unsigned int i = 0; i < result.size_x; i ++ ) // column
	{
	
		glm::vec3 position(
			(float) i * result.real_size_x + 0.5f * result.real_size_x,
			(float) j * result.real_size_y + 0.5f * result.real_size_y,
			(float) k * result.real_size_z + 0.5f * result.real_size_z);

		float radius = glm::length(position - center);
		float mixParam = (radius) / maxRadius;

		result.data[k * result.size_x * result.size_y
		           +j * result.size_x
				   +i] = (1.0f - mixParam) * centerValue + (mixParam) * outerValue;
	}}}
	
	std::cout << std::endl;
	return result;
}

template<typename T> 
VolumeData<T> SyntheticVolume::generateNoiseVolume(unsigned int width, unsigned int height, unsigned int depth, T avgValue, T maxDeviation)
{
	VolumeData<T> result;

	result.size_x = width;
	result.size_y = depth;
	result.size_z = height;

	result.real_size_x = 1.0f / (float) width;
	result.real_size_y = 1.0f / (float) depth;
	result.real_size_z = 1.0f / (float) height;

	//TODO
	
	result.data = std::vector<T>(); // reset
	return result;
}


#endif
