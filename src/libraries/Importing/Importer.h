#ifndef IMPORTER_H
#define IMPORTER_H

#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>

#include <glm/glm.hpp>

#include <Core/DebugLog.h>
#include <Core/VolumeData.h>

#include <algorithm>

#include "ddsbase.h"

namespace Importer {
	/**
	 * @param path to file prefix relative to resources folder file suffix is assumed to be .1 .2 .. .num_files
	 * @param size_x of slice file
	 * @param size_y of slice file
	 * @param num_files of slice files. will be loaded in ascending order
	 * @return data from files
	 */
	template<class T>
	VolumeData<T> load3DData(std::string path, unsigned size_x, unsigned size_y, unsigned int num_files, unsigned int num_bytes_per_entry = 1)
	{
		DEBUGLOG->log("Loading files with prefix :" + path);
		DEBUGLOG->log("Reading slice data...");

		VolumeData<T> result;
		result.size_x = size_x;
		result.size_y = size_y;
		result.size_z = num_files;
		result.data.clear();

		T min = SHRT_MAX;
		T max = SHRT_MIN;

		//result.midSlice.resize(size_x*size_y);

		DEBUGLOG->indent();
		for (unsigned int i = 1; i <= num_files; i++)
		{
			std::string current_file_path = path + "." + DebugLog::to_string(i);

			std::cout << ".";

//			DEBUGLOG->log("current file:" + current_file_path);

			// read file into input vector
			std::ifstream file( current_file_path.c_str(), std::ifstream::binary);	
			std::vector<char> input;

			if (file.is_open()) {
				// get length of file:
				file.seekg (0, file.end);
				int length = (int) file.tellg();
				file.seekg (0, file.beg);

				// allocate memory:
				input.resize(length);

				// read data as a block:
				file.read( &input[0], length );

				file.close();
			}

			// create data vector for this slice
			std::vector<T> slice(size_x * size_y, 0);

			for(unsigned int j = 0 ; j < slice.size(); j++)
			{
				int val = input[num_bytes_per_entry*j];
				for (unsigned int k = 1; k < num_bytes_per_entry; k++)
				{
					val = (val << 8) + input[ num_bytes_per_entry*j + k];
				}
	    		
				slice[j] = (T) val;
				
				//if ( i == num_files / 2)
				//{
				//	result.midSlice[j] = val;
				//	//DEBUGLOG->log("x: ", j - (j / size_x)*size_x);
				//	//DEBUGLOG->log("y: ", j / size_x);
				//	//DEBUGLOG->log("MidSlice: ", val);
				//}

				min = std::min<T>((T) val, min);
				max = std::max<T>((T) val, max);
			}

			// push slice to data vector
			result.data.insert(result.data.end(), slice.begin(), slice.end());
		}
		std::cout << std::endl;
		DEBUGLOG->outdent();

		result.min = min;		
		result.max = max;

		return result;
	}

	template <class T> 
	VolumeData<T> load3DDataPVM(std::string path)
	{
		DEBUGLOG->log("Loading PVM file: " + path);
		DEBUGLOG->log("Reading slice data...");
		unsigned char *volume;

		unsigned int width,height,depth,
				components;

		float scalex,scaley,scalez;

		char *output,*dot;
		char *outname;

		// read and uncompress PVM volume
		VolumeData<T> volData;

		if ((volume=readPVMvolume(path.c_str(),
								&width,&height,&depth,&components,
								&scalex,&scaley,&scalez))==NULL)
		{
			DEBUGLOG->log("ERROR: failed to load PVM file: " + path);
			return volData;
		}
   

		volData.real_size_x = 1.0f / (float) width;
		volData.real_size_y = 1.0f / (float) depth;
		volData.real_size_z = 1.0f / (float) height;
		volData.size_x = width;
		volData.size_y = depth;
		volData.size_z = height;
		//volData.data.resize(width * height * depth * sizeof(char));
		volData.data.resize(width * height * depth);
		std::copy(volume, volume + width * height * depth * sizeof(char),volData.data.begin());

		volData.min = CHAR_MAX;
		volData.max = CHAR_MIN;
		for (auto v : volData.data)
		{
			volData.min = std::min<T>(v, volData.min);
			volData.max = std::max<T>(v, volData.max);
		}

		if (volData.min > volData.max) // swap
		{
			T tmp = volData.min;
			volData.min = volData.max;
			volData.max = tmp;
		}

		return volData;
	}


	VolumeData<short> loadBruder();

	template<class T>
	VolumeData<T> loadBruder()
	{
		VolumeData<short> result_ = loadBruder();
		VolumeData<T> result;
		result.size_x = (T) result_.size_x;
		result.size_y = (T) result_.size_y;
		result.size_z = (T) result_.size_z;
		result.max = (T) result_.max;
		result.min = (T) result_.min;
		result.real_size_x = (T) result_.real_size_x;
		result.real_size_y = (T) result_.real_size_y;
		result.real_size_z = (T) result_.real_size_z;
		
		result.data.clear();
		result.data = std::vector<T>(result_.data.begin(), result_.data.end()); 

		return result;
	}

} // namespace Importer



#endif