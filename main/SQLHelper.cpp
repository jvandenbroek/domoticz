#include "stdafx.h"
#include "SQLHelper.h"
#include <iostream>     /* standard I/O functions                         */
#include <iomanip>
#include "RFXtrx.h"
#include "RFXNames.h"
#include "localtime_r.h"
#include "Logger.h"
#include "mainworker.h"
#ifdef WITH_EXTERNAL_SQLITE
#include <sqlite3.h>
#else
#include "../sqlite/sqlite3.h"
#endif
#include "../hardware/hardwaretypes.h"
#include "../smtpclient/SMTPClient.h"
#include "WebServerHelper.h"
#include "../webserver/Base64.h"
#include "unzip.h"
#include "../notifications/NotificationHelper.h"
#include "IFTTT.h"
#ifdef ENABLE_PYTHON
#include "../hardware/plugins/Plugins.h"
#endif

#ifndef WIN32
#include <pwd.h>
#else
#include "../msbuild/WindowsHelper.h"
#endif
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

extern http::server::CWebServerHelper m_webservers;
extern std::string szWWWFolder;
extern std::string szUserDataFolder;

CSQLHelper::CSQLHelper(void)
{
	m_LastSwitchRowID=0;
	m_dbase=NULL;
	m_stoprequested=false;
	m_sensortimeoutcounter=0;
	m_bAcceptNewHardware=true;
	m_bAllowWidgetOrdering=true;
	m_ActiveTimerPlan=0;
	m_windunit=WINDUNIT_MS;
	m_tempunit=TEMPUNIT_C;
	m_weightunit=WEIGHTUNIT_KG;
	SetUnitsAndScale();
	m_bAcceptHardwareTimerActive=false;
	m_iAcceptHardwareTimerCounter=0;
	m_bEnableEventSystem = true;
	m_bDisableDzVentsSystem = false;
	m_ShortLogInterval = 5;
	m_bPreviousAcceptNewHardware = false;

	SetDatabaseName("domoticz.db");
}

CSQLHelper::~CSQLHelper(void)
{
	if (m_background_task_thread)
	{
		m_stoprequested = true;
		m_background_task_thread->join();
	}
	if (m_dbase!=NULL)
	{
		OptimizeDatabase(m_dbase);
		sqlite3_close(m_dbase);
		m_dbase=NULL;
	}
}


bool CSQLHelper::StartThread()
{
	m_background_task_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CSQLHelper::Do_Work, this)));

	return (m_background_task_thread!=NULL);
}

bool CSQLHelper::SwitchLightFromTasker(const std::string &idx, const std::string &switchcmd, const std::string &level, const std::string &color)
{
	_tColor ocolor(color);
	return SwitchLightFromTasker(strtoui64(idx), switchcmd, atoi(level.c_str()), ocolor);
}

bool CSQLHelper::SwitchLightFromTasker(uint64_t idx, const std::string &switchcmd, int level, _tColor color)
{
	//Get Device details
	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT HardwareID, DeviceID,Unit,Type,SubType,SwitchType,AddjValue2,nValue,sValue,Name,Options FROM DeviceStatus WHERE (ID == %" PRIu64 ")", idx);
	if (result.size()<1)
		return false;

	std::vector<std::string> sd = result[0];
	return m_mainworker.SwitchLightInt(sd, switchcmd, level, color, false);
}

void CSQLHelper::Do_Work()
{
	std::vector<_tTaskItem> _items2do;

	while (!m_stoprequested)
	{
		sleep_milliseconds(static_cast<const long>(1000.0f / timer_resolution_hz));

		if (m_bAcceptHardwareTimerActive)
		{
			m_iAcceptHardwareTimerCounter -= static_cast<float>(1. / timer_resolution_hz);
			if (m_iAcceptHardwareTimerCounter <= (1.0f / timer_resolution_hz / 2))
			{
				m_bAcceptHardwareTimerActive = false;
				m_bAcceptNewHardware = m_bPreviousAcceptNewHardware;
				UpdatePreferencesVar("AcceptNewHardware", (m_bAcceptNewHardware == true) ? 1 : 0);
				if (!m_bAcceptNewHardware)
				{
					_log.Log(LOG_STATUS, "Receiving of new sensors disabled!...");
				}
			}
		}

		{ // additional scope for lock (accessing size should be within lock too)
			boost::lock_guard<boost::mutex> l(m_background_task_mutex);
			if (m_background_task_queue.size() > 0)
			{
				_items2do.clear();

				std::vector<_tTaskItem>::iterator itt = m_background_task_queue.begin();
				while (itt != m_background_task_queue.end())
				{
					if (itt->_DelayTime)
					{
						struct timeval tvDiff, DelayTimeEnd;
						getclock(&DelayTimeEnd);
						if (timeval_subtract(&tvDiff, &DelayTimeEnd, &itt->_DelayTimeBegin)) {
							tvDiff.tv_sec = 0;
							tvDiff.tv_usec = 0;
						}
						float diff = ((tvDiff.tv_usec / 1000000.0f) + tvDiff.tv_sec);
						if ((itt->_DelayTime) <= diff)
						{
							_items2do.push_back(*itt);
							itt = m_background_task_queue.erase(itt);
						}
						else
							++itt;
					}
					else
					{
						_items2do.push_back(*itt);
						itt = m_background_task_queue.erase(itt);
					}
				}
			}
		}

		if (_items2do.size() < 1) {
			continue;
		}

		std::vector<_tTaskItem>::iterator itt = _items2do.begin();
		while (itt != _items2do.end())
		{
			if (_log.isTraceEnabled())
				_log.Log(LOG_TRACE, "SQLH: Do Task ItemType:%d Cmd:%s Value:%s ", itt->_ItemType, itt->_command.c_str(), itt->_sValue.c_str());

			if (itt->_ItemType == TITEM_SWITCHCMD)
			{
				if (itt->_switchtype == STYPE_Motion)
				{
					std::string devname = "";
					switch (itt->_devType)
					{
					case pTypeLighting1:
					case pTypeLighting2:
					case pTypeLighting3:
					case pTypeLighting5:
					case pTypeLighting6:
					case pTypeColorSwitch:
					case pTypeGeneralSwitch:
					case pTypeHomeConfort:
						SwitchLightFromTasker(itt->_idx, "Off", 0, NoColor);
						break;
					case pTypeSecurity1:
						switch (itt->_subType)
						{
						case sTypeSecX10M:
							SwitchLightFromTasker(itt->_idx, "No Motion", 0, NoColor);
							break;
						default:
							//just update internally
							UpdateValueInt(itt->_HardwareID, itt->_ID.c_str(), itt->_unit, itt->_devType, itt->_subType, itt->_signallevel, itt->_batterylevel, itt->_nValue, itt->_sValue.c_str(), devname, true);
							break;
						}
						break;
					case pTypeLighting4:
						//only update internally
						UpdateValueInt(itt->_HardwareID, itt->_ID.c_str(), itt->_unit, itt->_devType, itt->_subType, itt->_signallevel, itt->_batterylevel, itt->_nValue, itt->_sValue.c_str(), devname, true);
						break;
					default:
						//unknown hardware type, sensor will only be updated internally
						UpdateValueInt(itt->_HardwareID, itt->_ID.c_str(), itt->_unit, itt->_devType, itt->_subType, itt->_signallevel, itt->_batterylevel, itt->_nValue, itt->_sValue.c_str(), devname, true);
						break;
					}
				}
				else
				{
					if (itt->_devType == pTypeLighting4)
					{
						//only update internally
						std::string devname = "";
						UpdateValueInt(itt->_HardwareID, itt->_ID.c_str(), itt->_unit, itt->_devType, itt->_subType, itt->_signallevel, itt->_batterylevel, itt->_nValue, itt->_sValue.c_str(), devname, true);
					}
					else
						SwitchLightFromTasker(itt->_idx, "Off", 0, NoColor);
				}
			}
			else if (itt->_ItemType == TITEM_EXECUTE_SCRIPT)
			{
				//start script
				_log.Log(LOG_STATUS, "Executing script: %s", itt->_ID.c_str());
#ifdef WIN32
				ShellExecute(NULL, "open", itt->_ID.c_str(), itt->_sValue.c_str(), NULL, SW_SHOWNORMAL);
#else
				std::string lscript = itt->_ID + " " + itt->_sValue;
				int ret = system(lscript.c_str());
				if (ret != 0)
				{
					_log.Log(LOG_ERROR, "Error executing script command (%s). returned: %d", itt->_ID.c_str(), ret);
				}
#endif
			}
			else if (itt->_ItemType == TITEM_EMAIL_CAMERA_SNAPSHOT)
			{
				m_mainworker.m_cameras.EmailCameraSnapshot(itt->_ID, itt->_sValue);
			}
			else if (itt->_ItemType == TITEM_GETURL)
			{
				std::string response;
				std::vector<std::string> headerData, extraHeaders;
				std::string postData = itt->_command;
				std::string callback = itt->_ID;

				if (!itt->_relatedEvent.empty())
					StringSplit(itt->_relatedEvent, "!#", extraHeaders);

				HTTPClient::_eHTTPmethod method = static_cast<HTTPClient::_eHTTPmethod>(itt->_switchtype);

				bool ret;
				if (method == HTTPClient::HTTP_METHOD_GET)
				{
					ret = HTTPClient::GET(itt->_sValue, extraHeaders, response, headerData);
				}
				else if (method == HTTPClient::HTTP_METHOD_POST)
				{
					ret = HTTPClient::POST(itt->_sValue, postData, extraHeaders, response, headerData);
				}

				if (m_bEnableEventSystem && !callback.empty())
				{
					if (ret)
						headerData.push_back("200");
					m_mainworker.m_eventsystem.TriggerURL(response, headerData, callback);
				}

				if (!ret)
				{
					_log.Log(LOG_ERROR, "Error opening url: %s", itt->_sValue.c_str());
				}
			}
			else if ((itt->_ItemType == TITEM_SEND_EMAIL) || (itt->_ItemType == TITEM_SEND_EMAIL_TO))
			{
				int nValue;
				if (GetPreferencesVar("EmailEnabled", nValue))
				{
					if (nValue)
					{
						std::string sValue;
						if (GetPreferencesVar("EmailServer", sValue))
						{
							if (sValue != "")
							{
								std::string EmailFrom;
								std::string EmailTo;
								std::string EmailServer = sValue;
								int EmailPort = 25;
								std::string EmailUsername;
								std::string EmailPassword;
								GetPreferencesVar("EmailFrom", EmailFrom);
								if (itt->_ItemType != TITEM_SEND_EMAIL_TO)
								{
									GetPreferencesVar("EmailTo", EmailTo);
								}
								else
								{
									EmailTo = itt->_command;
								}
								GetPreferencesVar("EmailUsername", EmailUsername);
								GetPreferencesVar("EmailPassword", EmailPassword);

								GetPreferencesVar("EmailPort", EmailPort);

								SMTPClient sclient;
								sclient.SetFrom(CURLEncode::URLDecode(EmailFrom.c_str()));
								sclient.SetTo(CURLEncode::URLDecode(EmailTo.c_str()));
								sclient.SetCredentials(base64_decode(EmailUsername), base64_decode(EmailPassword));
								sclient.SetServer(CURLEncode::URLDecode(EmailServer.c_str()), EmailPort);
								sclient.SetSubject(CURLEncode::URLDecode(itt->_ID));
								sclient.SetHTMLBody(itt->_sValue);
								bool bRet = sclient.SendEmail();

								if (bRet)
									_log.Log(LOG_STATUS, "Notification sent (Email)");
								else
									_log.Log(LOG_ERROR, "Notification failed (Email)");
							}
						}
					}
				}
			}
			else if (itt->_ItemType == TITEM_SEND_SMS)
			{
				m_notifications.SendMessage(0, std::string(""), "clickatell", itt->_ID, itt->_ID, std::string(""), 1, std::string(""), false);
			}
			else if (itt->_ItemType == TITEM_SWITCHCMD_EVENT)
			{
				SwitchLightFromTasker(itt->_idx, itt->_command.c_str(), itt->_level, itt->_Color);
			}

			else if (itt->_ItemType == TITEM_SWITCHCMD_SCENE)
			{
				m_mainworker.SwitchScene(itt->_idx, itt->_command.c_str());
			}
			else if (itt->_ItemType == TITEM_SET_VARIABLE)
			{
				std::vector<std::vector<std::string> > result;
				std::stringstream s_str;
				result = safe_query("SELECT Name, ValueType FROM UserVariables WHERE (ID == %" PRIu64 ")", itt->_idx);
				if (result.size() > 0)
				{
					std::vector<std::string> sd = result[0];
					s_str.clear();
					s_str.str("");
					s_str << itt->_idx;
					std::string updateResult = UpdateUserVariable(s_str.str(), sd[0], sd[1], itt->_sValue, (itt->_nValue == 0) ? false : true);
					if (updateResult != "OK") {
						_log.Log(LOG_ERROR, "Error updating variable %s: %s", sd[0].c_str(), updateResult.c_str());
					}
					else
					{
						_log.Log(LOG_STATUS, "Set UserVariable %s = %s", sd[0].c_str(), CURLEncode::URLDecode(itt->_sValue).c_str());
					}
				}
				else
				{
					_log.Log(LOG_ERROR, "Variable not found!");
				}
			}
			else if (itt->_ItemType == TITEM_SET_SETPOINT)
			{
				std::stringstream sstr;
				sstr << itt->_idx;
				std::string idx = sstr.str();
				float fValue = (float)atof(itt->_sValue.c_str());
				m_mainworker.SetSetPoint(idx, fValue, itt->_command, itt->_sUntil);
			}
			else if (itt->_ItemType == TITEM_SEND_NOTIFICATION)
			{
				std::vector<std::string> splitresults;
				StringSplit(itt->_command, "!#", splitresults);
				if (splitresults.size() == 5) {
					std::string subsystem = splitresults[4];
					if (subsystem.empty() || subsystem == " ") {
						subsystem = NOTIFYALL;
					}
					m_notifications.SendMessageEx(0, std::string(""), subsystem, splitresults[0], splitresults[1], splitresults[2], static_cast<int>(itt->_idx), splitresults[3], true);
				}
			}
			else if (itt->_ItemType == TITEM_SEND_IFTTT_TRIGGER)
			{
				std::vector<std::string> splitresults;
				StringSplit(itt->_command, "!#", splitresults);
				if (!splitresults.empty())
				{
					std::string sValue1, sValue2, sValue3;
					if (splitresults.size() > 0)
						sValue1 = splitresults[0];
					if (splitresults.size() > 1)
						sValue2 = splitresults[1];
					if (splitresults.size() > 2)
						sValue3 = splitresults[2];
					IFTTT::Send_IFTTT_Trigger(itt->_ID, sValue1, sValue2, sValue3);
				}
			}
			else if (itt->_ItemType == TITEM_UPDATEDEVICE)
			{
				m_mainworker.m_eventsystem.UpdateDevice(itt->_idx, itt->_nValue, itt->_sValue, itt->_HardwareID, (itt->_switchtype ? true : false));
			}

			++itt;
		}
		_items2do.clear();
	}
}

void CSQLHelper::SetDatabaseName(const std::string &DBName)
{
	m_dbase_name=DBName;
}

