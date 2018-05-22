#include "stdafx.h"
#include "../SQLHelper.h"
#include "../localtime_r.h"
#include "../Logger.h"
#include "../../webserver/Base64.h"
#include "../mainworker.h"
#include "../../notifications/NotificationHelper.h"
#include <boost/lexical_cast.hpp>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#ifdef ENABLE_PYTHON
#include "../../hardware/plugins/Plugins.h"
#endif

extern std::string szUserDataFolder;

void CSQLHelper::UpdatePreferencesVar(const std::string &Key, const std::string &sValue)
{
	UpdatePreferencesVar(Key, 0, sValue);
}
void CSQLHelper::UpdatePreferencesVar(const std::string &Key, const double Value)
{
	std::string sValue = boost::to_string(Value);
	UpdatePreferencesVar(Key, 0, sValue);
}

void CSQLHelper::UpdatePreferencesVar(const std::string &Key, const int nValue)
{
	UpdatePreferencesVar(Key, nValue, "");
}

void CSQLHelper::UpdatePreferencesVar(const std::string &Key, const int nValue, const std::string &sValue)
{
	if (!m_dbase)
		return;

	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ROWID FROM Preferences WHERE (Key='%q')",
		Key.c_str());
	if (result.size() == 0)
	{
		//Insert
		result = safe_query("INSERT INTO Preferences (Key, nValue, sValue) VALUES ('%q', %d,'%q')",
			Key.c_str(), nValue, sValue.c_str());
	}
	else
	{
		//Update
		result = safe_query("UPDATE Preferences SET Key='%q', nValue=%d, sValue='%q' WHERE (ROWID = '%q')",
			Key.c_str(), nValue, sValue.c_str(), result[0][0].c_str());
	}
}

void CSQLHelper::UpdateDeviceValue(const char * FieldName , std::string &Value , std::string &Idx )
{
	safe_query("UPDATE DeviceStatus SET %s='%s' , LastUpdate='%s' WHERE (ID == %s )", FieldName, Value.c_str(), TimeToString(NULL, TF_DateTime).c_str(), Idx.c_str());
}

void CSQLHelper::UpdateDeviceValue(const char * FieldName , int Value , std::string &Idx )
{
	safe_query("UPDATE DeviceStatus SET %s=%d , LastUpdate='%s' WHERE (ID == %s )", FieldName, Value, TimeToString(NULL, TF_DateTime).c_str(),Idx.c_str());
}

void CSQLHelper::UpdateDeviceValue(const char * FieldName , float Value , std::string &Idx )
{
	safe_query("UPDATE DeviceStatus SET %s=%4.2f , LastUpdate='%s' WHERE (ID == %s )", FieldName, Value, TimeToString(NULL, TF_DateTime).c_str(),Idx.c_str());
}

uint64_t CSQLHelper::UpdateValue(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, const unsigned char signallevel, const unsigned char batterylevel, const int nValue, std::string &devname, const bool bUseOnOffAction)
{
	return UpdateValue(HardwareID, ID, unit, devType, subType, signallevel, batterylevel, nValue, "", devname, bUseOnOffAction);
}

uint64_t CSQLHelper::UpdateValue(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, const unsigned char signallevel, const unsigned char batterylevel, const char* sValue, std::string &devname, const bool bUseOnOffAction)
{
	return UpdateValue(HardwareID, ID, unit, devType, subType, signallevel, batterylevel, 0, sValue, devname, bUseOnOffAction);
}

