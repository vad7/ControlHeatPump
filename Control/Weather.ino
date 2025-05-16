/*
 * Copyright (c) by Vadim Kulakov vad7@yahoo.com, vad711
 *
 * "Народный контроллер" для тепловых насосов.
 * Данное програмноое обеспечение предназначено для управления
 * различными типами тепловых насосов для отопления и ГВС.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 */

#include "Constant.h"

#ifdef WEATHER_FORECAST

// <"key">:<value>,
const char WF_JSON_Sunrise[] = "\"sunrise\"";
const char WF_JSON_Hourly[] = "\"hourly\"";
const char WF_JSON_DT[] = "\"dt\"";
const char WF_JSON_Clouds[] = "\"clouds\"";
const char WF_JSON_Temp[] = "\"feels_like\"";

// Возврат 0 - ОК
int WF_ProcessForecast(char *json)
{
	char *fld = strstr(json, WF_JSON_Sunrise);
	if(!fld) {
		/*if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf("WF: no data\n");*/
		WF_BoilerTargetPercent = 100;
		return -2;
	}
	uint32_t t = strtoul(fld += sizeof(WF_JSON_Sunrise), NULL, 0);
	if(t == ULONG_MAX || t == 0) {
		WF_BoilerTargetPercent = 100;
		return -3;
	}
	t += WF_ForecastAfterSunrise;
	fld = strstr(fld, WF_JSON_Hourly);
	if(!fld) {
		WF_BoilerTargetPercent = 100;
		return -4;
	}
	fld += sizeof(WF_JSON_Hourly);
	int32_t avg_cl = 0, avg_t = 0;
	uint8_t i = 1;
	for(; i <= WF_ForecastAggregateHours; i++) {
		fld = strstr(fld, WF_JSON_DT);
		if(!fld) break;
		uint32_t nt = strtoul(fld += sizeof(WF_JSON_DT), NULL, 0);
		if(nt == ULONG_MAX) break;
		if(nt < t) {
			i--;
			continue;
		}
		fld = strstr(fld, WF_JSON_Temp);
		if(!fld) break;
		int32_t n = strtol(fld += sizeof(WF_JSON_Temp), NULL, 0);
		if(n == LONG_MAX || n == LONG_MIN) break;
		avg_t += n;
		fld = strstr(fld, WF_JSON_Clouds);
		if(!fld) break;
		n = strtol(fld += sizeof(WF_JSON_Clouds), NULL, 0);
		if(n == LONG_MAX || n == LONG_MIN) break;
		avg_cl += n;
	}
	i--;
	if(i) {
		if((avg_t /= i) > HP.Option.WF_MinTemp) {
			avg_cl /= i;
			/*if(GETBIT(WR.Flags, WR_fLog))*/ journal.jprintf_time("WF: Clouds(%d)=%d", i, avg_cl);
			if(avg_cl < 100) {
				//avg = (avg - 66) * 3; // 66..99 -> 0..98
				avg_cl = (avg_cl - 60) * 2; // 60..99 -> 0..98
				if(avg_cl < 0) avg_cl = 0;
			}
			avg_cl += WF_SunByMonth[rtcSAM3X8.get_months()-1];
			if(avg_cl > 100) avg_cl = 100;
			WF_BoilerTargetPercent = avg_cl;
			/*if(GETBIT(WR.Flags, WR_fLog))*/ journal.jprintf(":%d%%, BoilerTrg=%.2d\n", avg_cl, HP.get_boilerTempTarget());
		} else {
			/*if(GETBIT(WR.Flags, WR_fLog))*/ journal.jprintf_time("WF: 100%, FeelTemp low: %d\n", avg_t);
			WF_BoilerTargetPercent = 100;
		}
		return OK;
	}
	/*if(GETBIT(WR.Flags, WR_fLog)) journal.jprintf_time("WF: no data\n");*/
	WF_BoilerTargetPercent = 100;
	return -1;
}

#endif