bool CSQLHelper::DoesColumnExistsInTable(const std::string &columnname, const std::string &tablename)
{
	if (!m_dbase)
	{
		_log.Log(LOG_ERROR, "Database not open!!...Check your user rights!..");
		return false;
	}
	bool columnExists = false;

	sqlite3_stmt *statement;
	std::string szQuery = "SELECT " + columnname + " FROM " + tablename;
	if (sqlite3_prepare_v2(m_dbase, szQuery.c_str(), -1, &statement, NULL) == SQLITE_OK)
	{
		columnExists = true;
		sqlite3_finalize(statement);
	}
	return columnExists;
}

void CSQLHelper::safe_exec_no_return(const char *fmt, ...)
{
	if (!m_dbase)
		return;

	va_list args;
	va_start(args, fmt);
	char *zQuery = sqlite3_vmprintf(fmt, args);
	va_end(args);
	if (!zQuery)
		return;
	sqlite3_exec(m_dbase, zQuery, NULL, NULL, NULL);
	sqlite3_free(zQuery);
}

std::vector<std::vector<std::string> > CSQLHelper::safe_query(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char *zQuery = sqlite3_vmprintf(fmt, args);
	va_end(args);
	if (!zQuery)
	{
		_log.Log(LOG_ERROR, "SQL: Out of memory, or invalid printf!....");
		std::vector<std::vector<std::string> > results;
		return results;
	}
	std::vector<std::vector<std::string> > results = query(zQuery);
	sqlite3_free(zQuery);
	return results;
}

std::vector<std::vector<std::string> > CSQLHelper::query(const std::string &szQuery)
{
	if (!m_dbase)
	{
		_log.Log(LOG_ERROR,"Database not open!!...Check your user rights!..");
		std::vector<std::vector<std::string> > results;
		return results;
	}
	boost::lock_guard<boost::mutex> l(m_sqlQueryMutex);

	sqlite3_stmt *statement;
	std::vector<std::vector<std::string> > results;

	if(sqlite3_prepare_v2(m_dbase, szQuery.c_str(), -1, &statement, 0) == SQLITE_OK)
	{
		int cols = sqlite3_column_count(statement);
		while(true)
		{
			int result = sqlite3_step(statement);
			if(result == SQLITE_ROW)
			{
				std::vector<std::string> values;
				for(int col = 0; col < cols; col++)
				{
					char* value = (char*)sqlite3_column_text(statement, col);
					if ((value == 0)&&(col==0))
						break;
					else if (value == 0)
						values.push_back(std::string("")); //insert empty string
					else
						values.push_back(value);
				}
				if (values.size()>0)
					results.push_back(values);
			}
			else
			{
				break;
			}
		}
		sqlite3_finalize(statement);
	}

	if (_log.isTraceEnabled()) {
		_log.Log(LOG_TRACE, "SQLQ query : %s", szQuery.c_str());
		if (!_log.TestFilter("SQLR"))
			LogQueryResult(results);
	}

	std::string error = sqlite3_errmsg(m_dbase);
	if(error != "not an error")
		_log.Log(LOG_ERROR, "SQL Query(\"%s\") : %s", szQuery.c_str(), error.c_str());
	return results;
}

std::vector<std::vector<std::string> > CSQLHelper::safe_queryBlob(const char *fmt, ...)
{
	va_list args;
	std::vector<std::vector<std::string> > results;
	va_start(args, fmt);
	char *zQuery = sqlite3_vmprintf(fmt, args);
	va_end(args);
	if (!zQuery)
	{
		_log.Log(LOG_ERROR, "SQL: Out of memory, or invalid printf!....");
		std::vector<std::vector<std::string> > results;
		return results;
	}
	results = queryBlob(zQuery);
	sqlite3_free(zQuery);
	return results;
}

std::vector<std::vector<std::string> > CSQLHelper::queryBlob(const std::string &szQuery)
{
	if (!m_dbase)
	{
		_log.Log(LOG_ERROR, "Database not open!!...Check your user rights!..");
		std::vector<std::vector<std::string> > results;
		return results;
	}
	boost::lock_guard<boost::mutex> l(m_sqlQueryMutex);

	sqlite3_stmt *statement;
	std::vector<std::vector<std::string> > results;

	if (sqlite3_prepare_v2(m_dbase, szQuery.c_str(), -1, &statement, 0) == SQLITE_OK)
	{
		int cols = sqlite3_column_count(statement);
		while (true)
		{
			int result = sqlite3_step(statement);
			if (result == SQLITE_ROW)
			{
				std::vector<std::string> values;
				for (int col = 0; col < cols; col++)
				{
					int blobSize = sqlite3_column_bytes(statement, col);
					char* value = (char*)sqlite3_column_blob(statement, col);
					if ((blobSize == 0) && (col == 0))
						break;
					else if (value == 0)
						values.push_back(std::string("")); //insert empty string
					else
						values.push_back(std::string(value,value+blobSize));
				}
				if (values.size()>0)
					results.push_back(values);
			}
			else
			{
				break;
			}
		}
		sqlite3_finalize(statement);
	}

	std::string error = sqlite3_errmsg(m_dbase);
	if (error != "not an error")
		_log.Log(LOG_ERROR, "SQL Query(\"%s\") : %s", szQuery.c_str(), error.c_str());
	return results;
}

uint64_t CSQLHelper::CreateDevice(const int HardwareID, const int SensorType, const int SensorSubType, std::string &devname, const unsigned long nid, const std::string &soptions)
{
	uint64_t DeviceRowIdx = -1;
	char ID[20];
	sprintf(ID, "%lu", nid);

#ifdef ENABLE_PYTHON
	{
		std::vector<std::vector<std::string> > result;
		result = m_sql.safe_query("SELECT Type FROM Hardware WHERE (ID == %d)", HardwareID);
		if (result.size() > 0)
		{
			std::vector<std::string> sd = result[0];
			_eHardwareTypes Type = (_eHardwareTypes)atoi(sd[0].c_str());
			if (Type == HTYPE_PythonPlugin)
			{
				// Not allowed to add device to plugin HW (plugin framework does not use key column "ID" but instead uses column "unit" as key)
				_log.Log(LOG_ERROR, "CSQLHelper::CreateDevice: Not allowed to add device owned by plugin %u!", HardwareID);
				return DeviceRowIdx;
			}
		}
	}
#endif

	switch (SensorType)
	{

	case pTypeTEMP:
	case pTypeWEIGHT:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0", devname);
		break;
	case pTypeUV:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0;0.0", devname);
		break;
	case pTypeRAIN:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0;0", devname);
		break;
	case pTypeTEMP_BARO:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0;1038.0;0;188.0", devname);
		break;
	case pTypeHUM:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 50, "1", devname);
		break;
	case pTypeTEMP_HUM:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0;50;1", devname);
		break;
	case pTypeTEMP_HUM_BARO:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0;50;1;1010;1", devname);
		break;
	case pTypeRFXMeter:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 10, 255, 0, "0", devname);
		break;
	case pTypeUsage:
	case pTypeLux:
	case pTypeP1Gas:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0", devname);
		break;
	case pTypeP1Power:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0;0;0;0;0;0", devname);
		break;
	case pTypeAirQuality:
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, devname);
		break;
	case pTypeCURRENT:
		//Current/Ampere
		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0.0;0.0;0.0", devname);
		break;
	case pTypeThermostat: //Thermostat Setpoint
	{
		unsigned char ID1 = (unsigned char)((nid & 0xFF000000) >> 24);
		unsigned char ID2 = (unsigned char)((nid & 0x00FF0000) >> 16);
		unsigned char ID3 = (unsigned char)((nid & 0x0000FF00) >> 8);
		unsigned char ID4 = (unsigned char)((nid & 0x000000FF));
		sprintf(ID, "%X%02X%02X%02X", ID1, ID2, ID3, ID4);

		DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "20.5", devname);
		break;
	}

	case pTypeGeneral:
	{
		switch (SensorSubType)
		{
		case sTypePressure: //Pressure (Bar)
		case sTypePercentage: //Percentage
		case sTypeWaterflow: //Waterflow
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "0.0", devname);
		}
		break;
		case sTypeCounterIncremental:		//Counter Incremental
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0", devname);
			break;
		case sTypeVoltage:		//Voltage
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "0.000", devname);
		}
		break;
		case sTypeTextStatus:		//Text
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "Hello World", devname);
		}
		break;
		case sTypeAlert:		//Alert
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "No Alert!", devname);
			break;
		case sTypeSoundLevel:		//Sound Level
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "65", devname);
		}
		break;
		case sTypeBaro:		//Barometer (hPa)
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "1021.34;0", devname);
		}
		break;
		case sTypeVisibility:		//Visibility (km)
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "10.3", devname);
			break;
		case sTypeDistance:		//Distance (cm)
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "123.4", devname);
		}
		break;
		case sTypeSoilMoisture:		//Soil Moisture
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 3, devname);
		}
		break;
		case sTypeLeafWetness:		//Leaf Wetness
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 2, devname);
		}
		break;
		case sTypeKwh:		//kWh
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "0;0.0", devname);
		}
		break;
		case sTypeCurrent:		//Current (Single)
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "6.4", devname);
		}
		break;
		case sTypeSolarRadiation:		//Solar Radiation
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "1.0", devname);
		}
		break;
		case sTypeCustom:			//Custom
		{
			if (!soptions.empty())
			{
				std::string rID = std::string(ID);
				padLeft(rID, 8, '0');
				DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 0, "0.0", devname);
				if (DeviceRowIdx != -1)
				{
					//Set the Label
					m_sql.safe_query("UPDATE DeviceStatus SET Options='%q' WHERE (ID==%" PRIu64 ")", soptions.c_str(), DeviceRowIdx);
				}
			}
			break;
		}
		}
		break;
	}

	case pTypeWIND:
	{
		switch (SensorSubType)
		{
		case sTypeWIND1:			// sTypeWIND1
		case sTypeWIND4:			//Wind + Temp + Chill
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0;N;0;0;0;0", devname);
			break;
		}
		break;
	}

	case pTypeGeneralSwitch:
	{
		switch (SensorSubType)
		{
		case sSwitchGeneralSwitch:		//Switch
		{
			sprintf(ID, "%08lX", nid);
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "100", devname);
		}
		break;
		case sSwitchTypeSelector:		//Selector Switch
		{
			unsigned char ID1 = (unsigned char)((nid & 0xFF000000) >> 24);
			unsigned char ID2 = (unsigned char)((nid & 0x00FF0000) >> 16);
			unsigned char ID3 = (unsigned char)((nid & 0x0000FF00) >> 8);
			unsigned char ID4 = (unsigned char)((nid & 0x000000FF));
			sprintf(ID, "%02X%02X%02X%02X", ID1, ID2, ID3, ID4);
			DeviceRowIdx = UpdateValue(HardwareID, ID, 1, SensorType, SensorSubType, 12, 255, 0, "0", devname);
			if (DeviceRowIdx != -1)
			{
				//Set switch type to selector
				m_sql.safe_query("UPDATE DeviceStatus SET SwitchType=%d WHERE (ID==%" PRIu64 ")", STYPE_Selector, DeviceRowIdx);
				//Set default device options
				m_sql.SetDeviceOptions(DeviceRowIdx, BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Level1|Level2|Level3", false));
			}
		}
		break;
		}
		break;
	}

	case pTypeColorSwitch:
	{
		switch (SensorSubType)
		{
		case sTypeColor_RGB:         //RGB switch
		case sTypeColor_RGB_W:       //RGBW switch
		case sTypeColor_RGB_CW_WW:   //RGBWW switch
		case sTypeColor_RGB_W_Z:     //RGBWZ switch
		case sTypeColor_RGB_CW_WW_Z: //RGBWWZ switch
		case sTypeColor_White:       //Monochrome white switch
		case sTypeColor_CW_WW:       //Adjustable color temperature white switch
		{
			std::string rID = std::string(ID);
			padLeft(rID, 8, '0');
			DeviceRowIdx = UpdateValue(HardwareID, rID.c_str(), 1, SensorType, SensorSubType, 12, 255, 1, devname);
			if (DeviceRowIdx != -1)
			{
				//Set switch type to dimmer
				m_sql.safe_query("UPDATE DeviceStatus SET SwitchType=%d WHERE (ID==%" PRIu64 ")", STYPE_Dimmer, DeviceRowIdx);
			}
		}
		break;
		}
		break;
	}
	}

	if (DeviceRowIdx != -1)
	{
		m_sql.safe_query("UPDATE DeviceStatus SET Used=1 WHERE (ID==%" PRIu64 ")", DeviceRowIdx);
		m_mainworker.m_eventsystem.GetCurrentStates();
	}

	return DeviceRowIdx;
}

uint64_t CSQLHelper::InsertDevice(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, const int switchType, const int nValue, const char* sValue, const std::string &devname, const unsigned char signallevel, const unsigned char batterylevel, const int used)
{
	//TODO: 'unsigned char unit' only allows 256 devices / plugin
	//TODO: return -1 as error code does not make sense for a function returning an unsigned value
	std::vector<std::vector<std::string> > result;
	uint64_t ulID = 0;
	std::string name = devname;

	if (!m_bAcceptNewHardware)
	{
#ifdef _DEBUG
		_log.Log(LOG_STATUS, "Device creation failed, Domoticz settings prevent accepting new devices.");
#endif
		return -1; //We do not allow new devices
	}

	if (devname == "")
	{
		name = "Unknown";
	}

	safe_query(
		"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SwitchType, SignalLevel, BatteryLevel, nValue, sValue, Name) "
		"VALUES ('%d','%q','%d','%d','%d','%d','%d','%d','%d','%q','%q')",
		HardwareID, ID, unit,
		devType, subType, switchType,
		signallevel, batterylevel,
		nValue, sValue, name.c_str());

	//Get new ID
	result = safe_query(
		"SELECT ID FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
		HardwareID, ID, unit, devType, subType);
	if (result.size() == 0)
	{
		_log.Log(LOG_ERROR, "Serious database error, problem getting ID from DeviceStatus!");
		return -1;
	}
	std::stringstream s_str(result[0][0]);
	s_str >> ulID;

	return ulID;
}

bool CSQLHelper::DoesDeviceExist(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType) {
	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ID,Name FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",HardwareID, ID, unit, devType, subType);
	if (result.size()==0) {
		return false;
	}
	else {
		return true;
	}
}

bool CSQLHelper::GetLastValue(const int HardwareID, const char* DeviceID, const unsigned char unit, const unsigned char devType, const unsigned char subType, int &nValue, std::string &sValue, struct tm &LastUpdateTime)
{
	bool result=false;
	std::vector<std::vector<std::string> > sqlresult;
	std::string sLastUpdate;
	//std::string sValue;
	//struct tm LastUpdateTime;
	sqlresult=safe_query(
		"SELECT nValue,sValue,LastUpdate FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d) order by LastUpdate desc limit 1",
		HardwareID,DeviceID,unit,devType,subType);

	if (sqlresult.size()!=0)
	{
		nValue=(int)atoi(sqlresult[0][0].c_str());
		sValue=sqlresult[0][1];
		sLastUpdate=sqlresult[0][2];

		time_t lutime;
		ParseSQLdatetime(lutime, LastUpdateTime, sLastUpdate);

		result=true;
	}

	return result;
}


void CSQLHelper::GetAddjustment(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, float &AddjValue, float &AddjMulti)
{
	AddjValue=0.0f;
	AddjMulti=1.0f;
	std::vector<std::vector<std::string> > result;
	result = safe_query(
		"SELECT AddjValue,AddjMulti FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
		HardwareID, ID, unit, devType, subType);
	if (result.size()!=0)
	{
		AddjValue = static_cast<float>(atof(result[0][0].c_str()));
		AddjMulti = static_cast<float>(atof(result[0][1].c_str()));
	}
}