uint64_t CSQLHelper::UpdateValue(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, const unsigned char signallevel, const unsigned char batterylevel, const int nValue, const char* sValue, std::string &devname, const bool bUseOnOffAction)
{
	uint64_t devRowID=UpdateValueInt(HardwareID, ID, unit, devType, subType, signallevel, batterylevel, nValue, sValue,devname,bUseOnOffAction);
	if (devRowID == -1)
		return -1;

	if (!IsLightOrSwitch(devType, subType))
	{
		return devRowID;
	}

	//Get the ID of this device
	std::vector<std::vector<std::string> > result,result2;
	std::vector<std::vector<std::string> >::const_iterator itt,itt2;

	result=safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",HardwareID, ID, unit, devType, subType);
	if (result.size()==0)
		return devRowID; //should never happen, because it was previously inserted if non-existent

	std::string idx=result[0][0];

	time_t now = time(0);
	struct tm ltime;
	localtime_r(&now,&ltime);

	//Check if this switch was a Sub/Slave device for other devices, if so adjust the state of those other devices
	result = safe_query("SELECT A.ParentID, B.Name, B.HardwareID, B.[Type], B.[SubType], B.Unit FROM LightSubDevices as A, DeviceStatus as B WHERE (A.DeviceRowID=='%q') AND (A.DeviceRowID!=A.ParentID) AND (B.[ID] == A.ParentID)", idx.c_str());
	if (result.size()>0)
	{
		//This is a sub/slave device for another main device
		//Set the Main Device state to the same state as this device
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;
			safe_query(
				"UPDATE DeviceStatus SET nValue=%d, sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (ID == '%q')",
				nValue,
				sValue,
				ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
				sd[0].c_str()
				);

			//Call the EventSystem for the main switch
			uint64_t ParentID = (uint64_t)atoll(sd[0].c_str());
			std::string ParentName = sd[1];
			int ParentHardwareID = atoi(sd[2].c_str());
			unsigned char ParentType = (unsigned char)atoi(sd[3].c_str());
			unsigned char ParentSubType = (unsigned char)atoi(sd[4].c_str());
			unsigned char ParentUnit = (unsigned char)atoi(sd[5].c_str());
			m_mainworker.m_eventsystem.ProcessDevice(ParentHardwareID, ParentID, ParentUnit, ParentType, ParentSubType, signallevel, batterylevel, nValue, sValue, ParentName, 0);

			//Set the status of all slave devices from this device (except the one we just received) to off
			//Check if this switch was a Sub/Slave device for other devices, if so adjust the state of those other devices
			result2 = safe_query(
				"SELECT a.DeviceRowID, b.Type FROM LightSubDevices a, DeviceStatus b WHERE (a.ParentID=='%q') AND (a.DeviceRowID!='%q') AND (b.ID == a.DeviceRowID) AND (a.DeviceRowID!=a.ParentID)",
				sd[0].c_str(),
				idx.c_str()
				);
			if (result2.size()>0)
			{
				for (itt2=result2.begin(); itt2!=result2.end(); ++itt2)
				{
					std::vector<std::string> sd=*itt2;
					int oDevType=atoi(sd[1].c_str());
					int newnValue=0;
					switch (oDevType)
					{
					case pTypeLighting1:
						newnValue=light1_sOff;
						break;
					case pTypeLighting2:
						newnValue=light2_sOff;
						break;
					case pTypeLighting3:
						newnValue=light3_sOff;
						break;
					case pTypeLighting4:
						newnValue=0;//light4_sOff;
						break;
					case pTypeLighting5:
						newnValue=light5_sOff;
						break;
					case pTypeLighting6:
						newnValue=light6_sOff;
						break;
					case pTypeColorSwitch:
						newnValue=Color_LedOff;
						break;
					case pTypeSecurity1:
						newnValue=sStatusNormal;
						break;
					case pTypeSecurity2:
						newnValue = 0;// sStatusNormal;
						break;
					case pTypeCurtain:
						newnValue=curtain_sOpen;
						break;
					case pTypeBlinds:
						newnValue=blinds_sOpen;
						break;
					case pTypeRFY:
						newnValue=rfy_sUp;
						break;
					case pTypeThermostat2:
						newnValue = thermostat2_sOff;
						break;
					case pTypeThermostat3:
						newnValue=thermostat3_sOff;
						break;
					case pTypeThermostat4:
						newnValue = thermostat4_sOff;
						break;
					case pTypeRadiator1:
						newnValue = Radiator1_sNight;
						break;
					case pTypeGeneralSwitch:
						newnValue = gswitch_sOff;
						break;
					case pTypeHomeConfort:
						newnValue = HomeConfort_sOff;
						break;
					default:
						continue;
					}
					safe_query(
						"UPDATE DeviceStatus SET nValue=%d, sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (ID == '%q')",
						newnValue,
						"",
						ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
						sd[0].c_str()
						);
				}
			}
			// TODO: Should plugin be notified?
		}
	}

	//If this is a 'Main' device, and it has Sub/Slave devices,
	//set the status of the Sub/Slave devices to Off, as we might be out of sync then
	result = safe_query(
		"SELECT a.DeviceRowID, b.Type FROM LightSubDevices a, DeviceStatus b WHERE (a.ParentID=='%q') AND (b.ID == a.DeviceRowID) AND (a.DeviceRowID!=a.ParentID)",
		idx.c_str()
		);
	if (result.size()>0)
	{
		//set the status to off
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;
			int oDevType=atoi(sd[1].c_str());
			int newnValue=0;
			switch (oDevType)
			{
			case pTypeLighting1:
				newnValue=light1_sOff;
				break;
			case pTypeLighting2:
				newnValue=light2_sOff;
				break;
			case pTypeLighting3:
				newnValue=light3_sOff;
				break;
			case pTypeLighting4:
				newnValue=0;//light4_sOff;
				break;
			case pTypeLighting5:
				newnValue=light5_sOff;
				break;
			case pTypeLighting6:
				newnValue=light6_sOff;
				break;
			case pTypeColorSwitch:
				newnValue=Color_LedOff;
				break;
			case pTypeSecurity1:
				newnValue=sStatusNormal;
				break;
			case pTypeSecurity2:
				newnValue = 0;// sStatusNormal;
				break;
			case pTypeCurtain:
				newnValue=curtain_sOpen;
				break;
			case pTypeBlinds:
				newnValue=blinds_sOpen;
				break;
			case pTypeRFY:
				newnValue=rfy_sUp;
				break;
			case pTypeThermostat2:
				newnValue = thermostat2_sOff;
				break;
			case pTypeThermostat3:
				newnValue=thermostat3_sOff;
				break;
			case pTypeThermostat4:
				newnValue = thermostat4_sOff;
				break;
			case pTypeRadiator1:
				newnValue = Radiator1_sNight;
				break;
			case pTypeGeneralSwitch:
				newnValue = gswitch_sOff;
				break;
			case pTypeHomeConfort:
				newnValue = HomeConfort_sOff;
				break;
			default:
				continue;
			}
			safe_query(
				"UPDATE DeviceStatus SET nValue=%d, sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (ID == '%q')",
				newnValue,
				"",
				ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
				sd[0].c_str()
				);
		}
		// TODO: Should plugin be notified?
	}
	return devRowID;
}

