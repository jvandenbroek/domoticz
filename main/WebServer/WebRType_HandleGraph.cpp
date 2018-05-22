#include "stdafx.h"
#include "../WebServer.h"
#include "../mainworker.h"
#include "localtime_r.h"
#include "../webserver/Base64.h"
#include "../json/json.h"
#include "SQLHelper.h"
#include "../notifications/NotificationHelper.h"

#ifndef WIN32
#include <sys/utsname.h>
#include <dirent.h>
#else
#include "../msbuild/WindowsHelper.h"
#include "dirent_windows.h"
#endif

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define round(a) ( int ) ( a + .5 )

//extern std::string szWWWFolder;

void CWebServer::RType_HandleGraph(WebEmSession & session, const request& req, Json::Value &root)
{
    uint64_t idx = 0;
    if (request::findValue(&req, "idx") != "")
    {
        std::stringstream s_str(request::findValue(&req, "idx"));
        s_str >> idx;
    }

    std::vector<std::vector<std::string> > result;
    char szTmp[300];

    std::string sensor = request::findValue(&req, "sensor");
    if (sensor.empty())
        return;
    std::string srange = request::findValue(&req, "range");
    if (srange.empty())
        return;

    time_t now = mytime(NULL);
    struct tm tm1;
    localtime_r(&now, &tm1);

    result = m_sql.safe_query("SELECT Type, SubType, SwitchType, AddjValue, AddjMulti, Options FROM DeviceStatus WHERE (ID == %" PRIu64 ")",
        idx);
    if (result.size() < 1)
        return;

    unsigned char dType = atoi(result[0][0].c_str());
    unsigned char dSubType = atoi(result[0][1].c_str());
    _eMeterType metertype = (_eMeterType)atoi(result[0][2].c_str());
    if (
        (dType == pTypeP1Power) ||
        (dType == pTypeENERGY) ||
        (dType == pTypePOWER) ||
        (dType == pTypeCURRENTENERGY) ||
        ((dType == pTypeGeneral) && (dSubType == sTypeKwh))
        )
    {
        metertype = MTYPE_ENERGY;
    }
    else if (dType == pTypeP1Gas)
        metertype = MTYPE_GAS;
    else if ((dType == pTypeRego6XXValue) && (dSubType == sTypeRego6XXCounter))
        metertype = MTYPE_COUNTER;

    double AddjValue = atof(result[0][3].c_str());
    double AddjMulti = atof(result[0][4].c_str());
    std::string sOptions = result[0][5].c_str();
    std::map<std::string, std::string> options = m_sql.BuildDeviceOptions(sOptions);

    std::string dbasetable = "";
    if (srange == "day") {
        if (sensor == "temp")
            dbasetable = "Temperature";
        else if (sensor == "rain")
            dbasetable = "Rain";
        else if (sensor == "Percentage")
            dbasetable = "Percentage";
        else if (sensor == "fan")
            dbasetable = "Fan";
        else if (sensor == "counter")
        {
            if ((dType == pTypeP1Power) || (dType == pTypeCURRENT) || (dType == pTypeCURRENTENERGY))
            {
                dbasetable = "MultiMeter";
            }
            else
            {
                dbasetable = "Meter";
            }
        }
        else if ((sensor == "wind") || (sensor == "winddir"))
            dbasetable = "Wind";
        else if (sensor == "uv")
            dbasetable = "UV";
        else
            return;
    }
    else
    {
        //week,year,month
        if (sensor == "temp")
            dbasetable = "Temperature_Calendar";
        else if (sensor == "rain")
            dbasetable = "Rain_Calendar";
        else if (sensor == "Percentage")
            dbasetable = "Percentage_Calendar";
        else if (sensor == "fan")
            dbasetable = "Fan_Calendar";
        else if (sensor == "counter")
        {
            if (
                (dType == pTypeP1Power) ||
                (dType == pTypeCURRENT) ||
                (dType == pTypeCURRENTENERGY) ||
                (dType == pTypeAirQuality) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeVisibility)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeDistance)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSoilMoisture)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeLeafWetness)) ||
                ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorAD)) ||
                ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorVolt)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeCurrent)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypePressure)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel)) ||
                (dType == pTypeLux) ||
                (dType == pTypeWEIGHT) ||
                (dType == pTypeUsage)
                )
                dbasetable = "MultiMeter_Calendar";
            else
                dbasetable = "Meter_Calendar";
        }
        else if ((sensor == "wind") || (sensor == "winddir"))
            dbasetable = "Wind_Calendar";
        else if (sensor == "uv")
            dbasetable = "UV_Calendar";
        else
            return;
    }
    unsigned char tempsign = m_sql.m_tempsign[0];
    int iPrev;

    if (srange == "day")
    {
        if (sensor == "temp") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Temperature, Chill, Humidity, Barometer, Date, SetPoint FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[4].substr(0, 16);
                    if (
                        (dType == pTypeRego6XXTemp) ||
                        (dType == pTypeTEMP) ||
                        (dType == pTypeTEMP_HUM) ||
                        (dType == pTypeTEMP_HUM_BARO) ||
                        (dType == pTypeTEMP_BARO) ||
                        ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                        ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp)) ||
                        ((dType == pTypeUV) && (dSubType == sTypeUV3)) ||
                        (dType == pTypeThermostat1) ||
                        (dType == pTypeRadiator1) ||
                        ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorTemp)) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeSystemTemp)) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeBaro)) ||
                        ((dType == pTypeThermostat) && (dSubType == sTypeThermSetpoint)) ||
                        (dType == pTypeEvohomeZone) ||
                        (dType == pTypeEvohomeWater)
                        )
                    {
                        double tvalue = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                        root["result"][ii]["te"] = tvalue;
                    }
                    if (
                        ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                        ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp))
                        )
                    {
                        double tvalue = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                        root["result"][ii]["ch"] = tvalue;
                    }
                    if ((dType == pTypeHUM) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO))
                    {
                        root["result"][ii]["hu"] = sd[2];
                    }
                    if (
                        (dType == pTypeTEMP_HUM_BARO) ||
                        (dType == pTypeTEMP_BARO) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        )
                    {
                        if (dType == pTypeTEMP_HUM_BARO)
                        {
                            if (dSubType == sTypeTHBFloat)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                            else
                                root["result"][ii]["ba"] = sd[3];
                        }
                        else if (dType == pTypeTEMP_BARO)
                        {
                            sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                        else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        {
                            sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                    }
                    if ((dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater))
                    {
                        double se = ConvertTemperature(atof(sd[5].c_str()), tempsign);
                        root["result"][ii]["se"] = se;
                    }

                    ii++;
                }
            }
        }
        else if (sensor == "Percentage") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Percentage, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[1].substr(0, 16);
                    root["result"][ii]["v"] = sd[0];
                    ii++;
                }
            }
        }
        else if (sensor == "fan") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Speed, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[1].substr(0, 16);
                    root["result"][ii]["v"] = sd[0];
                    ii++;
                }
            }
        }

        else if (sensor == "counter")
        {
            if (dType == pTypeP1Power)
            {
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                // P1 counter values can only increment, so these are more reliable for sorting than time which can decrement (when DST is turned off)
                result = m_sql.safe_query("SELECT Value1, Value2, Value3, Value4, Value5, Value6, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Value1 ASC, Value5 ASC, Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    bool bHaveDeliverd = false;
                    bool bHaveFirstValue = false;
                    long long lastUsage1, lastUsage2, lastDeliv1, lastDeliv2;
                    time_t lastTime = 0;

                    long long firstUsage1, firstUsage2, firstDeliv1, firstDeliv2;

                    int nMeterType = 0;
                    m_sql.GetPreferencesVar("SmartMeterType", nMeterType);

                    int lastDay = 0;
                    int lastDST = -1;

                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        if (nMeterType == 0)
                        {
                            long long actUsage1, actUsage2, actDeliv1, actDeliv2;
                            std::stringstream s_str1(sd[0]);
                            s_str1 >> actUsage1;
                            std::stringstream s_str2(sd[4]);
                            s_str2 >> actUsage2;
                            std::stringstream s_str3(sd[1]);
                            s_str3 >> actDeliv1;
                            std::stringstream s_str4(sd[5]);
                            s_str4 >> actDeliv2;

                            actDeliv1 = (actDeliv1 < 10) ? 0 : actDeliv1;
                            actDeliv2 = (actDeliv2 < 10) ? 0 : actDeliv2;

                            std::string stime = sd[6];
                            struct tm ntime;
                            time_t atime;
                            ParseSQLdatetime(atime, ntime, stime, -1);
                            if (lastDST > ntime.tm_isdst) // DST was turned off
                            {
                                struct tm ltime;
                                time_t tmptime;
                                localtime_r(&lastTime, &ltime);
                                int year = ltime.tm_year + 1900;
                                int mon = ltime.tm_mon + 1;
                                int day = ltime.tm_mday;
                                int hour = ltime.tm_hour;
                                int min = ltime.tm_min;
                                int sec = ltime.tm_sec;
                                constructTime(tmptime, ltime, year, mon, day, hour, min, sec, 0);
                                if (ltime.tm_isdst == 0)
                                    lastTime = tmptime;
                                else // no standard time match for lastTime
                                {
                                    localtime_r(&atime, &ltime);
                                    year = ltime.tm_year + 1900;
                                    mon = ltime.tm_mon + 1;
                                    day = ltime.tm_mday;
                                    hour = ltime.tm_hour;
                                    min = ltime.tm_min;
                                    sec = ltime.tm_sec;
                                    constructTime(tmptime, ltime, year, mon, day, hour, min, sec, 1);
                                    if (ltime.tm_isdst == 1)
                                        atime = tmptime;
                                }
                            }
                            if (lastDay != ntime.tm_mday)
                            {
                                lastDay = ntime.tm_mday;
                                firstUsage1 = actUsage1;
                                firstUsage2 = actUsage2;
                                firstDeliv1 = actDeliv1;
                                firstDeliv2 = actDeliv2;
                            }

                            if (bHaveFirstValue)
                            {
                                long curUsage1 = (long)(actUsage1 - lastUsage1);
                                long curUsage2 = (long)(actUsage2 - lastUsage2);
                                long curDeliv1 = (long)(actDeliv1 - lastDeliv1);
                                long curDeliv2 = (long)(actDeliv2 - lastDeliv2);

                                float tdiff = static_cast<float>(difftime(atime, lastTime));
                                if (tdiff == 0)
                                    tdiff = 3600; // only reason for the same time to occur twice is a DST shift, meaning the actual time difference must be an hour
                                else if (tdiff <= -3600)
                                    break; // impossible input - meter replaced?
                                else if (tdiff < 0)
                                    tdiff += 3600; // assume that DST was turned off - can there be another reason?

                                float tlaps = 3600.0f / tdiff;
                                curUsage1 *= int(tlaps);
                                curUsage2 *= int(tlaps);
                                curDeliv1 *= int(tlaps);
                                curDeliv2 *= int(tlaps);

                                if (curUsage1 > 100000)
                                    curUsage1 = 0;
                                if (curUsage2 > 100000)
                                    curUsage2 = 0;
                                if (curDeliv1 > 100000)
                                    curDeliv1 = 0;
                                if (curDeliv2 > 100000)
                                    curDeliv2 = 0;

                                root["result"][ii]["d"] = sd[6].substr(0, 16);

                                if ((curDeliv1 != 0) || (curDeliv2 != 0))
                                    bHaveDeliverd = true;

                                sprintf(szTmp, "%ld", curUsage1);
                                root["result"][ii]["v"] = szTmp;
                                sprintf(szTmp, "%ld", curUsage2);
                                root["result"][ii]["v2"] = szTmp;
                                sprintf(szTmp, "%ld", curDeliv1);
                                root["result"][ii]["r1"] = szTmp;
                                sprintf(szTmp, "%ld", curDeliv2);
                                root["result"][ii]["r2"] = szTmp;

                                long pUsage1 = (long)(actUsage1 - firstUsage1);
                                long pUsage2 = (long)(actUsage2 - firstUsage2);

                                sprintf(szTmp, "%ld", pUsage1 + pUsage2);
                                root["result"][ii]["eu"] = szTmp;
                                if (bHaveDeliverd)
                                {
                                    long pDeliv1 = (long)(actDeliv1 - firstDeliv1);
                                    long pDeliv2 = (long)(actDeliv2 - firstDeliv2);
                                    sprintf(szTmp, "%ld", pDeliv1 + pDeliv2);
                                    root["result"][ii]["eg"] = szTmp;
                                }

                                ii++;
                            }
                            else
                            {
                                bHaveFirstValue = true;
                                if ((ntime.tm_hour != 0) && (ntime.tm_min != 0))
                                {
                                    struct tm ltime;
                                    localtime_r(&atime, &tm1);
                                    getNoon(atime, ltime, ntime.tm_year + 1900, ntime.tm_mon + 1, ntime.tm_mday - 1); // We're only interested in finding the date
                                    int year = ltime.tm_year + 1900;
                                    int mon = ltime.tm_mon + 1;
                                    int day = ltime.tm_mday;
                                    sprintf(szTmp, "%04d-%02d-%02d", year, mon, day);
                                    std::vector<std::vector<std::string> > result2;
                                    result2 = m_sql.safe_query(
                                        "SELECT Counter1, Counter2, Counter3, Counter4 FROM Multimeter_Calendar WHERE (DeviceRowID==%" PRIu64 ") AND (Date=='%q')",
                                        idx, szTmp);
                                    if (!result2.empty())
                                    {
                                        std::vector<std::string> sd = result2[0];
                                        std::stringstream s_str1(sd[0]);
                                        s_str1 >> firstUsage1;
                                        std::stringstream s_str2(sd[1]);
                                        s_str2 >> firstDeliv1;
                                        std::stringstream s_str3(sd[2]);
                                        s_str3 >> firstUsage2;
                                        std::stringstream s_str4(sd[3]);
                                        s_str4 >> firstDeliv2;
                                        lastDay = ntime.tm_mday;
                                    }
                                }

                            }
                            lastUsage1 = actUsage1;
                            lastUsage2 = actUsage2;
                            lastDeliv1 = actDeliv1;
                            lastDeliv2 = actDeliv2;
                            lastTime = atime;
                            lastDST = ntime.tm_isdst;
                        }
                        else
                        {
                            //this meter has no decimals, so return the use peaks
                            root["result"][ii]["d"] = sd[6].substr(0, 16);

                            if (sd[3] != "0")
                                bHaveDeliverd = true;
                            root["result"][ii]["v"] = sd[2];
                            root["result"][ii]["r1"] = sd[3];
                            ii++;

                        }
                    }
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
            }
            else if (dType == pTypeAirQuality)
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["co2"] = sd[0];
                        ii++;
                    }
                }
            }
            else if ((dType == pTypeGeneral) && ((dSubType == sTypeSoilMoisture) || (dSubType == sTypeLeafWetness)))
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["v"] = sd[0];
                        ii++;
                    }
                }
            }
            else if (
                ((dType == pTypeGeneral) && (dSubType == sTypeVisibility)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeDistance)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeCurrent)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypePressure)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel))
                )
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;
                float vdiv = 10.0f;
                if (
                    ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                    ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                    )
                {
                    vdiv = 1000.0f;
                }
                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        float fValue = float(atof(sd[0].c_str())) / vdiv;
                        if (metertype == 1)
                            fValue *= 0.6214f;
                        if ((dType == pTypeGeneral) && (dSubType == sTypeVoltage))
                            sprintf(szTmp, "%.3f", fValue);
                        else if ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                            sprintf(szTmp, "%.3f", fValue);
                        else
                            sprintf(szTmp, "%.1f", fValue);
                        root["result"][ii]["v"] = szTmp;
                        ii++;
                    }
                }
            }
            else if ((dType == pTypeRFXSensor) && ((dSubType == sTypeRFXSensorAD) || (dSubType == sTypeRFXSensorVolt)))
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["v"] = sd[0];
                        ii++;
                    }
                }
            }
            else if (dType == pTypeLux)
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["lux"] = sd[0];
                        ii++;
                    }
                }
            }
            else if (dType == pTypeWEIGHT)
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        sprintf(szTmp, "%.1f", m_sql.m_weightscale * atof(sd[0].c_str()) / 10.0f );
                        root["result"][ii]["v"] = szTmp;
                        ii++;
                    }
                }
            }
            else if (dType == pTypeUsage)
            {//day
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["u"] = atof(sd[0].c_str()) / 10.0f;
                        ii++;
                    }
                }
            }
            else if (dType == pTypeCURRENT)
            {
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                //CM113
                int displaytype = 0;
                int voltage = 230;
                m_sql.GetPreferencesVar("CM113DisplayType", displaytype);
                m_sql.GetPreferencesVar("ElectricVoltage", voltage);

                root["displaytype"] = displaytype;

                result = m_sql.safe_query("SELECT Value1, Value2, Value3, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    bool bHaveL1 = false;
                    bool bHaveL2 = false;
                    bool bHaveL3 = false;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[3].substr(0, 16);

                        float fval1 = static_cast<float>(atof(sd[0].c_str()) / 10.0f);
                        float fval2 = static_cast<float>(atof(sd[1].c_str()) / 10.0f);
                        float fval3 = static_cast<float>(atof(sd[2].c_str()) / 10.0f);

                        if (fval1 != 0)
                            bHaveL1 = true;
                        if (fval2 != 0)
                            bHaveL2 = true;
                        if (fval3 != 0)
                            bHaveL3 = true;

                        if (displaytype == 0)
                        {
                            sprintf(szTmp, "%.1f", fval1);
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%.1f", fval2);
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%.1f", fval3);
                            root["result"][ii]["v3"] = szTmp;
                        }
                        else
                        {
                            sprintf(szTmp, "%d", int(fval1*voltage));
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%d", int(fval2*voltage));
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%d", int(fval3*voltage));
                            root["result"][ii]["v3"] = szTmp;
                        }
                        ii++;
                    }
                    if (
                        (!bHaveL1) &&
                        (!bHaveL2) &&
                        (!bHaveL3)
                        ) {
                        root["haveL1"] = true; //show at least something
                    }
                    else {
                        if (bHaveL1)
                            root["haveL1"] = true;
                        if (bHaveL2)
                            root["haveL2"] = true;
                        if (bHaveL3)
                            root["haveL3"] = true;
                    }
                }
            }
            else if (dType == pTypeCURRENTENERGY)
            {
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                //CM113
                int displaytype = 0;
                int voltage = 230;
                m_sql.GetPreferencesVar("CM113DisplayType", displaytype);
                m_sql.GetPreferencesVar("ElectricVoltage", voltage);

                root["displaytype"] = displaytype;

                result = m_sql.safe_query("SELECT Value1, Value2, Value3, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    int ii = 0;
                    bool bHaveL1 = false;
                    bool bHaveL2 = false;
                    bool bHaveL3 = false;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[3].substr(0, 16);

                        float fval1 = static_cast<float>(atof(sd[0].c_str()) / 10.0f);
                        float fval2 = static_cast<float>(atof(sd[1].c_str()) / 10.0f);
                        float fval3 = static_cast<float>(atof(sd[2].c_str()) / 10.0f);

                        if (fval1 != 0)
                            bHaveL1 = true;
                        if (fval2 != 0)
                            bHaveL2 = true;
                        if (fval3 != 0)
                            bHaveL3 = true;

                        if (displaytype == 0)
                        {
                            sprintf(szTmp, "%.1f", fval1);
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%.1f", fval2);
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%.1f", fval3);
                            root["result"][ii]["v3"] = szTmp;
                        }
                        else
                        {
                            sprintf(szTmp, "%d", int(fval1*voltage));
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%d", int(fval2*voltage));
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%d", int(fval3*voltage));
                            root["result"][ii]["v3"] = szTmp;
                        }
                        ii++;
                    }
                    if (
                        (!bHaveL1) &&
                        (!bHaveL2) &&
                        (!bHaveL3)
                        ) {
                        root["haveL1"] = true; //show at least something
                    }
                    else {
                        if (bHaveL1)
                            root["haveL1"] = true;
                        if (bHaveL2)
                            root["haveL2"] = true;
                        if (bHaveL3)
                            root["haveL3"] = true;
                    }
                }
            }
            else if ((dType == pTypeENERGY) || (dType == pTypePOWER) || (dType == pTypeYouLess) || ((dType == pTypeGeneral) && (dSubType == sTypeKwh)))
            {
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;
                root["ValueQuantity"] = options["ValueQuantity"];
                root["ValueUnits"] = options["ValueUnits"];

                float EnergyDivider = 1000.0f;
                float GasDivider = 100.0f;
                float WaterDivider = 100.0f;
                int tValue;
                if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
                {
                    EnergyDivider = float(tValue);
                }
                if (m_sql.GetPreferencesVar("MeterDividerGas", tValue))
                {
                    GasDivider = float(tValue);
                }
                if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
                {
                    WaterDivider = float(tValue);
                }
                if (dType == pTypeP1Gas)
                    GasDivider = 1000;
                else if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                    EnergyDivider *= 100.0f;

                //First check if we had any usage in the short log, if not, its probably a meter without usage
                bool bHaveUsage = true;
                result = m_sql.safe_query("SELECT MIN([Usage]), MAX([Usage]) FROM %s WHERE (DeviceRowID==%" PRIu64 ")", dbasetable.c_str(), idx);
                if (result.size() > 0)
                {
                    std::stringstream s_str1(result[0][0]);
                    long long minValue;
                    s_str1 >> minValue;
                    std::stringstream s_str2(result[0][1]);
                    long long maxValue;
                    s_str2 >> maxValue;
                    if ((minValue == 0) && (maxValue == 0))
                    {
                        bHaveUsage = false;
                    }
                }

                int ii = 0;
                result = m_sql.safe_query("SELECT Value,[Usage], Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);

                int method = 0;
                std::string sMethod = request::findValue(&req, "method");
                if (sMethod.size() > 0)
                    method = atoi(sMethod.c_str());
                if (bHaveUsage == false)
                    method = 0;

                if ((dType == pTypeYouLess) && ((metertype == MTYPE_ENERGY) || (metertype == MTYPE_ENERGY_GENERATED)))
                    method = 1;

                if (method != 0)
                {
                    //realtime graph
                    if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                        EnergyDivider /= 100.0f;
                }
                root["method"] = method;
                bool bHaveFirstValue = false;
                bool bHaveFirstRealValue = false;
                float FirstValue = 0;
                long long ulFirstRealValue = 0;
                long long ulFirstValue = 0;
                long long ulLastValue = 0;
                std::string LastDateTime = "";
                time_t lastTime = 0;

                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        //If method == 1, provide BOTH hourly and instant usage for combined graph
                        {
                            //bars / hour
                            std::string actDateTimeHour = sd[2].substr(0, 13);

                            std::stringstream s_str1(sd[0]);
                            long long actValue;
                            s_str1 >> actValue;

                            if (actValue >= ulLastValue)
                                ulLastValue = actValue;

                            if (actDateTimeHour != LastDateTime || ((method == 1) && (itt + 1 == result.end())))
                            {
                                if (bHaveFirstValue)
                                {
                                    //root["result"][ii]["d"] = LastDateTime + (method == 1 ? ":30" : ":00");
                                    //^^ not necessarely bad, but is currently inconsistent with all other day graphs
                                    root["result"][ii]["d"] = LastDateTime + ":00";

                                    long long ulTotalValue = ulLastValue - ulFirstValue;
                                    if (ulTotalValue == 0)
                                    {
                                        //Could be the P1 Gas Meter, only transmits one every 1 a 2 hours
                                        ulTotalValue = ulLastValue - ulFirstRealValue;
                                    }
                                    ulFirstRealValue = ulLastValue;
                                    float TotalValue = float(ulTotalValue);
                                    switch (metertype)
                                    {
                                    case MTYPE_ENERGY:
                                    case MTYPE_ENERGY_GENERATED:
                                        sprintf(szTmp, "%.3f", (TotalValue / EnergyDivider)*1000.0f);	//from kWh -> Watt
                                        break;
                                    case MTYPE_GAS:
                                        sprintf(szTmp, "%.3f", TotalValue / GasDivider);
                                        break;
                                    case MTYPE_WATER:
                                        sprintf(szTmp, "%.3f", TotalValue / WaterDivider);
                                        break;
                                    case MTYPE_COUNTER:
                                        sprintf(szTmp, "%.1f", TotalValue);
                                        break;
                                    default:
                                        strcpy(szTmp, "0");
                                        break;
                                    }
                                    root["result"][ii][method == 1 ? "eu" : "v"] = szTmp;
                                    ii++;
                                }
                                LastDateTime = actDateTimeHour;
                                bHaveFirstValue = false;
                            }

                            if (!bHaveFirstValue)
                            {
                                ulFirstValue = ulLastValue;
                                bHaveFirstValue = true;
                            }
                            if (!bHaveFirstRealValue)
                            {
                                bHaveFirstRealValue = true;
                                ulFirstRealValue = ulLastValue;
                            }
                        }

                        if (method == 1)
                        {
                            std::stringstream s_str1(sd[1]);
                            long long actValue;
                            s_str1 >> actValue;

                            root["result"][ii]["d"] = sd[2].substr(0, 16);

                            float TotalValue = float(actValue);
                            if ((dType == pTypeGeneral) && (dSubType == sTypeKwh))
                                TotalValue /= 10.0f;
                            switch (metertype)
                            {
                            case MTYPE_ENERGY:
                            case MTYPE_ENERGY_GENERATED:
                                sprintf(szTmp, "%.3f", (TotalValue / EnergyDivider)*1000.0f);	//from kWh -> Watt
                                break;
                            case MTYPE_GAS:
                                sprintf(szTmp, "%.2f", TotalValue / GasDivider);
                                break;
                            case MTYPE_WATER:
                                sprintf(szTmp, "%.3f", TotalValue / WaterDivider);
                                break;
                            case MTYPE_COUNTER:
                                sprintf(szTmp, "%.1f", TotalValue);
                                break;
                            default:
                                strcpy(szTmp, "0");
                                break;
                            }
                            root["result"][ii]["v"] = szTmp;
                            ii++;
                        }
                    }
                }
            }
            else
            {
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;
                root["ValueQuantity"] = options["ValueQuantity"];
                root["ValueUnits"] = options["ValueUnits"];

                float EnergyDivider = 1000.0f;
                float GasDivider = 100.0f;
                float WaterDivider = 100.0f;
                int tValue;
                if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
                {
                    EnergyDivider = float(tValue);
                }
                if (m_sql.GetPreferencesVar("MeterDividerGas", tValue))
                {
                    GasDivider = float(tValue);
                }
                if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
                {
                    WaterDivider = float(tValue);
                }
                if (dType == pTypeP1Gas)
                    GasDivider = 1000.0f;
                else if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                    EnergyDivider *= 100.0f;

                int ii = 0;
                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);

                int method = 0;
                std::string sMethod = request::findValue(&req, "method");
                if (sMethod.size() > 0)
                    method = atoi(sMethod.c_str());

                bool bHaveFirstValue = false;
                bool bHaveFirstRealValue = false;
                float FirstValue = 0;
                unsigned long long ulFirstRealValue = 0;
                unsigned long long ulFirstValue = 0;
                unsigned long long ulLastValue = 0;

                std::string LastDateTime = "";
                time_t lastTime = 0;

                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        if (method == 0)
                        {
                            //bars / hour

                            std::stringstream s_str1(sd[0]);
                            unsigned long long actValue;
                            s_str1 >> actValue;

                            std::string actDateTimeHour = sd[1].substr(0, 13);
                            if (actDateTimeHour != LastDateTime)
                            {
                                if (bHaveFirstValue)
                                {
                                    struct tm ntime;
                                    time_t atime;
                                    if (actDateTimeHour.size() == 10)
                                        actDateTimeHour += " 00";
                                    constructTime(atime, ntime,
                                        atoi(actDateTimeHour.substr(0, 4).c_str()),
                                        atoi(actDateTimeHour.substr(5, 2).c_str()),
                                        atoi(actDateTimeHour.substr(8, 2).c_str()),
                                        atoi(actDateTimeHour.substr(11, 2).c_str()) - 1,
                                        0, 0, -1);

                                    char szTime[50];
                                    sprintf(szTime, "%04d-%02d-%02d %02d:00", ntime.tm_year + 1900, ntime.tm_mon + 1, ntime.tm_mday, ntime.tm_hour);
                                    root["result"][ii]["d"] = szTime;

                                    float TotalValue = float(actValue - ulFirstValue);

                                    //if (TotalValue != 0)
                                    {
                                        switch (metertype)
                                        {
                                        case MTYPE_ENERGY:
                                        case MTYPE_ENERGY_GENERATED:
                                            sprintf(szTmp, "%.3f", (TotalValue / EnergyDivider)*1000.0f);	//from kWh -> Watt
                                            break;
                                        case MTYPE_GAS:
                                            sprintf(szTmp, "%.3f", TotalValue / GasDivider);
                                            break;
                                        case MTYPE_WATER:
                                            sprintf(szTmp, "%.3f", TotalValue / WaterDivider);
                                            break;
                                        case MTYPE_COUNTER:
                                            sprintf(szTmp, "%.1f", TotalValue);
                                            break;
                                        default:
                                            strcpy(szTmp, "0");
                                            break;
                                        }
                                        root["result"][ii]["v"] = szTmp;
                                        ii++;
                                    }
                                }
                                ulFirstValue = actValue;
                                LastDateTime = actDateTimeHour;
                            }

                            if (!bHaveFirstValue)
                            {
                                ulFirstValue = actValue;
                                bHaveFirstValue = true;
                            }
                            ulLastValue = actValue;
                        }
                        else
                        {
                            //realtime graph
                            std::stringstream s_str1(sd[0]);
                            unsigned long long actValue;
                            s_str1 >> actValue;

                            std::string stime = sd[1];
                            struct tm ntime;
                            time_t atime;
                            ParseSQLdatetime(atime, ntime, stime, -1);
                            if (bHaveFirstRealValue)
                            {
                                long long curValue = actValue - ulLastValue;

                                float tdiff = static_cast<float>(difftime(atime, lastTime));
                                if (tdiff == 0)
                                    tdiff = 3600; // only reason for the same time to occur twice is a DST shift, meaning the actual time difference must be an hour
                                float tlaps = 3600.0f / tdiff;
                                curValue *= int(tlaps);

                                root["result"][ii]["d"] = sd[1].substr(0, 16);

                                float TotalValue = float(curValue);
                                //if (TotalValue != 0)
                                {
                                    switch (metertype)
                                    {
                                    case MTYPE_ENERGY:
                                    case MTYPE_ENERGY_GENERATED:
                                        sprintf(szTmp, "%.3f", (TotalValue / EnergyDivider)*1000.0f);	//from kWh -> Watt
                                        break;
                                    case MTYPE_GAS:
                                        sprintf(szTmp, "%.2f", TotalValue / GasDivider);
                                        break;
                                    case MTYPE_WATER:
                                        sprintf(szTmp, "%.3f", TotalValue / WaterDivider);
                                        break;
                                    case MTYPE_COUNTER:
                                        sprintf(szTmp, "%.1f", TotalValue);
                                        break;
                                    default:
                                        strcpy(szTmp, "0");
                                        break;
                                    }
                                    root["result"][ii]["v"] = szTmp;
                                    ii++;
                                }

                            }
                            else
                                bHaveFirstRealValue = true;
                            ulLastValue = actValue;
                            lastTime = atime;
                        }
                    }
                }
                if ((bHaveFirstValue) && (method == 0))
                {
                    //add last value
                    root["result"][ii]["d"] = LastDateTime + ":00";

                    unsigned long long ulTotalValue = ulLastValue - ulFirstValue;

                    float TotalValue = float(ulTotalValue);

                    //if (TotalValue != 0)
                    {
                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", (TotalValue / EnergyDivider)*1000.0f);	//from kWh -> Watt
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.3f", TotalValue / GasDivider);
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", TotalValue / WaterDivider);
                            break;
                        case MTYPE_COUNTER:
                            sprintf(szTmp, "%.1f", TotalValue);
                            break;
                        default:
                            strcpy(szTmp, "0");
                            break;
                        }
                        root["result"][ii]["v"] = szTmp;
                        ii++;
                    }
                }
            }
        }
        else if (sensor == "uv") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Level, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[1].substr(0, 16);
                    root["result"][ii]["uvi"] = sd[0];
                    ii++;
                }
            }
        }
        else if (sensor == "rain") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            int LastHour = -1;
            float LastTotalPreviousHour = -1;

            float LastValue = -1;
            std::string LastDate = "";

            result = m_sql.safe_query("SELECT Total, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;
                    float ActTotal = static_cast<float>(atof(sd[0].c_str()));
                    int Hour = atoi(sd[1].substr(11, 2).c_str());
                    if (Hour != LastHour)
                    {
                        if (LastHour != -1)
                        {
                            int NextCalculatedHour = (LastHour + 1) % 24;
                            if (Hour != NextCalculatedHour)
                            {
                                //Looks like we have a GAP somewhere, finish the last hour
                                root["result"][ii]["d"] = LastDate;
                                double mmval = ActTotal - LastValue;
                                mmval *= AddjMulti;
                                sprintf(szTmp, "%.1f", mmval);
                                root["result"][ii]["mm"] = szTmp;
                                ii++;
                            }
                            else
                            {
                                root["result"][ii]["d"] = sd[1].substr(0, 16);
                                double mmval = ActTotal - LastTotalPreviousHour;
                                mmval *= AddjMulti;
                                sprintf(szTmp, "%.1f", mmval);
                                root["result"][ii]["mm"] = szTmp;
                                ii++;
                            }
                        }
                        LastHour = Hour;
                        LastTotalPreviousHour = ActTotal;
                    }
                    LastValue = ActTotal;
                    LastDate = sd[1];
                }
            }
        }
        else if (sensor == "wind") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Direction, Speed, Gust, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                int ii = 0;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[3].substr(0, 16);
                    root["result"][ii]["di"] = sd[0];

                    int intSpeed = atoi(sd[1].c_str());
                    int intGust = atoi(sd[2].c_str());
                    if (m_sql.m_windunit != WINDUNIT_Beaufort)
                    {
                        sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                        root["result"][ii]["gu"] = szTmp;
                    }
                    else
                    {
                        float windspeedms = float(intSpeed)*0.1f;
                        float windgustms = float(intGust)*0.1f;
                        sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                        root["result"][ii]["gu"] = szTmp;
                    }
                    ii++;
                }
            }
        }
        else if (sensor == "winddir") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Direction, Speed, Gust FROM %s WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date ASC", dbasetable.c_str(), idx);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                std::map<int, int> _directions;
                int wdirtabletemp[17][8];
                std::string szLegendLabels[7];
                int ii = 0;

                int totalvalues = 0;
                //init dir list
                int idir;
                for (idir = 0; idir < 360 + 1; idir++)
                    _directions[idir] = 0;
                for (ii = 0; ii < 17; ii++)
                {
                    for (int jj = 0; jj < 8; jj++)
                    {
                        wdirtabletemp[ii][jj] = 0;
                    }
                }

                if (m_sql.m_windunit == WINDUNIT_MS)
                {
                    szLegendLabels[0] = "&lt; 0.5 " + m_sql.m_windsign;
                    szLegendLabels[1] = "0.5-2 " + m_sql.m_windsign;
                    szLegendLabels[2] = "2-4 " + m_sql.m_windsign;
                    szLegendLabels[3] = "4-6 " + m_sql.m_windsign;
                    szLegendLabels[4] = "6-8 " + m_sql.m_windsign;
                    szLegendLabels[5] = "8-10 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 10" + m_sql.m_windsign;
                }
                else if (m_sql.m_windunit == WINDUNIT_KMH)
                {
                    szLegendLabels[0] = "&lt; 2 " + m_sql.m_windsign;
                    szLegendLabels[1] = "2-4 " + m_sql.m_windsign;
                    szLegendLabels[2] = "4-6 " + m_sql.m_windsign;
                    szLegendLabels[3] = "6-10 " + m_sql.m_windsign;
                    szLegendLabels[4] = "10-20 " + m_sql.m_windsign;
                    szLegendLabels[5] = "20-36 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 36" + m_sql.m_windsign;
                }
                else if (m_sql.m_windunit == WINDUNIT_MPH)
                {
                    szLegendLabels[0] = "&lt; 3 " + m_sql.m_windsign;
                    szLegendLabels[1] = "3-7 " + m_sql.m_windsign;
                    szLegendLabels[2] = "7-12 " + m_sql.m_windsign;
                    szLegendLabels[3] = "12-18 " + m_sql.m_windsign;
                    szLegendLabels[4] = "18-24 " + m_sql.m_windsign;
                    szLegendLabels[5] = "24-46 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 46" + m_sql.m_windsign;
                }
                else if (m_sql.m_windunit == WINDUNIT_Knots)
                {
                    szLegendLabels[0] = "&lt; 3 " + m_sql.m_windsign;
                    szLegendLabels[1] = "3-7 " + m_sql.m_windsign;
                    szLegendLabels[2] = "7-17 " + m_sql.m_windsign;
                    szLegendLabels[3] = "17-27 " + m_sql.m_windsign;
                    szLegendLabels[4] = "27-34 " + m_sql.m_windsign;
                    szLegendLabels[5] = "34-41 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 41" + m_sql.m_windsign;
                }
                else if (m_sql.m_windunit == WINDUNIT_Beaufort)
                {
                    szLegendLabels[0] = "&lt; 2 " + m_sql.m_windsign;
                    szLegendLabels[1] = "2-4 " + m_sql.m_windsign;
                    szLegendLabels[2] = "4-6 " + m_sql.m_windsign;
                    szLegendLabels[3] = "6-8 " + m_sql.m_windsign;
                    szLegendLabels[4] = "8-10 " + m_sql.m_windsign;
                    szLegendLabels[5] = "10-12 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 12" + m_sql.m_windsign;
                }
                else {
                    //Todo !
                    szLegendLabels[0] = "&lt; 0.5 " + m_sql.m_windsign;
                    szLegendLabels[1] = "0.5-2 " + m_sql.m_windsign;
                    szLegendLabels[2] = "2-4 " + m_sql.m_windsign;
                    szLegendLabels[3] = "4-6 " + m_sql.m_windsign;
                    szLegendLabels[4] = "6-8 " + m_sql.m_windsign;
                    szLegendLabels[5] = "8-10 " + m_sql.m_windsign;
                    szLegendLabels[6] = "&gt; 10" + m_sql.m_windsign;
                }


                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;
                    float fdirection = static_cast<float>(atof(sd[0].c_str()));
                    if (fdirection >= 360)
                        fdirection = 0;
                    int direction = int(fdirection);
                    float speedOrg = static_cast<float>(atof(sd[1].c_str()));
                    float gustOrg = static_cast<float>(atof(sd[2].c_str()));
                    if ((gustOrg == 0) && (speedOrg != 0))
                        gustOrg = speedOrg;
                    if (gustOrg == 0)
                        continue; //no direction if wind is still
                    float speed = speedOrg* m_sql.m_windscale;
                    float gust = gustOrg * m_sql.m_windscale;
                    int bucket = int(fdirection / 22.5f);

                    int speedpos = 0;

                    if (m_sql.m_windunit == WINDUNIT_MS)
                    {
                        if (gust < 0.5f) speedpos = 0;
                        else if (gust < 2.0f) speedpos = 1;
                        else if (gust < 4.0f) speedpos = 2;
                        else if (gust < 6.0f) speedpos = 3;
                        else if (gust < 8.0f) speedpos = 4;
                        else if (gust < 10.0f) speedpos = 5;
                        else speedpos = 6;
                    }
                    else if (m_sql.m_windunit == WINDUNIT_KMH)
                    {
                        if (gust < 2.0f) speedpos = 0;
                        else if (gust < 4.0f) speedpos = 1;
                        else if (gust < 6.0f) speedpos = 2;
                        else if (gust < 10.0f) speedpos = 3;
                        else if (gust < 20.0f) speedpos = 4;
                        else if (gust < 36.0f) speedpos = 5;
                        else speedpos = 6;
                    }
                    else if (m_sql.m_windunit == WINDUNIT_MPH)
                    {
                        if (gust < 3.0f) speedpos = 0;
                        else if (gust < 7.0f) speedpos = 1;
                        else if (gust < 12.0f) speedpos = 2;
                        else if (gust < 18.0f) speedpos = 3;
                        else if (gust < 24.0f) speedpos = 4;
                        else if (gust < 46.0f) speedpos = 5;
                        else speedpos = 6;
                    }
                    else if (m_sql.m_windunit == WINDUNIT_Knots)
                    {
                        if (gust < 3.0f) speedpos = 0;
                        else if (gust < 7.0f) speedpos = 1;
                        else if (gust < 17.0f) speedpos = 2;
                        else if (gust < 27.0f) speedpos = 3;
                        else if (gust < 34.0f) speedpos = 4;
                        else if (gust < 41.0f) speedpos = 5;
                        else speedpos = 6;
                    }
                    else if (m_sql.m_windunit == WINDUNIT_Beaufort)
                    {
                        float gustms = gustOrg*0.1f;
                        int iBeaufort = MStoBeaufort(gustms);
                        if (iBeaufort < 2) speedpos = 0;
                        else if (iBeaufort < 4) speedpos = 1;
                        else if (iBeaufort < 6) speedpos = 2;
                        else if (iBeaufort < 8) speedpos = 3;
                        else if (iBeaufort < 10) speedpos = 4;
                        else if (iBeaufort < 12) speedpos = 5;
                        else speedpos = 6;
                    }
                    else
                    {
                        //Still todo !
                        if (gust < 0.5f) speedpos = 0;
                        else if (gust < 2.0f) speedpos = 1;
                        else if (gust < 4.0f) speedpos = 2;
                        else if (gust < 6.0f) speedpos = 3;
                        else if (gust < 8.0f) speedpos = 4;
                        else if (gust < 10.0f) speedpos = 5;
                        else speedpos = 6;
                    }
                    wdirtabletemp[bucket][speedpos]++;
                    _directions[direction]++;
                    totalvalues++;
                }

                for (int jj = 0; jj < 7; jj++)
                {
                    root["result_speed"][jj]["label"] = szLegendLabels[jj];

                    for (ii = 0; ii < 16; ii++)
                    {
                        float svalue = 0;
                        if (totalvalues > 0)
                        {
                            svalue = (100.0f / totalvalues)*wdirtabletemp[ii][jj];
                        }
                        sprintf(szTmp, "%.2f", svalue);
                        root["result_speed"][jj]["sp"][ii] = szTmp;
                    }
                }
                ii = 0;
                for (idir = 0; idir < 360 + 1; idir++)
                {
                    if (_directions[idir] != 0)
                    {
                        root["result"][ii]["dig"] = idir;
                        float percentage = 0;
                        if (totalvalues > 0)
                        {
                            percentage = (float(100.0 / float(totalvalues))*float(_directions[idir]));
                        }
                        sprintf(szTmp, "%.2f", percentage);
                        root["result"][ii]["div"] = szTmp;
                        ii++;
                    }
                }
            }
        }

    }//day
    else if (srange == "week")
    {
        if (sensor == "rain") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            char szDateStart[40];
            char szDateEnd[40];
            sprintf(szDateEnd, "%04d-%02d-%02d", tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday);

            //Subtract one week
            time_t weekbefore;
            struct tm tm2;
            getNoon(weekbefore, tm2, tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday - 7); // We only want the date
            sprintf(szDateStart, "%04d-%02d-%02d", tm2.tm_year + 1900, tm2.tm_mon + 1, tm2.tm_mday);

            result = m_sql.safe_query("SELECT Total, Rate, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[2].substr(0, 16);
                    double mmval = atof(sd[0].c_str());
                    mmval *= AddjMulti;
                    sprintf(szTmp, "%.1f", mmval);
                    root["result"][ii]["mm"] = szTmp;
                    ii++;
                }
            }
            //add today (have to calculate it)
            if (dSubType != sTypeRAINWU)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Total), MAX(Total), MAX(Rate) FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT Total, Total, Rate FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q') ORDER BY ROWID DESC LIMIT 1",
                    idx, szDateEnd);
            }
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                float total_min = static_cast<float>(atof(sd[0].c_str()));
                float total_max = static_cast<float>(atof(sd[1].c_str()));
                int rate = atoi(sd[2].c_str());

                double total_real = 0;
                if (dSubType != sTypeRAINWU)
                {
                    total_real = total_max - total_min;
                }
                else
                {
                    total_real = total_max;
                }
                total_real *= AddjMulti;
                sprintf(szTmp, "%.1f", total_real);
                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["mm"] = szTmp;
                ii++;
            }
        }
        else if (sensor == "counter")
        {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;
            root["ValueQuantity"] = options["ValueQuantity"];
            root["ValueUnits"] = options["ValueUnits"];

            float EnergyDivider = 1000.0f;
            float GasDivider = 100.0f;
            float WaterDivider = 100.0f;
            int tValue;
            if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
            {
                EnergyDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerGas", tValue))
            {
                GasDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
            {
                WaterDivider = float(tValue);
            }
            if (dType == pTypeP1Gas)
                GasDivider = 1000.0f;
            else if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                EnergyDivider *= 100.0f;
            //else if (dType==pTypeRFXMeter)
            //EnergyDivider*=1000.0f;

            char szDateStart[40];
            char szDateEnd[40];
            sprintf(szDateEnd, "%04d-%02d-%02d", tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday);

            //Subtract one week
            time_t weekbefore;
            struct tm tm2;
            getNoon(weekbefore, tm2, tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday - 7); // We only want the date
            sprintf(szDateStart, "%04d-%02d-%02d", tm2.tm_year + 1900, tm2.tm_mon + 1, tm2.tm_mday);

            int ii = 0;
            if (dType == pTypeP1Power)
            {
                result = m_sql.safe_query("SELECT Value1,Value2,Value5,Value6,Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    bool bHaveDeliverd = false;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;
                        root["result"][ii]["d"] = sd[4].substr(0, 16);
                        std::string szValueUsage1 = sd[0];
                        std::string szValueDeliv1 = sd[1];
                        std::string szValueUsage2 = sd[2];
                        std::string szValueDeliv2 = sd[3];

                        float fUsage1 = (float)(atof(szValueUsage1.c_str()));
                        float fUsage2 = (float)(atof(szValueUsage2.c_str()));
                        float fDeliv1 = (float)(atof(szValueDeliv1.c_str()));
                        float fDeliv2 = (float)(atof(szValueDeliv2.c_str()));

                        fDeliv1 = (fDeliv1 < 10) ? 0 : fDeliv1;
                        fDeliv2 = (fDeliv2 < 10) ? 0 : fDeliv2;

                        if ((fDeliv1 != 0) || (fDeliv2 != 0))
                            bHaveDeliverd = true;
                        sprintf(szTmp, "%.3f", fUsage1 / EnergyDivider);
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.3f", fUsage2 / EnergyDivider);
                        root["result"][ii]["v2"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv1 / EnergyDivider);
                        root["result"][ii]["r1"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv2 / EnergyDivider);
                        root["result"][ii]["r2"] = szTmp;
                        ii++;
                    }
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
                //add today (have to calculate it)
                result = m_sql.safe_query(
                    "SELECT MIN(Value1), MAX(Value1), MIN(Value2), MAX(Value2),MIN(Value5), MAX(Value5), MIN(Value6), MAX(Value6) FROM MultiMeter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];

                    unsigned long long total_min_usage_1, total_min_usage_2, total_max_usage_1, total_max_usage_2, total_real_usage_1, total_real_usage_2;
                    unsigned long long total_min_deliv_1, total_min_deliv_2, total_max_deliv_1, total_max_deliv_2, total_real_deliv_1, total_real_deliv_2;

                    bool bHaveDeliverd = false;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min_usage_1;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max_usage_1;
                    std::stringstream s_str3(sd[4]);
                    s_str3 >> total_min_usage_2;
                    std::stringstream s_str4(sd[5]);
                    s_str4 >> total_max_usage_2;

                    total_real_usage_1 = total_max_usage_1 - total_min_usage_1;
                    total_real_usage_2 = total_max_usage_2 - total_min_usage_2;

                    std::stringstream s_str5(sd[2]);
                    s_str5 >> total_min_deliv_1;
                    std::stringstream s_str6(sd[3]);
                    s_str6 >> total_max_deliv_1;
                    std::stringstream s_str7(sd[6]);
                    s_str7 >> total_min_deliv_2;
                    std::stringstream s_str8(sd[7]);
                    s_str8 >> total_max_deliv_2;

                    total_real_deliv_1 = total_max_deliv_1 - total_min_deliv_1;
                    total_real_deliv_2 = total_max_deliv_2 - total_min_deliv_2;
                    if ((total_real_deliv_1 != 0) || (total_real_deliv_2 != 0))
                        bHaveDeliverd = true;

                    root["result"][ii]["d"] = szDateEnd;

                    sprintf(szTmp, "%llu", total_real_usage_1);
                    std::string szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v"] = szTmp;
                    sprintf(szTmp, "%llu", total_real_usage_2);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v2"] = szTmp;

                    sprintf(szTmp, "%llu", total_real_deliv_1);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["r1"] = szTmp;
                    sprintf(szTmp, "%llu", total_real_deliv_2);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["r2"] = szTmp;

                    ii++;
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
            } //if (dType == pTypeP1Power)
            else
            {
                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        std::string szValue = sd[0];
                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                            szValue = szTmp;
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / GasDivider);
                            szValue = szTmp;
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                            szValue = szTmp;
                            break;
                        case MTYPE_COUNTER:
                            sprintf(szTmp, "%.0f", atof(szValue.c_str()));
                            szValue = szTmp;
                            break;
                        default:
                            szValue = "0";
                            break;
                        }
                        root["result"][ii]["v"] = szValue;
                        ii++;
                    }
                }
                //add today (have to calculate it)
                result = m_sql.safe_query("SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];

                    unsigned long long total_min, total_max, total_real;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max;
                    total_real = total_max - total_min;
                    sprintf(szTmp, "%llu", total_real);
                    std::string szValue = szTmp;
                    switch (metertype)
                    {
                    case MTYPE_ENERGY:
                    case MTYPE_ENERGY_GENERATED:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                        szValue = szTmp;
                        break;
                    case MTYPE_GAS:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / GasDivider);
                        szValue = szTmp;
                        break;
                    case MTYPE_WATER:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                        szValue = szTmp;
                        break;
                    default:
                        szValue = "0";
                        break;
                    }

                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["v"] = szValue;
                    ii++;
                }
            } //if (dType != pTypeP1Power)
        }
    }//week
    else if ((srange == "month") || (srange == "year"))
    {
        char szDateStart[40];
        char szDateEnd[40];
        char szDateStartPrev[40];
        char szDateEndPrev[40];

        std::string sactmonth = request::findValue(&req, "actmonth");
        std::string sactyear = request::findValue(&req, "actyear");

        int actMonth = atoi(sactmonth.c_str());
        int actYear = atoi(sactyear.c_str());

        if ((sactmonth != "") && (sactyear != ""))
        {
            sprintf(szDateStart, "%04d-%02d-%02d", actYear, actMonth, 1);
            sprintf(szDateStartPrev, "%04d-%02d-%02d", actYear - 1, actMonth, 1);
            actMonth++;
            if (actMonth == 13)
            {
                actMonth = 1;
                actYear++;
            }
            sprintf(szDateEnd, "%04d-%02d-%02d", actYear, actMonth, 1);
            sprintf(szDateEndPrev, "%04d-%02d-%02d", actYear - 1, actMonth, 1);
        }
        else if (sactyear != "")
        {
            sprintf(szDateStart, "%04d-%02d-%02d", actYear, 1, 1);
            sprintf(szDateStartPrev, "%04d-%02d-%02d", actYear - 1, 1, 1);
            actYear++;
            sprintf(szDateEnd, "%04d-%02d-%02d", actYear, 1, 1);
            sprintf(szDateEndPrev, "%04d-%02d-%02d", actYear - 1, 1, 1);
        }
        else
        {
            sprintf(szDateEnd, "%04d-%02d-%02d", tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday);
            sprintf(szDateEndPrev, "%04d-%02d-%02d", tm1.tm_year + 1900 - 1, tm1.tm_mon + 1, tm1.tm_mday);

            struct tm tm2;
            if (srange == "month")
            {
                //Subtract one month
                time_t monthbefore;
                getNoon(monthbefore, tm2, tm1.tm_year + 1900, tm1.tm_mon, tm1.tm_mday);
            }
            else
            {
                //Subtract one year
                time_t yearbefore;
                getNoon(yearbefore, tm2, tm1.tm_year + 1900 - 1, tm1.tm_mon + 1, tm1.tm_mday);
            }

            sprintf(szDateStart, "%04d-%02d-%02d", tm2.tm_year + 1900, tm2.tm_mon + 1, tm2.tm_mday);
            sprintf(szDateStartPrev, "%04d-%02d-%02d", tm2.tm_year + 1900 - 1, tm2.tm_mon + 1, tm2.tm_mday);
        }

        if (sensor == "temp") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            //Actual Year
            result = m_sql.safe_query(
                "SELECT Temp_Min, Temp_Max, Chill_Min, Chill_Max,"
                " Humidity, Barometer, Temp_Avg, Date, SetPoint_Min,"
                " SetPoint_Max, SetPoint_Avg "
                "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                " AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[7].substr(0, 16);

                    if (
                        (dType == pTypeRego6XXTemp) || (dType == pTypeTEMP) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO) || (dType == pTypeTEMP_BARO) || (dType == pTypeWIND) || (dType == pTypeThermostat1) || (dType == pTypeRadiator1) ||
                        ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorTemp)) ||
                        ((dType == pTypeUV) && (dSubType == sTypeUV3)) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeSystemTemp)) ||
                        ((dType == pTypeThermostat) && (dSubType == sTypeThermSetpoint)) ||
                        (dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        )
                    {
                        bool bOK = true;
                        if (dType == pTypeWIND)
                        {
                            bOK = ((dSubType == sTypeWIND4) || (dSubType == sTypeWINDNoTemp));
                        }
                        if (bOK)
                        {
                            double te = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                            double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                            double ta = ConvertTemperature(atof(sd[6].c_str()), tempsign);
                            root["result"][ii]["te"] = te;
                            root["result"][ii]["tm"] = tm;
                            root["result"][ii]["ta"] = ta;
                        }
                    }
                    if (
                        ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                        ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp))
                        )
                    {
                        double ch = ConvertTemperature(atof(sd[3].c_str()), tempsign);
                        double cm = ConvertTemperature(atof(sd[2].c_str()), tempsign);
                        root["result"][ii]["ch"] = ch;
                        root["result"][ii]["cm"] = cm;
                    }
                    if ((dType == pTypeHUM) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO))
                    {
                        root["result"][ii]["hu"] = sd[4];
                    }
                    if (
                        (dType == pTypeTEMP_HUM_BARO) ||
                        (dType == pTypeTEMP_BARO) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        )
                    {
                        if (dType == pTypeTEMP_HUM_BARO)
                        {
                            if (dSubType == sTypeTHBFloat)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                            else
                                root["result"][ii]["ba"] = sd[5];
                        }
                        else if (dType == pTypeTEMP_BARO)
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                        else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                    }
                    if ((dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater))
                    {
                        double sm = ConvertTemperature(atof(sd[8].c_str()), tempsign);
                        double sx = ConvertTemperature(atof(sd[9].c_str()), tempsign);
                        double se = ConvertTemperature(atof(sd[10].c_str()), tempsign);
                        root["result"][ii]["sm"] = sm;
                        root["result"][ii]["se"] = se;
                        root["result"][ii]["sx"] = sx;
                    }
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT MIN(Temperature), MAX(Temperature),"
                " MIN(Chill), MAX(Chill), AVG(Humidity),"
                " AVG(Barometer), AVG(Temperature), MIN(SetPoint),"
                " MAX(SetPoint), AVG(SetPoint) "
                "FROM Temperature WHERE (DeviceRowID==%" PRIu64 ""
                " AND Date>='%q')",
                idx, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                root["result"][ii]["d"] = szDateEnd;
                if (
                    ((dType == pTypeRego6XXTemp) || (dType == pTypeTEMP) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO) || (dType == pTypeTEMP_BARO) || (dType == pTypeWIND) || (dType == pTypeThermostat1) || (dType == pTypeRadiator1)) ||
                    ((dType == pTypeUV) && (dSubType == sTypeUV3)) ||
                    ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                    ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp)) ||
                    (dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater)
                    )
                {
                    double te = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                    double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                    double ta = ConvertTemperature(atof(sd[6].c_str()), tempsign);

                    root["result"][ii]["te"] = te;
                    root["result"][ii]["tm"] = tm;
                    root["result"][ii]["ta"] = ta;
                }
                if (
                    ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                    ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp))
                    )
                {
                    double ch = ConvertTemperature(atof(sd[3].c_str()), tempsign);
                    double cm = ConvertTemperature(atof(sd[2].c_str()), tempsign);
                    root["result"][ii]["ch"] = ch;
                    root["result"][ii]["cm"] = cm;
                }
                if ((dType == pTypeHUM) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO))
                {
                    root["result"][ii]["hu"] = sd[4];
                }
                if (
                    (dType == pTypeTEMP_HUM_BARO) ||
                    (dType == pTypeTEMP_BARO) ||
                    ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                    )
                {
                    if (dType == pTypeTEMP_HUM_BARO)
                    {
                        if (dSubType == sTypeTHBFloat)
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                        else
                            root["result"][ii]["ba"] = sd[5];
                    }
                    else if (dType == pTypeTEMP_BARO)
                    {
                        sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                        root["result"][ii]["ba"] = szTmp;
                    }
                    else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                    {
                        sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                        root["result"][ii]["ba"] = szTmp;
                    }
                }
                if ((dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater))
                {
                    double sx = ConvertTemperature(atof(sd[8].c_str()), tempsign);
                    double sm = ConvertTemperature(atof(sd[7].c_str()), tempsign);
                    double se = ConvertTemperature(atof(sd[9].c_str()), tempsign);
                    root["result"][ii]["se"] = se;
                    root["result"][ii]["sm"] = sm;
                    root["result"][ii]["sx"] = sx;
                }
                ii++;
            }
            //Previous Year
            result = m_sql.safe_query(
                "SELECT Temp_Min, Temp_Max, Chill_Min, Chill_Max,"
                " Humidity, Barometer, Temp_Avg, Date, SetPoint_Min,"
                " SetPoint_Max, SetPoint_Avg "
                "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                " AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
            if (result.size() > 0)
            {
                iPrev = 0;
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["resultprev"][iPrev]["d"] = sd[7].substr(0, 16);

                    if (
                        (dType == pTypeRego6XXTemp) || (dType == pTypeTEMP) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO) || (dType == pTypeTEMP_BARO) || (dType == pTypeWIND) || (dType == pTypeThermostat1) || (dType == pTypeRadiator1) ||
                        ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorTemp)) ||
                        ((dType == pTypeUV) && (dSubType == sTypeUV3)) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeSystemTemp)) ||
                        ((dType == pTypeThermostat) && (dSubType == sTypeThermSetpoint)) ||
                        (dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater)
                        )
                    {
                        bool bOK = true;
                        if (dType == pTypeWIND)
                        {
                            bOK = ((dSubType == sTypeWIND4) || (dSubType == sTypeWINDNoTemp));
                        }
                        if (bOK)
                        {
                            double te = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                            double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                            double ta = ConvertTemperature(atof(sd[6].c_str()), tempsign);
                            root["resultprev"][iPrev]["te"] = te;
                            root["resultprev"][iPrev]["tm"] = tm;
                            root["resultprev"][iPrev]["ta"] = ta;
                        }
                    }
                    if (
                        ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                        ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp))
                        )
                    {
                        double ch = ConvertTemperature(atof(sd[3].c_str()), tempsign);
                        double cm = ConvertTemperature(atof(sd[2].c_str()), tempsign);
                        root["resultprev"][iPrev]["ch"] = ch;
                        root["resultprev"][iPrev]["cm"] = cm;
                    }
                    if ((dType == pTypeHUM) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO))
                    {
                        root["resultprev"][iPrev]["hu"] = sd[4];
                    }
                    if (
                        (dType == pTypeTEMP_HUM_BARO) ||
                        (dType == pTypeTEMP_BARO) ||
                        ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        )
                    {
                        if (dType == pTypeTEMP_HUM_BARO)
                        {
                            if (dSubType == sTypeTHBFloat)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                root["resultprev"][iPrev]["ba"] = szTmp;
                            }
                            else
                                root["resultprev"][iPrev]["ba"] = sd[5];
                        }
                        else if (dType == pTypeTEMP_BARO)
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["resultprev"][iPrev]["ba"] = szTmp;
                        }
                        else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["resultprev"][iPrev]["ba"] = szTmp;
                        }
                    }
                    if ((dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater))
                    {
                        double sx = ConvertTemperature(atof(sd[8].c_str()), tempsign);
                        double sm = ConvertTemperature(atof(sd[7].c_str()), tempsign);
                        double se = ConvertTemperature(atof(sd[9].c_str()), tempsign);
                        root["resultprev"][iPrev]["se"] = se;
                        root["resultprev"][iPrev]["sm"] = sm;
                        root["resultprev"][iPrev]["sx"] = sx;
                    }
                    iPrev++;
                }
            }
        }
        else if (sensor == "Percentage") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Percentage_Min, Percentage_Max, Percentage_Avg, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[3].substr(0, 16);
                    root["result"][ii]["v_min"] = sd[0];
                    root["result"][ii]["v_max"] = sd[1];
                    root["result"][ii]["v_avg"] = sd[2];
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT MIN(Percentage), MAX(Percentage), AVG(Percentage) FROM Percentage WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                idx, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];
                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["v_min"] = sd[0];
                root["result"][ii]["v_max"] = sd[1];
                root["result"][ii]["v_avg"] = sd[2];
                ii++;
            }

        }
        else if (sensor == "fan") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Speed_Min, Speed_Max, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[2].substr(0, 16);
                    root["result"][ii]["v_max"] = sd[1];
                    root["result"][ii]["v_min"] = sd[0];
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query("SELECT MIN(Speed), MAX(Speed) FROM Fan WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                idx, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];
                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["v_max"] = sd[1];
                root["result"][ii]["v_min"] = sd[0];
                ii++;
            }

        }
        else if (sensor == "uv") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Level, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[1].substr(0, 16);
                    root["result"][ii]["uvi"] = sd[0];
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT MAX(Level) FROM UV WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                idx, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["uvi"] = sd[0];
                ii++;
            }
            //Previous Year
            result = m_sql.safe_query("SELECT Level, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
            if (result.size() > 0)
            {
                iPrev = 0;
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["resultprev"][iPrev]["d"] = sd[1].substr(0, 16);
                    root["resultprev"][iPrev]["uvi"] = sd[0];
                    iPrev++;
                }
            }
        }
        else if (sensor == "rain") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query("SELECT Total, Rate, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[2].substr(0, 16);
                    double mmval = atof(sd[0].c_str());
                    mmval *= AddjMulti;
                    sprintf(szTmp, "%.1f", mmval);
                    root["result"][ii]["mm"] = szTmp;
                    ii++;
                }
            }
            //add today (have to calculate it)
            if (dSubType != sTypeRAINWU)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Total), MAX(Total), MAX(Rate) FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT Total, Total, Rate FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q') ORDER BY ROWID DESC LIMIT 1",
                    idx, szDateEnd);
            }
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                float total_min = static_cast<float>(atof(sd[0].c_str()));
                float total_max = static_cast<float>(atof(sd[1].c_str()));
                int rate = atoi(sd[2].c_str());

                double total_real = 0;
                if (dSubType != sTypeRAINWU)
                {
                    total_real = total_max - total_min;
                }
                else
                {
                    total_real = total_max;
                }
                total_real *= AddjMulti;
                sprintf(szTmp, "%.1f", total_real);
                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["mm"] = szTmp;
                ii++;
            }
            //Previous Year
            result = m_sql.safe_query(
                "SELECT Total, Rate, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
            if (result.size() > 0)
            {
                iPrev = 0;
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["resultprev"][iPrev]["d"] = sd[2].substr(0, 16);
                    double mmval = atof(sd[0].c_str());
                    mmval *= AddjMulti;
                    sprintf(szTmp, "%.1f", mmval);
                    root["resultprev"][iPrev]["mm"] = szTmp;
                    iPrev++;
                }
            }
        }
        else if (sensor == "counter") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;
            root["ValueQuantity"] = options["ValueQuantity"];
            root["ValueUnits"] = options["ValueUnits"];

            int nValue = 0;
            std::string sValue = "";

            result = m_sql.safe_query("SELECT nValue, sValue FROM DeviceStatus WHERE (ID==%" PRIu64 ")",
                idx);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];
                nValue = atoi(sd[0].c_str());
                sValue = sd[1];
            }

            float EnergyDivider = 1000.0f;
            float GasDivider = 100.0f;
            float WaterDivider = 100.0f;
            int tValue;
            if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
            {
                EnergyDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerGas", tValue))
            {
                GasDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
            {
                WaterDivider = float(tValue);
            }
            if (dType == pTypeP1Gas)
                GasDivider = 1000.0f;
            else if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                EnergyDivider *= 100.0f;
            //				else if (dType==pTypeRFXMeter)
            //				EnergyDivider*=1000.0f;

            int ii = 0;
            iPrev = 0;
            if (dType == pTypeP1Power)
            {
                //Actual Year
                result = m_sql.safe_query(
                    "SELECT Value1,Value2,Value5,Value6, Date,"
                    " Counter1, Counter2, Counter3, Counter4 "
                    "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                    " AND Date<='%q') ORDER BY Date ASC",
                    dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    bool bHaveDeliverd = false;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[4].substr(0, 16);

                        double counter_1 = atof(sd[5].c_str());
                        double counter_2 = atof(sd[6].c_str());
                        double counter_3 = atof(sd[7].c_str());
                        double counter_4 = atof(sd[8].c_str());

                        std::string szUsage1 = sd[0];
                        std::string szDeliv1 = sd[1];
                        std::string szUsage2 = sd[2];
                        std::string szDeliv2 = sd[3];

                        float fUsage_1 = static_cast<float>(atof(szUsage1.c_str()));
                        float fUsage_2 = static_cast<float>(atof(szUsage2.c_str()));
                        float fDeliv_1 = static_cast<float>(atof(szDeliv1.c_str()));
                        float fDeliv_2 = static_cast<float>(atof(szDeliv2.c_str()));

                        fDeliv_1 = (fDeliv_1 < 10) ? 0 : fDeliv_1;
                        fDeliv_2 = (fDeliv_2 < 10) ? 0 : fDeliv_2;

                        if ((fDeliv_1 != 0) || (fDeliv_2 != 0))
                            bHaveDeliverd = true;
                        sprintf(szTmp, "%.3f", fUsage_1 / EnergyDivider);
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.3f", fUsage_2 / EnergyDivider);
                        root["result"][ii]["v2"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv_1 / EnergyDivider);
                        root["result"][ii]["r1"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv_2 / EnergyDivider);
                        root["result"][ii]["r2"] = szTmp;

                        if (counter_1 != 0)
                        {
                            sprintf(szTmp, "%.3f", (counter_1 - fUsage_1) / EnergyDivider);
                        }
                        else
                        {
                            strcpy(szTmp, "0");
                        }
                        root["result"][ii]["c1"] = szTmp;

                        if (counter_2 != 0)
                        {
                            sprintf(szTmp, "%.3f", (counter_2 - fDeliv_1) / EnergyDivider);
                        }
                        else
                        {
                            strcpy(szTmp, "0");
                        }
                        root["result"][ii]["c2"] = szTmp;

                        if (counter_3 != 0)
                        {
                            sprintf(szTmp, "%.3f", (counter_3 - fUsage_2) / EnergyDivider);
                        }
                        else
                        {
                            strcpy(szTmp, "0");
                        }
                        root["result"][ii]["c3"] = szTmp;

                        if (counter_4 != 0)
                        {
                            sprintf(szTmp, "%.3f", (counter_4 - fDeliv_2) / EnergyDivider);
                        }
                        else
                        {
                            strcpy(szTmp, "0");
                        }
                        root["result"][ii]["c4"] = szTmp;

                        ii++;
                    }
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
                //Previous Year
                result = m_sql.safe_query(
                    "SELECT Value1,Value2,Value5,Value6, Date "
                    "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC",
                    dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
                if (result.size() > 0)
                {
                    bool bHaveDeliverd = false;
                    iPrev = 0;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["resultprev"][iPrev]["d"] = sd[4].substr(0, 16);

                        std::string szUsage1 = sd[0];
                        std::string szDeliv1 = sd[1];
                        std::string szUsage2 = sd[2];
                        std::string szDeliv2 = sd[3];

                        float fUsage_1 = static_cast<float>(atof(szUsage1.c_str()));
                        float fUsage_2 = static_cast<float>(atof(szUsage2.c_str()));
                        float fDeliv_1 = static_cast<float>(atof(szDeliv1.c_str()));
                        float fDeliv_2 = static_cast<float>(atof(szDeliv2.c_str()));

                        if ((fDeliv_1 != 0) || (fDeliv_2 != 0))
                            bHaveDeliverd = true;
                        sprintf(szTmp, "%.3f", fUsage_1 / EnergyDivider);
                        root["resultprev"][iPrev]["v"] = szTmp;
                        sprintf(szTmp, "%.3f", fUsage_2 / EnergyDivider);
                        root["resultprev"][iPrev]["v2"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv_1 / EnergyDivider);
                        root["resultprev"][iPrev]["r1"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv_2 / EnergyDivider);
                        root["resultprev"][iPrev]["r2"] = szTmp;
                        iPrev++;
                    }
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
            }
            else if (dType == pTypeAirQuality)
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value1,Value2,Value3,Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[3].substr(0, 16);
                        root["result"][ii]["co2_min"] = sd[0];
                        root["result"][ii]["co2_max"] = sd[1];
                        root["result"][ii]["co2_avg"] = sd[2];
                        ii++;
                    }
                }
                result = m_sql.safe_query("SELECT Value2,Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
                if (result.size() > 0)
                {
                    iPrev = 0;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["resultprev"][iPrev]["d"] = sd[1].substr(0, 16);
                        root["resultprev"][iPrev]["co2_max"] = sd[0];
                        iPrev++;
                    }
                }
            }
            else if (
                ((dType == pTypeGeneral) && ((dSubType == sTypeSoilMoisture) || (dSubType == sTypeLeafWetness))) ||
                ((dType == pTypeRFXSensor) && ((dSubType == sTypeRFXSensorAD) || (dSubType == sTypeRFXSensorVolt)))
                )
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value1,Value2, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[2].substr(0, 16);
                        root["result"][ii]["v_min"] = sd[0];
                        root["result"][ii]["v_max"] = sd[1];
                        ii++;
                    }
                }
            }
            else if (
                ((dType == pTypeGeneral) && (dSubType == sTypeVisibility)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeDistance)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeCurrent)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypePressure)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel))
                )
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                float vdiv = 10.0f;
                if (
                    ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                    ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                    )
                {
                    vdiv = 1000.0f;
                }

                result = m_sql.safe_query("SELECT Value1,Value2,Value3,Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        float fValue1 = float(atof(sd[0].c_str())) / vdiv;
                        float fValue2 = float(atof(sd[1].c_str())) / vdiv;
                        float fValue3 = float(atof(sd[2].c_str())) / vdiv;
                        root["result"][ii]["d"] = sd[3].substr(0, 16);

                        if (metertype == 1)
                        {
                            fValue1 *= 0.6214f;
                            fValue2 *= 0.6214f;
                        }
                        if (
                            ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                            ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                            )
                        {
                            sprintf(szTmp, "%.3f", fValue1);
                            root["result"][ii]["v_min"] = szTmp;
                            sprintf(szTmp, "%.3f", fValue2);
                            root["result"][ii]["v_max"] = szTmp;
                            if (fValue3 != 0)
                            {
                                sprintf(szTmp, "%.3f", fValue3);
                                root["result"][ii]["v_avg"] = szTmp;
                            }
                        }
                        else
                        {
                            sprintf(szTmp, "%.1f", fValue1);
                            root["result"][ii]["v_min"] = szTmp;
                            sprintf(szTmp, "%.1f", fValue2);
                            root["result"][ii]["v_max"] = szTmp;
                            if (fValue3 != 0)
                            {
                                sprintf(szTmp, "%.1f", fValue3);
                                root["result"][ii]["v_avg"] = szTmp;
                            }
                        }
                        ii++;
                    }
                }
            }
            else if (dType == pTypeLux)
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query("SELECT Value1,Value2,Value3, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[3].substr(0, 16);
                        root["result"][ii]["lux_min"] = sd[0];
                        root["result"][ii]["lux_max"] = sd[1];
                        root["result"][ii]["lux_avg"] = sd[2];
                        ii++;
                    }
                }
            }
            else if (dType == pTypeWEIGHT)
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query(
                    "SELECT Value1,Value2, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[2].substr(0, 16);
                        sprintf(szTmp, "%.1f", m_sql.m_weightscale * atof(sd[0].c_str()) / 10.0f);
                        root["result"][ii]["v_min"] = szTmp;
                        sprintf(szTmp, "%.1f", m_sql.m_weightscale * atof(sd[1].c_str()) / 10.0f);
                        root["result"][ii]["v_max"] = szTmp;
                        ii++;
                    }
                }
            }
            else if (dType == pTypeUsage)
            {//month/year
                root["status"] = "OK";
                root["title"] = "Graph " + sensor + " " + srange;

                result = m_sql.safe_query(
                    "SELECT Value1,Value2, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[2].substr(0, 16);
                        root["result"][ii]["u_min"] = atof(sd[0].c_str()) / 10.0f;
                        root["result"][ii]["u_max"] = atof(sd[1].c_str()) / 10.0f;
                        ii++;
                    }
                }
            }
            else if (dType == pTypeCURRENT)
            {
                result = m_sql.safe_query("SELECT Value1,Value2,Value3,Value4,Value5,Value6, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    //CM113
                    int displaytype = 0;
                    int voltage = 230;
                    m_sql.GetPreferencesVar("CM113DisplayType", displaytype);
                    m_sql.GetPreferencesVar("ElectricVoltage", voltage);

                    root["displaytype"] = displaytype;

                    bool bHaveL1 = false;
                    bool bHaveL2 = false;
                    bool bHaveL3 = false;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[6].substr(0, 16);

                        float fval1 = static_cast<float>(atof(sd[0].c_str()) / 10.0f);
                        float fval2 = static_cast<float>(atof(sd[1].c_str()) / 10.0f);
                        float fval3 = static_cast<float>(atof(sd[2].c_str()) / 10.0f);
                        float fval4 = static_cast<float>(atof(sd[3].c_str()) / 10.0f);
                        float fval5 = static_cast<float>(atof(sd[4].c_str()) / 10.0f);
                        float fval6 = static_cast<float>(atof(sd[5].c_str()) / 10.0f);

                        if ((fval1 != 0) || (fval2 != 0))
                            bHaveL1 = true;
                        if ((fval3 != 0) || (fval4 != 0))
                            bHaveL2 = true;
                        if ((fval5 != 0) || (fval6 != 0))
                            bHaveL3 = true;

                        if (displaytype == 0)
                        {
                            sprintf(szTmp, "%.1f", fval1);
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%.1f", fval2);
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%.1f", fval3);
                            root["result"][ii]["v3"] = szTmp;
                            sprintf(szTmp, "%.1f", fval4);
                            root["result"][ii]["v4"] = szTmp;
                            sprintf(szTmp, "%.1f", fval5);
                            root["result"][ii]["v5"] = szTmp;
                            sprintf(szTmp, "%.1f", fval6);
                            root["result"][ii]["v6"] = szTmp;
                        }
                        else
                        {
                            sprintf(szTmp, "%d", int(fval1*voltage));
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%d", int(fval2*voltage));
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%d", int(fval3*voltage));
                            root["result"][ii]["v3"] = szTmp;
                            sprintf(szTmp, "%d", int(fval4*voltage));
                            root["result"][ii]["v4"] = szTmp;
                            sprintf(szTmp, "%d", int(fval5*voltage));
                            root["result"][ii]["v5"] = szTmp;
                            sprintf(szTmp, "%d", int(fval6*voltage));
                            root["result"][ii]["v6"] = szTmp;
                        }

                        ii++;
                    }
                    if (
                        (!bHaveL1) &&
                        (!bHaveL2) &&
                        (!bHaveL3)
                        ) {
                        root["haveL1"] = true; //show at least something
                    }
                    else {
                        if (bHaveL1)
                            root["haveL1"] = true;
                        if (bHaveL2)
                            root["haveL2"] = true;
                        if (bHaveL3)
                            root["haveL3"] = true;
                    }
                }
            }
            else if (dType == pTypeCURRENTENERGY)
            {
                result = m_sql.safe_query("SELECT Value1,Value2,Value3,Value4,Value5,Value6, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    //CM180i
                    int displaytype = 0;
                    int voltage = 230;
                    m_sql.GetPreferencesVar("CM113DisplayType", displaytype);
                    m_sql.GetPreferencesVar("ElectricVoltage", voltage);

                    root["displaytype"] = displaytype;

                    bool bHaveL1 = false;
                    bool bHaveL2 = false;
                    bool bHaveL3 = false;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[6].substr(0, 16);

                        float fval1 = static_cast<float>(atof(sd[0].c_str()) / 10.0f);
                        float fval2 = static_cast<float>(atof(sd[1].c_str()) / 10.0f);
                        float fval3 = static_cast<float>(atof(sd[2].c_str()) / 10.0f);
                        float fval4 = static_cast<float>(atof(sd[3].c_str()) / 10.0f);
                        float fval5 = static_cast<float>(atof(sd[4].c_str()) / 10.0f);
                        float fval6 = static_cast<float>(atof(sd[5].c_str()) / 10.0f);

                        if ((fval1 != 0) || (fval2 != 0))
                            bHaveL1 = true;
                        if ((fval3 != 0) || (fval4 != 0))
                            bHaveL2 = true;
                        if ((fval5 != 0) || (fval6 != 0))
                            bHaveL3 = true;

                        if (displaytype == 0)
                        {
                            sprintf(szTmp, "%.1f", fval1);
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%.1f", fval2);
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%.1f", fval3);
                            root["result"][ii]["v3"] = szTmp;
                            sprintf(szTmp, "%.1f", fval4);
                            root["result"][ii]["v4"] = szTmp;
                            sprintf(szTmp, "%.1f", fval5);
                            root["result"][ii]["v5"] = szTmp;
                            sprintf(szTmp, "%.1f", fval6);
                            root["result"][ii]["v6"] = szTmp;
                        }
                        else
                        {
                            sprintf(szTmp, "%d", int(fval1*voltage));
                            root["result"][ii]["v1"] = szTmp;
                            sprintf(szTmp, "%d", int(fval2*voltage));
                            root["result"][ii]["v2"] = szTmp;
                            sprintf(szTmp, "%d", int(fval3*voltage));
                            root["result"][ii]["v3"] = szTmp;
                            sprintf(szTmp, "%d", int(fval4*voltage));
                            root["result"][ii]["v4"] = szTmp;
                            sprintf(szTmp, "%d", int(fval5*voltage));
                            root["result"][ii]["v5"] = szTmp;
                            sprintf(szTmp, "%d", int(fval6*voltage));
                            root["result"][ii]["v6"] = szTmp;
                        }

                        ii++;
                    }
                    if (
                        (!bHaveL1) &&
                        (!bHaveL2) &&
                        (!bHaveL3)
                        ) {
                        root["haveL1"] = true; //show at least something
                    }
                    else {
                        if (bHaveL1)
                            root["haveL1"] = true;
                        if (bHaveL2)
                            root["haveL2"] = true;
                        if (bHaveL3)
                            root["haveL3"] = true;
                    }
                }
            }
            else
            {
                if (dType == pTypeP1Gas)
                {
                    //Add last counter value
                    sprintf(szTmp, "%.3f", atof(sValue.c_str()) / 1000.0);
                    root["counter"] = szTmp;
                }
                else if (dType == pTypeENERGY)
                {
                    size_t spos = sValue.find(";");
                    if (spos != std::string::npos)
                    {
                        float fvalue = static_cast<float>(atof(sValue.substr(spos + 1).c_str()));
                        sprintf(szTmp, "%.3f", fvalue / (EnergyDivider / 100.0f));
                        root["counter"] = szTmp;
                    }
                }
                else if ((dType == pTypeGeneral) && (dSubType == sTypeKwh))
                {
                    size_t spos = sValue.find(";");
                    if (spos != std::string::npos)
                    {
                        float fvalue = static_cast<float>(atof(sValue.substr(spos + 1).c_str()));
                        sprintf(szTmp, "%.3f", fvalue / EnergyDivider);
                        root["counter"] = szTmp;
                    }
                }
                else if (dType == pTypeRFXMeter)
                {
                    //Add last counter value
                    float fvalue = static_cast<float>(atof(sValue.c_str()));
                    switch (metertype)
                    {
                    case MTYPE_ENERGY:
                    case MTYPE_ENERGY_GENERATED:
                        sprintf(szTmp, "%.3f", AddjValue + (fvalue / EnergyDivider));
                        break;
                    case MTYPE_GAS:
                        sprintf(szTmp, "%.2f", AddjValue + (fvalue / GasDivider));
                        break;
                    case MTYPE_WATER:
                        sprintf(szTmp, "%.3f", AddjValue + (fvalue / WaterDivider));
                        break;
                    default:
                        strcpy(szTmp, "");
                        break;
                    }
                    root["counter"] = szTmp;
                }
                else if (dType == pTypeYouLess)
                {
                    std::vector<std::string> results;
                    StringSplit(sValue, ";", results);
                    if (results.size() == 2)
                    {
                        //Add last counter value
                        float fvalue = static_cast<float>(atof(results[0].c_str()));
                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", fvalue / EnergyDivider);
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.2f", fvalue / GasDivider);
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", fvalue / WaterDivider);
                            break;
                        default:
                            strcpy(szTmp, "");
                            break;
                        }
                        root["counter"] = szTmp;
                    }
                }
                else
                {
                    //Add last counter value
                    if (sValue.find('.') != std::string::npos)
                    {
                        sprintf(szTmp, "%.3f", AddjValue + atof(sValue.c_str()));
                    }
                    else
                    {
                        unsigned long long ullCounter;
                        std::stringstream s_str1(sValue);
                        s_str1 >> ullCounter;

                        sprintf(szTmp, "%llu", static_cast<unsigned long long>(AddjValue) + ullCounter);
                    }
                    root["counter"] = szTmp;
                }
                //Actual Year
                result = m_sql.safe_query("SELECT Value, Date, Counter FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[1].substr(0, 16);

                        std::string szValue = sd[0];

                        double fcounter = atof(sd[2].c_str());

                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                            root["result"][ii]["v"] = szTmp;
                            if (fcounter != 0)
                                sprintf(szTmp, "%.3f", AddjValue + ((fcounter - atof(szValue.c_str())) / EnergyDivider));
                            else
                                strcpy(szTmp, "0");
                            root["result"][ii]["c"] = szTmp;
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.2f", atof(szValue.c_str()) / GasDivider);
                            root["result"][ii]["v"] = szTmp;
                            if (fcounter != 0)
                                sprintf(szTmp, "%.2f", AddjValue + ((fcounter - atof(szValue.c_str())) / GasDivider));
                            else
                                strcpy(szTmp, "0");
                            root["result"][ii]["c"] = szTmp;
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                            root["result"][ii]["v"] = szTmp;
                            if (fcounter != 0)
                                sprintf(szTmp, "%.3f", AddjValue + ((fcounter - atof(szValue.c_str())) / WaterDivider));
                            else
                                strcpy(szTmp, "0");
                            root["result"][ii]["c"] = szTmp;
                            break;
                        case MTYPE_COUNTER:
                            sprintf(szTmp, "%.0f", atof(szValue.c_str()));
                            root["result"][ii]["v"] = szTmp;
                            if (fcounter != 0)
                                sprintf(szTmp, "%.0f", AddjValue + ((fcounter - atof(szValue.c_str()))));
                            else
                                strcpy(szTmp, "0");
                            root["result"][ii]["c"] = szTmp;
                            break;
                        }
                        ii++;
                    }
                }
                //Past Year
                result = m_sql.safe_query("SELECT Value, Date, Counter FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
                if (result.size() > 0)
                {
                    iPrev = 0;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["resultprev"][iPrev]["d"] = sd[1].substr(0, 16);

                        std::string szValue = sd[0];
                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                            root["resultprev"][iPrev]["v"] = szTmp;
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.2f", atof(szValue.c_str()) / GasDivider);
                            root["resultprev"][iPrev]["v"] = szTmp;
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                            root["resultprev"][iPrev]["v"] = szTmp;
                            break;
                        case MTYPE_COUNTER:
                            sprintf(szTmp, "%.0f", atof(szValue.c_str()));
                            root["resultprev"][iPrev]["v"] = szTmp;
                            break;
                        }
                        iPrev++;
                    }
                }
            }
            //add today (have to calculate it)

            if ((sactmonth != "") || (sactyear != ""))
            {
                struct tm loctime;
                time_t now = mytime(NULL);
                localtime_r(&now, &loctime);
                if ((sactmonth != "") && (sactyear != ""))
                {
                    bool bIsThisMonth = (atoi(sactyear.c_str()) == loctime.tm_year + 1900) && (atoi(sactmonth.c_str()) == loctime.tm_mon + 1);
                    if (bIsThisMonth)
                    {
                        sprintf(szDateEnd, "%04d-%02d-%02d", loctime.tm_year + 1900, loctime.tm_mon + 1, loctime.tm_mday);
                    }
                }
                else if (sactyear != "")
                {
                    bool bIsThisYear = (atoi(sactyear.c_str()) == loctime.tm_year + 1900);
                    if (bIsThisYear)
                    {
                        sprintf(szDateEnd, "%04d-%02d-%02d", loctime.tm_year + 1900, loctime.tm_mon + 1, loctime.tm_mday);
                    }

                }
            }

            if (dType == pTypeP1Power)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value1), MAX(Value1), MIN(Value2),"
                    " MAX(Value2), MIN(Value5), MAX(Value5),"
                    " MIN(Value6), MAX(Value6) "
                    "FROM MultiMeter WHERE (DeviceRowID=%" PRIu64 ""
                    " AND Date>='%q')",
                    idx, szDateEnd);
                bool bHaveDeliverd = false;
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];
                    unsigned long long total_min_usage_1, total_min_usage_2, total_max_usage_1, total_max_usage_2, total_real_usage_1, total_real_usage_2;
                    unsigned long long total_min_deliv_1, total_min_deliv_2, total_max_deliv_1, total_max_deliv_2, total_real_deliv_1, total_real_deliv_2;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min_usage_1;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max_usage_1;
                    std::stringstream s_str3(sd[4]);
                    s_str3 >> total_min_usage_2;
                    std::stringstream s_str4(sd[5]);
                    s_str4 >> total_max_usage_2;

                    total_real_usage_1 = total_max_usage_1 - total_min_usage_1;
                    total_real_usage_2 = total_max_usage_2 - total_min_usage_2;

                    std::stringstream s_str5(sd[2]);
                    s_str5 >> total_min_deliv_1;
                    std::stringstream s_str6(sd[3]);
                    s_str6 >> total_max_deliv_1;
                    std::stringstream s_str7(sd[6]);
                    s_str7 >> total_min_deliv_2;
                    std::stringstream s_str8(sd[7]);
                    s_str8 >> total_max_deliv_2;

                    total_real_deliv_1 = total_max_deliv_1 - total_min_deliv_1;
                    total_real_deliv_2 = total_max_deliv_2 - total_min_deliv_2;

                    if ((total_real_deliv_1 != 0) || (total_real_deliv_2 != 0))
                        bHaveDeliverd = true;

                    root["result"][ii]["d"] = szDateEnd;

                    std::string szValue;

                    sprintf(szTmp, "%llu", total_real_usage_1);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v"] = szTmp;
                    sprintf(szTmp, "%llu", total_real_usage_2);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v2"] = szTmp;

                    sprintf(szTmp, "%llu", total_real_deliv_1);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["r1"] = szTmp;
                    sprintf(szTmp, "%llu", total_real_deliv_2);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["r2"] = szTmp;

                    ii++;
                }
                if (bHaveDeliverd)
                {
                    root["delivered"] = true;
                }
            }
            else if (dType == pTypeAirQuality)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value), AVG(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["co2_min"] = result[0][0];
                    root["result"][ii]["co2_max"] = result[0][1];
                    root["result"][ii]["co2_avg"] = result[0][2];
                    ii++;
                }
            }
            else if (
                ((dType == pTypeGeneral) && ((dSubType == sTypeSoilMoisture) || (dSubType == sTypeLeafWetness))) ||
                ((dType == pTypeRFXSensor) && ((dSubType == sTypeRFXSensorAD) || (dSubType == sTypeRFXSensorVolt)))
                )
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["v_min"] = result[0][0];
                    root["result"][ii]["v_max"] = result[0][1];
                    ii++;
                }
            }
            else if (
                ((dType == pTypeGeneral) && (dSubType == sTypeVisibility)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeDistance)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeCurrent)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypePressure)) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel))
                )
            {
                float vdiv = 10.0f;
                if (
                    ((dType == pTypeGeneral) && (dSubType == sTypeVoltage)) ||
                    ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                    )
                {
                    vdiv = 1000.0f;
                }

                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    float fValue1 = float(atof(result[0][0].c_str())) / vdiv;
                    float fValue2 = float(atof(result[0][1].c_str())) / vdiv;
                    if (metertype == 1)
                    {
                        fValue1 *= 0.6214f;
                        fValue2 *= 0.6214f;
                    }

                    if ((dType == pTypeGeneral) && (dSubType == sTypeVoltage))
                        sprintf(szTmp, "%.3f", fValue1);
                    else if ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                        sprintf(szTmp, "%.3f", fValue1);
                    else
                        sprintf(szTmp, "%.1f", fValue1);
                    root["result"][ii]["v_min"] = szTmp;
                    if ((dType == pTypeGeneral) && (dSubType == sTypeVoltage))
                        sprintf(szTmp, "%.3f", fValue2);
                    else if ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
                        sprintf(szTmp, "%.3f", fValue2);
                    else
                        sprintf(szTmp, "%.1f", fValue2);
                    root["result"][ii]["v_max"] = szTmp;
                    ii++;
                }
            }
            else if (dType == pTypeLux)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value), AVG(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["lux_min"] = result[0][0];
                    root["result"][ii]["lux_max"] = result[0][1];
                    root["result"][ii]["lux_avg"] = result[0][2];
                    ii++;
                }
            }
            else if (dType == pTypeWEIGHT)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    sprintf(szTmp, "%.1f", m_sql.m_weightscale* atof(result[0][0].c_str()) / 10.0f);
                    root["result"][ii]["v_min"] = szTmp;
                    sprintf(szTmp, "%.1f", m_sql.m_weightscale * atof(result[0][1].c_str()) / 10.0f);
                    root["result"][ii]["v_max"] = szTmp;
                    ii++;
                }
            }
            else if (dType == pTypeUsage)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID=%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["u_min"] = atof(result[0][0].c_str()) / 10.0f;
                    root["result"][ii]["u_max"] = atof(result[0][1].c_str()) / 10.0f;
                    ii++;
                }
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd);
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];
                    unsigned long long total_min, total_max, total_real;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max;
                    total_real = total_max - total_min;
                    sprintf(szTmp, "%llu", total_real);

                    root["result"][ii]["d"] = szDateEnd;

                    std::string szValue = szTmp;
                    switch (metertype)
                    {
                    case MTYPE_ENERGY:
                    case MTYPE_ENERGY_GENERATED:
                    {
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                        root["result"][ii]["v"] = szTmp;

                        std::vector<std::string> mresults;
                        StringSplit(sValue, ";", mresults);
                        if (mresults.size() == 2)
                        {
                            sValue = mresults[1];
                        }
                        if (dType == pTypeENERGY)
                            sprintf(szTmp, "%.3f", AddjValue + (((atof(sValue.c_str())*100.0f) - atof(szValue.c_str())) / EnergyDivider));
                        else
                            sprintf(szTmp, "%.3f", AddjValue + ((atof(sValue.c_str()) - atof(szValue.c_str())) / EnergyDivider));
                        root["result"][ii]["c"] = szTmp;
                    }
                    break;
                    case MTYPE_GAS:
                        sprintf(szTmp, "%.2f", atof(szValue.c_str()) / GasDivider);
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.2f", AddjValue + ((atof(sValue.c_str()) - atof(szValue.c_str())) / GasDivider));
                        root["result"][ii]["c"] = szTmp;
                        break;
                    case MTYPE_WATER:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.3f", AddjValue + ((atof(sValue.c_str()) - atof(szValue.c_str())) / WaterDivider));
                        root["result"][ii]["c"] = szTmp;
                        break;
                    case MTYPE_COUNTER:
                        sprintf(szTmp, "%.0f", atof(szValue.c_str()));
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.0f", AddjValue + ((atof(sValue.c_str()) - atof(szValue.c_str()))));
                        root["result"][ii]["c"] = szTmp;
                        break;
                    }
                    ii++;
                }
            }
        }
        else if (sensor == "wind") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            int ii = 0;

            result = m_sql.safe_query(
                "SELECT Direction, Speed_Min, Speed_Max, Gust_Min,"
                " Gust_Max, Date "
                "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                " AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStart, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[5].substr(0, 16);
                    root["result"][ii]["di"] = sd[0];

                    int intSpeed = atoi(sd[2].c_str());
                    int intGust = atoi(sd[4].c_str());
                    if (m_sql.m_windunit != WINDUNIT_Beaufort)
                    {
                        sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                        root["result"][ii]["gu"] = szTmp;
                    }
                    else
                    {
                        float windspeedms = float(intSpeed)*0.1f;
                        float windgustms = float(intGust)*0.1f;
                        sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                        root["result"][ii]["gu"] = szTmp;
                    }
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT AVG(Direction), MIN(Speed), MAX(Speed),"
                " MIN(Gust), MAX(Gust) "
                "FROM Wind WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q') ORDER BY Date ASC",
                idx, szDateEnd);
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["di"] = sd[0];

                int intSpeed = atoi(sd[2].c_str());
                int intGust = atoi(sd[4].c_str());
                if (m_sql.m_windunit != WINDUNIT_Beaufort)
                {
                    sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                    root["result"][ii]["sp"] = szTmp;
                    sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                    root["result"][ii]["gu"] = szTmp;
                }
                else
                {
                    float windspeedms = float(intSpeed)*0.1f;
                    float windgustms = float(intGust)*0.1f;
                    sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                    root["result"][ii]["sp"] = szTmp;
                    sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                    root["result"][ii]["gu"] = szTmp;
                }
                ii++;
            }
            //Previous Year
            result = m_sql.safe_query(
                "SELECT Direction, Speed_Min, Speed_Max, Gust_Min,"
                " Gust_Max, Date "
                "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                " AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStartPrev, szDateEndPrev);
            if (result.size() > 0)
            {
                iPrev = 0;
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["resultprev"][iPrev]["d"] = sd[5].substr(0, 16);
                    root["resultprev"][iPrev]["di"] = sd[0];

                    int intSpeed = atoi(sd[2].c_str());
                    int intGust = atoi(sd[4].c_str());
                    if (m_sql.m_windunit != WINDUNIT_Beaufort)
                    {
                        sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                        root["resultprev"][iPrev]["sp"] = szTmp;
                        sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                        root["resultprev"][iPrev]["gu"] = szTmp;
                    }
                    else
                    {
                        float windspeedms = float(intSpeed)*0.1f;
                        float windgustms = float(intGust)*0.1f;
                        sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                        root["resultprev"][iPrev]["sp"] = szTmp;
                        sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                        root["resultprev"][iPrev]["gu"] = szTmp;
                    }
                    iPrev++;
                }
            }
        }
    }//month or year
    else if ((srange.substr(0, 1) == "2") && (srange.substr(10, 1) == "T") && (srange.substr(11, 1) == "2")) // custom range 2013-01-01T2013-12-31
    {
        std::string szDateStart = srange.substr(0, 10);
        std::string szDateEnd = srange.substr(11, 10);
        std::string sgraphtype = request::findValue(&req, "graphtype");
        std::string sgraphTemp = request::findValue(&req, "graphTemp");
        std::string sgraphChill = request::findValue(&req, "graphChill");
        std::string sgraphHum = request::findValue(&req, "graphHum");
        std::string sgraphBaro = request::findValue(&req, "graphBaro");
        std::string sgraphDew = request::findValue(&req, "graphDew");
        std::string sgraphSet = request::findValue(&req, "graphSet");

        if (sensor == "temp") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            bool sendTemp = false;
            bool sendChill = false;
            bool sendHum = false;
            bool sendBaro = false;
            bool sendDew = false;
            bool sendSet = false;

            if ((sgraphTemp == "true") &&
                ((dType == pTypeRego6XXTemp) || (dType == pTypeTEMP) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO) || (dType == pTypeTEMP_BARO) || (dType == pTypeWIND) || (dType == pTypeThermostat1) || (dType == pTypeRadiator1) ||
                ((dType == pTypeUV) && (dSubType == sTypeUV3)) ||
                    ((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                    ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp)) ||
                    ((dType == pTypeRFXSensor) && (dSubType == sTypeRFXSensorTemp)) ||
                    ((dType == pTypeThermostat) && (dSubType == sTypeThermSetpoint)) ||
                    (dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater)
                    )
                )
            {
                sendTemp = true;
            }
            if ((sgraphSet == "true") &&
                ((dType == pTypeEvohomeZone) || (dType == pTypeEvohomeWater))) //FIXME cheat for water setpoint is just on or off
            {
                sendSet = true;
            }
            if ((sgraphChill == "true") &&
                (((dType == pTypeWIND) && (dSubType == sTypeWIND4)) ||
                ((dType == pTypeWIND) && (dSubType == sTypeWINDNoTemp)))
                )
            {
                sendChill = true;
            }
            if ((sgraphHum == "true") &&
                ((dType == pTypeHUM) || (dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO))
                )
            {
                sendHum = true;
            }
            if ((sgraphBaro == "true") && (
                (dType == pTypeTEMP_HUM_BARO) ||
                (dType == pTypeTEMP_BARO) ||
                ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                ))
            {
                sendBaro = true;
            }
            if ((sgraphDew == "true") && ((dType == pTypeTEMP_HUM) || (dType == pTypeTEMP_HUM_BARO)))
            {
                sendDew = true;
            }

            if (sgraphtype == "1")
            {
                // Need to get all values of the end date so 23:59:59 is appended to the date string
                result = m_sql.safe_query(
                    "SELECT Temperature, Chill, Humidity, Barometer,"
                    " Date, DewPoint, SetPoint "
                    "FROM Temperature WHERE (DeviceRowID==%" PRIu64 ""
                    " AND Date>='%q' AND Date<='%q 23:59:59') ORDER BY Date ASC",
                    idx, szDateStart.c_str(), szDateEnd.c_str());
                int ii = 0;
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[4];//.substr(0,16);
                        if (sendTemp)
                        {
                            double te = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                            double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                            root["result"][ii]["te"] = te;
                            root["result"][ii]["tm"] = tm;
                        }
                        if (sendChill)
                        {
                            double ch = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                            double cm = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                            root["result"][ii]["ch"] = ch;
                            root["result"][ii]["cm"] = cm;
                        }
                        if (sendHum)
                        {
                            root["result"][ii]["hu"] = sd[2];
                        }
                        if (sendBaro)
                        {
                            if (dType == pTypeTEMP_HUM_BARO)
                            {
                                if (dSubType == sTypeTHBFloat)
                                {
                                    sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                                    root["result"][ii]["ba"] = szTmp;
                                }
                                else
                                    root["result"][ii]["ba"] = sd[3];
                            }
                            else if (dType == pTypeTEMP_BARO)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                            else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                            {
                                sprintf(szTmp, "%.1f", atof(sd[3].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                        }
                        if (sendDew)
                        {
                            double dp = ConvertTemperature(atof(sd[5].c_str()), tempsign);
                            root["result"][ii]["dp"] = dp;
                        }
                        if (sendSet)
                        {
                            double se = ConvertTemperature(atof(sd[6].c_str()), tempsign);
                            root["result"][ii]["se"] = se;
                        }
                        ii++;
                    }
                }
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT Temp_Min, Temp_Max, Chill_Min, Chill_Max,"
                    " Humidity, Barometer, Date, DewPoint, Temp_Avg,"
                    " SetPoint_Min, SetPoint_Max, SetPoint_Avg "
                    "FROM Temperature_Calendar "
                    "WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                    " AND Date<='%q') ORDER BY Date ASC",
                    idx, szDateStart.c_str(), szDateEnd.c_str());
                int ii = 0;
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[6].substr(0, 16);
                        if (sendTemp)
                        {
                            double te = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                            double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                            double ta = ConvertTemperature(atof(sd[8].c_str()), tempsign);

                            root["result"][ii]["te"] = te;
                            root["result"][ii]["tm"] = tm;
                            root["result"][ii]["ta"] = ta;
                        }
                        if (sendChill)
                        {
                            double ch = ConvertTemperature(atof(sd[3].c_str()), tempsign);
                            double cm = ConvertTemperature(atof(sd[2].c_str()), tempsign);

                            root["result"][ii]["ch"] = ch;
                            root["result"][ii]["cm"] = cm;
                        }
                        if (sendHum)
                        {
                            root["result"][ii]["hu"] = sd[4];
                        }
                        if (sendBaro)
                        {
                            if (dType == pTypeTEMP_HUM_BARO)
                            {
                                if (dSubType == sTypeTHBFloat)
                                {
                                    sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                    root["result"][ii]["ba"] = szTmp;
                                }
                                else
                                    root["result"][ii]["ba"] = sd[5];
                            }
                            else if (dType == pTypeTEMP_BARO)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                            else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                            {
                                sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                        }
                        if (sendDew)
                        {
                            double dp = ConvertTemperature(atof(sd[7].c_str()), tempsign);
                            root["result"][ii]["dp"] = dp;
                        }
                        if (sendSet)
                        {
                            double sm = ConvertTemperature(atof(sd[9].c_str()), tempsign);
                            double sx = ConvertTemperature(atof(sd[10].c_str()), tempsign);
                            double se = ConvertTemperature(atof(sd[11].c_str()), tempsign);
                            root["result"][ii]["sm"] = sm;
                            root["result"][ii]["se"] = se;
                            root["result"][ii]["sx"] = sx;
                            char szTmp[1024];
                            snprintf(szTmp, sizeof(szTmp), "%.1f %.1f %.1f", sm, se, sx);
                            _log.Log(LOG_STATUS, "%s", szTmp);

                        }
                        ii++;
                    }
                }

                //add today (have to calculate it)
                result = m_sql.safe_query(
                    "SELECT MIN(Temperature), MAX(Temperature),"
                    " MIN(Chill), MAX(Chill), AVG(Humidity),"
                    " AVG(Barometer), MIN(DewPoint), AVG(Temperature),"
                    " MIN(SetPoint), MAX(SetPoint), AVG(SetPoint) "
                    "FROM Temperature WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd.c_str());
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];

                    root["result"][ii]["d"] = szDateEnd;
                    if (sendTemp)
                    {
                        double te = ConvertTemperature(atof(sd[1].c_str()), tempsign);
                        double tm = ConvertTemperature(atof(sd[0].c_str()), tempsign);
                        double ta = ConvertTemperature(atof(sd[7].c_str()), tempsign);

                        root["result"][ii]["te"] = te;
                        root["result"][ii]["tm"] = tm;
                        root["result"][ii]["ta"] = ta;
                    }
                    if (sendChill)
                    {
                        double ch = ConvertTemperature(atof(sd[3].c_str()), tempsign);
                        double cm = ConvertTemperature(atof(sd[2].c_str()), tempsign);
                        root["result"][ii]["ch"] = ch;
                        root["result"][ii]["cm"] = cm;
                    }
                    if (sendHum)
                    {
                        root["result"][ii]["hu"] = sd[4];
                    }
                    if (sendBaro)
                    {
                        if (dType == pTypeTEMP_HUM_BARO)
                        {
                            if (dSubType == sTypeTHBFloat)
                            {
                                sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                                root["result"][ii]["ba"] = szTmp;
                            }
                            else
                                root["result"][ii]["ba"] = sd[5];
                        }
                        else if (dType == pTypeTEMP_BARO)
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                        else if ((dType == pTypeGeneral) && (dSubType == sTypeBaro))
                        {
                            sprintf(szTmp, "%.1f", atof(sd[5].c_str()) / 10.0f);
                            root["result"][ii]["ba"] = szTmp;
                        }
                    }
                    if (sendDew)
                    {
                        double dp = ConvertTemperature(atof(sd[6].c_str()), tempsign);
                        root["result"][ii]["dp"] = dp;
                    }
                    if (sendSet)
                    {
                        double sm = ConvertTemperature(atof(sd[8].c_str()), tempsign);
                        double sx = ConvertTemperature(atof(sd[9].c_str()), tempsign);
                        double se = ConvertTemperature(atof(sd[10].c_str()), tempsign);

                        root["result"][ii]["sm"] = sm;
                        root["result"][ii]["se"] = se;
                        root["result"][ii]["sx"] = sx;
                    }
                    ii++;
                }
            }
        }
        else if (sensor == "uv") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query(
                "SELECT Level, Date FROM %s WHERE (DeviceRowID==%" PRIu64 ""
                " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStart.c_str(), szDateEnd.c_str());
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[1].substr(0, 16);
                    root["result"][ii]["uvi"] = sd[0];
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT MAX(Level) FROM UV WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                idx, szDateEnd.c_str());
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["uvi"] = sd[0];
                ii++;
            }
        }
        else if (sensor == "rain") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            result = m_sql.safe_query(
                "SELECT Total, Rate, Date FROM %s "
                "WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStart.c_str(), szDateEnd.c_str());
            int ii = 0;
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[2].substr(0, 16);
                    root["result"][ii]["mm"] = sd[0];
                    ii++;
                }
            }
            //add today (have to calculate it)
            if (dSubType != sTypeRAINWU)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Total), MAX(Total), MAX(Rate) FROM Rain WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd.c_str());
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT Total, Total, Rate FROM Rain WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q') ORDER BY ROWID DESC LIMIT 1",
                    idx, szDateEnd.c_str());
            }
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                float total_min = static_cast<float>(atof(sd[0].c_str()));
                float total_max = static_cast<float>(atof(sd[1].c_str()));
                int rate = atoi(sd[2].c_str());

                float total_real = 0;
                if (dSubType != sTypeRAINWU)
                {
                    total_real = total_max - total_min;
                }
                else
                {
                    total_real = total_max;
                }
                sprintf(szTmp, "%.1f", total_real);
                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["mm"] = szTmp;
                ii++;
            }
        }
        else if (sensor == "counter") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;
            root["ValueQuantity"] = options["ValueQuantity"];
            root["ValueUnits"] = options["ValueUnits"];

            float EnergyDivider = 1000.0f;
            float GasDivider = 100.0f;
            float WaterDivider = 100.0f;
            int tValue;
            if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
            {
                EnergyDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerGas", tValue))
            {
                GasDivider = float(tValue);
            }
            if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
            {
                WaterDivider = float(tValue);
            }
            if (dType == pTypeP1Gas)
                GasDivider = 1000.0f;
            else if ((dType == pTypeENERGY) || (dType == pTypePOWER))
                EnergyDivider *= 100.0f;

            int ii = 0;
            if (dType == pTypeP1Power)
            {
                result = m_sql.safe_query(
                    "SELECT Value1,Value2,Value5,Value6, Date "
                    "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                    " AND Date<='%q') ORDER BY Date ASC",
                    dbasetable.c_str(), idx, szDateStart.c_str(), szDateEnd.c_str());
                if (result.size() > 0)
                {
                    bool bHaveDeliverd = false;
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        root["result"][ii]["d"] = sd[4].substr(0, 16);

                        std::string szUsage1 = sd[0];
                        std::string szDeliv1 = sd[1];
                        std::string szUsage2 = sd[2];
                        std::string szDeliv2 = sd[3];

                        float fUsage = (float)(atof(szUsage1.c_str()) + atof(szUsage2.c_str()));
                        float fDeliv = (float)(atof(szDeliv1.c_str()) + atof(szDeliv2.c_str()));

                        if (fDeliv != 0)
                            bHaveDeliverd = true;
                        sprintf(szTmp, "%.3f", fUsage / EnergyDivider);
                        root["result"][ii]["v"] = szTmp;
                        sprintf(szTmp, "%.3f", fDeliv / EnergyDivider);
                        root["result"][ii]["v2"] = szTmp;
                        ii++;
                    }
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
            }
            else
            {
                result = m_sql.safe_query("SELECT Value, Date FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q' AND Date<='%q') ORDER BY Date ASC", dbasetable.c_str(), idx, szDateStart.c_str(), szDateEnd.c_str());
                if (result.size() > 0)
                {
                    std::vector<std::vector<std::string> >::const_iterator itt;
                    for (itt = result.begin(); itt != result.end(); ++itt)
                    {
                        std::vector<std::string> sd = *itt;

                        std::string szValue = sd[0];
                        switch (metertype)
                        {
                        case MTYPE_ENERGY:
                        case MTYPE_ENERGY_GENERATED:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                            szValue = szTmp;
                            break;
                        case MTYPE_GAS:
                            sprintf(szTmp, "%.2f", atof(szValue.c_str()) / GasDivider);
                            szValue = szTmp;
                            break;
                        case MTYPE_WATER:
                            sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                            szValue = szTmp;
                            break;
                        }
                        root["result"][ii]["d"] = sd[1].substr(0, 16);
                        root["result"][ii]["v"] = szValue;
                        ii++;
                    }
                }
            }
            //add today (have to calculate it)
            if (dType == pTypeP1Power)
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value1), MAX(Value1), MIN(Value2),"
                    " MAX(Value2),MIN(Value5), MAX(Value5),"
                    " MIN(Value6), MAX(Value6) "
                    "FROM MultiMeter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd.c_str());
                bool bHaveDeliverd = false;
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];
                    unsigned long long total_min_usage_1, total_min_usage_2, total_max_usage_1, total_max_usage_2, total_real_usage;
                    unsigned long long total_min_deliv_1, total_min_deliv_2, total_max_deliv_1, total_max_deliv_2, total_real_deliv;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min_usage_1;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max_usage_1;
                    std::stringstream s_str3(sd[4]);
                    s_str3 >> total_min_usage_2;
                    std::stringstream s_str4(sd[5]);
                    s_str4 >> total_max_usage_2;

                    total_real_usage = (total_max_usage_1 + total_max_usage_2) - (total_min_usage_1 + total_min_usage_2);

                    std::stringstream s_str5(sd[2]);
                    s_str5 >> total_min_deliv_1;
                    std::stringstream s_str6(sd[3]);
                    s_str6 >> total_max_deliv_1;
                    std::stringstream s_str7(sd[6]);
                    s_str7 >> total_min_deliv_2;
                    std::stringstream s_str8(sd[7]);
                    s_str8 >> total_max_deliv_2;

                    total_real_deliv = (total_max_deliv_1 + total_max_deliv_2) - (total_min_deliv_1 + total_min_deliv_2);

                    if (total_real_deliv != 0)
                        bHaveDeliverd = true;

                    root["result"][ii]["d"] = szDateEnd;

                    sprintf(szTmp, "%llu", total_real_usage);
                    std::string szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v"] = szTmp;
                    sprintf(szTmp, "%llu", total_real_deliv);
                    szValue = szTmp;
                    sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                    root["result"][ii]["v2"] = szTmp;
                    ii++;
                    if (bHaveDeliverd)
                    {
                        root["delivered"] = true;
                    }
                }
            }
            else
            {
                result = m_sql.safe_query(
                    "SELECT MIN(Value), MAX(Value) FROM Meter WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q')",
                    idx, szDateEnd.c_str());
                if (result.size() > 0)
                {
                    std::vector<std::string> sd = result[0];
                    unsigned long long total_min, total_max, total_real;

                    std::stringstream s_str1(sd[0]);
                    s_str1 >> total_min;
                    std::stringstream s_str2(sd[1]);
                    s_str2 >> total_max;
                    total_real = total_max - total_min;
                    sprintf(szTmp, "%llu", total_real);

                    std::string szValue = szTmp;
                    switch (metertype)
                    {
                    case MTYPE_ENERGY:
                    case MTYPE_ENERGY_GENERATED:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / EnergyDivider);
                        szValue = szTmp;
                        break;
                    case MTYPE_GAS:
                        sprintf(szTmp, "%.2f", atof(szValue.c_str()) / GasDivider);
                        szValue = szTmp;
                        break;
                    case MTYPE_WATER:
                        sprintf(szTmp, "%.3f", atof(szValue.c_str()) / WaterDivider);
                        szValue = szTmp;
                        break;
                    }

                    root["result"][ii]["d"] = szDateEnd;
                    root["result"][ii]["v"] = szValue;
                    ii++;
                }
            }
        }
        else if (sensor == "wind") {
            root["status"] = "OK";
            root["title"] = "Graph " + sensor + " " + srange;

            int ii = 0;

            result = m_sql.safe_query(
                "SELECT Direction, Speed_Min, Speed_Max, Gust_Min,"
                " Gust_Max, Date "
                "FROM %s WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q'"
                " AND Date<='%q') ORDER BY Date ASC",
                dbasetable.c_str(), idx, szDateStart.c_str(), szDateEnd.c_str());
            if (result.size() > 0)
            {
                std::vector<std::vector<std::string> >::const_iterator itt;
                for (itt = result.begin(); itt != result.end(); ++itt)
                {
                    std::vector<std::string> sd = *itt;

                    root["result"][ii]["d"] = sd[5].substr(0, 16);
                    root["result"][ii]["di"] = sd[0];

                    int intSpeed = atoi(sd[2].c_str());
                    int intGust = atoi(sd[4].c_str());
                    if (m_sql.m_windunit != WINDUNIT_Beaufort)
                    {
                        sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                        root["result"][ii]["gu"] = szTmp;
                    }
                    else
                    {
                        float windspeedms = float(intSpeed)*0.1f;
                        float windgustms = float(intGust)*0.1f;
                        sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                        root["result"][ii]["sp"] = szTmp;
                        sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                        root["result"][ii]["gu"] = szTmp;
                    }
                    ii++;
                }
            }
            //add today (have to calculate it)
            result = m_sql.safe_query(
                "SELECT AVG(Direction), MIN(Speed), MAX(Speed), MIN(Gust), MAX(Gust) FROM Wind WHERE (DeviceRowID==%" PRIu64 " AND Date>='%q') ORDER BY Date ASC",
                idx, szDateEnd.c_str());
            if (result.size() > 0)
            {
                std::vector<std::string> sd = result[0];

                root["result"][ii]["d"] = szDateEnd;
                root["result"][ii]["di"] = sd[0];

                int intSpeed = atoi(sd[2].c_str());
                int intGust = atoi(sd[4].c_str());
                if (m_sql.m_windunit != WINDUNIT_Beaufort)
                {
                    sprintf(szTmp, "%.1f", float(intSpeed) * m_sql.m_windscale);
                    root["result"][ii]["sp"] = szTmp;
                    sprintf(szTmp, "%.1f", float(intGust) * m_sql.m_windscale);
                    root["result"][ii]["gu"] = szTmp;
                }
                else
                {
                    float windspeedms = float(intSpeed)*0.1f;
                    float windgustms = float(intGust)*0.1f;
                    sprintf(szTmp, "%d", MStoBeaufort(windspeedms));
                    root["result"][ii]["sp"] = szTmp;
                    sprintf(szTmp, "%d", MStoBeaufort(windgustms));
                    root["result"][ii]["gu"] = szTmp;
                }
                ii++;
            }
        }
    }//custom range
}