void CSQLHelper::GetMeterType(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, int &meterType)
{
	meterType=0;
	std::vector<std::vector<std::string> > result;
	result=safe_query(
		"SELECT SwitchType FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
		HardwareID, ID, unit, devType, subType);
	if (result.size()!=0)
	{
		meterType=atoi(result[0][0].c_str());
	}
}

void CSQLHelper::GetAddjustment2(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, float &AddjValue, float &AddjMulti)
{
	AddjValue=0.0f;
	AddjMulti=1.0f;
	std::vector<std::vector<std::string> > result;
	result=safe_query(
		"SELECT AddjValue2,AddjMulti2 FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
		HardwareID, ID, unit, devType, subType);
	if (result.size()!=0)
	{
		AddjValue = static_cast<float>(atof(result[0][0].c_str()));
		AddjMulti = static_cast<float>(atof(result[0][1].c_str()));
	}
}

bool CSQLHelper::GetPreferencesVar(const std::string &Key, std::string &sValue)
{
	if (!m_dbase)
		return false;


	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT sValue FROM Preferences WHERE (Key='%q')",
		Key.c_str());
	if (result.size() < 1)
		return false;
	std::vector<std::string> sd = result[0];
	sValue = sd[0];
	return true;
}

bool CSQLHelper::GetPreferencesVar(const std::string &Key, double &Value)
{

	std::string sValue;
	int nValue;
	Value = 0;
	bool res = GetPreferencesVar(Key, nValue, sValue);
	if (!res)
		return false;
	Value = atof(sValue.c_str());
	return true;
}
bool CSQLHelper::GetPreferencesVar(const std::string &Key, int &nValue, std::string &sValue)
{
	if (!m_dbase)
		return false;

	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT nValue, sValue FROM Preferences WHERE (Key='%q')",
		Key.c_str());
	if (result.size() < 1)
		return false;
	std::vector<std::string> sd = result[0];
	nValue = atoi(sd[0].c_str());
	sValue = sd[1];
	return true;
}

bool CSQLHelper::GetPreferencesVar(const std::string &Key, int &nValue)
{
	std::string sValue;
	return GetPreferencesVar(Key, nValue, sValue);
}
void CSQLHelper::DeletePreferencesVar(const std::string &Key)
{
  std::string sValue ;
	if (!m_dbase)
		return ;

  //if found, delete
  if ( GetPreferencesVar(Key,sValue)== true)
  {
	  TSqlQueryResult result;
	  result = safe_query("DELETE FROM Preferences WHERE (Key='%q')",Key.c_str());
  }
}



int CSQLHelper::GetLastBackupNo(const char *Key, int &nValue)
{
	if (!m_dbase)
		return false;

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT nValue FROM BackupLog WHERE (Key='%q')",Key);
	if (result.size()<1)
		return -1;
	std::vector<std::string> sd=result[0];
	nValue=atoi(sd[0].c_str());
	return nValue;
}

void CSQLHelper::SetLastBackupNo(const char *Key, const int nValue)
{
	if (!m_dbase)
		return;

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ROWID FROM BackupLog WHERE (Key='%q')",Key);
	if (result.size()==0)
	{
		//Insert
		safe_query(
			"INSERT INTO BackupLog (Key, nValue) "
			"VALUES ('%q','%d')",
			Key,
			nValue);
	}
	else
	{
		//Update
		uint64_t ID = 0;
		std::stringstream s_str( result[0][0] );
		s_str >> ID;

		safe_query(
			"UPDATE BackupLog SET Key='%q', nValue=%d "
			"WHERE (ROWID = %" PRIu64 ")",
			Key,
			nValue,
			ID);
	}
}

void CSQLHelper::UpdateRFXCOMHardwareDetails(const int HardwareID, const int msg1, const int msg2, const int msg3, const int msg4, const int msg5, const int msg6)
{
	safe_query("UPDATE Hardware SET Mode1=%d, Mode2=%d, Mode3=%d, Mode4=%d, Mode5=%d, Mode6=%d WHERE (ID == %d)",
		msg1, msg2, msg3, msg4, msg5, msg6, HardwareID);
}

bool CSQLHelper::HasTimers(const uint64_t Idx)
{
	if (!m_dbase)
		return false;

	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT COUNT(*) FROM Timers WHERE (DeviceRowID==%" PRIu64 ") AND (TimerPlan==%d)", Idx, m_ActiveTimerPlan);
	if (result.size() != 0)
	{
		std::vector<std::string> sd = result[0];
		int totaltimers = atoi(sd[0].c_str());
		if (totaltimers > 0)
			return true;
	}
	result = safe_query("SELECT COUNT(*) FROM SetpointTimers WHERE (DeviceRowID==%" PRIu64 ") AND (TimerPlan==%d)", Idx, m_ActiveTimerPlan);
	if (result.size() != 0)
	{
		std::vector<std::string> sd = result[0];
		int totaltimers = atoi(sd[0].c_str());
		return (totaltimers > 0);
	}
	return false;
}

bool CSQLHelper::HasTimers(const std::string &Idx)
{
	std::stringstream s_str( Idx );
	uint64_t idxll;
	s_str >> idxll;
	return HasTimers(idxll);
}

bool CSQLHelper::HasSceneTimers(const uint64_t Idx)
{
	if (!m_dbase)
		return false;

	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT COUNT(*) FROM SceneTimers WHERE (SceneRowID==%" PRIu64 ") AND (TimerPlan==%d)", Idx, m_ActiveTimerPlan);
	if (result.size()==0)
		return false;
	std::vector<std::string> sd=result[0];
	int totaltimers=atoi(sd[0].c_str());
	return (totaltimers>0);
}

bool CSQLHelper::HasSceneTimers(const std::string &Idx)
{
	std::stringstream s_str( Idx );
	uint64_t idxll;
	s_str >> idxll;
	return HasSceneTimers(idxll);
}

void CSQLHelper::ScheduleShortlog()
{
#ifdef _DEBUG
	//return;
#endif
	if (!m_dbase)
		return;

	try
	{
		//Force WAL flush
		sqlite3_wal_checkpoint(m_dbase, NULL);

		UpdateTemperatureLog();
		UpdateRainLog();
		UpdateWindLog();
		UpdateUVLog();
		UpdateMeter();
		UpdateMultiMeter();
		UpdatePercentageLog();
		UpdateFanLog();
		//Removing the line below could cause a very large database,
		//and slow(large) data transfer (specially when working remote!!)
		CleanupShortLog();
	}
	catch (boost::exception & e)
	{
		_log.Log(LOG_ERROR, "Domoticz: Error running the shortlog schedule script!");
#ifdef _DEBUG
		_log.Log(LOG_ERROR, "-----------------\n%s\n----------------", boost::diagnostic_information(e).c_str());
#else
		(void)e;
#endif
		return;
	}
}

void CSQLHelper::ScheduleDay()
{
	if (!m_dbase)
		return;

	try
	{
		//Force WAL flush
		sqlite3_wal_checkpoint(m_dbase, NULL);

		AddCalendarTemperature();
		AddCalendarUpdateRain();
		AddCalendarUpdateUV();
		AddCalendarUpdateWind();
		AddCalendarUpdateMeter();
		AddCalendarUpdateMultiMeter();
		AddCalendarUpdatePercentage();
		AddCalendarUpdateFan();
		CleanupLightSceneLog();
	}
	catch (boost::exception & e)
	{
		_log.Log(LOG_ERROR, "Domoticz: Error running the daily schedule script!");
#ifdef _DEBUG
		_log.Log(LOG_ERROR, "-----------------\n%s\n----------------", boost::diagnostic_information(e).c_str());
#else
		(void)e;
#endif
		return;
	}
}

void CSQLHelper::AddCalendarTemperature()
{
	//Get All temperature devices in the Temperature Table
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Temperature ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		result=safe_query("SELECT MIN(Temperature), MAX(Temperature), AVG(Temperature), MIN(Chill), MAX(Chill), AVG(Humidity), AVG(Barometer), MIN(DewPoint), MIN(SetPoint), MAX(SetPoint), AVG(SetPoint) FROM Temperature WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float temp_min = static_cast<float>(atof(sd[0].c_str()));
			float temp_max = static_cast<float>(atof(sd[1].c_str()));
			float temp_avg = static_cast<float>(atof(sd[2].c_str()));
			float chill_min = static_cast<float>(atof(sd[3].c_str()));
			float chill_max = static_cast<float>(atof(sd[4].c_str()));
			int humidity=atoi(sd[5].c_str());
			int barometer=atoi(sd[6].c_str());
			float dewpoint = static_cast<float>(atof(sd[7].c_str()));
			float setpoint_min=static_cast<float>(atof(sd[8].c_str()));
			float setpoint_max=static_cast<float>(atof(sd[9].c_str()));
			float setpoint_avg=static_cast<float>(atof(sd[10].c_str()));
			//insert into calendar table
			result=safe_query(
				"INSERT INTO Temperature_Calendar (DeviceRowID, Temp_Min, Temp_Max, Temp_Avg, Chill_Min, Chill_Max, Humidity, Barometer, DewPoint, SetPoint_Min, SetPoint_Max, SetPoint_Avg, Date) "
				"VALUES ('%" PRIu64 "', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%d', '%d', '%.2f', '%.2f', '%.2f', '%.2f', '%q')",
				ID,
				temp_min,
				temp_max,
				temp_avg,
				chill_min,
				chill_max,
				humidity,
				barometer,
				dewpoint,
				setpoint_min,
				setpoint_max,
				setpoint_avg,
				szDateStart
				);
		}
	}
}

void CSQLHelper::AddCalendarUpdateRain()
{
	//Get All UV devices
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Rain ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		//Get Device Information
		result=safe_query("SELECT SubType FROM DeviceStatus WHERE (ID='%" PRIu64 "')",ID);
		if (result.size()<1)
			continue;
		std::vector<std::string> sd=result[0];

		unsigned char subType=atoi(sd[0].c_str());

		if (subType!=sTypeRAINWU)
		{
			result=safe_query("SELECT MIN(Total), MAX(Total), MAX(Rate) FROM Rain WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
				ID,
				szDateStart,
				szDateEnd
				);
		}
		else
		{
			result=safe_query("SELECT Total, Total, Rate FROM Rain WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q') ORDER BY ROWID DESC LIMIT 1",
				ID,
				szDateStart,
				szDateEnd
				);
		}

		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float total_min = static_cast<float>(atof(sd[0].c_str()));
			float total_max = static_cast<float>(atof(sd[1].c_str()));
			int rate=atoi(sd[2].c_str());

			float total_real=0;
			if (subType!=sTypeRAINWU)
			{
				total_real=total_max-total_min;
			}
			else
			{
				total_real=total_max;
			}


			if (total_real<1000)
			{
				//insert into calendar table
				result=safe_query(
					"INSERT INTO Rain_Calendar (DeviceRowID, Total, Rate, Date) "
					"VALUES ('%" PRIu64 "', '%.2f', '%d', '%q')",
					ID,
					total_real,
					rate,
					szDateStart
					);
			}
		}
	}
}

void CSQLHelper::AddCalendarUpdateMeter()
{
	float EnergyDivider=1000.0f;
	float GasDivider=100.0f;
	float WaterDivider=100.0f;
	float musage=0;
	int tValue;
	if (GetPreferencesVar("MeterDividerEnergy", tValue))
	{
		EnergyDivider=float(tValue);
	}
	if (GetPreferencesVar("MeterDividerGas", tValue))
	{
		GasDivider=float(tValue);
	}
	if (GetPreferencesVar("MeterDividerWater", tValue))
	{
		WaterDivider=float(tValue);
	}

	//Get All Meter devices
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Meter ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		//Get Device Information
		result=safe_query("SELECT Name, HardwareID, DeviceID, Unit, Type, SubType, SwitchType FROM DeviceStatus WHERE (ID='%" PRIu64 "')",ID);
		if (result.size()<1)
			continue;
		std::vector<std::string> sd=result[0];
		std::string devname = sd[0];
		//int hardwareID= atoi(sd[1].c_str());
		//std::string DeviceID=sd[2];
		//unsigned char Unit = atoi(sd[3].c_str());
		unsigned char devType=atoi(sd[4].c_str());
		unsigned char subType=atoi(sd[5].c_str());
		_eSwitchType switchtype=(_eSwitchType) atoi(sd[6].c_str());
		_eMeterType metertype=(_eMeterType)switchtype;

		float tGasDivider=GasDivider;

		if (devType==pTypeP1Power)
		{
			metertype=MTYPE_ENERGY;
		}
		else if (devType==pTypeP1Gas)
		{
			metertype=MTYPE_GAS;
			tGasDivider=1000.0f;
		}
        else if ((devType==pTypeRego6XXValue) && (subType==sTypeRego6XXCounter))
		{
			metertype=MTYPE_COUNTER;
		}


		result=safe_query("SELECT MIN(Value), MAX(Value), AVG(Value) FROM Meter WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			double total_min=(double)atof(sd[0].c_str());
			double total_max = (double)atof(sd[1].c_str());
			double avg_value = (double)atof(sd[2].c_str());

			if (
				(devType!=pTypeAirQuality)&&
				(devType!=pTypeRFXSensor)&&
				(!((devType==pTypeGeneral)&&(subType==sTypeVisibility)))&&
				(!((devType == pTypeGeneral) && (subType == sTypeDistance))) &&
				(!((devType == pTypeGeneral) && (subType == sTypeSolarRadiation))) &&
				(!((devType==pTypeGeneral)&&(subType==sTypeSoilMoisture)))&&
				(!((devType==pTypeGeneral)&&(subType==sTypeLeafWetness)))&&
				(!((devType == pTypeGeneral) && (subType == sTypeVoltage))) &&
				(!((devType == pTypeGeneral) && (subType == sTypeCurrent))) &&
				(!((devType == pTypeGeneral) && (subType == sTypePressure))) &&
				(!((devType == pTypeGeneral) && (subType == sTypeSoundLevel))) &&
				(devType != pTypeLux) &&
				(devType!=pTypeWEIGHT)&&
				(devType!=pTypeUsage)
				)
			{
				double total_real=total_max-total_min;
				double counter = total_max;

				//insert into calendar table
				result=safe_query(
					"INSERT INTO Meter_Calendar (DeviceRowID, Value, Counter, Date) "
					"VALUES ('%" PRIu64 "', '%.2f', '%.2f', '%q')",
					ID,
					total_real,
					counter,
					szDateStart
					);

				//Check for Notification
				musage=0;
				switch (metertype)
				{
				case MTYPE_ENERGY:
				case MTYPE_ENERGY_GENERATED:
					musage=float(total_real)/EnergyDivider;
					if (musage!=0)
						m_notifications.CheckAndHandleNotification(ID, devname, devType, subType, NTYPE_TODAYENERGY, musage);
					break;
				case MTYPE_GAS:
					musage=float(total_real)/tGasDivider;
					if (musage!=0)
						m_notifications.CheckAndHandleNotification(ID, devname, devType, subType, NTYPE_TODAYGAS, musage);
					break;
				case MTYPE_WATER:
					musage=float(total_real)/WaterDivider;
					if (musage!=0)
						m_notifications.CheckAndHandleNotification(ID, devname, devType, subType, NTYPE_TODAYGAS, musage);
					break;
				case MTYPE_COUNTER:
					musage=float(total_real);
					if (musage!=0)
						m_notifications.CheckAndHandleNotification(ID, devname, devType, subType, NTYPE_TODAYCOUNTER, musage);
					break;
				default:
					//Unhandled
					musage = 0;
					break;
				}
			}
			else
			{
				//AirQuality/Usage Meter/Moisture/RFXSensor/Voltage/Lux/SoundLevel insert into MultiMeter_Calendar table
				result=safe_query(
					"INSERT INTO MultiMeter_Calendar (DeviceRowID, Value1,Value2,Value3,Value4,Value5,Value6, Date) "
					"VALUES ('%" PRIu64 "', '%.2f','%.2f','%.2f','%.2f','%.2f','%.2f', '%q')",
					ID,
					total_min,total_max, avg_value,0.0f,0.0f,0.0f,
					szDateStart
					);
			}
			if (
				(devType!=pTypeAirQuality)&&
				(devType!=pTypeRFXSensor)&&
				((devType != pTypeGeneral) && (subType != sTypeVisibility)) &&
				((devType != pTypeGeneral) && (subType != sTypeDistance)) &&
				((devType != pTypeGeneral) && (subType != sTypeSolarRadiation)) &&
				((devType != pTypeGeneral) && (subType != sTypeVoltage)) &&
				((devType != pTypeGeneral) && (subType != sTypeCurrent)) &&
				((devType != pTypeGeneral) && (subType != sTypePressure)) &&
				((devType != pTypeGeneral) && (subType != sTypeSoilMoisture)) &&
				((devType != pTypeGeneral) && (subType != sTypeLeafWetness)) &&
				((devType != pTypeGeneral) && (subType != sTypeSoundLevel)) &&
				(devType != pTypeLux) &&
				(devType!=pTypeWEIGHT)
				)
			{
				result = safe_query("SELECT Value FROM Meter WHERE (DeviceRowID='%" PRIu64 "') ORDER BY ROWID DESC LIMIT 1", ID);
				if (result.size() > 0)
				{
					std::vector<std::string> sd = result[0];
					//Insert the last (max) counter value into the meter table to get the "today" value correct.
					result = safe_query(
						"INSERT INTO Meter (DeviceRowID, Value, Date) "
						"VALUES ('%" PRIu64 "', '%q', '%q')",
						ID,
						sd[0].c_str(),
						szDateEnd
					);
				}
			}
		}
		else
		{
			//no new meter result received in last day
			//insert into calendar table
			result=safe_query(
				"INSERT INTO Meter_Calendar (DeviceRowID, Value, Date) "
				"VALUES ('%" PRIu64 "', '%.2f', '%q')",
				ID,
				0.0f,
				szDateStart
				);
		}
	}
}