uint64_t CSQLHelper::UpdateValueInt(const int HardwareID, const char* ID, const unsigned char unit, const unsigned char devType, const unsigned char subType, const unsigned char signallevel, const unsigned char batterylevel, const int nValue, const char* sValue, std::string &devname, const bool bUseOnOffAction)
		//TODO: 'unsigned char unit' only allows 256 devices / plugin
		//TODO: return -1 as error code does not make sense for a function returning an unsigned value
{
	if (!m_dbase)
		return -1;

	uint64_t ulID=0;
	bool bDeviceUsed = false;
	bool bSameDeviceStatusValue = false;
	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ID,Name, Used, SwitchType, nValue, sValue, LastUpdate, Options FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",HardwareID, ID, unit, devType, subType);
	if (result.size()==0)
	{
		//Insert
		ulID = InsertDevice(HardwareID, ID, unit, devType, subType,	0, nValue, sValue, devname, signallevel, batterylevel);

		if (ulID < 1)
			return -1;

#ifdef ENABLE_PYTHON
		//TODO: Plugins should perhaps be blocked from implicitly adding a device by update? It's most likely a bug due to updating a removed device..
		CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(HardwareID);
		if (pHardware != NULL && pHardware->HwdType == HTYPE_PythonPlugin)
		{
			_log.Log(LOG_TRACE, "CSQLHelper::UpdateValueInt: Notifying plugin %u about creation of device %u", HardwareID, unit);
			Plugins::CPlugin *pPlugin = (Plugins::CPlugin*)pHardware;
			pPlugin->DeviceAdded(unit);
		}
#endif
	}
	else
	{
		//Update
		std::stringstream s_str( result[0][0] );
		s_str >> ulID;
		std::string sOption=result[0][7];
		devname=result[0][1];
		bDeviceUsed= atoi(result[0][2].c_str())!=0;
		_eSwitchType stype = (_eSwitchType)atoi(result[0][3].c_str());
		int old_nValue = atoi(result[0][4].c_str());
		std::string old_sValue = result[0][5];
		time_t now = time(0);
		struct tm ltime;
		localtime_r(&now,&ltime);
		//Commit: If Option 1: energy is computed as usage*time
		//Default is option 0, read from device
		if (sOption == "1" && devType == pTypeGeneral && subType == sTypeKwh)
		{
			std::vector<std::string> parts;
			struct tm ntime;
			double interval;
			float nEnergy;
			char sCompValue[100];
			std::string sLastUpdate = result[0][6];
			time_t lutime;
			ParseSQLdatetime(lutime, ntime, sLastUpdate, ltime.tm_isdst);

			interval = difftime(now,lutime);
			StringSplit(result[0][5].c_str(), ";", parts);
			nEnergy = static_cast<float>(strtof(parts[0].c_str(), NULL)*interval / 3600 + strtof(parts[1].c_str(), NULL)); //Rob: whats happening here... strtof ?
			StringSplit(sValue, ";", parts);
			sprintf(sCompValue, "%s;%.1f", parts[0].c_str(), nEnergy);
			sValue = sCompValue;
		}
		//~ use different update queries based on the device type
		if (devType == pTypeGeneral && subType == sTypeCounterIncremental)
		{
			result = safe_query(
				"UPDATE DeviceStatus SET SignalLevel=%d, BatteryLevel=%d, nValue= nValue + %d, sValue= sValue + '%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' "
				"WHERE (ID = %" PRIu64 ")",
				signallevel,batterylevel,
				nValue,sValue,
				ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
				ulID);
		}
		else
		{
			if (
				(stype == STYPE_DoorContact) ||
				(stype == STYPE_DoorLock) ||
				(stype == STYPE_DoorLockInverted) ||
				(stype == STYPE_Contact)
				)
			{
				//Check if we received the same state as before, if yes, don't do anything (only update)
				//This is specially handy for devices that send a keep-alive status every xx minutes
				//like professional alarm system equipment
				//.. we should make this an option of course
				bSameDeviceStatusValue = (
					(nValue == old_nValue) &&
					(sValue == old_sValue)
					);
			}

			result = safe_query(
				"UPDATE DeviceStatus SET SignalLevel=%d, BatteryLevel=%d, nValue=%d, sValue='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' "
				"WHERE (ID = %" PRIu64 ")",
				signallevel, batterylevel,
				nValue, sValue,
				ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
				ulID);
		}
	}

	if (bSameDeviceStatusValue)
		return ulID; //status has not changed, no need to process further

	switch (devType)
	{
	case pTypeRego6XXValue:
        if(subType != sTypeRego6XXStatus)
        {
            break;
        }
	case pTypeGeneral:
		if ((devType == pTypeGeneral) && (subType != sTypeTextStatus) && (subType != sTypeAlert))
		{
			break;
		}
	case pTypeLighting1:
	case pTypeLighting2:
	case pTypeLighting3:
	case pTypeLighting4:
	case pTypeLighting5:
	case pTypeLighting6:
	case pTypeColorSwitch:
	case pTypeSecurity1:
	case pTypeSecurity2:
	case pTypeEvohome:
	case pTypeEvohomeRelay:
	case pTypeCurtain:
	case pTypeBlinds:
	case pTypeFan:
	case pTypeRFY:
	case pTypeChime:
	case pTypeThermostat2:
	case pTypeThermostat3:
	case pTypeThermostat4:
	case pTypeRemote:
	case pTypeGeneralSwitch:
	case pTypeHomeConfort:
	case pTypeRadiator1:
		if ((devType == pTypeRadiator1) && (subType != sTypeSmartwaresSwitchRadiator))
			break;
		//Add Lighting log
		m_LastSwitchID=ID;
		m_LastSwitchRowID=ulID;
		result = safe_query(
			"INSERT INTO LightingLog (DeviceRowID, nValue, sValue) "
			"VALUES ('%" PRIu64 "', '%d', '%q')",
			ulID,
			nValue,sValue);

		if (!bDeviceUsed)
			return ulID;	//don't process further as the device is not used
		std::string lstatus="";

		result = safe_query(
			"SELECT Name,SwitchType,AddjValue,StrParam1,StrParam2,Options,LastLevel FROM DeviceStatus WHERE (ID = %" PRIu64 ")",
			ulID);
		if (result.size()>0)
		{
			bool bHaveGroupCmd = false;
			int maxDimLevel = 0;
			int llevel = 0;
			bool bHaveDimmer = false;
			std::vector<std::string> sd=result[0];
			std::string Name=sd[0];
			_eSwitchType switchtype=(_eSwitchType)atoi(sd[1].c_str());
			float AddjValue = static_cast<float>(atof(sd[2].c_str()));
			GetLightStatus(devType, subType, switchtype,nValue, sValue, lstatus, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);

			bool bIsLightSwitchOn=IsLightSwitchOn(lstatus);
			std::string slevel = sd[6];

			if ((bIsLightSwitchOn) && (llevel != 0) && (llevel != 255) ||
				(switchtype == STYPE_BlindsPercentage) || (switchtype == STYPE_BlindsPercentageInverted))
			{
				if (((switchtype == STYPE_BlindsPercentage) ||
					(switchtype == STYPE_BlindsPercentageInverted)) &&
					(nValue == light2_sOn))
				{
						llevel = 100;
				}
				//update level for device
				safe_query(
					"UPDATE DeviceStatus SET LastLevel='%d' WHERE (ID = %" PRIu64 ")",
					llevel,
					ulID);
				if (bUseOnOffAction)
				    slevel = boost::lexical_cast<std::string>(llevel);
			}

			if (bUseOnOffAction)
			{
				//Perform any On/Off actions
				std::string OnAction=sd[3];
				std::string OffAction=sd[4];
				std::string Options=sd[5];

				if(devType==pTypeEvohome)
				{
					bIsLightSwitchOn=true;//Force use of OnAction for all actions

				} else if (switchtype == STYPE_Selector) {
					bIsLightSwitchOn = (llevel > 0) ? true : false;
					OnAction = CURLEncode::URLDecode(GetSelectorSwitchLevelAction(BuildDeviceOptions(Options, true), llevel));
					OffAction = CURLEncode::URLDecode(GetSelectorSwitchLevelAction(BuildDeviceOptions(Options, true), 0));
				}
				if (bIsLightSwitchOn) {
					stdreplace(OnAction, "{level}", slevel);
					stdreplace(OnAction, "{status}", lstatus);
					stdreplace(OnAction, "{deviceid}", ID);
				} else {
					stdreplace(OffAction, "{level}", slevel);
					stdreplace(OffAction, "{status}", lstatus);
					stdreplace(OffAction, "{deviceid}", ID);
				}
				HandleOnOffAction(bIsLightSwitchOn, OnAction, OffAction);
			}

			//Check if we need to email a snapshot of a Camera
			std::string emailserver;
			int n2Value;
			if (GetPreferencesVar("EmailServer",n2Value,emailserver))
			{
				if (emailserver!="")
				{
					result=safe_query(
						"SELECT CameraRowID, DevSceneDelay FROM CamerasActiveDevices WHERE (DevSceneType==0) AND (DevSceneRowID==%" PRIu64 ") AND (DevSceneWhen==%d)",
						ulID,
						(bIsLightSwitchOn==true)?0:1
						);
					if (result.size()>0)
					{
						std::vector<std::vector<std::string> >::const_iterator ittCam;
						for (ittCam=result.begin(); ittCam!=result.end(); ++ittCam)
						{
							std::vector<std::string> sd=*ittCam;
							std::string camidx=sd[0];
							float delay= static_cast<float>(atof(sd[1].c_str()));
							std::string subject=Name + " Status: " + lstatus;
							AddTaskItem(_tTaskItem::EmailCameraSnapshot(delay+1,camidx,subject));
						}
					}
				}
			}

			if (m_bEnableEventSystem)
			{
				//Execute possible script
				std::string scriptname;
#ifdef WIN32
				scriptname = szUserDataFolder + "scripts\\domoticz_main.bat";
#else
				scriptname = szUserDataFolder + "scripts/domoticz_main";
#endif
				if (file_exist(scriptname.c_str()))
				{
					//Add parameters
					std::stringstream s_scriptparams;
					std::string nszUserDataFolder = szUserDataFolder;
					if (nszUserDataFolder == "")
						nszUserDataFolder = ".";
					s_scriptparams << nszUserDataFolder << " " << HardwareID << " " << ulID << " " << (bIsLightSwitchOn ? "On" : "Off") << " \"" << lstatus << "\"" << " \"" << devname << "\"";
					//add script to background worker
					boost::lock_guard<boost::mutex> l(m_background_task_mutex);
					m_background_task_queue.push_back(_tTaskItem::ExecuteScript(1, scriptname, s_scriptparams.str()));
				}
			}

			_eHardwareTypes HWtype= HTYPE_Domoticz; //just a value
			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(HardwareID);
			if (pHardware != NULL)
				HWtype = pHardware->HwdType;

			//Check for notifications
			if (HWtype != HTYPE_LogitechMediaServer) // Skip notifications for LMS here; is handled by the LMS plug-in
			{
				if (switchtype == STYPE_Selector)
					m_notifications.CheckAndHandleSwitchNotification(ulID, devname, (bIsLightSwitchOn) ? NTYPE_SWITCH_ON : NTYPE_SWITCH_OFF, llevel);
				else
					m_notifications.CheckAndHandleSwitchNotification(ulID, devname, (bIsLightSwitchOn) ? NTYPE_SWITCH_ON : NTYPE_SWITCH_OFF);
			}
			if (bIsLightSwitchOn)
			{
				if ((int)AddjValue!=0) //Off Delay
				{
					bool bAdd2DelayQueue=false;
					int cmd=0;
					if (
						(switchtype == STYPE_OnOff) ||
						(switchtype == STYPE_Motion) ||
						(switchtype == STYPE_Dimmer) ||
						(switchtype == STYPE_PushOn) ||
						(switchtype == STYPE_DoorContact) ||
						(switchtype == STYPE_DoorLock) ||
						(switchtype == STYPE_DoorLockInverted) ||
						(switchtype == STYPE_Selector)
						)
					{
						switch (devType)
						{
						case pTypeLighting1:
							cmd=light1_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeLighting2:
							cmd=light2_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeLighting3:
							cmd=light3_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeLighting4:
							cmd=light2_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeLighting5:
							cmd=light5_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeLighting6:
							cmd=light6_sOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeRemote:
							cmd=light2_sOff;
							break;
						case pTypeColorSwitch:
							cmd=Color_LedOff;
							bAdd2DelayQueue=true;
							break;
						case pTypeRFY:
							cmd = rfy_sStop;
							bAdd2DelayQueue = true;
							break;
						case pTypeRadiator1:
							cmd = Radiator1_sNight;
							bAdd2DelayQueue = true;
							break;
						case pTypeGeneralSwitch:
							cmd = gswitch_sOff;
							bAdd2DelayQueue = true;
							break;
						case pTypeHomeConfort:
							cmd = HomeConfort_sOff;
							bAdd2DelayQueue = true;
							break;
						}
					}
	/* Smoke detectors are manually reset!
					else if (
						(devType==pTypeSecurity1)&&
						((subType==sTypeKD101)||(subType==sTypeSA30))
						)
					{
						cmd=sStatusPanicOff;
						bAdd2DelayQueue=true;
					}
	*/
					if (bAdd2DelayQueue==true)
					{
						boost::lock_guard<boost::mutex> l(m_background_task_mutex);
						_tTaskItem tItem=_tTaskItem::SwitchLight(AddjValue,ulID,HardwareID,ID,unit,devType,subType,switchtype,signallevel,batterylevel,cmd,sValue);
						//Remove all instances with this device from the queue first
						//otherwise command will be send twice, and first one will be to soon as it is currently counting
						std::vector<_tTaskItem>::iterator itt=m_background_task_queue.begin();
						while (itt!=m_background_task_queue.end())
						{
							if (
								(itt->_ItemType==TITEM_SWITCHCMD)&&
								(itt->_idx==ulID)&&
								(itt->_HardwareID == HardwareID)&&
								(itt->_nValue == cmd)
								)
							{
								itt=m_background_task_queue.erase(itt);
							}
							else
								++itt;
						}
						//finally add it to the queue
						m_background_task_queue.push_back(tItem);
					}
				}
			}
		}//end of check for notifications

		//Check Scene Status
		CheckSceneStatusWithDevice(ulID);
		break;
	}

	if (_log.isTraceEnabled()) _log.Log(LOG_TRACE,"SQLH UpdateValueInt %s HwID:%d  DevID:%s Type:%d  sType:%d nValue:%d sValue:%s ", devname.c_str(),HardwareID, ID, devType, subType, nValue, sValue );

	if (bDeviceUsed)
		m_mainworker.m_eventsystem.ProcessDevice(HardwareID, ulID, unit, devType, subType, signallevel, batterylevel, nValue, sValue, devname, 0);
	return ulID;
}

