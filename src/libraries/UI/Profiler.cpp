#include "Profiler.h"

#include <time.h>
#include <algorithm>

static bool g_initRand = false;
float randFloat(float min, float max) //!< returns a random number between min and max
{
	if (!g_initRand)
	{
		srand(time(NULL));
		g_initRand = true;
	}

	return (((float) rand() / (float) RAND_MAX) * (max - min) + min); 
}

ImVec4 Profiler::randColor()
{
	//return ImVec4(randFloat(0.4f, 0.8f), randFloat(0.4f,0.8f), randFloat(0.4f,0.8f), 0.5f);
	return ImColor::HSV(randFloat(0.0f,1.0f), 0.4f, 0.8f, 0.6f);
}


Profiler::Profiler()
{

}

void Profiler::imguiInterface(float startTime, float endTime, bool* open)
{

	if (!ImGui::Begin("Profiler", open, ImVec2(600,120), -1.0f, ImGuiWindowFlags_NoCollapse)) return;

	ImGui::SetWindowSize("Profiler", ImVec2( ImGui::GetWindowWidth(),120) );
	
	// range
	float wWidth = (float) ImGui::GetWindowContentRegionWidth();
	float r = endTime - startTime;

	auto winX = [&](float time) {
		return ((time - startTime) / r )* wWidth;
	};

	//COLUMNS
	//m_columns[0] = startTime; //overwrite start value
	//m_columns.back() = endTime; //overwrite end value
	//ImGui::Columns(m_columns.size());
	//ImGui::Separator();
	//for (int i = 0; i < m_columns.size();i++)
	//{
	//	ImGui::SetColumnOffset(i, winX(m_columns[i]));
	//}
	
	// TIME INFO
	ImGui::Value("start", startTime);
	//while ( ImGui::GetColumnIndex() != ImGui::GetColumnsCount()-1)
	//{
	//	ImGui::NextColumn(); //go to last column
	//}
	//ImGui::SameLine( ImGui::GetColumnWidth() - 70 );
	ImGui::SameLine( ImGui::GetWindowContentRegionWidth() - 70 );
	ImGui::Value("end", endTime);
	ImGui::NextColumn(); //go to first column
	ImGui::Separator();

	// MARKERS
	for (auto m : m_markers)
	{
		float x = winX(m.times[0]); // where we want to place it
		//while (ImGui::GetColumnIndex() < (m_columns.size()-1) && ImGui::GetColumnOffset(ImGui::GetColumnIndex()+1) <= x)
		//{
		//	ImGui::NextColumn();
		//}
		//x -= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column

		if ( x < 0 || x > wWidth ) { continue; }
		x = std::max(4.0f, x);
		ImGui::SameLine(x);
		ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Button, m.color);
			ImGui::PushStyleColor(ImGuiCol_Text, m.color);
			ImGui::Button("", ImVec2(4,0));
			ImGui::Text(m.tag.c_str());
			ImGui::PopStyleColor(2);
		ImGui::EndGroup();
		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Text(m.tag.c_str());
			ImGui::Value("t", m.times[0]);
			if (m.desc.compare("") != 0) ImGui::Text(m.desc.c_str());
			ImGui::EndTooltip();
		}
	}
	//while ( ImGui::GetColumnIndex() != ImGui::GetColumnsCount()-1)
	//{
	//	ImGui::NextColumn(); //go to last column
	//}
	//ImGui::NextColumn(); //go to first column
	ImGui::Separator();

	// RANGES
	for (auto r : m_ranges)
	{
		float xStart = winX(r.times[0]);
		float xEnd =   winX(r.times[1]);

		if ( xEnd < 0 || xStart > wWidth )
		{
			continue;
		}
		if ( xStart < 0 )
		{
			xStart = 0;
		}
		if (xEnd > wWidth) 
		{
			xEnd = wWidth;
		}
		xStart = std::max(4.0f, xStart);
		xEnd = std::max(xStart+4.0f, xEnd);

		//while (ImGui::GetColumnIndex() < (m_columns.size()-1) && ImGui::GetColumnOffset(ImGui::GetColumnIndex()+1) <= xStart)
		//{
		//	ImGui::NextColumn();
		//}
		//xStart -= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column
		//xEnd-= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column
			
		ImGui::SameLine(xStart);
		ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Button, r.color);
			ImGui::Button(r.tag.c_str(), ImVec2(xEnd-xStart,0));
			ImGui::PopStyleColor(1);
		ImGui::EndGroup();

		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Text(r.tag.c_str());
			ImGui::Value("tStart", r.times[0]);
			ImGui::Value("tEnd  ", r.times[1]);
			ImGui::Value("t     ", r.times[1] - r.times[0]);
			if (r.desc.compare("") != 0) ImGui::Text(r.desc.c_str());
			ImGui::EndTooltip();
		}
	}
	
	//ImGui::Columns(1);
	ImGui::Separator();

	ImGui::End();
}

std::set<Profiler::Entry>::iterator Profiler::addMarkerTime(float time, std::string tag, std::string desc) { 
	Profiler::Entry e;
	e.color=randColor();
	e.tag = tag;
	e.desc = desc;
	e.times[0] = time;

	return m_markers.insert(e).first;
}
	
std::set<Profiler::Entry>::iterator Profiler::addRangeTime(float start, float end, std::string tag, std::string desc) { 
	Profiler::Entry e;
	e.color=randColor();
	e.tag = tag;
	e.desc = desc;
	e.times[0] = start;	
	e.times[1] = end;	

	return 	m_ranges.insert(e).first; 
}

std::set<Profiler::Entry>::iterator Profiler::addColumn(float time, std::string desc) { 
	Profiler::Entry e;
	e.color=randColor();
	e.tag = "";
	e.desc = desc;
	e.times[0] = time;

	return 	m_columns.insert(e).first; 
}

void Profiler::clear()
{
	m_columns.clear();
	m_ranges.clear();
	m_markers.clear();
}

std::set<Profiler::Entry>::iterator Profiler::setRangeByTag(std::string tag, float start, float end, std::string desc)
{
	for (auto e = m_ranges.begin(); e != m_ranges.end(); ++e)
	{
		if ((*e).tag.compare(tag) == 0)
		{
			auto b = (*e);
			b.times[0] = start;
			b.times[1] = end;
			b.desc = desc;

			m_ranges.erase(e); // remove old entry
			return m_ranges.insert(b).first; 
		}
	}

	return addRangeTime(start, end, tag, desc);
}

std::set<Profiler::Entry>::iterator Profiler::setMarkerByTag(std::string tag, float time, std::string desc)
{
	for (auto e = m_markers.begin(); e != m_markers.end(); ++e)
	{
		if ((*e).tag.compare(tag) == 0)
		{
			auto b = (*e);
			b.times[0] = time;
			b.desc = desc;

			m_markers.erase(e); // remove old entry
			return m_markers.insert(b).first; 
		}
	}

	return addMarkerTime(time, tag, desc);
}
