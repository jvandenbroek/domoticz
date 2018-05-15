#pragma once
#include "RFXNames.h"
class SunRiseSet
{
public:
	struct _tSubRiseSetResults
	{
		struct _tHourMin
		{
			int hour;
			int min;
		};
		double latit;
		double longit;
		int year;
		int month;
		int day;
		std::map<_eTimerType, _tHourMin> sunTime;
	};
	static bool GetSunRiseSet(const double latit, const double longit, _tSubRiseSetResults &result);
	static bool GetSunRiseSet(const double latit, const double longit, const int year, const int month, const int day, _tSubRiseSetResults &result);
private:
	struct _tTimerMap
	{
		_eTimerType timerTypeStart;
		_eTimerType timerTypeEnd;
		double altitude;
		double upperLimb;
	};
	static const _tTimerMap timerMap[];
	static double UtcToLocal(double time, double tz);
	static double __daylen__( int year, int month, int day, double lon, double lat, double altit, int upper_limb);
	static int __sunriset__(int year, int month, int day, double lon, double lat, double altit, int upper_limb, double *rise, double *set);
	static void sunpos(double d, double *lon, double *r);
	static void sun_RA_dec(double d, double *RA, double *dec, double *r);
	static double revolution(double x);
	static double rev180(double x);
	static double GMST0(double d);
	static void RoundMinuteHour(int &min, int &hour);
};