void CSQLHelper::AddCalendarUpdateMultiMeter()
{
	float EnergyDivider=1000.0f;
	int tValue;
	if (GetPreferencesVar("MeterDividerEnergy", tValue))
	{
		EnergyDivider=float(tValue);
	}

	//Get All meter devices
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM MultiMeter ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		//Get Device Information
		result=safe_query("SELECT Name, HardwareID, DeviceID, Unit, Type, SubType, SwitchType FROM DeviceStatus WHERE (ID='%" PRIu64 "')",ID);
		if (result.size()<1)
			continue;
		std::vector<std::string> sd=result[0];

		std::string devname = sd[0];
		//int hardwareID= atoi(sd[1].c_str());
		//std::string DeviceID=sd[2];
		//unsigned char Unit = atoi(sd[3].c_str());
		unsigned char devType=atoi(sd[4].c_str());
		unsigned char subType=atoi(sd[5].c_str());
		//_eSwitchType switchtype=(_eSwitchType) atoi(sd[6].c_str());
		//_eMeterType metertype=(_eMeterType)switchtype;

		result=safe_query(
			"SELECT MIN(Value1), MAX(Value1), MIN(Value2), MAX(Value2), MIN(Value3), MAX(Value3), MIN(Value4), MAX(Value4), MIN(Value5), MAX(Value5), MIN(Value6), MAX(Value6) FROM MultiMeter WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float total_real[6];
			float counter1 = 0;
			float counter2 = 0;
			float counter3 = 0;
			float counter4 = 0;

			if (devType==pTypeP1Power)
			{
				for (int ii=0; ii<6; ii++)
				{
					float total_min = static_cast<float>(atof(sd[(ii * 2) + 0].c_str()));
					float total_max = static_cast<float>(atof(sd[(ii * 2) + 1].c_str()));
					total_real[ii]=total_max-total_min;
				}
				counter1 = static_cast<float>(atof(sd[1].c_str()));
				counter2 = static_cast<float>(atof(sd[3].c_str()));
				counter3 = static_cast<float>(atof(sd[9].c_str()));
				counter4 = static_cast<float>(atof(sd[11].c_str()));
			}
			else
			{
				for (int ii=0; ii<6; ii++)
				{
					float fvalue = static_cast<float>(atof(sd[ii].c_str()));
					total_real[ii]=fvalue;
				}
			}

			//insert into calendar table
			result=safe_query(
				"INSERT INTO MultiMeter_Calendar (DeviceRowID, Value1, Value2, Value3, Value4, Value5, Value6, Counter1, Counter2, Counter3, Counter4, Date) "
				"VALUES ('%" PRIu64 "', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%.2f', '%q')",
				ID,
				total_real[0],
				total_real[1],
				total_real[2],
				total_real[3],
				total_real[4],
				total_real[5],
				counter1,
				counter2,
				counter3,
				counter4,
				szDateStart
				);

			//Check for Notification
			if (devType==pTypeP1Power)
			{
				float musage=(total_real[0]+total_real[2])/EnergyDivider;
				m_notifications.CheckAndHandleNotification(ID, devname, devType, subType, NTYPE_TODAYENERGY, musage);
			}
/*
			//Insert the last (max) counter values into the table to get the "today" value correct.
			sprintf(szTmp,
				"INSERT INTO MultiMeter (DeviceRowID, Value1, Value2, Value3, Value4, Value5, Value6, Date) "
				"VALUES (%" PRIu64 ", %s, %s, %s, %s, %s, %s, '%s')",
				ID,
				sd[0].c_str(),
				sd[1].c_str(),
				sd[2].c_str(),
				sd[3].c_str(),
				sd[4].c_str(),
				sd[5].c_str(),
				szDateEnd
				);
				result=query(szTmp);
*/
		}
	}
}

void CSQLHelper::AddCalendarUpdateWind()
{
	//Get All Wind devices
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Wind ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		result=safe_query("SELECT AVG(Direction), MIN(Speed), MAX(Speed), MIN(Gust), MAX(Gust) FROM Wind WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float Direction = static_cast<float>(atof(sd[0].c_str()));
			int speed_min=atoi(sd[1].c_str());
			int speed_max=atoi(sd[2].c_str());
			int gust_min=atoi(sd[3].c_str());
			int gust_max=atoi(sd[4].c_str());

			//insert into calendar table
			result=safe_query(
				"INSERT INTO Wind_Calendar (DeviceRowID, Direction, Speed_Min, Speed_Max, Gust_Min, Gust_Max, Date) "
				"VALUES ('%" PRIu64 "', '%.2f', '%d', '%d', '%d', '%d', '%q')",
				ID,
				Direction,
				speed_min,
				speed_max,
				gust_min,
				gust_max,
				szDateStart
				);
		}
	}
}

void CSQLHelper::AddCalendarUpdateUV()
{
	//Get All UV devices
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM UV ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		result=safe_query("SELECT MAX(Level) FROM UV WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float level = static_cast<float>(atof(sd[0].c_str()));

			//insert into calendar table
			result=safe_query(
				"INSERT INTO UV_Calendar (DeviceRowID, Level, Date) "
				"VALUES ('%" PRIu64 "', '%g', '%q')",
				ID,
				level,
				szDateStart
				);
		}
	}
}

void CSQLHelper::AddCalendarUpdatePercentage()
{
	//Get All Percentage devices in the Percentage Table
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Percentage ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		result=safe_query("SELECT MIN(Percentage), MAX(Percentage), AVG(Percentage) FROM Percentage WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			float percentage_min = static_cast<float>(atof(sd[0].c_str()));
			float percentage_max = static_cast<float>(atof(sd[1].c_str()));
			float percentage_avg = static_cast<float>(atof(sd[2].c_str()));
			//insert into calendar table
			result=safe_query(
				"INSERT INTO Percentage_Calendar (DeviceRowID, Percentage_Min, Percentage_Max, Percentage_Avg, Date) "
				"VALUES ('%" PRIu64 "', '%g', '%g', '%g','%q')",
				ID,
				percentage_min,
				percentage_max,
				percentage_avg,
				szDateStart
				);
		}
	}
}


void CSQLHelper::AddCalendarUpdateFan()
{
	//Get All FAN devices in the Fan Table
	std::vector<std::vector<std::string> > resultdevices;
	resultdevices=safe_query("SELECT DISTINCT(DeviceRowID) FROM Fan ORDER BY DeviceRowID");
	if (resultdevices.size()<1)
		return; //nothing to do

	char szDateStart[40];
	char szDateEnd[40];

	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now,&ltime);
	sprintf(szDateEnd,"%04d-%02d-%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday);

	time_t yesterday;
	struct tm tm2;
	getNoon(yesterday,tm2,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday-1); // we only want the date
	sprintf(szDateStart,"%04d-%02d-%02d",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday);

	std::vector<std::vector<std::string> > result;

	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=resultdevices.begin(); itt!=resultdevices.end(); ++itt)
	{
		std::vector<std::string> sddev=*itt;
		uint64_t ID;
		std::stringstream s_str( sddev[0] );
		s_str >> ID;

		result=safe_query("SELECT MIN(Speed), MAX(Speed), AVG(Speed) FROM Fan WHERE (DeviceRowID='%" PRIu64 "' AND Date>='%q' AND Date<'%q')",
			ID,
			szDateStart,
			szDateEnd
			);
		if (result.size()>0)
		{
			std::vector<std::string> sd=result[0];

			int speed_min=(int)atoi(sd[0].c_str());
			int speed_max=(int)atoi(sd[1].c_str());
			int speed_avg=(int)atoi(sd[2].c_str());
			//insert into calendar table
			result=safe_query(
				"INSERT INTO Fan_Calendar (DeviceRowID, Speed_Min, Speed_Max, Speed_Avg, Date) "
				"VALUES ('%" PRIu64 "', '%d', '%d', '%d','%q')",
				ID,
				speed_min,
				speed_max,
				speed_avg,
				szDateStart
				);
		}
	}
}

void CSQLHelper::CleanupShortLog()
{
	int n5MinuteHistoryDays=1;
	if(GetPreferencesVar("5MinuteHistoryDays", n5MinuteHistoryDays))
    {
        // If the history days is zero then all data in the short logs is deleted!
        if(n5MinuteHistoryDays == 0)
        {
            _log.Log(LOG_ERROR,"CleanupShortLog(): MinuteHistoryDays is zero!");
            return;
        }
#if 0
		char szDateStr[40];
		time_t clear_time = mytime(NULL) - (n5MinuteHistoryDays * 24 * 3600);
		struct tm ltime;
		localtime_r(&clear_time, &ltime);
		sprintf(szDateStr, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
		_log.Log(LOG_STATUS, "Cleaning up shortlog older than %s", szDateStr);
#endif

		char szQuery[250];
		std::string szQueryFilter = "strftime('%s',datetime('now','localtime')) - strftime('%s',Date) > (SELECT p.nValue * 86400 From Preferences AS p WHERE p.Key='5MinuteHistoryDays')";

		sprintf(szQuery, "DELETE FROM Temperature WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM Rain WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM Wind WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM UV WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM Meter WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM MultiMeter WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM Percentage WHERE %s", szQueryFilter.c_str());
		query(szQuery);

		sprintf(szQuery, "DELETE FROM Fan WHERE %s", szQueryFilter.c_str());
		query(szQuery);
	}
}

void CSQLHelper::ClearShortLog()
{
	query("DELETE FROM Temperature");
	query("DELETE FROM Rain");
	query("DELETE FROM Wind");
	query("DELETE FROM UV");
	query("DELETE FROM Meter");
	query("DELETE FROM MultiMeter");
	query("DELETE FROM Percentage");
	query("DELETE FROM Fan");
	VacuumDatabase();
}

void CSQLHelper::VacuumDatabase()
{
	query("VACUUM");
}

void CSQLHelper::OptimizeDatabase(sqlite3 *dbase)
{
	if (dbase == NULL)
		return;
	sqlite3_exec(dbase, "PRAGMA optimize;", NULL, NULL, NULL);
}

void CSQLHelper::DeleteHardware(const std::string &idx)
{
	safe_query("DELETE FROM Hardware WHERE (ID == '%q')",idx.c_str());

	//and now delete all records in the DeviceStatus table itself
	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID == '%q')",idx.c_str());
	if (result.size()>0)
	{
		std::string devs2delete = "";
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd = *itt;
			if (!devs2delete.empty())
				devs2delete += ";";
			devs2delete += sd[0];
		}
		DeleteDevices(devs2delete);
	}
	//also delete all records in other tables
	safe_query("DELETE FROM ZWaveNodes WHERE (HardwareID== '%q')",idx.c_str());
	safe_query("DELETE FROM EnoceanSensors WHERE (HardwareID== '%q')", idx.c_str());
	safe_query("DELETE FROM MySensors WHERE (HardwareID== '%q')", idx.c_str());
	safe_query("DELETE FROM WOLNodes WHERE (HardwareID == '%q')",idx.c_str());
}

void CSQLHelper::DeleteCamera(const std::string &idx)
{
	safe_query("DELETE FROM Cameras WHERE (ID == '%q')",idx.c_str());
	safe_query("DELETE FROM CamerasActiveDevices WHERE (CameraRowID == '%q')",idx.c_str());
}

void CSQLHelper::DeletePlan(const std::string &idx)
{
	safe_query("DELETE FROM Plans WHERE (ID == '%q')",idx.c_str());
}

void CSQLHelper::DeleteEvent(const std::string &idx)
{
	safe_query("DELETE FROM EventRules WHERE (EMID == '%q')",idx.c_str());
	safe_query("DELETE FROM EventMaster WHERE (ID == '%q')",idx.c_str());
}

