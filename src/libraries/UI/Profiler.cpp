#include "Profiler.h"

#include <UI/imgui/imgui.h>
#include <time.h>

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
	: m_columns(1),
	m_columnColors(1, ImVec4(0.0f,0.0f,0.0f,0.0f))
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
	m_columns[0] = startTime; //overwrite start value
	ImGui::Columns(m_columns.size());
	ImGui::Separator();
	for (int i = 0; i < m_columns.size();i++)
	{
		ImGui::SetColumnOffset(i, winX(m_columns[i]));
	}
	
	// TIME INFO
	ImGui::Value("start", startTime);
	while ( ImGui::GetColumnIndex() != ImGui::GetColumnsCount()-1)
	{
		ImGui::NextColumn(); //go to last column
	}
	ImGui::SameLine( ImGui::GetColumnWidth() - 70 );
	ImGui::Value("end", endTime);
	ImGui::NextColumn(); //go to first column
	ImGui::Separator();

	// MARKERS
	for (int i = 0; i < m_markerTimes.size(); i++)
	{
		int x = winX(m_markerTimes[i]); // where we want to place it
		while (ImGui::GetColumnIndex() < (m_columns.size()-1) && ImGui::GetColumnOffset(ImGui::GetColumnIndex()+1) <= x)
		{
			ImGui::NextColumn();
		}
		x -= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column

		if ( x < 0 || x > wWidth ) { continue; }
		ImGui::SameLine(x);
		ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Button, m_markerColors[ i ]);
			ImGui::PushStyleColor(ImGuiCol_Text, m_markerColors[ i ]);
			ImGui::Button("", ImVec2(4,0));
			ImGui::Text(m_markerTags[i].c_str());
			ImGui::PopStyleColor(2);
		ImGui::EndGroup();
		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Value("t", m_markerTimes[i]);
			if (m_markerDescs[i].compare("") != 0) ImGui::Text(m_markerDescs[i].c_str());
			ImGui::EndTooltip();
		}
	}
	while ( ImGui::GetColumnIndex() != ImGui::GetColumnsCount()-1)
	{
		ImGui::NextColumn(); //go to last column
	}
	ImGui::NextColumn(); //go to first column
	ImGui::Separator();

	// RANGES
	for (int i = 0; i < m_startTimes.size(); i++)
	{
		int xStart = winX(m_startTimes[i]);
		int xEnd =   winX(m_endTimes[i]);

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

		while (ImGui::GetColumnIndex() < (m_columns.size()-1) && ImGui::GetColumnOffset(ImGui::GetColumnIndex()+1) <= xStart)
		{
			ImGui::NextColumn();
		}
		xStart -= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column
		xEnd-= ImGui::GetColumnOffset(ImGui::GetColumnIndex()); // place relative to current column
			
		ImGui::SameLine(xStart);
		ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Button, m_rangeColors[ i ]);
			ImGui::Button(m_rangeTags[i].c_str(), ImVec2(xEnd-xStart,0));
			ImGui::PopStyleColor(1);
		ImGui::EndGroup();

		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Value("tStart", m_startTimes[i]);
			ImGui::Value("tEnd  ", m_endTimes[i]);
			ImGui::Value("t     ", m_endTimes[i] - m_startTimes[i]);
			if (m_rangeDescs[i].compare("") != 0) ImGui::Text(m_rangeDescs[i].c_str());
			ImGui::EndTooltip();
		}
	}
	
	ImGui::Columns(1);
	ImGui::Separator();

	ImGui::End();
}

int Profiler::addMarkerTime(float time, std::string tag, std::string desc) { 
	m_markerTimes.push_back(time);  
	m_markerTags.push_back(tag); 
	m_markerColors.push_back(randColor()); 
	m_markerDescs.push_back(desc); 
	return m_markerTimes.size()-1;	
}
	
int Profiler::addRangeTime(float start, float end, std::string tag, std::string desc) { 
	m_startTimes.push_back(start);  
	m_rangeTags.push_back(tag);   
	m_rangeDescs.push_back(desc);  
	m_endTimes.push_back(end);
	m_rangeColors.push_back(randColor()); 
	return m_endTimes.size()-1; 
}

int Profiler::addColumn(float time, std::string desc) { 
	m_columns.push_back(time); 
	m_columnColors.push_back(randColor());
	m_columnDescs.push_back(desc); 
	return m_columns.size()-1; 
}