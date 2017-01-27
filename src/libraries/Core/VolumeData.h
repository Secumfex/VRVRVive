#ifndef CORE_VOLUMEDATA_H_
#define CORE_VOLUMEDATA_H_

#include <vector>

template<class T>
struct VolumeData
{
	unsigned int size_x; //!< x: left
	unsigned int size_y; //!< y: forward
	unsigned int size_z; //!< z: up

	std::vector<T> data; //!< size: x

	//std::vector<T> midSlice;

	float real_size_x; // actual step size in mm
	float real_size_y; // acutal step size in mm
	float real_size_z; // acutal step size in mm

	T min;
	T max;

	VolumeData<T>(){ data = std::vector<T>(); }
};

#endif