void CSQLHelper::UpdateTemperatureLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR Type=%d OR (Type=%d AND SubType=%d) OR (Type=%d AND SubType=%d) OR (Type=%d AND SubType=%d))",
		pTypeTEMP,
		pTypeHUM,
		pTypeTEMP_HUM,
		pTypeTEMP_HUM_BARO,
		pTypeTEMP_BARO,
		pTypeUV,
		pTypeWIND,
		pTypeThermostat1,
		pTypeRFXSensor,
		pTypeRego6XXTemp,
		pTypeEvohomeZone,
		pTypeEvohomeWater,
		pTypeRadiator1,
		pTypeGeneral,sTypeSystemTemp,
		pTypeThermostat,sTypeThermSetpoint,
		pTypeGeneral, sTypeBaro
		);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;

			unsigned char dType=atoi(sd[1].c_str());
			unsigned char dSubType=atoi(sd[2].c_str());
			int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			if (dType != pTypeRadiator1)
			{
				//do not include sensors that have no reading within an hour (except for devices that do not provide feedback, like the smartware radiator)
				std::string sLastUpdate = sd[5];
				struct tm ntime;
				time_t checktime;
				ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

				if (difftime(now,checktime) >= SensorTimeOut * 60)
					continue;
			}

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<1)
				continue; //impossible

			float temp=0;
			float chill=0;
			unsigned char humidity=0;
			int barometer=0;
			float dewpoint=0;
			float setpoint=0;

			switch (dType)
			{
			case pTypeRego6XXTemp:
			case pTypeTEMP:
			case pTypeThermostat:
				temp = static_cast<float>(atof(splitresults[0].c_str()));
				break;
			case pTypeThermostat1:
				temp = static_cast<float>(atof(splitresults[0].c_str()));
				break;
			case pTypeRadiator1:
				temp = static_cast<float>(atof(splitresults[0].c_str()));
				break;
			case pTypeEvohomeWater:
				if (splitresults.size()>=2)
				{
					temp=static_cast<float>(atof(splitresults[0].c_str()));
					setpoint=static_cast<float>((splitresults[1]=="On")?60:0);
					//FIXME hack setpoint just on or off...may throw graph out so maybe pick sensible on off values?
					//(if the actual hw set point was retrievable should use that otherwise some config option)
					//actually if we plot the average it should give us an idea of how often hw has been switched on
					//more meaningful if it was plotted against the zone valve & boiler relay i guess (actual time hw heated)
				}
				break;
			case pTypeEvohomeZone:
				if (splitresults.size()>=2)
				{
					temp=static_cast<float>(atof(splitresults[0].c_str()));
					setpoint=static_cast<float>(atof(splitresults[1].c_str()));
				}
				break;
			case pTypeHUM:
				humidity=nValue;
				break;
			case pTypeTEMP_HUM:
				if (splitresults.size()>=2)
				{
					temp = static_cast<float>(atof(splitresults[0].c_str()));
					humidity=atoi(splitresults[1].c_str());
					dewpoint=(float)CalculateDewPoint(temp,humidity);
				}
				break;
			case pTypeTEMP_HUM_BARO:
				if (splitresults.size()==5)
				{
					temp = static_cast<float>(atof(splitresults[0].c_str()));
					humidity=atoi(splitresults[1].c_str());
					if (dSubType==sTypeTHBFloat)
						barometer=int(atof(splitresults[3].c_str())*10.0f);
					else
						barometer=atoi(splitresults[3].c_str());
					dewpoint=(float)CalculateDewPoint(temp,humidity);
				}
				break;
			case pTypeTEMP_BARO:
				if (splitresults.size()>=2)
				{
					temp = static_cast<float>(atof(splitresults[0].c_str()));
					barometer=int(atof(splitresults[1].c_str())*10.0f);
				}
				break;
			case pTypeUV:
				if (dSubType!=sTypeUV3)
					continue;
				if (splitresults.size()>=2)
				{
					temp = static_cast<float>(atof(splitresults[1].c_str()));
				}
				break;
			case pTypeWIND:
				if ((dSubType!=sTypeWIND4)&&(dSubType!=sTypeWINDNoTemp))
					continue;
				if (splitresults.size()>=6)
				{
					temp = static_cast<float>(atof(splitresults[4].c_str()));
					chill = static_cast<float>(atof(splitresults[5].c_str()));
				}
				break;
			case pTypeRFXSensor:
				if (dSubType!=sTypeRFXSensorTemp)
					continue;
				temp = static_cast<float>(atof(splitresults[0].c_str()));
				break;
			case pTypeGeneral:
				if (dSubType == sTypeSystemTemp)
				{
					temp = static_cast<float>(atof(splitresults[0].c_str()));
				}
				else if (dSubType == sTypeBaro)
				{
					if (splitresults.size() != 2)
						continue;
					barometer = int(atof(splitresults[0].c_str())*10.0f);
				}
				break;
			}
			//insert record
			safe_query(
				"INSERT INTO Temperature (DeviceRowID, Temperature, Chill, Humidity, Barometer, DewPoint, SetPoint) "
				"VALUES ('%" PRIu64 "', '%.2f', '%.2f', '%d', '%d', '%.2f', '%.2f')",
				ID,
				temp,
				chill,
				humidity,
				barometer,
				dewpoint,
				setpoint
				);
		}
	}
}