//Argument, one or multiple devices separated by a semicolumn (;)
void CSQLHelper::DeleteDevices(const std::string &idx)
{
	std::vector<std::string> _idx;
	StringSplit(idx, ";", _idx);
	if (!_idx.empty())
	{
		std::set<std::pair<std::string, std::string> > removeddevices;
		std::vector<std::string>::const_iterator itt;
#ifdef ENABLE_PYTHON
		for (itt = _idx.begin(); itt != _idx.end(); ++itt)
		{
			_log.Log(LOG_TRACE, "CSQLHelper::DeleteDevices: Delete %s", (*itt).c_str());
			std::vector<std::vector<std::string> > result;
			result = safe_query("SELECT HardwareID, Unit FROM DeviceStatus WHERE (ID == '%q')", (*itt).c_str());
			if (result.size() > 0)
			{
				std::vector<std::string> sd = result[0];
				std::string HwID = sd[0];
				std::string Unit = sd[1];
				CDomoticzHardwareBase *pHardware = m_mainworker.GetHardwareByIDType(HwID, HTYPE_PythonPlugin);
				if (pHardware != NULL)
				{
					std::pair<std::string, std::string> p = std::make_pair(HwID,Unit);
					removeddevices.insert(std::make_pair(HwID,Unit));
				}
			}
		}
#endif
		{
			//Avoid mutex deadlock here
			boost::lock_guard<boost::mutex> l(m_sqlQueryMutex);

			char* errorMessage;
			sqlite3_exec(m_dbase, "BEGIN TRANSACTION", NULL, NULL, &errorMessage);

			for (itt = _idx.begin(); itt != _idx.end(); ++itt)
			{
				safe_exec_no_return("DELETE FROM LightingLog WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM LightSubDevices WHERE (ParentID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM LightSubDevices WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Notifications WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Rain WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Rain_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Temperature WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Temperature_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Timers WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM SetpointTimers WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM UV WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM UV_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Wind WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Wind_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Meter WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Meter_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM MultiMeter WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM MultiMeter_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Percentage WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM Percentage_Calendar WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM SceneDevices WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM DeviceToPlansMap WHERE (DeviceRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM CamerasActiveDevices WHERE (DevSceneType==0) AND (DevSceneRowID == '%q')", (*itt).c_str());
				safe_exec_no_return("DELETE FROM SharedDevices WHERE (DeviceRowID== '%q')", (*itt).c_str());
				//notify eventsystem device is no longer present
				std::stringstream sstridx(*itt);
				uint64_t ullidx;
				sstridx >> ullidx;
				m_mainworker.m_eventsystem.RemoveSingleState(ullidx, m_mainworker.m_eventsystem.REASON_DEVICE);
				//and now delete all records in the DeviceStatus table itself
				safe_exec_no_return("DELETE FROM DeviceStatus WHERE (ID == '%q')", (*itt).c_str());
			}
			sqlite3_exec(m_dbase, "COMMIT TRANSACTION", NULL, NULL, &errorMessage);
		}
#ifdef ENABLE_PYTHON
		std::set<std::pair<std::string, std::string> >::iterator it;
		for (it = removeddevices.begin(); it != removeddevices.end(); ++it)
		{
			int HwID = atoi((*it).first.c_str());
			int Unit = atoi((*it).second.c_str());
			// Notify plugin to sync plugins' device list
			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(HwID);
			if (pHardware != NULL && pHardware->HwdType == HTYPE_PythonPlugin)
			{
				_log.Log(LOG_TRACE, "CSQLHelper::DeleteDevices: Notifying plugin %u about deletion of device %u", HwID, Unit);
				Plugins::CPlugin *pPlugin = (Plugins::CPlugin*)pHardware;
				pPlugin->DeviceRemoved(Unit);
			}

		}
#endif
	}
	else
		return;

	m_notifications.ReloadNotifications();
}

