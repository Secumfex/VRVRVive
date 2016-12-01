#ifndef PROFILER_H
#define PROFILER_H

#include <vector>

class Profiler
{
protected: //idc
//public:
	std::vector<std::string> m_rangeTags;
	std::vector<float> m_startTimes;
	std::vector<float> m_endTimes;

	std::vector<std::string> m_markerTags;
	std::vector<float> m_markerTimes;
	
	std::vector<float> m_columns;
public:
	Profiler() {}
	virtual ~Profiler() {}
	
	inline int addMarkerTime(float time, std::string tag) { m_markerTimes.push_back(time);  m_markerTags.push_back(tag); return m_markerTimes.size()-1;	}
	inline int addRangeTime(float start, float end, std::string tag) { m_startTimes.push_back(start);  m_rangeTags.push_back(tag);  m_endTimes.push_back(end); return m_endTimes.size()-1; }
	inline int addColumn(float time) { m_columns.push_back(time); return m_columns.size()-1; }

	inline const std::vector<float>& getStartTimes(){ return m_startTimes; }
	inline const std::vector<float>& getEndTimes(){ return m_endTimes; }
	inline const std::vector<std::string>& getRangeTags(){ return m_rangeTags; }
	inline const std::vector<float>& getColumns(){ return m_columns; }

	void imguiInterface(float startTime, float endTime);
};


#endif