void CSQLHelper::UpdateRainLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d)", pTypeRAIN);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;
			//unsigned char dType=atoi(sd[1].c_str());
			//unsigned char dSubType=atoi(sd[2].c_str());
			//int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[5];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<2)
				continue; //impossible

			int rate=atoi(splitresults[0].c_str());
			float total = static_cast<float>(atof(splitresults[1].c_str()));

			//insert record
			safe_query(
				"INSERT INTO Rain (DeviceRowID, Total, Rate) "
				"VALUES ('%" PRIu64 "', '%.2f', '%d')",
				ID,
				total,
				rate
				);
		}
	}
}

void CSQLHelper::UpdateWindLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID,DeviceID, Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d)", pTypeWIND);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;

			unsigned short DeviceID;
			std::stringstream s_str2(sd[1]);
			s_str2 >> DeviceID;

			//unsigned char dType=atoi(sd[2].c_str());
			//unsigned char dSubType=atoi(sd[3].c_str());
			//int nValue=atoi(sd[4].c_str());
			std::string sValue=sd[5];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[6];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<4)
				continue; //impossible

			float direction = static_cast<float>(atof(splitresults[0].c_str()));

			int speed = atoi(splitresults[2].c_str());
			int gust = atoi(splitresults[3].c_str());

			std::map<unsigned short, _tWindCalculationStruct>::iterator itt = m_mainworker.m_wind_calculator.find(DeviceID);
			if (itt != m_mainworker.m_wind_calculator.end())
			{
				int speed_max, gust_max, speed_min, gust_min;
				itt->second.GetMMSpeedGust(speed_min, speed_max, gust_min, gust_max);
				if (speed_max != -1)
					speed = speed_max;
				if (gust_max != -1)
					gust = gust_max;
			}


			//insert record
			safe_query(
				"INSERT INTO Wind (DeviceRowID, Direction, Speed, Gust) "
				"VALUES ('%" PRIu64 "', '%.2f', '%d', '%d')",
				ID,
				direction,
				speed,
				gust
				);
		}
	}
}