void CSQLHelper::TransferDevice(const std::string &idx, const std::string &newidx)
{
	std::vector<std::vector<std::string> > result;

	safe_query("UPDATE LightingLog SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());
	safe_query("UPDATE LightSubDevices SET ParentID='%q' WHERE (ParentID == '%q')",newidx.c_str(),idx.c_str());
	safe_query("UPDATE LightSubDevices SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());
	safe_query("UPDATE Notifications SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());
	safe_query("UPDATE DeviceToPlansMap SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')", newidx.c_str(), idx.c_str());
	safe_query("UPDATE SharedDevices SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')", newidx.c_str(), idx.c_str());

	//Rain
	result=safe_query("SELECT Date FROM Rain WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Rain SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Rain SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM Rain_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Rain_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Rain_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//Temperature
	result=safe_query("SELECT Date FROM Temperature WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Temperature SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Temperature SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM Temperature_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Temperature_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Temperature_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	safe_query("UPDATE Timers SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//UV
	result=safe_query("SELECT Date FROM UV WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE UV SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE UV SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM UV_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE UV_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE UV_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//Wind
	result=safe_query("SELECT Date FROM Wind WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Wind SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Wind SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM Wind_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Wind_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Wind_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//Meter
	result=safe_query("SELECT Date FROM Meter WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Meter SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Meter SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM Meter_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE Meter_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE Meter_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//Multimeter
	result=safe_query("SELECT Date FROM MultiMeter WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE MultiMeter SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE MultiMeter SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	result=safe_query("SELECT Date FROM MultiMeter_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1",newidx.c_str());
	if (result.size()>0)
		safe_query("UPDATE MultiMeter_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')",newidx.c_str(),idx.c_str(),result[0][0].c_str());
	else
		safe_query("UPDATE MultiMeter_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')",newidx.c_str(),idx.c_str());

	//Percentage
	result = safe_query("SELECT Date FROM Percentage WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1", newidx.c_str());
	if (result.size() > 0)
		safe_query("UPDATE Percentage SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')", newidx.c_str(), idx.c_str(), result[0][0].c_str());
	else
		safe_query("UPDATE Percentage SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')", newidx.c_str(), idx.c_str());

	result = safe_query("SELECT Date FROM Percentage_Calendar WHERE (DeviceRowID == '%q') ORDER BY Date ASC LIMIT 1", newidx.c_str());
	if (result.size() > 0)
		safe_query("UPDATE Percentage_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q') AND (Date<'%q')", newidx.c_str(), idx.c_str(), result[0][0].c_str());
	else
		safe_query("UPDATE Percentage_Calendar SET DeviceRowID='%q' WHERE (DeviceRowID == '%q')", newidx.c_str(), idx.c_str());
}

void CSQLHelper::CheckAndUpdateDeviceOrder()
{
	std::vector<std::vector<std::string> > result;

	//Get All ID's where Order=0
	result=safe_query("SELECT ROWID FROM DeviceStatus WHERE ([Order]==0)");
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			safe_query("UPDATE DeviceStatus SET [Order] = (SELECT MAX([Order]) FROM DeviceStatus)+1 WHERE (ROWID == '%q')", sd[0].c_str());
		}
	}
}

void CSQLHelper::CheckAndUpdateSceneDeviceOrder()
{
	std::vector<std::vector<std::string> > result;

	//Get All ID's where Order=0
	result=safe_query("SELECT ROWID FROM SceneDevices WHERE ([Order]==0)");
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			safe_query("UPDATE SceneDevices SET [Order] = (SELECT MAX([Order]) FROM SceneDevices)+1 WHERE (ROWID == '%q')", sd[0].c_str());
		}
	}
}

void CSQLHelper::CleanupLightSceneLog()
{
	//cleanup the lighting log
	int nMaxDays=30;
	GetPreferencesVar("LightHistoryDays", nMaxDays);

	char szDateEnd[40];
	time_t now = mytime(NULL);
	struct tm tm1;
	localtime_r(&now,&tm1);

	time_t daybefore;
	struct tm tm2;
	constructTime(daybefore,tm2,tm1.tm_year+1900,tm1.tm_mon+1,tm1.tm_mday-nMaxDays,tm1.tm_hour,tm1.tm_min,0,tm1.tm_isdst);
	sprintf(szDateEnd,"%04d-%02d-%02d %02d:%02d:00",tm2.tm_year+1900,tm2.tm_mon+1,tm2.tm_mday,tm2.tm_hour,tm2.tm_min);


	safe_query("DELETE FROM LightingLog WHERE (Date<'%q')", szDateEnd);
	safe_query("DELETE FROM SceneLog WHERE (Date<'%q')", szDateEnd);
}

bool CSQLHelper::DoesSceneByNameExits(const std::string &SceneName)
{
	std::vector<std::vector<std::string> > result;

	//Get All ID's where Order=0
	result=safe_query("SELECT ID FROM Scenes WHERE (Name=='%q')", SceneName.c_str());
	return (result.size()>0);
}

void CSQLHelper::CheckSceneStatusWithDevice(const std::string &DevIdx)
{
	std::stringstream s_str( DevIdx );
	uint64_t idxll;
	s_str >> idxll;
	return CheckSceneStatusWithDevice(idxll);
}

void CSQLHelper::CheckSceneStatusWithDevice(const uint64_t DevIdx)
{
	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT SceneRowID FROM SceneDevices WHERE (DeviceRowID == %" PRIu64 ")", DevIdx);
	std::vector<std::vector<std::string> >::const_iterator itt;
	for (itt=result.begin(); itt!=result.end(); ++itt)
	{
		std::vector<std::string> sd=*itt;
		CheckSceneStatus(sd[0]);
	}
}

void CSQLHelper::CheckSceneStatus(const std::string &Idx)
{
	std::stringstream s_str( Idx );
	uint64_t idxll;
	s_str >> idxll;
	return CheckSceneStatus(idxll);
}

void CSQLHelper::CheckSceneStatus(const uint64_t Idx)
{
	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT nValue FROM Scenes WHERE (ID == %" PRIu64 ")", Idx);
	if (result.size()<1)
		return; //not found

	unsigned char orgValue=(unsigned char)atoi(result[0][0].c_str());
	unsigned char newValue=orgValue;

	result=safe_query("SELECT a.ID, a.DeviceID, a.Unit, a.Type, a.SubType, a.SwitchType, a.nValue, a.sValue FROM DeviceStatus AS a, SceneDevices as b WHERE (a.ID == b.DeviceRowID) AND (b.SceneRowID == %" PRIu64 ")",
		Idx);
	if (result.size()<1)
		return; //no devices in scene

	std::vector<std::vector<std::string> >::const_iterator itt;

	std::vector<bool> _DeviceStatusResults;

	for (itt=result.begin(); itt!=result.end(); ++itt)
	{
		std::vector<std::string> sd=*itt;
		int nValue=atoi(sd[6].c_str());
		std::string sValue=sd[7];
		//unsigned char Unit=atoi(sd[2].c_str());
		unsigned char dType=atoi(sd[3].c_str());
		unsigned char dSubType=atoi(sd[4].c_str());
		_eSwitchType switchtype=(_eSwitchType)atoi(sd[5].c_str());

		std::string lstatus="";
		int llevel=0;
		bool bHaveDimmer=false;
		bool bHaveGroupCmd=false;
		int maxDimLevel=0;

		GetLightStatus(dType, dSubType, switchtype,nValue, sValue, lstatus, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);
		_DeviceStatusResults.push_back(IsLightSwitchOn(lstatus));
	}

	//Check if all on/off
	int totOn=0;
	int totOff=0;

	std::vector<bool>::const_iterator itt2;
	for (itt2=_DeviceStatusResults.begin(); itt2!=_DeviceStatusResults.end(); ++itt2)
	{
		if (*itt2==true)
			totOn++;
		else
			totOff++;
	}
	if (totOn==_DeviceStatusResults.size())
	{
		//All are on
		newValue=1;
	}
	else if (totOff==_DeviceStatusResults.size())
	{
		//All are Off
		newValue=0;
	}
	else
	{
		//Some are on, some are off
		newValue=2;
	}
	if (newValue!=orgValue)
	{
		//Set new Scene status
		safe_query("UPDATE Scenes SET nValue=%d WHERE (ID == %" PRIu64 ")",
			int(newValue), Idx);
	}
}

void CSQLHelper::DeleteDataPoint(const char *ID, const std::string &Date)
{
	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT Type,SubType FROM DeviceStatus WHERE (ID==%q)",ID);
	if (result.size()<1)
		return;
	//std::vector<std::string> sd=result[0];
	//unsigned char dType=atoi(sd[0].c_str());
	//unsigned char dSubType=atoi(sd[1].c_str());

	if (Date.find(':')!=std::string::npos)
	{
		char szDateEnd[100];

		time_t now = mytime(NULL);
		struct tm tLastUpdate;
		localtime_r(&now, &tLastUpdate);

		time_t cEndTime;
		ParseSQLdatetime(cEndTime, tLastUpdate, Date, tLastUpdate.tm_isdst);
		tLastUpdate.tm_min += 2;
		cEndTime = mktime(&tLastUpdate);

//GB3:	ToDo: Database should know the difference between Summer and Winter time,
//	or we'll be deleting both entries if the DataPoint is inside a DST jump

		sprintf(szDateEnd, "%04d-%02d-%02d %02d:%02d:%02d", tLastUpdate.tm_year + 1900, tLastUpdate.tm_mon + 1, tLastUpdate.tm_mday, tLastUpdate.tm_hour, tLastUpdate.tm_min, tLastUpdate.tm_sec);
		//Short log
		safe_query("DELETE FROM Rain WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM Wind WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM UV WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM Temperature WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM Meter WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM MultiMeter WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM Percentage WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
		safe_query("DELETE FROM Fan WHERE (DeviceRowID=='%q') AND (Date>='%q') AND (Date<='%q')",ID,Date.c_str(),szDateEnd);
	}
	else
	{
		//Day/Month/Year
		safe_query("DELETE FROM Rain_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM Wind_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM UV_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM Temperature_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM Meter_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM MultiMeter_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM Percentage_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
		safe_query("DELETE FROM Fan_Calendar WHERE (DeviceRowID=='%q') AND (Date=='%q')",ID,Date.c_str());
	}
}

void CSQLHelper::AddTaskItem(const _tTaskItem &tItem, const bool cancelItem)
{
	boost::lock_guard<boost::mutex> l(m_background_task_mutex);

	// Check if an event for the same device is already in queue, and if so, replace it
	if (_log.isTraceEnabled())
	   _log.Log(LOG_TRACE, "SQLH AddTask: Request to add task: idx=%" PRIu64 ", DelayTime=%f, Command='%s', Level=%d, Color='%s', RelatedEvent='%s'", tItem._idx, tItem._DelayTime, tItem._command.c_str(), tItem._level, tItem._Color.toString().c_str(), tItem._relatedEvent.c_str());
	// Remove any previous task linked to the same device

	if (
		(tItem._ItemType == TITEM_SWITCHCMD_EVENT) ||
		(tItem._ItemType == TITEM_SWITCHCMD_SCENE) ||
		(tItem._ItemType == TITEM_UPDATEDEVICE) ||
		(tItem._ItemType == TITEM_SET_VARIABLE)
		)
	{
		std::vector<_tTaskItem>::iterator itt = m_background_task_queue.begin();
		while (itt != m_background_task_queue.end())
		{
			if (_log.isTraceEnabled())
				 _log.Log(LOG_TRACE, "SQLH AddTask: Comparing with item in queue: idx=%" PRId64 ", DelayTime=%f, Command='%s', Level=%d, Color='%s', RelatedEvent='%s'", itt->_idx, itt->_DelayTime, itt->_command.c_str(), itt->_level, itt->_Color.toString().c_str(), itt->_relatedEvent.c_str());
			if (itt->_idx == tItem._idx && itt->_ItemType == tItem._ItemType)
			{
				float iDelayDiff = tItem._DelayTime - itt->_DelayTime;
				if (iDelayDiff < (1./timer_resolution_hz/2))
				{
					if (_log.isTraceEnabled())
						 _log.Log(LOG_TRACE, "SQLH AddTask: => Already present. Cancelling previous task item");
					itt = m_background_task_queue.erase(itt);
				}
				else
					++itt;
			}
			else
				++itt;
		}
	}
	// _log.Log(LOG_NORM, "=> Adding new task item");
	if (!cancelItem)
		m_background_task_queue.push_back(tItem);
}

void CSQLHelper::EventsGetTaskItems(std::vector<_tTaskItem> &currentTasks)
{
	boost::lock_guard<boost::mutex> l(m_background_task_mutex);

	currentTasks.clear();

    for(std::vector<_tTaskItem>::iterator it = m_background_task_queue.begin(); it != m_background_task_queue.end(); ++it)
    {
		currentTasks.push_back(*it);
	}
}

bool CSQLHelper::RestoreDatabase(const std::string &dbase)
{
	_log.Log(LOG_STATUS, "Restore Database: Starting...");
	//write file to disk
	std::string fpath;
#ifdef WIN32
	size_t bpos=m_dbase_name.rfind('\\');
#else
	size_t bpos=m_dbase_name.rfind('/');
#endif
	if (bpos!=std::string::npos)
		fpath=m_dbase_name.substr(0,bpos+1);
#ifdef WIN32
	std::string outputfile=fpath+"restore.db";
#else
	std::string outputfile = "/tmp/restore.db";
#endif
	std::ofstream outfile;
	outfile.open(outputfile.c_str(),std::ios::out|std::ios::binary|std::ios::trunc);
	if (!outfile.is_open())
	{
		_log.Log(LOG_ERROR, "Restore Database: Could not open backup file for writing!");
		return false;
	}
	outfile << dbase;
	outfile.flush();
	outfile.close();
	//check if we can open the database (check if valid)
	sqlite3 *dbase_restore=NULL;
	int rc = sqlite3_open(outputfile.c_str(), &dbase_restore);
	if (rc)
	{
		_log.Log(LOG_ERROR,"Restore Database: Could not open SQLite3 database: %s", sqlite3_errmsg(dbase_restore));
		sqlite3_close(dbase_restore);
		return false;
	}
	if (dbase_restore==NULL)
		return false;
	//could still be not valid
	std::stringstream ss;
	ss << "SELECT sValue FROM Preferences WHERE (Key='DB_Version')";
	sqlite3_stmt *statement;
	if(sqlite3_prepare_v2(dbase_restore, ss.str().c_str(), -1, &statement, 0) != SQLITE_OK)
	{
		_log.Log(LOG_ERROR, "Restore Database: Seems this is not our database, or it is corrupted!");
		sqlite3_close(dbase_restore);
		return false;
	}
	OptimizeDatabase(dbase_restore);
	sqlite3_close(dbase_restore);
	//we have a valid database!
	std::remove(outputfile.c_str());
	//stop database
	sqlite3_close(m_dbase);
	m_dbase=NULL;
	std::ofstream outfile2;
	outfile2.open(m_dbase_name.c_str(),std::ios::out|std::ios::binary|std::ios::trunc);
	if (!outfile2.is_open())
	{
		_log.Log(LOG_ERROR, "Restore Database: Could not open backup file for writing!");
		return false;
	}
	outfile2 << dbase;
	outfile2.flush();
	outfile2.close();
	//change ownership
#ifndef WIN32
	struct stat info;
	if (stat(m_dbase_name.c_str(), &info)==0)
	{
		struct passwd *pw = getpwuid(info.st_uid);
		int ret=chown(m_dbase_name.c_str(),pw->pw_uid,pw->pw_gid);
		if (ret!=0)
		{
			_log.Log(LOG_ERROR, "Restore Database: Could not set database ownership (chown returned an error!)");
		}
	}
#endif
	if (!OpenDatabase())
	{
		_log.Log(LOG_ERROR, "Restore Database: Error opening new database!");
		return false;
	}
	//Cleanup the database
	VacuumDatabase();
	_log.Log(LOG_STATUS, "Restore Database: Succeeded!");
	return true;
}

bool CSQLHelper::BackupDatabase(const std::string &OutputFile)
{
	if (!m_dbase)
		return false; //database not open!

	//First cleanup the database
	OptimizeDatabase(m_dbase);
	VacuumDatabase();

	boost::lock_guard<boost::mutex> l(m_sqlQueryMutex);

	int rc;                     // Function return code
	sqlite3 *pFile;             // Database connection opened on zFilename
	sqlite3_backup *pBackup;    // Backup handle used to copy data

	// Open the database file identified by zFilename.
	rc = sqlite3_open(OutputFile.c_str(), &pFile);
	if( rc!=SQLITE_OK )
		return false;

	// Open the sqlite3_backup object used to accomplish the transfer
    pBackup = sqlite3_backup_init(pFile, "main", m_dbase, "main");
    if( pBackup )
	{
      // Each iteration of this loop copies 5 database pages from database
      // pDb to the backup database.
      do {
        rc = sqlite3_backup_step(pBackup, 5);
        //xProgress(  sqlite3_backup_remaining(pBackup), sqlite3_backup_pagecount(pBackup) );
        //if( rc==SQLITE_OK || rc==SQLITE_BUSY || rc==SQLITE_LOCKED ){
          //sqlite3_sleep(250);
        //}
      } while( rc==SQLITE_OK || rc==SQLITE_BUSY || rc==SQLITE_LOCKED );

      /* Release resources allocated by backup_init(). */
      sqlite3_backup_finish(pBackup);
    }
    rc = sqlite3_errcode(pFile);
	// Close the database connection opened on database file zFilename
	// and return the result of this function.
	sqlite3_close(pFile);
	return ( rc==SQLITE_OK );
}

uint64_t CSQLHelper::UpdateValueLighting2GroupCmd(const int HardwareID, const char* ID, const unsigned char unit,
	const unsigned char devType, const unsigned char subType,
	const unsigned char signallevel, const unsigned char batterylevel,
	const int nValue, const char* sValue,
	std::string &devname,
	const bool bUseOnOffAction)
{
	// We only have to update all others units within the ID group. If the current unit does not have the same value,
	// it will be updated too. The reason we choose the UpdateValue is the propagation of the change to all units involved, including LogUpdate.

	uint64_t devRowIndex = -1;
	typedef std::vector<std::vector<std::string> > VectorVectorString;

	VectorVectorString result = safe_query("SELECT Unit FROM DeviceStatus WHERE ((DeviceID=='%q') AND (Type==%d) AND (SubType==%d) AND (nValue!=%d))",
		ID,
		pTypeLighting2,
		subType,
		nValue);

	for (VectorVectorString::const_iterator itt = result.begin(); itt != result.end(); ++itt)
	{
		unsigned char theUnit = atoi((*itt)[0].c_str()); // get the unit value
		devRowIndex = UpdateValue(HardwareID, ID, theUnit, devType, subType, signallevel, batterylevel, nValue, sValue, devname, bUseOnOffAction);
	}
	return devRowIndex;
}

void CSQLHelper::Lighting2GroupCmd(const std::string &ID, const unsigned char subType, const unsigned char GroupCmd)
{
	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now, &ltime);

	safe_query("UPDATE DeviceStatus SET nValue='%d', sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (DeviceID=='%q') And (Type==%d) And (SubType==%d) And (nValue!=%d)",
		GroupCmd,
		"OFF",
		ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
		ID.c_str(),
		pTypeLighting2,
		subType,
		GroupCmd);
}

uint64_t CSQLHelper::UpdateValueHomeConfortGroupCmd(const int HardwareID, const char* ID, const unsigned char unit,
	const unsigned char devType, const unsigned char subType,
	const unsigned char signallevel, const unsigned char batterylevel,
	const int nValue, const char* sValue,
	std::string &devname,
	const bool bUseOnOffAction)
{
	// We only have to update all others units within the ID group. If the current unit does not have the same value,
	// it will be updated too. The reason we choose the UpdateValue is the propagation of the change to all units involved, including LogUpdate.

	uint64_t devRowIndex = -1;
	typedef std::vector<std::vector<std::string> > VectorVectorString;

	VectorVectorString result = safe_query("SELECT Unit FROM DeviceStatus WHERE ((DeviceID=='%q') AND (Type==%d) AND (SubType==%d) AND (nValue!=%d))",
		ID,
		pTypeHomeConfort,
		subType,
		nValue);

	for (VectorVectorString::const_iterator itt = result.begin(); itt != result.end(); ++itt)
	{
		unsigned char theUnit = atoi((*itt)[0].c_str()); // get the unit value
		devRowIndex = UpdateValue(HardwareID, ID, theUnit, devType, subType, signallevel, batterylevel, nValue, sValue, devname, bUseOnOffAction);
	}
	return devRowIndex;
}

void CSQLHelper::HomeConfortGroupCmd(const std::string &ID, const unsigned char subType, const unsigned char GroupCmd)
{
	time_t now = mytime(NULL);
	struct tm ltime;
	localtime_r(&now, &ltime);

	safe_query("UPDATE DeviceStatus SET nValue='%s', sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (DeviceID=='%q') And (Type==%d) And (SubType==%d) And (nValue!=%d)",
		GroupCmd,
		"OFF",
		ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
		ID.c_str(),
		pTypeHomeConfort,
		subType,
		GroupCmd);
}

void CSQLHelper::GeneralSwitchGroupCmd(const std::string &ID, const unsigned char subType, const unsigned char GroupCmd)
{
	safe_query("UPDATE DeviceStatus SET nValue = %d WHERE (DeviceID=='%q') And (Type==%d) And (SubType==%d)", GroupCmd, ID.c_str(), pTypeGeneralSwitch, subType);
}

void CSQLHelper::SetUnitsAndScale()
{
	//Wind
	if (m_windunit==WINDUNIT_MS)
	{
		m_windsign="m/s";
		m_windscale=0.1f;
	}
	else if (m_windunit==WINDUNIT_KMH)
	{
		m_windsign="km/h";
		m_windscale=0.36f;
	}
	else if (m_windunit==WINDUNIT_MPH)
	{
		m_windsign="mph";
		m_windscale=0.223693629205f;
	}
	else if (m_windunit==WINDUNIT_Knots)
	{
		m_windsign="kn";
		m_windscale=0.1943844492457398f;
	}
	else if (m_windunit == WINDUNIT_Beaufort)
	{
		m_windsign = "bf";
		m_windscale = 1;
	}

	//Temp
	if (m_tempunit==TEMPUNIT_C)
	{
		m_tempsign="C";
		m_tempscale=1.0f;
	}
    else if (m_tempunit==TEMPUNIT_F)
	{
		m_tempsign="F";
		m_tempscale=1.0f; // *1.8 + 32
	}

    if(m_weightunit == WEIGHTUNIT_KG)
    {
        m_weightsign="kg";
        m_weightscale=1.0f;
    }
    else if(m_weightunit == WEIGHTUNIT_LB)
    {
        m_weightsign="lb";
        m_weightscale=2.20462f;
    }
}

bool CSQLHelper::HandleOnOffAction(const bool bIsOn, const std::string &OnAction, const std::string &OffAction)
{
	if (_log.isTraceEnabled())
	{
		if (bIsOn)
			_log.Log(LOG_TRACE, "SQLH HandleOnOffAction: OnAction:%s", OnAction.c_str());
		else
			_log.Log(LOG_TRACE, "SQLH HandleOnOffAction: OffAction:%s", OffAction.c_str());
	}

	if (bIsOn)
	{
		if (OnAction.empty())
			return true;

		if ((OnAction.find("http://") != std::string::npos) || (OnAction.find("https://") != std::string::npos))
		{
			AddTaskItem(_tTaskItem::GetHTTPPage(0.2f, OnAction, "SwitchActionOn"));
		}
		else if (OnAction.find("script://") != std::string::npos)
		{
			//Execute possible script
			if (OnAction.find("../") != std::string::npos)
			{
				_log.Log(LOG_ERROR, "SQLHelper: Invalid script location! '%s'", OnAction.c_str());
				return false;
			}

			std::string scriptname = OnAction.substr(9);
#if !defined WIN32
			if (scriptname.find("/") != 0)
				scriptname = szUserDataFolder + "scripts/" + scriptname;
#endif
			std::string scriptparams="";
			//Add parameters
			int pindex=scriptname.find(' ');
			if (pindex!=std::string::npos)
			{
				scriptparams=scriptname.substr(pindex+1);
				scriptname=scriptname.substr(0,pindex);
			}
			if (file_exist(scriptname.c_str()))
			{
				AddTaskItem(_tTaskItem::ExecuteScript(0.2f,scriptname,scriptparams));
			}
			else
				_log.Log(LOG_ERROR, "SQLHelper: Error script not found '%s'", scriptname.c_str());
		}
		return true;
	}

	//Off action
	if (OffAction.empty())
		return true;

	if ((OffAction.find("http://") != std::string::npos) || (OffAction.find("https://") != std::string::npos))
	{
		AddTaskItem(_tTaskItem::GetHTTPPage(0.2f, OffAction, "SwitchActionOff"));
	}
	else if (OffAction.find("script://") != std::string::npos)
	{
		//Execute possible script
		if (OffAction.find("../") != std::string::npos)
		{
			_log.Log(LOG_ERROR, "SQLHelper: Invalid script location! '%s'", OffAction.c_str());
			return false;
		}

		std::string scriptname = OffAction.substr(9);
#if !defined WIN32
		if (scriptname.find("/") != 0)
			scriptname = szUserDataFolder + "scripts/" + scriptname;
#endif
		std::string scriptparams = "";
		int pindex = scriptname.find(' ');
		if (pindex != std::string::npos)
		{
			scriptparams = scriptname.substr(pindex + 1);
			scriptname = scriptname.substr(0, pindex);
		}
		if (file_exist(scriptname.c_str()))
		{
			AddTaskItem(_tTaskItem::ExecuteScript(0.2f, scriptname, scriptparams));
		}
	}
	return true;
}

//Executed every hour
void CSQLHelper::CheckBatteryLow()
{
	int iBatteryLowLevel=0;
	GetPreferencesVar("BatteryLowNotification", iBatteryLowLevel);
	if (iBatteryLowLevel==0)
		return;//disabled

	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ID,Name, BatteryLevel FROM DeviceStatus WHERE (Used!=0 AND BatteryLevel<%d AND BatteryLevel!=255)", iBatteryLowLevel);
	if (result.size() < 1)
		return;

	time_t now = mytime(NULL);
	struct tm stoday;
	localtime_r(&now, &stoday);

	uint64_t ulID;
	std::vector<std::vector<std::string> >::const_iterator itt;

	//check if last batterylow_notification is not sent today and if true, send notification
	for (itt = result.begin(); itt != result.end(); ++itt)
	{
		std::vector<std::string> sd = *itt;
		std::stringstream s_str(sd[0]);
		s_str >> ulID;
		bool bDoSend = true;
		std::map<uint64_t, int>::const_iterator sitt;
		sitt = m_batterylowlastsend.find(ulID);
		if (sitt != m_batterylowlastsend.end())
		{
			bDoSend = (stoday.tm_mday != sitt->second);
		}
		if (bDoSend)
		{
			char szTmp[300];
			int batlevel = atoi(sd[2].c_str());
			if (batlevel==0)
				sprintf(szTmp, "Battery Low: %s (Level: Low)", sd[1].c_str());
			else
				sprintf(szTmp, "Battery Low: %s (Level: %d %%)", sd[1].c_str(), batlevel);
			m_notifications.SendMessageEx(0, std::string(""), NOTIFYALL, szTmp, szTmp, std::string(""), 1, std::string(""), true);
			m_batterylowlastsend[ulID] = stoday.tm_mday;
		}
	}
}

//Executed every hour
void CSQLHelper::CheckDeviceTimeout()
{
	int TimeoutCheckInterval=1;
	GetPreferencesVar("SensorTimeoutNotification", TimeoutCheckInterval);

	if (TimeoutCheckInterval==0)
		return;
	m_sensortimeoutcounter+=1;
	if (m_sensortimeoutcounter<TimeoutCheckInterval)
		return;
	m_sensortimeoutcounter=0;

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);
	time_t now = mytime(NULL);
	struct tm stoday;
	localtime_r(&now,&stoday);
	now-=(SensorTimeOut*60);
	struct tm ltime;
	localtime_r(&now,&ltime);

	std::vector<std::vector<std::string> > result;
	result = safe_query(
		"SELECT ID, Name, LastUpdate FROM DeviceStatus WHERE (Used!=0 AND LastUpdate<='%04d-%02d-%02d %02d:%02d:%02d' "
		"AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d "
		"AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d AND Type!=%d) "
		"ORDER BY Name",
		ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
		pTypeLighting1,
		pTypeLighting2,
		pTypeLighting3,
		pTypeLighting4,
		pTypeLighting5,
		pTypeLighting6,
		pTypeFan,
		pTypeRadiator1,
		pTypeColorSwitch,
		pTypeSecurity1,
		pTypeCurtain,
		pTypeBlinds,
		pTypeRFY,
		pTypeChime,
		pTypeThermostat2,
		pTypeThermostat3,
		pTypeThermostat4,
		pTypeRemote,
		pTypeGeneralSwitch,
		pTypeHomeConfort
		);
	if (result.size()<1)
		return;

	uint64_t ulID;
	std::vector<std::vector<std::string> >::const_iterator itt;

	//check if last timeout_notification is not sent today and if true, send notification
	for (itt=result.begin(); itt!=result.end(); ++itt)
	{
		std::vector<std::string> sd=*itt;
		std::stringstream s_str( sd[0] );
		s_str >> ulID;
		bool bDoSend=true;
		std::map<uint64_t,int>::const_iterator sitt;
		sitt=m_timeoutlastsend.find(ulID);
		if (sitt!=m_timeoutlastsend.end())
		{
			bDoSend=(stoday.tm_mday!=sitt->second);
		}
		if (bDoSend)
		{
			char szTmp[300];
			sprintf(szTmp,"Sensor Timeout: %s, Last Received: %s",sd[1].c_str(),sd[2].c_str());
			m_notifications.SendMessageEx(0, std::string(""), NOTIFYALL, szTmp, szTmp, std::string(""), 1, std::string(""), true);
			m_timeoutlastsend[ulID]=stoday.tm_mday;
		}
	}
}

void CSQLHelper::FixDaylightSavingTableSimple(const std::string &TableName)
{
	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT t.RowID, u.RowID, t.Date FROM %s as t, %s as u WHERE (t.[Date] == u.[Date]) AND (t.[DeviceRowID] == u.[DeviceRowID]) AND (t.[RowID] != u.[RowID]) ORDER BY t.[RowID]",
		TableName.c_str(),
		TableName.c_str());
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		std::stringstream sstr;
		unsigned long ID1;
		unsigned long ID2;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;
			sstr.clear();
			sstr.str("");
			sstr << sd[0];
			sstr >> ID1;
			sstr.clear();
			sstr.str("");
			sstr << sd[1];
			sstr >> ID2;
			if (ID2>ID1)
			{
				std::string szDate=sd[2];
				std::vector<std::vector<std::string> > result2;
				result2=safe_query("SELECT date('%q','+1 day')",
					szDate.c_str());

				std::string szDateNew=result2[0][0];

				//Check if Date+1 exists, if yes, remove current double value
				result2=safe_query("SELECT RowID FROM %s WHERE (Date='%q') AND (RowID=='%q')",
					TableName.c_str(), szDateNew.c_str(), sd[1].c_str());
				if (result2.size()>0)
				{
					//Delete row
					safe_query("DELETE FROM %s WHERE (RowID=='%q')", TableName.c_str(), sd[1].c_str());
				}
				else
				{
					//Update date
					safe_query("UPDATE %s SET Date='%q' WHERE (RowID=='%q')", TableName.c_str(), szDateNew.c_str(), sd[1].c_str());
				}
			}
		}
	}
}

void CSQLHelper::FixDaylightSaving()
{
	//First the easy tables
	FixDaylightSavingTableSimple("Fan_Calendar");
	FixDaylightSavingTableSimple("Percentage_Calendar");
	FixDaylightSavingTableSimple("Rain_Calendar");
	FixDaylightSavingTableSimple("Temperature_Calendar");
	FixDaylightSavingTableSimple("UV_Calendar");
	FixDaylightSavingTableSimple("Wind_Calendar");

	//Meter_Calendar
	std::vector<std::vector<std::string> > result;

	result=safe_query("SELECT t.RowID, u.RowID, t.Value, u.Value, t.Date from Meter_Calendar as t, Meter_Calendar as u WHERE (t.[Date] == u.[Date]) AND (t.[DeviceRowID] == u.[DeviceRowID]) AND (t.[RowID] != u.[RowID]) ORDER BY t.[RowID]");
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		std::stringstream sstr;
		unsigned long ID1;
		unsigned long ID2;
		unsigned long long Value1;
		unsigned long long Value2;
		unsigned long long ValueDest;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd1=*itt;

			sstr.clear();
			sstr.str("");
			sstr << sd1[0];
			sstr >> ID1;
			sstr.clear();
			sstr.str("");
			sstr << sd1[1];
			sstr >> ID2;
			sstr.clear();
			sstr.str("");
			sstr << sd1[2];
			sstr >> Value1;
			sstr.clear();
			sstr.str("");
			sstr << sd1[3];
			sstr >> Value2;
			if (ID2>ID1)
			{
				if (Value2>Value1)
					ValueDest=Value2-Value1;
				else
					ValueDest=Value2;

				std::string szDate=sd1[4];
				std::vector<std::vector<std::string> > result2;
				result2=safe_query("SELECT date('%q','+1 day')", szDate.c_str());

				std::string szDateNew=result2[0][0];

				//Check if Date+1 exists, if yes, remove current double value
				result2=safe_query("SELECT RowID FROM Meter_Calendar WHERE (Date='%q') AND (RowID=='%q')", szDateNew.c_str(), sd1[1].c_str());
				if (result2.size()>0)
				{
					//Delete Row
					safe_query("DELETE FROM Meter_Calendar WHERE (RowID=='%q')", sd1[1].c_str());
				}
				else
				{
					//Update row with new Date
					safe_query("UPDATE Meter_Calendar SET Date='%q', Value=%llu WHERE (RowID=='%q')", szDateNew.c_str(), ValueDest, sd1[1].c_str());
				}
			}
		}
	}

	//Last (but not least) MultiMeter_Calendar
	result=safe_query("SELECT t.RowID, u.RowID, t.Value1, t.Value2, t.Value3, t.Value4, t.Value5, t.Value6, u.Value1, u.Value2, u.Value3, u.Value4, u.Value5, u.Value6, t.Date from MultiMeter_Calendar as t, MultiMeter_Calendar as u WHERE (t.[Date] == u.[Date]) AND (t.[DeviceRowID] == u.[DeviceRowID]) AND (t.[RowID] != u.[RowID]) ORDER BY t.[RowID]");
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		std::stringstream sstr;
		unsigned long ID1;
		unsigned long ID2;
		unsigned long long tValue1;
		unsigned long long tValue2;
		unsigned long long tValue3;
		unsigned long long tValue4;
		unsigned long long tValue5;
		unsigned long long tValue6;

		unsigned long long uValue1;
		unsigned long long uValue2;
		unsigned long long uValue3;
		unsigned long long uValue4;
		unsigned long long uValue5;
		unsigned long long uValue6;

		unsigned long long ValueDest1;
		unsigned long long ValueDest2;
		unsigned long long ValueDest3;
		unsigned long long ValueDest4;
		unsigned long long ValueDest5;
		unsigned long long ValueDest6;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd1=*itt;

			sstr.clear();
			sstr.str("");
			sstr << sd1[0];
			sstr >> ID1;
			sstr.clear();
			sstr.str("");
			sstr << sd1[1];
			sstr >> ID2;

			sstr.clear();
			sstr.str("");
			sstr << sd1[2];
			sstr >> tValue1;
			sstr.clear();
			sstr.str("");
			sstr << sd1[3];
			sstr >> tValue2;
			sstr.clear();
			sstr.str("");
			sstr << sd1[4];
			sstr >> tValue3;
			sstr.clear();
			sstr.str("");
			sstr << sd1[5];
			sstr >> tValue4;
			sstr.clear();
			sstr.str("");
			sstr << sd1[6];
			sstr >> tValue5;
			sstr.clear();
			sstr.str("");
			sstr << sd1[7];
			sstr >> tValue6;

			sstr.clear();
			sstr.str("");
			sstr << sd1[8];
			sstr >> uValue1;
			sstr.clear();
			sstr.str("");
			sstr << sd1[9];
			sstr >> uValue2;
			sstr.clear();
			sstr.str("");
			sstr << sd1[10];
			sstr >> uValue3;
			sstr.clear();
			sstr.str("");
			sstr << sd1[11];
			sstr >> uValue4;
			sstr.clear();
			sstr.str("");
			sstr << sd1[12];
			sstr >> uValue5;
			sstr.clear();
			sstr.str("");
			sstr << sd1[13];
			sstr >> uValue6;

			if (ID2>ID1)
			{
				if (uValue1>tValue1)
					ValueDest1=uValue1-tValue1;
				else
					ValueDest1=uValue1;
				if (uValue2>tValue2)
					ValueDest2=uValue2-tValue2;
				else
					ValueDest2=uValue2;
				if (uValue3>tValue3)
					ValueDest3=uValue3-tValue3;
				else
					ValueDest3=uValue3;
				if (uValue4>tValue4)
					ValueDest4=uValue4-tValue4;
				else
					ValueDest4=uValue4;
				if (uValue5>tValue5)
					ValueDest5=uValue5-tValue5;
				else
					ValueDest5=uValue5;
				if (uValue6>tValue6)
					ValueDest6=uValue6-tValue6;
				else
					ValueDest6=uValue6;

				std::string szDate=sd1[14];
				std::vector<std::vector<std::string> > result2;
				result2=safe_query("SELECT date('%q','+1 day')", szDate.c_str());

				std::string szDateNew=result2[0][0];

				//Check if Date+1 exists, if yes, remove current double value
				result2=safe_query("SELECT RowID FROM MultiMeter_Calendar WHERE (Date='%q') AND (RowID=='%q')", szDateNew.c_str(), sd1[1].c_str());
				if (result2.size()>0)
				{
					//Delete Row
					safe_query("DELETE FROM MultiMeter_Calendar WHERE (RowID=='%q')", sd1[1].c_str());
				}
				else
				{
					//Update row with new Date
					safe_query("UPDATE MultiMeter_Calendar SET Date='%q', Value1=%llu, Value2=%llu, Value3=%llu, Value4=%llu, Value5=%llu, Value6=%llu WHERE (RowID=='%q')",
						szDateNew.c_str(), ValueDest1, ValueDest2, ValueDest3, ValueDest4, ValueDest5, ValueDest6, sd1[1].c_str());
				}
			}
		}
	}

}

