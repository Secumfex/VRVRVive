#include "FileReader.h"

#include <Core/DebugLog.h>
#include <iostream>
#include <fstream>

FileReader::FileReader()
{

}

FileReader::FileReader(std::string filename)
{
	readFileToBuffer(filename);
}

FileReader::~FileReader()
{

}

bool FileReader::readFileToBuffer(std::string filename)
{
	std::ifstream file( filename.c_str() );

	if ( !file.is_open() )
	{
		DEBUGLOG->log("ERROR: could not open file: " + filename);
		return false;
	}
	else
	{
		m_buffer << file.rdbuf();
		file.close();
	}
	return true;
}

std::vector<std::string> FileReader::getLines(bool skipEmptyLines)
{
	std::vector<std::string> result;
	while (m_buffer) // still good
	{
		std::string line;
		std::getline(m_buffer, line);
		
		if (skipEmptyLines && line == "")
		{
			continue; // skip
		}

		result.push_back(line);
	}
	return result;
}


std::stringstream& FileReader::getBuffer() { return m_buffer; }