void CSQLHelper::UpdateUVLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d) OR (Type=%d AND SubType=%d)",
		pTypeUV,
		pTypeGeneral, sTypeUV
	);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;
			//unsigned char dType=atoi(sd[1].c_str());
			//unsigned char dSubType=atoi(sd[2].c_str());
			//int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[5];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<1)
				continue; //impossible

			float level = static_cast<float>(atof(splitresults[0].c_str()));

			//insert record
			safe_query(
				"INSERT INTO UV (DeviceRowID, Level) "
				"VALUES ('%" PRIu64 "', '%g')",
				ID,
				level
				);
		}
	}
}

void CSQLHelper::UpdateMeter()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	std::vector<std::vector<std::string> > result2;

	result=safe_query(
		"SELECT ID,Name,HardwareID,DeviceID,Unit,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE ("
		"Type=%d OR " //pTypeRFXMeter
		"Type=%d OR " //pTypeP1Gas
		"Type=%d OR " //pTypeYouLess
		"Type=%d OR " //pTypeENERGY
		"Type=%d OR " //pTypePOWER
		"Type=%d OR " //pTypeAirQuality
		"Type=%d OR " //pTypeUsage
		"Type=%d OR " //pTypeLux
		"Type=%d OR " //pTypeWEIGHT
		"(Type=%d AND SubType=%d) OR " //pTypeRego6XXValue,sTypeRego6XXCounter
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeVisibility
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeSolarRadiation
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeSoilMoisture
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeLeafWetness
		"(Type=%d AND SubType=%d) OR " //pTypeRFXSensor,sTypeRFXSensorAD
		"(Type=%d AND SubType=%d) OR" //pTypeRFXSensor,sTypeRFXSensorVolt
		"(Type=%d AND SubType=%d) OR"  //pTypeGeneral,sTypeVoltage
		"(Type=%d AND SubType=%d) OR"  //pTypeGeneral,sTypeCurrent
		"(Type=%d AND SubType=%d) OR"  //pTypeGeneral,sTypeSoundLevel
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeDistance
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypePressure
		"(Type=%d AND SubType=%d) OR " //pTypeGeneral,sTypeCounterIncremental
		"(Type=%d AND SubType=%d)"     //pTypeGeneral,sTypeKwh
		")",
		pTypeRFXMeter,
		pTypeP1Gas,
		pTypeYouLess,
		pTypeENERGY,
		pTypePOWER,
		pTypeAirQuality,
		pTypeUsage,
		pTypeLux,
		pTypeWEIGHT,
		pTypeRego6XXValue,sTypeRego6XXCounter,
		pTypeGeneral,sTypeVisibility,
		pTypeGeneral,sTypeSolarRadiation,
		pTypeGeneral,sTypeSoilMoisture,
		pTypeGeneral,sTypeLeafWetness,
		pTypeRFXSensor,sTypeRFXSensorAD,
		pTypeRFXSensor,sTypeRFXSensorVolt,
		pTypeGeneral, sTypeVoltage,
		pTypeGeneral, sTypeCurrent,
		pTypeGeneral, sTypeSoundLevel,
		pTypeGeneral, sTypeDistance,
		pTypeGeneral, sTypePressure,
		pTypeGeneral, sTypeCounterIncremental,
		pTypeGeneral, sTypeKwh
		);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			char szTmp[200];
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;
			std::string devname = sd[1];
			int hardwareID= atoi(sd[2].c_str());
			std::string DeviceID=sd[3];
			unsigned char Unit = atoi(sd[4].c_str());

			unsigned char dType=atoi(sd[5].c_str());
			unsigned char dSubType=atoi(sd[6].c_str());
			int nValue=atoi(sd[7].c_str());
			std::string sValue=sd[8];
			std::string sLastUpdate=sd[9];

			std::string susage="0";

			//do not include sensors that have no reading within an hour
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);


			//Check for timeout, if timeout then dont add value
			if (dType!=pTypeP1Gas)
			{
				if (difftime(now,checktime) >= SensorTimeOut * 60)
					continue;
			}
			else
			{
				//P1 Gas meter transmits results every 1 a 2 hours
				if (difftime(now,checktime) >= 3 * 3600)
					continue;
			}

			if (dType==pTypeYouLess)
			{
				std::vector<std::string> splitresults;
				StringSplit(sValue, ";", splitresults);
				if (splitresults.size()<2)
					continue;
				sValue=splitresults[0];
				susage = splitresults[1];
			}
			else if (dType==pTypeENERGY)
			{
				std::vector<std::string> splitresults;
				StringSplit(sValue, ";", splitresults);
				if (splitresults.size()<2)
					continue;
				susage=splitresults[0];
				double fValue=atof(splitresults[1].c_str())*100;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if (dType==pTypePOWER)
			{
				std::vector<std::string> splitresults;
				StringSplit(sValue, ";", splitresults);
				if (splitresults.size()<2)
					continue;
				susage=splitresults[0];
				double fValue=atof(splitresults[1].c_str())*100;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if (dType==pTypeAirQuality)
			{
				sprintf(szTmp,"%d",nValue);
				sValue=szTmp;
				m_notifications.CheckAndHandleNotification(ID, hardwareID, DeviceID, devname, Unit, dType, dSubType, (int)nValue);
			}
			else if ((dType==pTypeGeneral)&&((dSubType==sTypeSoilMoisture)||(dSubType==sTypeLeafWetness)))
			{
				sprintf(szTmp,"%d",nValue);
				sValue=szTmp;
			}
			else if ((dType==pTypeGeneral)&&(dSubType==sTypeVisibility))
			{
				double fValue=atof(sValue.c_str())*10.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypeDistance))
			{
				double fValue = atof(sValue.c_str())*10.0f;
				sprintf(szTmp, "%.0f", fValue);
				sValue = szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypeSolarRadiation))
			{
				double fValue=atof(sValue.c_str())*10.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypeSoundLevel))
			{
				double fValue = atof(sValue.c_str())*10.0f;
				sprintf(szTmp, "%.0f", fValue);
				sValue = szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypeKwh))
			{
				std::vector<std::string> splitresults;
				StringSplit(sValue, ";", splitresults);
				if (splitresults.size() < 2)
					continue;

				double fValue = atof(splitresults[0].c_str())*10.0f;
				sprintf(szTmp, "%.0f", fValue);
				susage = szTmp;

				fValue = atof(splitresults[1].c_str());
				sprintf(szTmp, "%.0f", fValue);
				sValue = szTmp;
			}
			else if (dType == pTypeLux)
			{
				double fValue=atof(sValue.c_str());
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if (dType==pTypeWEIGHT)
			{
				double fValue=atof(sValue.c_str())*10.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if (dType==pTypeRFXSensor)
			{
				double fValue=atof(sValue.c_str());
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if ((dType==pTypeGeneral) && (dSubType == sTypeCounterIncremental))
			{
				double fValue=atof(sValue.c_str());
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if ((dType==pTypeGeneral)&&(dSubType==sTypeVoltage))
			{
				double fValue=atof(sValue.c_str())*1000.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypeCurrent))
			{
				double fValue = atof(sValue.c_str())*1000.0f;
				sprintf(szTmp, "%.0f", fValue);
				sValue = szTmp;
			}
			else if ((dType == pTypeGeneral) && (dSubType == sTypePressure))
			{
				double fValue=atof(sValue.c_str())*10.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}
			else if (dType == pTypeUsage)
			{
				double fValue=atof(sValue.c_str())*10.0f;
				sprintf(szTmp,"%.0f",fValue);
				sValue=szTmp;
			}

			long long MeterValue;
			std::stringstream s_str2( sValue );
			s_str2 >> MeterValue;

			long long MeterUsage;
			std::stringstream s_str3( susage );
			s_str3 >> MeterUsage;

			//insert record
			safe_query(
				"INSERT INTO Meter (DeviceRowID, Value, [Usage]) "
				"VALUES ('%" PRIu64 "', '%lld', '%lld')",
				ID,
				MeterValue,
				MeterUsage
				);
		}
	}
}

