
#include "pch.h"
#include "perfutil.h"


static uint64_t systemTickFrequency = 0;


uint64_t GetCurrentTimeSytemTicks()
{
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return time.QuadPart;
}

uint64_t GetSytemTickFrequency()
{
	if (systemTickFrequency == 0)
	{
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		systemTickFrequency = frequency.QuadPart;
	}
	return systemTickFrequency;
}

double GetPerfTimeDiffSeconds(const uint64_t first, const uint64_t second)
{
	if (second < first)
	{
		uint64_t ticks = (first - second);
		return -(double)ticks / (double)systemTickFrequency;
	}
	else
	{
		uint64_t ticks = (second - first);
		return (double)ticks / (double)systemTickFrequency;
	}
}


PerfTimer::PerfTimer()
	: m_lastTimesMS()
	, m_numAverages(0)
{
	GetSytemTickFrequency();
}

PerfTimer::PerfTimer(int numAverages)
	: m_lastTimesMS()
	, m_numAverages(numAverages)
{
	m_lastTimesMS.reserve(numAverages);
	GetSytemTickFrequency();
}

uint64_t PerfTimer::StartPerfTimer()
{
	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);
	m_startTime = startTime.QuadPart;
	return startTime.QuadPart;
}

uint64_t PerfTimer::EndPerfTimer()
{
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);

	uint64_t perfTimeTicks = endTime.QuadPart - m_startTime;
	float perfTime = (float)(endTime.QuadPart - m_startTime);
	perfTime *= 1000.0f;
	perfTime /= systemTickFrequency;

	if (m_numAverages == 0)
	{
		return perfTimeTicks;
	}

	if (m_lastTimesMS.size() < m_numAverages)
	{
		m_lastTimeIndex = (uint32_t)m_lastTimesMS.size();
		m_lastTimesMS.push_back(perfTime);
	}
	else
	{
		m_lastTimeIndex = (m_lastTimeIndex + 1) % m_numAverages;
		m_lastTimesMS[m_lastTimeIndex] = perfTime;
	}

	return perfTimeTicks;
}


float PerfTimer::EndPerfTimerMS()
{
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);

	float perfTime = (float)(endTime.QuadPart - m_startTime);
	perfTime *= 1000.0f;
	perfTime /= systemTickFrequency;

	if (m_numAverages == 0)
	{
		return perfTime;
	}

	if (m_lastTimesMS.size() < m_numAverages)
	{
		m_lastTimeIndex = (uint32_t)m_lastTimesMS.size();
		m_lastTimesMS.push_back(perfTime);
	}
	else
	{
		m_lastTimeIndex = (m_lastTimeIndex + 1) % m_numAverages;
		m_lastTimesMS[m_lastTimeIndex] = perfTime;
	}

	return perfTime;
}

float PerfTimer::GetStartTimeDiffMS(const PerfTimer& compare) const
{
	float perfTime = (float)m_startTime - (float)compare.m_startTime;
	perfTime *= 1000.0f;
	perfTime /= systemTickFrequency;
	return perfTime;
}

float PerfTimer::GetStartTimeDiffMS(const uint64_t compare) const
{
	float perfTime = (float)m_startTime - (float)compare;
	perfTime *= 1000.0f;
	perfTime /= systemTickFrequency;
	return perfTime;
}

uint64_t PerfTimer::AveragesAddTimeToNow(const uint64_t startTime)
{
	m_startTime = startTime;
	return EndPerfTimer();
}

void PerfTimer::AveragesAddTimeInterval(const uint64_t startTime, const uint64_t endTime)
{
	m_startTime = startTime;
	float perfTime = (float)(endTime - m_startTime);
	perfTime *= 1000.0f;
	perfTime /= systemTickFrequency;

	if (m_numAverages == 0)
	{
		return;
	}

	if (m_lastTimesMS.size() < m_numAverages)
	{
		m_lastTimeIndex = (uint32_t)m_lastTimesMS.size();
		m_lastTimesMS.push_back(perfTime);
	}
	else
	{
		m_lastTimeIndex = (m_lastTimeIndex + 1) % m_numAverages;
		m_lastTimesMS[m_lastTimeIndex] = perfTime;
	}
}

float PerfTimer::GetAverageTimeMS() const
{
	float average = 0;

	for (const float& val : m_lastTimesMS)
	{
		average += val;
	}
	return average / m_lastTimesMS.size();
}
