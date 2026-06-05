
#pragma once

uint64_t GetCurrentTimeSytemTicks();
uint64_t GetSytemTickFrequency();

double GetPerfTimeDiffSeconds(const uint64_t first, const uint64_t second);

class PerfTimer
{
public:
	PerfTimer();
	PerfTimer(int numAverages);
	uint64_t StartPerfTimer();
	uint64_t EndPerfTimer();
	float EndPerfTimerMS();
	float GetStartTimeDiffMS(const PerfTimer& compare) const;
	float GetStartTimeDiffMS(const uint64_t compare) const;

	uint64_t AveragesAddTimeToNow(const uint64_t startTime);
	void AveragesAddTimeInterval(const uint64_t startTime, const uint64_t endTime);
	float GetAverageTimeMS() const;

	uint64_t m_startTime = 0;
private:
	uint32_t m_numAverages;
	std::vector<float> m_lastTimesMS;
	uint32_t m_lastTimeIndex = 0;
};
