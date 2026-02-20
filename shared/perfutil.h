
#pragma once


inline LARGE_INTEGER StartPerfTimer()
{
	LARGE_INTEGER startTime;
	QueryPerformanceCounter(&startTime);
	return startTime;
}

inline float EndPerfTimer(LARGE_INTEGER startTime)
{
	LARGE_INTEGER endTime, perfFrequency;

	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTime.QuadPart - startTime.QuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float EndPerfTimer(uint64_t startTimeQuadPart)
{
	LARGE_INTEGER endTime, perfFrequency;

	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTime.QuadPart - startTimeQuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float GetPerfTimerDiff(uint64_t startTimeQuadPart, uint64_t endTimeQuadPart)
{
	LARGE_INTEGER perfFrequency;

	QueryPerformanceFrequency(&perfFrequency);

	float perfTime = (float)(endTimeQuadPart - startTimeQuadPart);
	perfTime *= 1000.0f;
	perfTime /= perfFrequency.QuadPart;
	return perfTime;
}

inline float UpdateAveragePerfTime(std::deque<float>& times, float newTime, int numAverages)
{
	if (times.size() >= numAverages)
	{
		times.pop_front();
	}

	times.push_back(newTime);

	float average = 0;

	for (const float& val : times)
	{
		average += val;
	}
	return average / times.size();
}