std::string CSQLHelper::DeleteUserVariable(const std::string &idx)
{
	safe_query("DELETE FROM UserVariables WHERE (ID=='%q')", idx.c_str());
	if (m_bEnableEventSystem)
	{
		m_mainworker.m_eventsystem.GetCurrentUserVariables();
	}

	return "OK";

}

std::string CSQLHelper::SaveUserVariable(const std::string &varname, const std::string &vartype, const std::string &varvalue)
{
	int typei = atoi(vartype.c_str());
	std::string dupeName = CheckUserVariableName(varname);
	if (dupeName != "OK")
		return dupeName;

	std::string formatError = CheckUserVariable(typei, varvalue);
	if (formatError != "OK")
		return formatError;

	std::string szVarValue = CURLEncode::URLDecode(varvalue.c_str());
	std::vector<std::vector<std::string> > result;
	safe_query("INSERT INTO UserVariables (Name,ValueType,Value) VALUES ('%q','%d','%q')",
		varname.c_str(),
		typei,
		szVarValue.c_str()
		);

	if (m_bEnableEventSystem)
	{
		m_mainworker.m_eventsystem.GetCurrentUserVariables();
		result = safe_query("SELECT ID, LastUpdate FROM UserVariables WHERE (Name == '%q')",
			varname.c_str()
		);
		if (result.size()>0)
		{
			std::vector<std::string> sd = result[0];
			std::stringstream vId_str(sd[0]);
			uint64_t vId;
			vId_str >> vId;
			m_mainworker.m_eventsystem.SetEventTrigger(vId, m_mainworker.m_eventsystem.REASON_USERVARIABLE, 0);
			m_mainworker.m_eventsystem.UpdateUserVariable(vId, "", szVarValue, typei, sd[1]);
		}
	}
	return "OK";
}

std::string CSQLHelper::UpdateUserVariable(const std::string &idx, const std::string &varname, const std::string &vartype, const std::string &varvalue, const bool eventtrigger)
{
	int typei = atoi(vartype.c_str());
	std::string formatError = CheckUserVariable(typei, varvalue);
	if (formatError != "OK")
		return formatError;

	/*
	std::vector<std::vector<std::string> > result;
	sprintf(szTmp, "SELECT Value FROM UserVariables WHERE (Name == '%s')",
		varname.c_str()
		);
	result = query(szTmp);
	if (result.size()>0)
	{
		std::vector<std::string> sd = result[0];
		if (varvalue == sd[0])
			return "New value same as current, not updating";
	}
	*/
	std::string szLastUpdate = TimeToString(NULL, TF_DateTime);
	std::string szVarValue = CURLEncode::URLDecode(varvalue.c_str());
	safe_query(
		"UPDATE UserVariables SET Name='%q', ValueType='%d', Value='%q', LastUpdate='%q' WHERE (ID == '%q')",
		varname.c_str(),
		typei,
		szVarValue.c_str(),
		szLastUpdate.c_str(),
		idx.c_str()
		);
	if (m_bEnableEventSystem)
	{
		std::stringstream vId_str(idx);
		uint64_t vId;
		vId_str >> vId;
		if (eventtrigger)
			m_mainworker.m_eventsystem.SetEventTrigger(vId, m_mainworker.m_eventsystem.REASON_USERVARIABLE, 0);
		m_mainworker.m_eventsystem.UpdateUserVariable(vId, varname, szVarValue, typei, szLastUpdate);
	}
	return "OK";
}

bool CSQLHelper::SetUserVariable(const uint64_t idx, const std::string &varvalue, const bool eventtrigger)
{
	std::string szLastUpdate = TimeToString(NULL, TF_DateTime);
	std::string szVarValue = CURLEncode::URLDecode(varvalue.c_str());
	safe_query(
		"UPDATE UserVariables SET Value='%q', LastUpdate='%q' WHERE (ID == %" PRIu64 ")",
		szVarValue.c_str(),
		szLastUpdate.c_str(),
		idx
		);
	if (m_bEnableEventSystem)
	{
		if (eventtrigger)
			m_mainworker.m_eventsystem.SetEventTrigger(idx, m_mainworker.m_eventsystem.REASON_USERVARIABLE, 0);
		m_mainworker.m_eventsystem.UpdateUserVariable(idx, "", szVarValue, -1, szLastUpdate);
	}
	return true;
}

std::string CSQLHelper::CheckUserVariableName(const std::string &varname)
{
	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT Name FROM UserVariables WHERE (Name=='%q')",
		varname.c_str());
	if (result.size() > 0)
	{
		return "Variable name already exists!";
	}
	return "OK";
}


