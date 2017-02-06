#ifndef FILEREADER_H_
#define FILEREADER_H_

#include <vector>
#include <sstream>
#include <string>

class FileReader {
protected:
	std::stringstream m_buffer;
public:
	FileReader();
	FileReader(std::string filename);
	virtual ~FileReader();
	bool readFileToBuffer(std::string filename);
	std::stringstream& getBuffer();
	std::vector<std::string> getLines(bool skipEmptyLines = true);
};

#endif
