#ifndef PROFILER_H
#define PROFILER_H

#include <vector>
struct ImVec4;

class Profiler
{
protected: //idc
//public:
	std::vector<ImVec4> m_rangeColors;
	std::vector<std::string> m_rangeTags;
	std::vector<std::string> m_rangeDescs;
	std::vector<float> m_startTimes;
	std::vector<float> m_endTimes;
	
	std::vector<ImVec4> m_markerColors;
	std::vector<std::string> m_markerTags;
	std::vector<std::string> m_markerDescs;
	std::vector<float> m_markerTimes;
	
	std::vector<ImVec4> m_columnColors;
	std::vector<std::string> m_columnDescs;
	std::vector<float> m_columns;

public:
	Profiler();
	virtual ~Profiler() {}
	
	ImVec4 randColor();

	int addMarkerTime(float time, std::string tag, std::string desc = "");
	int addRangeTime(float start, float end, std::string tag, std::string desc = "");
	int addColumn(float time, std::string desc = "");

	inline const std::vector<float>& getStartTimes(){ return m_startTimes; }
	inline const std::vector<float>& getEndTimes(){ return m_endTimes; }
	inline const std::vector<std::string>& getRangeTags(){ return m_rangeTags; }
	inline const std::vector<float>& getColumns(){ return m_columns; }

	void clear();

	void imguiInterface(float startTime, float endTime, bool* open = NULL);
};


#endif
