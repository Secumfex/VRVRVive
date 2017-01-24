#include "CSVWriter.h"

#include <iostream>
#include <fstream>

CSVWriter::CSVWriter()
{

}

CSVWriter::~CSVWriter()
{

}

bool CSVWriter::writeToFile(std::string name)
{
    ofstream myfile;
	myfile.open(name.c_str());
    
	// write header
	for (auto h : m_headers)
	{
		myfile << h.c_str();
		myfile << ",";
	}

	myfile << "\n";
    
	// write data

	int col = 0;
	for (auto d : m_data)
	{
		myfile << d.c_str();
		myfile << ",";
		
		col++;
		if( col % m_headers.size() == 0)
		{
			myfile << "\n";
		}
	}	

	myfile.close();
	return true;
}
