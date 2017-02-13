#ifndef CSVWRITER_H_
#define CSVWRITER_H_

#include <vector>
#include <string>
#include <Core/DebugLog.h>

class CSVWriter {
	std::vector<std::string> m_headers;
	std::vector<std::string> m_data;
public:
	CSVWriter();
	virtual ~CSVWriter();

	inline void setHeaders(std::vector<std::string> headers) {m_headers = headers;}
	inline bool setData(std::vector<std::string> data){ 
		if (data.size() % m_headers.size() != 0 ) { DEBUGLOG->log("ERROR: data to number of columns mismatch"); return false; }
		else{ m_data = data; return true;}		
	}
	inline bool addRow(std::vector<std::string> row){ 
		if (row.size() != m_headers.size()) { DEBUGLOG->log("ERROR: row columns to number of columns mismatch"); return false; }
		else{ m_data.insert(m_data.end(), row.begin(), row.end()); return true;}
	}
	inline void clearData(){ m_data.clear(); } 

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

#endif