std::string CSQLHelper::CheckUserVariable(const int vartype, const std::string &varvalue)
{

	if (varvalue.size() > 200) {
		return "String exceeds maximum size";
	}
	if (vartype == 0) {
		//integer
		std::istringstream iss(varvalue);
		int i;
		iss >> std::noskipws >> i;
		if (!(iss.eof() && !iss.fail()))
		{
			return "Not a valid integer";
		}
	}
	else if (vartype == 1) {
		//float
		std::istringstream iss(varvalue);
		float f;
		iss >> std::noskipws >> f;
		if (!(iss.eof() && !iss.fail()))
		{
			return "Not a valid float";
		}
	}
	else if (vartype == 3) {
		//date
		int d, m, y;
		if (!CheckDate(varvalue, d, m, y))
		{
			return "Not a valid date notation (DD/MM/YYYY)";
		}
	}
	else if (vartype == 4) {
		//time
		if (!CheckTime(varvalue))
			return "Not a valid time notation (HH:MM)";
	}
	else if (vartype == 5) {
		return "OK";
	}
	return "OK";
}


std::vector<std::vector<std::string> > CSQLHelper::GetUserVariables()
{
	return safe_query("SELECT ID,Name,ValueType,Value,LastUpdate FROM UserVariables");
}

bool CSQLHelper::CheckDate(const std::string &sDate, int& d, int& m, int& y)
{
	std::istringstream is(sDate);
	char delimiter;
	if (is >> d >> delimiter >> m >> delimiter >> y) {
		struct tm t = { 0 };
		t.tm_mday = d;
		t.tm_mon = m - 1;
		t.tm_year = y - 1900;
		t.tm_isdst = -1;

		time_t when = mktime(&t);
		struct tm norm;
		localtime_r(&when, &norm);

		return (norm.tm_mday == d    &&
			norm.tm_mon == m - 1 &&
			norm.tm_year == y - 1900);
	}
	return false;
}

bool CSQLHelper::CheckTime(const std::string &sTime)
{

	int iSemiColon = sTime.find(':');
	if ((iSemiColon == std::string::npos) || (iSemiColon < 1) || (iSemiColon > 2) || (iSemiColon == sTime.length()-1)) return false;
	if ((sTime.length() < 3) || (sTime.length() > 5)) return false;
	if (atoi(sTime.substr(0, iSemiColon).c_str()) >= 24) return false;
	if (atoi(sTime.substr(iSemiColon + 1).c_str()) >= 60) return false;
	return true;
}

void CSQLHelper::AllowNewHardwareTimer(const int iTotMinutes)
{
	m_iAcceptHardwareTimerCounter = iTotMinutes * 60.0f;
	if (m_bAcceptHardwareTimerActive == false)
	{
		m_bPreviousAcceptNewHardware = m_bAcceptNewHardware;
	}
	m_bAcceptNewHardware = true;
	m_bAcceptHardwareTimerActive = true;
	_log.Log(LOG_STATUS, "New sensors allowed for %d minutes...", iTotMinutes);
}

std::string CSQLHelper::GetDeviceValue(const char * FieldName , const char *Idx )
{
	TSqlQueryResult result = safe_query("SELECT %s from DeviceStatus WHERE (ID == %s )",FieldName, Idx );
  if (result.size()>0)
	  return  result[0][0];
  else
	  return  "";
}

//return temperature value from Svalue : is code temperature;humidity;???
float CSQLHelper::getTemperatureFromSValue(const char * sValue)
{
	std::vector<std::string> splitresults;
	StringSplit(sValue, ";", splitresults);
	if (splitresults.size()<1)
      return 0;
    else
      return (float)atof(splitresults[0].c_str());
}
void LogRow (TSqlRowQuery * row)
{
		std::string Row;
		for (unsigned int j=0;j<(*row).size();j++)
			Row = Row+(*row)[j]+";";
    _log.Log(LOG_TRACE,"SQLR result: %s",Row.c_str());
}
void CSQLHelper::LogQueryResult (TSqlQueryResult &result)
{
	for (unsigned int i=0;i<result.size();i++)
	{
		LogRow( &result[i] );
	}
}
bool CSQLHelper::InsertCustomIconFromZip(const std::string &szZip, std::string &ErrorMessage)
{
	//write file to disk
#ifdef WIN32
	std::string outputfile = "custom_icons.zip";
#else
	std::string outputfile = "/tmp/custom_icons.zip";
#endif
	std::ofstream outfile;
	outfile.open(outputfile.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outfile.is_open())
	{
		ErrorMessage = "Error writing zip to disk";
		return false;
	}
	outfile << szZip;
	outfile.flush();
	outfile.close();

	return InsertCustomIconFromZipFile(outputfile, ErrorMessage);
}

bool CSQLHelper::InsertCustomIconFromZipFile(const std::string &szZipFile, std::string &ErrorMessage)
{
	clx::basic_unzip<char> in(szZipFile);
	if (!in.is_open())
	{
		ErrorMessage = "Error opening zip file";
		return false;
	}

	int iTotalAdded = 0;

	for (clx::unzip::iterator pos = in.begin(); pos != in.end(); ++pos) {
		//_log.Log(LOG_STATUS, "unzip: %s", pos->path().c_str());
		std::string fpath = pos->path();

		//Skip strange folders
		if (fpath.find("__MACOSX") != std::string::npos)
			continue;

		int ipos = fpath.find("icons.txt");
		if (ipos != std::string::npos)
		{
			std::string rpath;
			if (ipos > 0)
				rpath = fpath.substr(0, ipos);

			uLong fsize;
			unsigned char *pFBuf = (unsigned char *)(pos).Extract(fsize, 1);
			if (pFBuf == NULL)
			{
				ErrorMessage = "Could not extract icons.txt";
				return false;
			}
			pFBuf[fsize] = 0; //null terminate

			std::string _defFile = std::string(pFBuf, pFBuf + fsize);
			free(pFBuf);

			std::vector<std::string> _Lines;
			StringSplit(_defFile, "\n", _Lines);
			std::vector<std::string>::const_iterator itt;

			for (itt = _Lines.begin(); itt != _Lines.end(); ++itt)
			{
				std::string sLine = (*itt);
				sLine.erase(std::remove(sLine.begin(), sLine.end(), '\r'), sLine.end());
				std::vector<std::string> splitresult;
				StringSplit(sLine, ";", splitresult);
				if (splitresult.size() == 3)
				{
					std::string IconBase = splitresult[0];
					std::string IconName = splitresult[1];
					std::string IconDesc = splitresult[2];

					//Check if this Icon(Name) does not exist in the database already
					std::vector<std::vector<std::string> > result;
					result = safe_query("SELECT ID FROM CustomImages WHERE Base='%q'", IconBase.c_str());
					bool bIsDuplicate = (result.size() > 0);
					int RowID = 0;
					if (bIsDuplicate)
					{
						RowID = atoi(result[0][0].c_str());
					}

					//Locate the files in the zip, if not present back out
					std::string IconFile16 = IconBase + ".png";
					std::string IconFile48On = IconBase + "48_On.png";
					std::string IconFile48Off = IconBase + "48_Off.png";

					std::map<std::string, std::string> _dbImageFiles;
					_dbImageFiles["IconSmall"] = IconFile16;
					_dbImageFiles["IconOn"] = IconFile48On;
					_dbImageFiles["IconOff"] = IconFile48Off;

					//Check if all icons are there
					std::map<std::string, std::string>::const_iterator iItt;
					for (iItt = _dbImageFiles.begin(); iItt != _dbImageFiles.end(); ++iItt)
					{
						//std::string TableField = iItt->first;
						std::string IconFile = rpath + iItt->second;
						if (in.find(IconFile) == in.end())
						{
							ErrorMessage = "Icon File: " + IconFile + " is not present";
							if (iTotalAdded > 0)
							{
								m_webservers.ReloadCustomSwitchIcons();
							}
							return false;
						}
					}

					//All good, now lets add it to the database
					if (!bIsDuplicate)
					{
						safe_query("INSERT INTO CustomImages (Base,Name, Description) VALUES ('%q', '%q', '%q')",
							IconBase.c_str(), IconName.c_str(), IconDesc.c_str());

						//Get our Database ROWID
						result = safe_query("SELECT ID FROM CustomImages WHERE Base='%q'", IconBase.c_str());
						if (result.size() == 0)
						{
							ErrorMessage = "Error adding new row to database!";
							if (iTotalAdded > 0)
							{
								m_webservers.ReloadCustomSwitchIcons();
							}
							return false;
						}
						RowID = atoi(result[0][0].c_str());
					}
					else
					{
						//Update
						safe_query("UPDATE CustomImages SET Name='%q', Description='%q' WHERE ID=%d",
							IconName.c_str(), IconDesc.c_str(), RowID);

						//Delete from disk, so it will be updated when we exit this function
						std::string IconFile16 = szWWWFolder + "/images/" + IconBase + ".png";
						std::string IconFile48On = szWWWFolder + "/images/" + IconBase + "48_On.png";
						std::string IconFile48Off = szWWWFolder + "/images/" + IconBase + "48_Off.png";
						std::remove(IconFile16.c_str());
						std::remove(IconFile48On.c_str());
						std::remove(IconFile48Off.c_str());
					}

					//Insert the Icons

					for (iItt = _dbImageFiles.begin(); iItt != _dbImageFiles.end(); ++iItt)
					{
						std::string TableField = iItt->first;
						std::string IconFile = rpath + iItt->second;

						sqlite3_stmt *stmt = NULL;
						char *zQuery = sqlite3_mprintf("UPDATE CustomImages SET %s = ? WHERE ID=%d", TableField.c_str(), RowID);
						if (!zQuery)
						{
							_log.Log(LOG_ERROR, "SQL: Out of memory, or invalid printf!....");
							return false;
						}
						int rc = sqlite3_prepare_v2(m_dbase, zQuery, -1, &stmt, NULL);
						sqlite3_free(zQuery);
						if (rc != SQLITE_OK) {
							ErrorMessage = "Problem inserting icon into database! " + std::string(sqlite3_errmsg(m_dbase));
							if (iTotalAdded > 0)
							{
								m_webservers.ReloadCustomSwitchIcons();
							}
							return false;
						}
						// SQLITE_STATIC because the statement is finalized
						// before the buffer is freed:
						pFBuf = (unsigned char *)in.find(IconFile).Extract(fsize);
						if (pFBuf == NULL)
						{
							ErrorMessage = "Could not extract File: " + IconFile16;
							if (iTotalAdded > 0)
							{
								m_webservers.ReloadCustomSwitchIcons();
							}
							return false;
						}
						rc = sqlite3_bind_blob(stmt, 1, pFBuf, fsize, SQLITE_STATIC);
						if (rc != SQLITE_OK) {
							ErrorMessage = "Problem inserting icon into database! " + std::string(sqlite3_errmsg(m_dbase));
							free(pFBuf);
							if (iTotalAdded > 0)
							{
								m_webservers.ReloadCustomSwitchIcons();
							}
							return false;
						}
						else {
							rc = sqlite3_step(stmt);
							if (rc != SQLITE_DONE)
							{
								free(pFBuf);
								ErrorMessage = "Problem inserting icon into database! " + std::string(sqlite3_errmsg(m_dbase));
								if (iTotalAdded > 0)
								{
									m_webservers.ReloadCustomSwitchIcons();
								}
								return false;
							}
						}
						sqlite3_finalize(stmt);
						free(pFBuf);
						iTotalAdded++;
					}
				}
			}

		}
	}

	if (iTotalAdded == 0)
	{
		//definition file not found
		ErrorMessage = "No Icon definition file not found";
		return false;
	}

	m_webservers.ReloadCustomSwitchIcons();
	return true;
}

std::map<std::string, std::string> CSQLHelper::BuildDeviceOptions(const std::string & options, const bool decode) {
	std::map<std::string, std::string> optionsMap;
	if (!options.empty()) {
		//_log.Log(LOG_STATUS, "DEBUG : Build device options from '%s'...", options.c_str());
		std::vector<std::string> optionsArray;
		StringSplit(options, ";", optionsArray);
		std::vector<std::string>::iterator itt;
		for (itt=optionsArray.begin(); itt!=optionsArray.end(); ++itt) {
			std::string oValue = *itt;
			if (oValue.empty()) {
				continue;
			}
			size_t tpos = oValue.find_first_of(':');
			if ((tpos != std::string::npos)&&(oValue.size()>tpos+1))
			{
				std::string optionName = oValue.substr(0, tpos);
				oValue = oValue.substr(tpos + 1);
				std::string optionValue = decode ? base64_decode(oValue.c_str()) : oValue;
				//_log.Log(LOG_STATUS, "DEBUG : Build device option ['%s': '%s'] => ['%s': '%s']", optionArray[0].c_str(), optionArray[1].c_str(), optionName.c_str(), optionValue.c_str());
				optionsMap.insert(std::pair<std::string, std::string>(optionName, optionValue));
			}
		}
	}
	//_log.Log(LOG_STATUS, "DEBUG : Build %d device(s) option(s)", optionsMap.size());
	return optionsMap;
}

std::map<std::string, std::string> CSQLHelper::GetDeviceOptions(const std::string & idx) {
	std::map<std::string, std::string> optionsMap;

	if (idx.empty()) {
		_log.Log(LOG_ERROR, "Cannot set options on device %s", idx.c_str());
		return optionsMap;
	}

	uint64_t ulID;
	std::stringstream s_str(idx);
	s_str >> ulID;
	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT Options FROM DeviceStatus WHERE (ID==%" PRIu64 ")", ulID);
	if (result.size() > 0) {
		std::vector<std::string> sd = result[0];
		optionsMap = BuildDeviceOptions(sd[0].c_str());
	}
	return optionsMap;
}

std::string CSQLHelper::FormatDeviceOptions(const std::map<std::string, std::string> & optionsMap) {
	std::string options;
	int count = optionsMap.size();
	if (count > 0) {
		int i = 0;
		std::stringstream ssoptions;
		std::map<std::string, std::string>::const_iterator itt;
		for (itt = optionsMap.begin(); itt != optionsMap.end(); ++itt)
		{
			i++;
			//_log.Log(LOG_STATUS, "DEBUG : Reading device option ['%s', '%s']", itt->first.c_str(), itt->second.c_str());
			std::string optionName = itt->first.c_str();
			std::string optionValue = base64_encode((const unsigned char*)itt->second.c_str(), itt->second.size());
			ssoptions << optionName << ":" << optionValue;
			if (i < count) {
				ssoptions << ";";
			}
		}
		options.assign(ssoptions.str());
	}

	return options;
}

bool CSQLHelper::SetDeviceOptions(const uint64_t idx, const std::map<std::string, std::string> & optionsMap) {
	if (idx < 1) {
		_log.Log(LOG_ERROR, "Cannot set options on device %" PRIu64 "", idx);
		return false;
	}

	if (optionsMap.empty()) {
		//_log.Log(LOG_STATUS, "DEBUG : removing options on device %" PRIu64 "", idx);
		safe_query("UPDATE DeviceStatus SET Options = null WHERE (ID==%" PRIu64 ")", idx);
	} else {
		std::string options = FormatDeviceOptions(optionsMap);
		if (options.empty()) {
			_log.Log(LOG_ERROR, "Cannot parse options for device %" PRIu64 "", idx);
			return false;
		}
		//_log.Log(LOG_STATUS, "DEBUG : setting options '%s' on device %" PRIu64 "", options.c_str(), idx);
		safe_query("UPDATE DeviceStatus SET Options = '%q' WHERE (ID==%" PRIu64 ")", options.c_str(), idx);
	}
	return true;
}
