#ifndef PROFILER_H
#define PROFILER_H

#include <set>
#include <UI/imgui/imgui.h> // god dommot fronk

class Profiler
{
public:
	struct Entry {
		int level;
		ImVec4 color;
		std::string tag;
		std::string desc;
		float times[2]; // 2nd entry might be unused
		inline bool operator<(const Profiler::Entry &other) { return (level == other.level) ? times[0] < other.times[0] : level < other.level; }
	};


protected: //idc
//public:
	std::set<Entry> m_ranges;
	std::set<Entry> m_markers;
	std::set<Entry> m_columns;

public:
	Profiler();
	virtual ~Profiler() {}
	
	ImVec4 randColor();

	std::set<Entry>::iterator addMarkerTime(float time, std::string tag, std::string desc = "", int level = 0);
	std::set<Entry>::iterator addRangeTime(float start, float end, std::string tag, std::string desc = "", int level = 0);
	std::set<Entry>::iterator addColumn(float time, std::string desc = "", int level = 0);

	inline const std::set<Entry>& getRanges(){ return m_ranges; }
	inline const std::set<Entry>& getMarkers(){ return m_markers; }
	inline const std::set<Entry>& getColumns(){ return m_columns; }
	
	std::set<Entry>::iterator setRangeByTag(std::string tag, float start, float end, std::string desc = "", int level = 0); // reuses color if found
	std::set<Entry>::iterator setMarkerByTag(std::string tag, float time, std::string desc = "", int level = 0); // reuses color if found

	void clear();

	void imguiInterface(float startTime, float endTime, bool* open = NULL, std::string prefix = "");
};

inline bool operator<(const Profiler::Entry &lhs, const Profiler::Entry &rhs) { return lhs.times[0] < rhs.times[0]; }

#endif