void CSQLHelper::UpdateMultiMeter()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d OR Type=%d OR Type=%d)",
		pTypeP1Power,
		pTypeCURRENT,
		pTypeCURRENTENERGY
		);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;
			unsigned char dType=atoi(sd[1].c_str());
			unsigned char dSubType=atoi(sd[2].c_str());
			//int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[5];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;
			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);

			unsigned long long value1=0;
			unsigned long long value2=0;
			unsigned long long value3=0;
			unsigned long long value4=0;
			unsigned long long value5=0;
			unsigned long long value6=0;

			if (dType==pTypeP1Power)
			{
				if (splitresults.size()!=6)
					continue; //impossible
				unsigned long long powerusage1;
				unsigned long long powerusage2;
				unsigned long long powerdeliv1;
				unsigned long long powerdeliv2;
				unsigned long long usagecurrent;
				unsigned long long delivcurrent;

				std::stringstream s_powerusage1(splitresults[0]);
				std::stringstream s_powerusage2(splitresults[1]);
				std::stringstream s_powerdeliv1(splitresults[2]);
				std::stringstream s_powerdeliv2(splitresults[3]);
				std::stringstream s_usagecurrent(splitresults[4]);
				std::stringstream s_delivcurrent(splitresults[5]);

				s_powerusage1 >> powerusage1;
				s_powerusage2 >> powerusage2;
				s_powerdeliv1 >> powerdeliv1;
				s_powerdeliv2 >> powerdeliv2;
				s_usagecurrent >> usagecurrent;
				s_delivcurrent >> delivcurrent;

				value1=powerusage1;
				value2=powerdeliv1;
				value5=powerusage2;
				value6=powerdeliv2;
				value3=usagecurrent;
				value4=delivcurrent;
			}
			else if ((dType==pTypeCURRENT)&&(dSubType==sTypeELEC1))
			{
				if (splitresults.size()!=3)
					continue; //impossible

				value1=(unsigned long)(atof(splitresults[0].c_str())*10.0f);
				value2=(unsigned long)(atof(splitresults[1].c_str())*10.0f);
				value3=(unsigned long)(atof(splitresults[2].c_str())*10.0f);
			}
			else if ((dType==pTypeCURRENTENERGY)&&(dSubType==sTypeELEC4))
			{
				if (splitresults.size()!=4)
					continue; //impossible

				value1=(unsigned long)(atof(splitresults[0].c_str())*10.0f);
				value2=(unsigned long)(atof(splitresults[1].c_str())*10.0f);
				value3=(unsigned long)(atof(splitresults[2].c_str())*10.0f);
				value4=(unsigned long long)(atof(splitresults[3].c_str())*1000.0f);
			}
			else
				continue;//don't know you (yet)

			//insert record
			safe_query(
				"INSERT INTO MultiMeter (DeviceRowID, Value1, Value2, Value3, Value4, Value5, Value6) "
				"VALUES ('%" PRIu64 "', '%llu', '%llu', '%llu', '%llu', '%llu', '%llu')",
				ID,
				value1,
				value2,
				value3,
				value4,
				value5,
				value6
				);
		}
	}
}

