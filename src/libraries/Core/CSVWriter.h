#ifndef CSVWRITER_H_
#define CSVWRITER_H_

#include <vector>
#include <string>
#include <Core/DebugLog.h>

template<class T>
class CSVWriter {
	std::vector<std::string> m_headers;
	std::vector<T> m_data;
public:
	CSVWriter();
	virtual ~CSVWriter();

	inline void setHeaders(std::vector<std::string> headers) {m_headers = headers;}
	inline bool setData(std::vector<T> data){ 
		if (data.size() % m_headers.size() != 0 ) { DEBUGLOG->log("ERROR: data to number of columns mismatch"); return false; }
		else{ m_data = data; return true;}		
	}
	inline bool addRow(std::vector<T> row){ 
		if (row.size() != m_headers.size()) { DEBUGLOG->log("ERROR: row columns to number of columns mismatch"); return false; }
		else{ m_data.insert(m_data.end(), row.begin(), row.end()); return true;}
	}
	inline void clearData(){ m_data.clear(); } 

	inline const std::vector<T>& getData(){return m_data;}
	inline const std::vector<std::string>& getHeaders(){return m_headers;}

	std::string convertStr( const T& v);

	bool writeToFile(std::string name);
};

//#include <functional>
//class ValueMonitor
//{
//public:
//	struct ValueGetter
//	{
//		void* valuePtr; // where to get the value
//		std::function<std::string(void*)>& stringifierFunc; // knows how to get the value as string
//	};
//
//	template <typename T>
//	void addMonitoredValue<T>(std::string name, ValueGetter getter)
//	{
//	}
//
//
//};

//////////////////////////////////// IMPLEMENTATION

#include <iostream>
#include <fstream>
template<class T>
CSVWriter<T>::CSVWriter()
{

}

template<class T>
CSVWriter<T>::~CSVWriter()
{

}

template<class T>
std::string CSVWriter<T>::convertStr( const T& v) { return std::to_string(v); }
std::string CSVWriter<std::string>::convertStr( const std::string& v) { return v; }

template<class T>
bool CSVWriter<T>::writeToFile(std::string name)
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
		myfile << convertStr(d).c_str();
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

#endif