void CSQLHelper::UpdatePercentageLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result = safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d AND SubType=%d) OR (Type=%d AND SubType=%d) OR (Type=%d AND SubType=%d)",
		pTypeGeneral, sTypePercentage,
		pTypeGeneral, sTypeWaterflow,
		pTypeGeneral, sTypeCustom
		);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;

			//unsigned char dType=atoi(sd[1].c_str());
			//unsigned char dSubType=atoi(sd[2].c_str());
			//int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[5];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<1)
				continue; //impossible

			float percentage = static_cast<float>(atof(sValue.c_str()));

			//insert record
			safe_query(
				"INSERT INTO Percentage (DeviceRowID, Percentage) "
				"VALUES ('%" PRIu64 "', '%g')",
				ID,
				percentage
				);
		}
	}
}

void CSQLHelper::UpdateFanLog()
{
	time_t now = mytime(NULL);
	if (now==0)
		return;
	struct tm tm1;
	localtime_r(&now,&tm1);

	int SensorTimeOut=60;
	GetPreferencesVar("SensorTimeout", SensorTimeOut);

	std::vector<std::vector<std::string> > result;
	result=safe_query("SELECT ID,Type,SubType,nValue,sValue,LastUpdate FROM DeviceStatus WHERE (Type=%d AND SubType=%d)",
		pTypeGeneral,sTypeFan
		);
	if (result.size()>0)
	{
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			std::vector<std::string> sd=*itt;

			uint64_t ID;
			std::stringstream s_str( sd[0] );
			s_str >> ID;

			//unsigned char dType=atoi(sd[1].c_str());
			//unsigned char dSubType=atoi(sd[2].c_str());
			//int nValue=atoi(sd[3].c_str());
			std::string sValue=sd[4];

			//do not include sensors that have no reading within an hour
			std::string sLastUpdate=sd[5];
			struct tm ntime;
			time_t checktime;
			ParseSQLdatetime(checktime, ntime, sLastUpdate, tm1.tm_isdst);

			if (difftime(now,checktime) >= SensorTimeOut * 60)
				continue;

			std::vector<std::string> splitresults;
			StringSplit(sValue, ";", splitresults);
			if (splitresults.size()<1)
				continue; //impossible

			int speed= (int)atoi(sValue.c_str());

			//insert record
			safe_query(
				"INSERT INTO Fan (DeviceRowID, Speed) "
				"VALUES ('%" PRIu64 "', '%d')",
				ID,
				speed
				);
		}
	}
}