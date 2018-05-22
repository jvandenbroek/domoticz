#include "stdafx.h"
#include "../SQLHelper.h"
#include "../RFXtrx.h"
#include "../localtime_r.h"
#ifdef WITH_EXTERNAL_SQLITE
#include <sqlite3.h>
#else
#include "../../sqlite/sqlite3.h"
#endif
#include "../Logger.h"
#include "../webserver/Base64.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define DB_VERSION 127

extern std::string szWWWFolder;
extern std::string szUserDataFolder;

static const char *sqlCreateDeviceStatus =
"CREATE TABLE IF NOT EXISTS [DeviceStatus] ("
"[ID] INTEGER PRIMARY KEY, "
"[HardwareID] INTEGER NOT NULL, "
"[DeviceID] VARCHAR(25) NOT NULL, "
"[Unit] INTEGER DEFAULT 0, "
"[Name] VARCHAR(100) DEFAULT Unknown, "
"[Used] INTEGER DEFAULT 0, "
"[Type] INTEGER NOT NULL, "
"[SubType] INTEGER NOT NULL, "
"[SwitchType] INTEGER DEFAULT 0, "
"[Favorite] INTEGER DEFAULT 0, "
"[SignalLevel] INTEGER DEFAULT 0, "
"[BatteryLevel] INTEGER DEFAULT 0, "
"[nValue] INTEGER DEFAULT 0, "
"[sValue] VARCHAR(200) DEFAULT null, "
"[LastUpdate] DATETIME DEFAULT (datetime('now','localtime')),"
"[Order] INTEGER BIGINT(10) default 0, "
"[AddjValue] FLOAT DEFAULT 0, "
"[AddjMulti] FLOAT DEFAULT 1, "
"[AddjValue2] FLOAT DEFAULT 0, "
"[AddjMulti2] FLOAT DEFAULT 1, "
"[StrParam1] VARCHAR(200) DEFAULT '', "
"[StrParam2] VARCHAR(200) DEFAULT '', "
"[LastLevel] INTEGER DEFAULT 0, "
"[Protected] INTEGER DEFAULT 0, "
"[CustomImage] INTEGER DEFAULT 0, "
"[Description] VARCHAR(200) DEFAULT '', "
"[Options] TEXT DEFAULT null, "
"[Color] TEXT DEFAULT NULL);";

static const char *sqlCreateDeviceStatusTrigger =
"CREATE TRIGGER IF NOT EXISTS devicestatusupdate AFTER INSERT ON DeviceStatus\n"
"BEGIN\n"
"	UPDATE DeviceStatus SET [Order] = (SELECT MAX([Order]) FROM DeviceStatus)+1 WHERE DeviceStatus.ID = NEW.ID;\n"
"END;\n";

static const char *sqlCreateEventActions =
"CREATE TABLE IF NOT EXISTS [EventActions] ("
"[ID] INTEGER PRIMARY KEY, "
"[ConditionID] INTEGER NOT NULL, "
"[ActionType] INTEGER NOT NULL, "
"[DeviceRowID] INTEGER DEFAULT 0, "
"[Param1] VARCHAR(120), "
"[Param2] VARCHAR(120), "
"[Param3] VARCHAR(120), "
"[Param4] VARCHAR(120), "
"[Param5] VARCHAR(120), "
"[Order] INTEGER BIGINT(10) default 0);";

static const char *sqlCreateEventActionsTrigger =
"CREATE TRIGGER IF NOT EXISTS eventactionsstatusupdate AFTER INSERT ON EventActions\n"
"BEGIN\n"
"  UPDATE EventActions SET [Order] = (SELECT MAX([Order]) FROM EventActions)+1 WHERE EventActions.ID = NEW.ID;\n"
"END;\n";

static const char *sqlCreateLightingLog =
"CREATE TABLE IF NOT EXISTS [LightingLog] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[nValue] INTEGER DEFAULT 0, "
"[sValue] VARCHAR(200), "
"[User] VARCHAR(100) DEFAULT (''), "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateSceneLog =
"CREATE TABLE IF NOT EXISTS [SceneLog] ("
"[SceneRowID] BIGINT(10) NOT NULL, "
"[nValue] INTEGER DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreatePreferences =
"CREATE TABLE IF NOT EXISTS [Preferences] ("
"[Key] VARCHAR(50) NOT NULL, "
"[nValue] INTEGER DEFAULT 0, "
"[sValue] VARCHAR(200));";

static const char *sqlCreateRain =
"CREATE TABLE IF NOT EXISTS [Rain] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Total] FLOAT NOT NULL, "
"[Rate] INTEGER DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateRain_Calendar =
"CREATE TABLE IF NOT EXISTS [Rain_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Total] FLOAT NOT NULL, "
"[Rate] INTEGER DEFAULT 0, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateTemperature =
"CREATE TABLE IF NOT EXISTS [Temperature] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Temperature] FLOAT NOT NULL, "
"[Chill] FLOAT DEFAULT 0, "
"[Humidity] INTEGER DEFAULT 0, "
"[Barometer] INTEGER DEFAULT 0, "
"[DewPoint] FLOAT DEFAULT 0, "
"[SetPoint] FLOAT DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateTemperature_Calendar =
"CREATE TABLE IF NOT EXISTS [Temperature_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Temp_Min] FLOAT NOT NULL, "
"[Temp_Max] FLOAT NOT NULL, "
"[Temp_Avg] FLOAT DEFAULT 0, "
"[Chill_Min] FLOAT DEFAULT 0, "
"[Chill_Max] FLOAT, "
"[Humidity] INTEGER DEFAULT 0, "
"[Barometer] INTEGER DEFAULT 0, "
"[DewPoint] FLOAT DEFAULT 0, "
"[SetPoint_Min] FLOAT DEFAULT 0, "
"[SetPoint_Max] FLOAT DEFAULT 0, "
"[SetPoint_Avg] FLOAT DEFAULT 0, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateTimers =
"CREATE TABLE IF NOT EXISTS [Timers] ("
"[ID] INTEGER PRIMARY KEY, "
"[Active] BOOLEAN DEFAULT true, "
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Date] DATE DEFAULT 0, "
"[Time] TIME NOT NULL, "
"[Type] INTEGER NOT NULL, "
"[Cmd] INTEGER NOT NULL, "
"[Level] INTEGER DEFAULT 15, "
"[Color] TEXT DEFAULT NULL, "
"[UseRandomness] INTEGER DEFAULT 0, "
"[TimerPlan] INTEGER DEFAULT 0, "
"[Days] INTEGER NOT NULL, "
"[Month] INTEGER DEFAULT 0, "
"[MDay] INTEGER DEFAULT 0, "
"[Occurence] INTEGER DEFAULT 0);";

static const char *sqlCreateUV =
"CREATE TABLE IF NOT EXISTS [UV] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Level] FLOAT NOT NULL, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateUV_Calendar =
"CREATE TABLE IF NOT EXISTS [UV_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Level] FLOAT, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateWind =
"CREATE TABLE IF NOT EXISTS [Wind] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Direction] FLOAT NOT NULL, "
"[Speed] INTEGER NOT NULL, "
"[Gust] INTEGER NOT NULL, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateWind_Calendar =
"CREATE TABLE IF NOT EXISTS [Wind_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Direction] FLOAT NOT NULL, "
"[Speed_Min] INTEGER NOT NULL, "
"[Speed_Max] INTEGER NOT NULL, "
"[Gust_Min] INTEGER NOT NULL, "
"[Gust_Max] INTEGER NOT NULL, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateMultiMeter =
"CREATE TABLE IF NOT EXISTS [MultiMeter] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Value1] BIGINT NOT NULL, "
"[Value2] BIGINT DEFAULT 0, "
"[Value3] BIGINT DEFAULT 0, "
"[Value4] BIGINT DEFAULT 0, "
"[Value5] BIGINT DEFAULT 0, "
"[Value6] BIGINT DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateMultiMeter_Calendar =
"CREATE TABLE IF NOT EXISTS [MultiMeter_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Value1] BIGINT NOT NULL, "
"[Value2] BIGINT NOT NULL, "
"[Value3] BIGINT NOT NULL, "
"[Value4] BIGINT NOT NULL, "
"[Value5] BIGINT NOT NULL, "
"[Value6] BIGINT NOT NULL, "
"[Counter1] BIGINT DEFAULT 0, "
"[Counter2] BIGINT DEFAULT 0, "
"[Counter3] BIGINT DEFAULT 0, "
"[Counter4] BIGINT DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";


static const char *sqlCreateNotifications =
"CREATE TABLE IF NOT EXISTS [Notifications] ("
"[ID] INTEGER PRIMARY KEY, "
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Params] VARCHAR(100), "
"[CustomMessage] VARCHAR(300) DEFAULT (''), "
"[ActiveSystems] VARCHAR(200) DEFAULT (''), "
"[Priority] INTEGER default 0, "
"[SendAlways] INTEGER default 0, "
"[LastSend] DATETIME DEFAULT 0);";

static const char *sqlCreateHardware =
"CREATE TABLE IF NOT EXISTS [Hardware] ("
"[ID] INTEGER PRIMARY KEY, "
"[Name] VARCHAR(200) NOT NULL, "
"[Enabled] INTEGER DEFAULT 1, "
"[Type] INTEGER NOT NULL, "
"[Address] VARCHAR(200), "
"[Port] INTEGER, "
"[SerialPort] VARCHAR(50) DEFAULT (''), "
"[Username] VARCHAR(100), "
"[Password] VARCHAR(100), "
"[Extra] TEXT DEFAULT (''),"
"[Mode1] CHAR DEFAULT 0, "
"[Mode2] CHAR DEFAULT 0, "
"[Mode3] CHAR DEFAULT 0, "
"[Mode4] CHAR DEFAULT 0, "
"[Mode5] CHAR DEFAULT 0, "
"[Mode6] CHAR DEFAULT 0, "
"[DataTimeout] INTEGER DEFAULT 0);";

static const char *sqlCreateUsers =
"CREATE TABLE IF NOT EXISTS [Users] ("
"[ID] INTEGER PRIMARY KEY, "
"[Active] INTEGER NOT NULL DEFAULT 0, "
"[Username] VARCHAR(200) NOT NULL, "
"[Password] VARCHAR(200) NOT NULL, "
"[Rights] INTEGER DEFAULT 255, "
"[TabsEnabled] INTEGER DEFAULT 255, "
"[RemoteSharing] INTEGER DEFAULT 0);";

static const char *sqlCreateMeter =
"CREATE TABLE IF NOT EXISTS [Meter] ("
"[DeviceRowID] BIGINT NOT NULL, "
"[Value] BIGINT NOT NULL, "
"[Usage] INTEGER DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateMeter_Calendar =
"CREATE TABLE IF NOT EXISTS [Meter_Calendar] ("
"[DeviceRowID] BIGINT NOT NULL, "
"[Value] BIGINT NOT NULL, "
"[Counter] BIGINT DEFAULT 0, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateLightSubDevices =
"CREATE TABLE IF NOT EXISTS [LightSubDevices] ("
"[ID] INTEGER PRIMARY KEY, "
"[DeviceRowID] INTEGER NOT NULL, "
"[ParentID] INTEGER NOT NULL);";

static const char *sqlCreateCameras =
"CREATE TABLE IF NOT EXISTS [Cameras] ("
"[ID] INTEGER PRIMARY KEY, "
"[Name] VARCHAR(200) NOT NULL, "
"[Enabled] INTEGER DEFAULT 1, "
"[Address] VARCHAR(200), "
"[Port] INTEGER, "
"[Protocol] INTEGER DEFAULT 0, "
"[Username] VARCHAR(100) DEFAULT (''), "
"[Password] VARCHAR(100) DEFAULT (''), "
"[ImageURL] VARCHAR(200) DEFAULT (''));";

static const char *sqlCreateCamerasActiveDevices =
"CREATE TABLE IF NOT EXISTS [CamerasActiveDevices] ("
"[ID] INTEGER PRIMARY KEY, "
"[CameraRowID] INTEGER NOT NULL, "
"[DevSceneType] INTEGER NOT NULL, "
"[DevSceneRowID] INTEGER NOT NULL, "
"[DevSceneWhen] INTEGER NOT NULL, "
"[DevSceneDelay] INTEGER NOT NULL);";

static const char *sqlCreatePlanMappings =
"CREATE TABLE IF NOT EXISTS [DeviceToPlansMap] ("
"[ID] INTEGER PRIMARY KEY, "
"[DeviceRowID] BIGINT NOT NULL, "
"[DevSceneType] INTEGER DEFAULT 0, "
"[PlanID] BIGINT NOT NULL, "
"[Order] INTEGER BIGINT(10) DEFAULT 0, "
"[XOffset] INTEGER default 0, "
"[YOffset] INTEGER default 0);";

static const char *sqlCreateDevicesToPlanStatusTrigger =
	"CREATE TRIGGER IF NOT EXISTS deviceplantatusupdate AFTER INSERT ON DeviceToPlansMap\n"
	"BEGIN\n"
	"	UPDATE DeviceToPlansMap SET [Order] = (SELECT MAX([Order]) FROM DeviceToPlansMap)+1 WHERE DeviceToPlansMap.ID = NEW.ID;\n"
	"END;\n";

static const char *sqlCreatePlans =
"CREATE TABLE IF NOT EXISTS [Plans] ("
"[ID] INTEGER PRIMARY KEY, "
"[Order] INTEGER BIGINT(10) default 0, "
"[Name] VARCHAR(200) NOT NULL, "
"[FloorplanID] INTEGER default 0, "
"[Area] VARCHAR(200) DEFAULT '');";

static const char *sqlCreatePlanOrderTrigger =
	"CREATE TRIGGER IF NOT EXISTS planordertrigger AFTER INSERT ON Plans\n"
	"BEGIN\n"
	"	UPDATE Plans SET [Order] = (SELECT MAX([Order]) FROM Plans)+1 WHERE Plans.ID = NEW.ID;\n"
	"END;\n";

static const char *sqlCreateScenes =
"CREATE TABLE IF NOT EXISTS [Scenes] (\n"
"[ID] INTEGER PRIMARY KEY, \n"
"[Name] VARCHAR(100) NOT NULL, \n"
"[Favorite] INTEGER DEFAULT 0, \n"
"[Order] INTEGER BIGINT(10) default 0, \n"
"[nValue] INTEGER DEFAULT 0, \n"
"[SceneType] INTEGER DEFAULT 0, \n"
"[Protected] INTEGER DEFAULT 0, \n"
"[OnAction] VARCHAR(200) DEFAULT '', "
"[OffAction] VARCHAR(200) DEFAULT '', "
"[Description] VARCHAR(200) DEFAULT '', "
"[Activators] VARCHAR(200) DEFAULT '', "
"[LastUpdate] DATETIME DEFAULT (datetime('now','localtime')));\n";

static const char *sqlCreateScenesTrigger =
"CREATE TRIGGER IF NOT EXISTS scenesupdate AFTER INSERT ON Scenes\n"
"BEGIN\n"
"	UPDATE Scenes SET [Order] = (SELECT MAX([Order]) FROM Scenes)+1 WHERE Scenes.ID = NEW.ID;\n"
"END;\n";

static const char *sqlCreateSceneDevices =
"CREATE TABLE IF NOT EXISTS [SceneDevices] ("
"[ID] INTEGER PRIMARY KEY, "
"[Order] INTEGER BIGINT(10) default 0, "
"[SceneRowID] BIGINT NOT NULL, "
"[DeviceRowID] BIGINT NOT NULL, "
"[Cmd] INTEGER DEFAULT 1, "
"[Level] INTEGER DEFAULT 100, "
"[Color] TEXT DEFAULT NULL, "
"[OnDelay] INTEGER DEFAULT 0, "
"[OffDelay] INTEGER DEFAULT 0);";

static const char *sqlCreateSceneDeviceTrigger =
	"CREATE TRIGGER IF NOT EXISTS scenedevicesupdate AFTER INSERT ON SceneDevices\n"
	"BEGIN\n"
	"	UPDATE SceneDevices SET [Order] = (SELECT MAX([Order]) FROM SceneDevices)+1 WHERE SceneDevices.ID = NEW.ID;\n"
	"END;\n";

static const char *sqlCreateTimerPlans =
"CREATE TABLE IF NOT EXISTS [TimerPlans] ("
"[ID] INTEGER PRIMARY KEY, "
"[Name] VARCHAR(200) NOT NULL);";

static const char *sqlCreateSceneTimers =
"CREATE TABLE IF NOT EXISTS [SceneTimers] ("
"[ID] INTEGER PRIMARY KEY, "
"[Active] BOOLEAN DEFAULT true, "
"[SceneRowID] BIGINT(10) NOT NULL, "
"[Date] DATE DEFAULT 0, "
"[Time] TIME NOT NULL, "
"[Type] INTEGER NOT NULL, "
"[Cmd] INTEGER NOT NULL, "
"[Level] INTEGER DEFAULT 15, "
"[UseRandomness] INTEGER DEFAULT 0, "
"[TimerPlan] INTEGER DEFAULT 0, "
"[Days] INTEGER NOT NULL, "
"[Month] INTEGER DEFAULT 0, "
"[MDay] INTEGER DEFAULT 0, "
"[Occurence] INTEGER DEFAULT 0);";

static const char *sqlCreateSetpointTimers =
"CREATE TABLE IF NOT EXISTS [SetpointTimers] ("
"[ID] INTEGER PRIMARY KEY, "
"[Active] BOOLEAN DEFAULT true, "
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Date] DATE DEFAULT 0, "
"[Time] TIME NOT NULL, "
"[Type] INTEGER NOT NULL, "
"[Temperature] FLOAT DEFAULT 0, "
"[TimerPlan] INTEGER DEFAULT 0, "
"[Days] INTEGER NOT NULL, "
"[Month] INTEGER DEFAULT 0, "
"[MDay] INTEGER DEFAULT 0, "
"[Occurence] INTEGER DEFAULT 0);";

static const char *sqlCreateSharedDevices =
"CREATE TABLE IF NOT EXISTS [SharedDevices] ("
"[ID] INTEGER PRIMARY KEY,  "
"[SharedUserID] BIGINT NOT NULL, "
"[DeviceRowID] BIGINT NOT NULL);";

static const char *sqlCreateEventMaster =
"CREATE TABLE IF NOT EXISTS [EventMaster] ("
"[ID] INTEGER PRIMARY KEY,  "
"[Name] VARCHAR(200) NOT NULL, "
"[Interpreter] VARCHAR(10) DEFAULT 'Blockly', "
"[Type] VARCHAR(10) DEFAULT 'All', "
"[XMLStatement] TEXT NOT NULL, "
"[Status] INTEGER DEFAULT 0);";

static const char *sqlCreateEventRules =
"CREATE TABLE IF NOT EXISTS [EventRules] ("
"[ID] INTEGER PRIMARY KEY, "
"[EMID] INTEGER, "
"[Conditions] TEXT NOT NULL, "
"[Actions] TEXT NOT NULL, "
"[SequenceNo] INTEGER NOT NULL, "
"FOREIGN KEY (EMID) REFERENCES EventMaster(ID));";

static const char *sqlCreateZWaveNodes =
	"CREATE TABLE IF NOT EXISTS [ZWaveNodes] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[HardwareID] INTEGER NOT NULL, "
	"[HomeID] INTEGER NOT NULL, "
	"[NodeID] INTEGER NOT NULL, "
	"[Name] VARCHAR(100) DEFAULT Unknown, "
	"[ProductDescription] VARCHAR(100) DEFAULT Unknown, "
	"[PollTime] INTEGER DEFAULT 0);";

static const char *sqlCreateWOLNodes =
	"CREATE TABLE IF NOT EXISTS [WOLNodes] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[HardwareID] INTEGER NOT NULL, "
	"[Name] VARCHAR(100) DEFAULT Unknown, "
	"[MacAddress] VARCHAR(50) DEFAULT Unknown, "
	"[Timeout] INTEGER DEFAULT 5);";

static const char *sqlCreatePercentage =
"CREATE TABLE IF NOT EXISTS [Percentage] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Percentage] FLOAT NOT NULL, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreatePercentage_Calendar =
"CREATE TABLE IF NOT EXISTS [Percentage_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Percentage_Min] FLOAT NOT NULL, "
"[Percentage_Max] FLOAT NOT NULL, "
"[Percentage_Avg] FLOAT DEFAULT 0, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateFan =
"CREATE TABLE IF NOT EXISTS [Fan] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Speed] INTEGER NOT NULL, "
"[Date] DATETIME DEFAULT (datetime('now','localtime')));";

static const char *sqlCreateFan_Calendar =
"CREATE TABLE IF NOT EXISTS [Fan_Calendar] ("
"[DeviceRowID] BIGINT(10) NOT NULL, "
"[Speed_Min] INTEGER NOT NULL, "
"[Speed_Max] INTEGER NOT NULL, "
"[Speed_Avg] INTEGER DEFAULT 0, "
"[Date] DATE NOT NULL);";

static const char *sqlCreateBackupLog =
"CREATE TABLE IF NOT EXISTS [BackupLog] ("
"[Key] VARCHAR(50) NOT NULL, "
"[nValue] INTEGER DEFAULT 0); ";

static const char *sqlCreateEnoceanSensors =
	"CREATE TABLE IF NOT EXISTS [EnoceanSensors] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[HardwareID] INTEGER NOT NULL, "
	"[DeviceID] VARCHAR(25) NOT NULL, "
	"[Manufacturer] INTEGER NOT NULL, "
	"[Profile] INTEGER NOT NULL, "
	"[Type] INTEGER NOT NULL);";

static const char *sqlCreateHttpLink =
	"CREATE TABLE IF NOT EXISTS [HttpLink] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[DeviceID]  BIGINT NOT NULL, "
	"[DelimitedValue] INTEGER DEFAULT 0, "
	"[TargetType] INTEGER DEFAULT 0, "
	"[TargetVariable] VARCHAR(100), "
	"[TargetDeviceID] INTEGER, "
	"[TargetProperty] VARCHAR(100), "
	"[Enabled] INTEGER DEFAULT 1, "
	"[IncludeUnit] INTEGER default 0); ";

static const char *sqlCreatePushLink =
"CREATE TABLE IF NOT EXISTS [PushLink] ("
"[ID] INTEGER PRIMARY KEY, "
"[PushType] INTEGER, "
"[DeviceID]  BIGINT NOT NULL, "
"[DelimitedValue] INTEGER DEFAULT 0, "
"[TargetType] INTEGER DEFAULT 0, "
"[TargetVariable] VARCHAR(100), "
"[TargetDeviceID] INTEGER, "
"[TargetProperty] VARCHAR(100), "
"[Enabled] INTEGER DEFAULT 1, "
"[IncludeUnit] INTEGER default 0); ";

static const char *sqlCreateGooglePubSubLink =
"CREATE TABLE IF NOT EXISTS [GooglePubSubLink] ("
"[ID] INTEGER PRIMARY KEY, "
"[DeviceID]  BIGINT NOT NULL, "
"[DelimitedValue] INTEGER DEFAULT 0, "
"[TargetType] INTEGER DEFAULT 0, "
"[TargetVariable] VARCHAR(100), "
"[TargetDeviceID] INTEGER, "
"[TargetProperty] VARCHAR(100), "
"[Enabled] INTEGER DEFAULT 1, "
"[IncludeUnit] INTEGER default 0); ";

static const char *sqlCreateFibaroLink =
"CREATE TABLE IF NOT EXISTS [FibaroLink] ("
"[ID] INTEGER PRIMARY KEY, "
"[DeviceID]  BIGINT NOT NULL, "
"[DelimitedValue] INTEGER DEFAULT 0, "
"[TargetType] INTEGER DEFAULT 0, "
"[TargetVariable] VARCHAR(100), "
"[TargetDeviceID] INTEGER, "
"[TargetProperty] VARCHAR(100), "
"[Enabled] INTEGER DEFAULT 1, "
"[IncludeUnit] INTEGER default 0); ";

static const char *sqlCreateUserVariables =
	"CREATE TABLE IF NOT EXISTS [UserVariables] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[Name] VARCHAR(200), "
	"[ValueType] INT NOT NULL, "
	"[Value] VARCHAR(200), "
	"[LastUpdate] DATETIME DEFAULT(datetime('now', 'localtime')));";

static const char *sqlCreateFloorplans =
	"CREATE TABLE IF NOT EXISTS [Floorplans] ("
	"[ID] INTEGER PRIMARY KEY, "
	"[Name] VARCHAR(200) NOT NULL, "
	"[ImageFile] VARCHAR(100) NOT NULL, "
	"[ScaleFactor] FLOAT DEFAULT 1.0, "
	"[Order] INTEGER BIGINT(10) default 0);";

static const char *sqlCreateFloorplanOrderTrigger =
	"CREATE TRIGGER IF NOT EXISTS floorplanordertrigger AFTER INSERT ON Floorplans\n"
	"BEGIN\n"
	"	UPDATE Floorplans SET [Order] = (SELECT MAX([Order]) FROM Floorplans)+1 WHERE Floorplans.ID = NEW.ID;\n"
	"END;\n";

static const char *sqlCreateCustomImages =
	"CREATE TABLE IF NOT EXISTS [CustomImages]("
	"	[ID] INTEGER PRIMARY KEY, "
	"	[Base] VARCHAR(80) NOT NULL, "
	"	[Name] VARCHAR(80) NOT NULL, "
	"	[Description] VARCHAR(80) NOT NULL, "
	"	[IconSmall] BLOB, "
	"	[IconOn] BLOB, "
	"	[IconOff] BLOB);";

static const char *sqlCreateMySensors =
	"CREATE TABLE IF NOT EXISTS [MySensors]("
	" [HardwareID] INTEGER NOT NULL,"
	" [ID] INTEGER NOT NULL,"
	" [Name] VARCHAR(100) DEFAULT Unknown,"
	" [SketchName] VARCHAR(100) DEFAULT Unknown,"
	" [SketchVersion] VARCHAR(40) DEFAULT(1.0));";

static const char *sqlCreateMySensorsVariables =
	"CREATE TABLE IF NOT EXISTS [MySensorsVars]("
	" [HardwareID] INTEGER NOT NULL,"
	" [NodeID] INTEGER NOT NULL,"
	" [ChildID] INTEGER NOT NULL,"
	" [VarID] INTEGER NOT NULL,"
	" [Value] VARCHAR(100) NOT NULL);";

static const char *sqlCreateMySensorsChilds =
"CREATE TABLE IF NOT EXISTS [MySensorsChilds]("
" [HardwareID] INTEGER NOT NULL,"
" [NodeID] INTEGER NOT NULL,"
" [ChildID] INTEGER NOT NULL,"
" [Name] VARCHAR(100) DEFAULT '',"
" [Type] INTEGER NOT NULL,"
" [UseAck] INTEGER DEFAULT 0,"
" [AckTimeout] INTEGER DEFAULT 1200);";

static const char *sqlCreateToonDevices =
	"CREATE TABLE IF NOT EXISTS [ToonDevices]("
	" [HardwareID] INTEGER NOT NULL,"
	" [UUID] VARCHAR(100) NOT NULL);";

static const char *sqlCreateUserSessions =
	"CREATE TABLE IF NOT EXISTS [UserSessions]("
	" [SessionID] VARCHAR(100) NOT NULL,"
	" [Username] VARCHAR(100) NOT NULL,"
	" [AuthToken] VARCHAR(100) UNIQUE NOT NULL,"
	" [ExpirationDate] DATETIME NOT NULL,"
	" [RemoteHost] VARCHAR(50) NOT NULL,"
	" [LastUpdate] DATETIME DEFAULT(datetime('now', 'localtime')),"
	" PRIMARY KEY([SessionID]));";

static const char *sqlCreateMobileDevices =
"CREATE TABLE IF NOT EXISTS [MobileDevices]("
"[ID] INTEGER PRIMARY KEY, "
"[Active] BOOLEAN DEFAULT false, "
"[Name] VARCHAR(100) DEFAULT '',"
"[DeviceType] VARCHAR(100) DEFAULT '',"
"[SenderID] TEXT NOT NULL,"
"[UUID] TEXT NOT NULL, "
"[LastUpdate] DATETIME DEFAULT(datetime('now', 'localtime'))"
");";

bool CSQLHelper::OpenDatabase()
{
	//Open Database
	int rc = sqlite3_open(m_dbase_name.c_str(), &m_dbase);
	if (rc)
	{
		_log.Log(LOG_ERROR,"Error opening SQLite3 database: %s", sqlite3_errmsg(m_dbase));
		sqlite3_close(m_dbase);
		return false;
	}
#ifndef WIN32
	//test, this could improve performance
	sqlite3_exec(m_dbase, "PRAGMA synchronous = NORMAL", NULL, NULL, NULL);
	sqlite3_exec(m_dbase, "PRAGMA journal_mode = WAL", NULL, NULL, NULL);
#else
	sqlite3_exec(m_dbase, "PRAGMA journal_mode=DELETE", NULL, NULL, NULL);
#endif
    sqlite3_exec(m_dbase, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
	std::vector<std::vector<std::string> > result=query("SELECT name FROM sqlite_master WHERE type='table' AND name='DeviceStatus'");
	bool bNewInstall=(result.size()==0);
	int dbversion=0;
	if (!bNewInstall)
	{
		GetPreferencesVar("DB_Version", dbversion);
		if (dbversion > DB_VERSION)
		{
			_log.Log(LOG_ERROR, "Database requires newer version (%d > %d)", dbversion, DB_VERSION);
			return false;
		}
		//Pre-SQL Patches
	}

	//create database (if not exists)
	query(sqlCreateDeviceStatus);
	query(sqlCreateDeviceStatusTrigger);
	query(sqlCreateEventActions);
	query(sqlCreateEventActionsTrigger);
	query(sqlCreateLightingLog);
	query(sqlCreateSceneLog);
	query(sqlCreatePreferences);
	query(sqlCreateRain);
	query(sqlCreateRain_Calendar);
	query(sqlCreateTemperature);
	query(sqlCreateTemperature_Calendar);
	query(sqlCreateTimers);
	query(sqlCreateSetpointTimers);
	query(sqlCreateUV);
	query(sqlCreateUV_Calendar);
	query(sqlCreateWind);
	query(sqlCreateWind_Calendar);
	query(sqlCreateMeter);
	query(sqlCreateMeter_Calendar);
	query(sqlCreateMultiMeter);
	query(sqlCreateMultiMeter_Calendar);
	query(sqlCreateNotifications);
	query(sqlCreateHardware);
	query(sqlCreateUsers);
	query(sqlCreateLightSubDevices);
    query(sqlCreateCameras);
	query(sqlCreateCamerasActiveDevices);
    query(sqlCreatePlanMappings);
	query(sqlCreateDevicesToPlanStatusTrigger);
    query(sqlCreatePlans);
	query(sqlCreatePlanOrderTrigger);
	query(sqlCreateScenes);
	query(sqlCreateScenesTrigger);
	query(sqlCreateSceneDevices);
	query(sqlCreateSceneDeviceTrigger);
	query(sqlCreateTimerPlans);
	query(sqlCreateSceneTimers);
	query(sqlCreateSharedDevices);
    query(sqlCreateEventMaster);
    query(sqlCreateEventRules);
	query(sqlCreateZWaveNodes);
	query(sqlCreateWOLNodes);
	query(sqlCreatePercentage);
	query(sqlCreatePercentage_Calendar);
	query(sqlCreateFan);
	query(sqlCreateFan_Calendar);
	query(sqlCreateBackupLog);
	query(sqlCreateEnoceanSensors);
	query(sqlCreateFibaroLink);
	query(sqlCreateHttpLink);
	query(sqlCreatePushLink);
	query(sqlCreateGooglePubSubLink);
	query(sqlCreateUserVariables);
	query(sqlCreateFloorplans);
	query(sqlCreateFloorplanOrderTrigger);
	query(sqlCreateCustomImages);
	query(sqlCreateMySensors);
	query(sqlCreateMySensorsVariables);
	query(sqlCreateMySensorsChilds);
	query(sqlCreateToonDevices);
	query(sqlCreateUserSessions);
	query(sqlCreateMobileDevices);
	//Add indexes to log tables
	query("create index if not exists ds_hduts_idx    on DeviceStatus(HardwareID, DeviceID, Unit, Type, SubType);");
	query("create index if not exists f_id_idx        on Fan(DeviceRowID);");
	query("create index if not exists f_id_date_idx   on Fan(DeviceRowID, Date);");
	query("create index if not exists fc_id_idx       on Fan_Calendar(DeviceRowID);");
	query("create index if not exists fc_id_date_idx  on Fan_Calendar(DeviceRowID, Date);");
	query("create index if not exists ll_id_idx       on LightingLog(DeviceRowID);");
	query("create index if not exists ll_id_date_idx  on LightingLog(DeviceRowID, Date);");
	query("create index if not exists sl_id_idx       on SceneLog(SceneRowID);");
	query("create index if not exists sl_id_date_idx  on SceneLog(SceneRowID, Date);");
	query("create index if not exists m_id_idx        on Meter(DeviceRowID);");
	query("create index if not exists m_id_date_idx   on Meter(DeviceRowID, Date);");
	query("create index if not exists mc_id_idx       on Meter_Calendar(DeviceRowID);");
	query("create index if not exists mc_id_date_idx  on Meter_Calendar(DeviceRowID, Date);");
	query("create index if not exists mm_id_idx       on MultiMeter(DeviceRowID);");
	query("create index if not exists mm_id_date_idx  on MultiMeter(DeviceRowID, Date);");
	query("create index if not exists mmc_id_idx      on MultiMeter_Calendar(DeviceRowID);");
	query("create index if not exists mmc_id_date_idx on MultiMeter_Calendar(DeviceRowID, Date);");
	query("create index if not exists p_id_idx        on Percentage(DeviceRowID);");
	query("create index if not exists p_id_date_idx   on Percentage(DeviceRowID, Date);");
	query("create index if not exists pc_id_idx       on Percentage_Calendar(DeviceRowID);");
	query("create index if not exists pc_id_date_idx  on Percentage_Calendar(DeviceRowID, Date);");
	query("create index if not exists r_id_idx        on Rain(DeviceRowID);");
	query("create index if not exists r_id_date_idx   on Rain(DeviceRowID, Date);");
	query("create index if not exists rc_id_idx       on Rain_Calendar(DeviceRowID);");
	query("create index if not exists rc_id_date_idx  on Rain_Calendar(DeviceRowID, Date);");
	query("create index if not exists t_id_idx        on Temperature(DeviceRowID);");
	query("create index if not exists t_id_date_idx   on Temperature(DeviceRowID, Date);");
	query("create index if not exists tc_id_idx       on Temperature_Calendar(DeviceRowID);");
	query("create index if not exists tc_id_date_idx  on Temperature_Calendar(DeviceRowID, Date);");
	query("create index if not exists u_id_idx        on UV(DeviceRowID);");
	query("create index if not exists u_id_date_idx   on UV(DeviceRowID, Date);");
	query("create index if not exists uv_id_idx       on UV_Calendar(DeviceRowID);");
	query("create index if not exists uv_id_date_idx  on UV_Calendar(DeviceRowID, Date);");
	query("create index if not exists w_id_idx        on Wind(DeviceRowID);");
	query("create index if not exists w_id_date_idx   on Wind(DeviceRowID, Date);");
	query("create index if not exists wc_id_idx       on Wind_Calendar(DeviceRowID);");
	query("create index if not exists wc_id_date_idx  on Wind_Calendar(DeviceRowID, Date);");

	if ((!bNewInstall) && (dbversion < DB_VERSION))
	{
		//Post-SQL Patches
		if (dbversion < 2)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [Order] INTEGER BIGINT(10) default 0");
			query(sqlCreateDeviceStatusTrigger);
			CheckAndUpdateDeviceOrder();
		}
		if (dbversion < 3)
		{
			query("ALTER TABLE Hardware ADD COLUMN [Enabled] INTEGER default 1");
		}
		if (dbversion < 4)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [AddjValue] FLOAT default 0");
			query("ALTER TABLE DeviceStatus ADD COLUMN [AddjMulti] FLOAT default 1");
		}
		if (dbversion < 5)
		{
			query("ALTER TABLE SceneDevices ADD COLUMN [Cmd] INTEGER default 1");
			query("ALTER TABLE SceneDevices ADD COLUMN [Level] INTEGER default 100");
		}
		if (dbversion < 6)
		{
			query("ALTER TABLE Cameras ADD COLUMN [ImageURL] VARCHAR(100)");
		}
		if (dbversion < 7)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [AddjValue2] FLOAT default 0");
			query("ALTER TABLE DeviceStatus ADD COLUMN [AddjMulti2] FLOAT default 1");
		}
		if (dbversion < 8)
		{
			query("DROP TABLE IF EXISTS [Cameras]");
			query(sqlCreateCameras);
		}
		if (dbversion < 9) {
			query("UPDATE Notifications SET Params = 'S' WHERE Params = ''");
		}
		if (dbversion < 10)
		{
			//P1 Smart meter power change, need to delete all short logs from today
			char szDateStart[40];
			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			struct tm ltime;
			ltime.tm_isdst = tm1.tm_isdst;
			ltime.tm_hour = 0;
			ltime.tm_min = 0;
			ltime.tm_sec = 0;
			ltime.tm_year = tm1.tm_year;
			ltime.tm_mon = tm1.tm_mon;
			ltime.tm_mday = tm1.tm_mday;

			sprintf(szDateStart, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);

			std::vector<std::vector<std::string> > result;
			result = safe_query("SELECT ID FROM DeviceStatus WHERE (Type=%d)", pTypeP1Power);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string idx = sd[0];
					safe_query("DELETE FROM MultiMeter WHERE (DeviceRowID='%q') AND (Date>='%q')", idx.c_str(), szDateStart);
				}
			}
		}
		if (dbversion < 11)
		{
			std::vector<std::vector<std::string> > result;

			result = safe_query("SELECT ID, Username, Password FROM Cameras ORDER BY ID");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string camuser = base64_encode((const unsigned char*)sd[1].c_str(), sd[1].size());
					std::string campwd = base64_encode((const unsigned char*)sd[2].c_str(), sd[2].size());
					safe_query("UPDATE Cameras SET Username='%q', Password='%q' WHERE (ID=='%q')",
						camuser.c_str(), campwd.c_str(), sd[0].c_str());
				}
			}
		}
		if (dbversion < 12)
		{
			std::vector<std::vector<std::string> > result;
			result = query("SELECT t.RowID, u.RowID from MultiMeter_Calendar as t, MultiMeter_Calendar as u WHERE (t.[Date] == u.[Date]) AND (t.[DeviceRowID] == u.[DeviceRowID]) AND (t.[RowID] != u.[RowID])");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					++itt;
					std::vector<std::string> sd = *itt;
					safe_query("DELETE FROM MultiMeter_Calendar WHERE (RowID=='%q')", sd[0].c_str());
				}
			}

		}
		if (dbversion < 13)
		{
			DeleteHardware("1001");
		}
		if (dbversion < 14)
		{
			query("ALTER TABLE Users ADD COLUMN [RemoteSharing] INTEGER default 0");
		}
		if (dbversion < 15)
		{
			query("DROP TABLE IF EXISTS [HardwareSharing]");
			query("ALTER TABLE DeviceStatus ADD COLUMN [LastLevel] INTEGER default 0");
		}
		if (dbversion < 16)
		{
			query("ALTER TABLE Events RENAME TO tmp_Events;");
			query("CREATE TABLE Events ([ID] INTEGER PRIMARY KEY, [Name] VARCHAR(200) NOT NULL, [XMLStatement] TEXT NOT NULL,[Conditions] TEXT, [Actions] TEXT);");
			query("INSERT INTO Events(Name, XMLStatement) SELECT Name, XMLStatement FROM tmp_Events;");
			query("DROP TABLE tmp_Events;");
		}
		if (dbversion < 17)
		{
			query("ALTER TABLE Events ADD COLUMN [Status] INTEGER default 0");
		}
		if (dbversion < 18)
		{
			query("ALTER TABLE Temperature ADD COLUMN [DewPoint] FLOAT default 0");
			query("ALTER TABLE Temperature_Calendar ADD COLUMN [DewPoint] FLOAT default 0");
		}
		if (dbversion < 19)
		{
			query("ALTER TABLE Scenes ADD COLUMN [SceneType] INTEGER default 0");
		}

		if (dbversion < 20)
		{
			query("INSERT INTO EventMaster(Name, XMLStatement, Status) SELECT Name, XMLStatement, Status FROM Events;");
			query("INSERT INTO EventRules(EMID, Conditions, Actions, SequenceNo) SELECT EventMaster.ID, Events.Conditions, Events.Actions, 1 FROM Events INNER JOIN EventMaster ON EventMaster.Name = Events.Name;");
			query("DROP TABLE Events;");
		}
		if (dbversion < 21)
		{
			//increase Video/Image URL for camera's
			//create a backup
			query("ALTER TABLE Cameras RENAME TO tmp_Cameras");
			//Create the new table
			query(sqlCreateCameras);
			//Copy values from tmp_Cameras back into our new table
			query(
				"INSERT INTO Cameras([ID],[Name],[Enabled],[Address],[Port],[Username],[Password],[ImageURL])"
				"SELECT [ID],[Name],[Enabled],[Address],[Port],[Username],[Password],[ImageURL]"
				"FROM tmp_Cameras");
			//Drop the tmp_Cameras table
			query("DROP TABLE tmp_Cameras");
		}
		if (dbversion < 22)
		{
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [Order] INTEGER BIGINT(10) default 0");
			query(sqlCreateDevicesToPlanStatusTrigger);
		}
		if (dbversion < 23)
		{
			query("ALTER TABLE Temperature_Calendar ADD COLUMN [Temp_Avg] FLOAT default 0");

			std::vector<std::vector<std::string> > result;
			result = query("SELECT RowID, (Temp_Max+Temp_Min)/2 FROM Temperature_Calendar");
			if (result.size() > 0)
			{
				sqlite3_exec(m_dbase, "BEGIN TRANSACTION;", NULL, NULL, NULL);
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					safe_query("UPDATE Temperature_Calendar SET Temp_Avg=%.1f WHERE RowID='%q'", atof(sd[1].c_str()), sd[0].c_str());
				}
				sqlite3_exec(m_dbase, "END TRANSACTION;", NULL, NULL, NULL);
			}
		}
		if (dbversion < 24)
		{
			query("ALTER TABLE SceneDevices ADD COLUMN [Order] INTEGER BIGINT(10) default 0");
			query(sqlCreateSceneDeviceTrigger);
			CheckAndUpdateSceneDeviceOrder();
		}
		if (dbversion < 25)
		{
			query("DROP TABLE IF EXISTS [Plans]");
			query(sqlCreatePlans);
			query(sqlCreatePlanOrderTrigger);
		}
		if (dbversion < 26)
		{
			query("DROP TABLE IF EXISTS [DeviceToPlansMap]");
			query(sqlCreatePlanMappings);
			query(sqlCreateDevicesToPlanStatusTrigger);
		}
		if (dbversion < 27)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [CustomImage] INTEGER default 0");
		}
		if (dbversion < 28)
		{
			query("ALTER TABLE Timers ADD COLUMN [UseRandomness] INTEGER default 0");
			query("ALTER TABLE SceneTimers ADD COLUMN [UseRandomness] INTEGER default 0");
			query("UPDATE Timers SET [Type]=2, [UseRandomness]=1 WHERE ([Type]=5)");
			query("UPDATE SceneTimers SET [Type]=2, [UseRandomness]=1 WHERE ([Type]=5)");
			//"[] INTEGER DEFAULT 0, "
		}
		if (dbversion < 30)
		{
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [DevSceneType] INTEGER default 0");
		}
		if (dbversion < 31)
		{
			query("ALTER TABLE Users ADD COLUMN [TabsEnabled] INTEGER default 255");
		}
		if (dbversion < 32)
		{
			query("ALTER TABLE SceneDevices ADD COLUMN [Hue] INTEGER default 0");
			query("ALTER TABLE SceneTimers ADD COLUMN [Hue] INTEGER default 0");
			query("ALTER TABLE Timers ADD COLUMN [Hue] INTEGER default 0");
		}
		if (dbversion < 33)
		{
			query("DROP TABLE IF EXISTS [Load]");
			query("DROP TABLE IF EXISTS [Load_Calendar]");
			query("DROP TABLE IF EXISTS [Fan]");
			query("DROP TABLE IF EXISTS [Fan_Calendar]");
			query(sqlCreatePercentage);
			query(sqlCreatePercentage_Calendar);
			query(sqlCreateFan);
			query(sqlCreateFan_Calendar);

			std::vector<std::vector<std::string> > result;
			result = query("SELECT ID FROM DeviceStatus WHERE (DeviceID LIKE 'WMI%')");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string idx = sd[0];
					safe_query("DELETE FROM Temperature WHERE (DeviceRowID='%q')", idx.c_str());
					safe_query("DELETE FROM Temperature_Calendar WHERE (DeviceRowID='%q')", idx.c_str());
				}
			}
			query("DELETE FROM DeviceStatus WHERE (DeviceID LIKE 'WMI%')");
		}
		if (dbversion < 34)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [StrParam1] VARCHAR(200) DEFAULT ''");
			query("ALTER TABLE DeviceStatus ADD COLUMN [StrParam2] VARCHAR(200) DEFAULT ''");
		}
		if (dbversion < 35)
		{
			query("ALTER TABLE Notifications ADD COLUMN [Priority] INTEGER default 0");
		}
		if (dbversion < 36)
		{
			query("ALTER TABLE Meter ADD COLUMN [Usage] INTEGER default 0");
		}
		if (dbversion < 37)
		{
			//move all load data from tables into the new percentage one
			query(
				"INSERT INTO Percentage([DeviceRowID],[Percentage],[Date])"
				"SELECT [DeviceRowID],[Load],[Date] FROM Load");
			query(
				"INSERT INTO Percentage_Calendar([DeviceRowID],[Percentage_Min],[Percentage_Max],[Percentage_Avg],[Date])"
				"SELECT [DeviceRowID],[Load_Min],[Load_Max],[Load_Avg],[Date] FROM Load_Calendar");
			query("DROP TABLE IF EXISTS [Load]");
			query("DROP TABLE IF EXISTS [Load_Calendar]");
		}
		if (dbversion < 38)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [Protected] INTEGER default 0");
		}
		if (dbversion < 39)
		{
			query("ALTER TABLE Scenes ADD COLUMN [Protected] INTEGER default 0");
		}
		if (dbversion < 40)
		{
			FixDaylightSaving();
		}
		if (dbversion < 41)
		{
			query("ALTER TABLE FibaroLink ADD COLUMN [IncludeUnit] INTEGER default 0");
		}
		if (dbversion < 42)
		{
			query("INSERT INTO Plans (Name) VALUES ('$Hidden Devices')");
		}
		if (dbversion < 43)
		{
			query("ALTER TABLE Scenes ADD COLUMN [OnAction] VARCHAR(200) DEFAULT ''");
			query("ALTER TABLE Scenes ADD COLUMN [OffAction] VARCHAR(200) DEFAULT ''");
		}
		if (dbversion < 44)
		{
			//Drop VideoURL
			//create a backup
			query("ALTER TABLE Cameras RENAME TO tmp_Cameras");
			//Create the new table
			query(sqlCreateCameras);
			//Copy values from tmp_Cameras back into our new table
			query(
				"INSERT INTO Cameras([ID],[Name],[Enabled],[Address],[Port],[Username],[Password],[ImageURL])"
				"SELECT [ID],[Name],[Enabled],[Address],[Port],[Username],[Password],[ImageURL]"
				"FROM tmp_Cameras");
			//Drop the tmp_Cameras table
			query("DROP TABLE tmp_Cameras");
		}
		if (dbversion < 45)
		{
			query("ALTER TABLE Timers ADD COLUMN [TimerPlan] INTEGER default 0");
			query("ALTER TABLE SceneTimers ADD COLUMN [TimerPlan] INTEGER default 0");
		}
		if (dbversion < 46)
		{
			query("ALTER TABLE Plans ADD COLUMN [FloorplanID] INTEGER default 0");
			query("ALTER TABLE Plans ADD COLUMN [Area] VARCHAR(200) DEFAULT ''");
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [XOffset] INTEGER default 0");
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [YOffset] INTEGER default 0");
		}
		if (dbversion < 47)
		{
			query("ALTER TABLE Hardware ADD COLUMN [DataTimeout] INTEGER default 0");
		}
		if (dbversion < 48)
		{
			result = safe_query("SELECT ID FROM DeviceStatus WHERE (Type=%d)", pTypeUsage);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string idx = sd[0];
					safe_query("UPDATE Meter SET Value = Value * 10 WHERE (DeviceRowID='%q')", idx.c_str());
					safe_query("UPDATE Meter_Calendar SET Value = Value * 10 WHERE (DeviceRowID='%q')", idx.c_str());
					safe_query("UPDATE MultiMeter_Calendar SET Value1 = Value1 * 10, Value2 = Value2 * 10 WHERE (DeviceRowID='%q')", idx.c_str());
				}
			}
			query("ALTER TABLE Hardware ADD COLUMN [DataTimeout] INTEGER default 0");
		}
		if (dbversion < 49)
		{
			query("ALTER TABLE Plans ADD COLUMN [FloorplanID] INTEGER default 0");
			query("ALTER TABLE Plans ADD COLUMN [Area] VARCHAR(200) DEFAULT ''");
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [XOffset] INTEGER default 0");
			query("ALTER TABLE DeviceToPlansMap ADD COLUMN [YOffset] INTEGER default 0");
		}
		if (dbversion < 50)
		{
			query("ALTER TABLE Timers ADD COLUMN [Date] DATE default 0");
			query("ALTER TABLE SceneTimers ADD COLUMN [Date] DATE default 0");
		}
		if (dbversion < 51)
		{
			query("ALTER TABLE Meter_Calendar ADD COLUMN [Counter] BIGINT default 0");
			query("ALTER TABLE MultiMeter_Calendar ADD COLUMN [Counter1] BIGINT default 0");
			query("ALTER TABLE MultiMeter_Calendar ADD COLUMN [Counter2] BIGINT default 0");
			query("ALTER TABLE MultiMeter_Calendar ADD COLUMN [Counter3] BIGINT default 0");
			query("ALTER TABLE MultiMeter_Calendar ADD COLUMN [Counter4] BIGINT default 0");
		}
		if (dbversion < 52)
		{
			//Move onboard system sensor (temperature) to the motherboard hardware
			std::vector<std::vector<std::string> > result;
			result = safe_query("SELECT ID FROM Hardware WHERE (Type==%d) AND (Name=='Motherboard') LIMIT 1", HTYPE_System);
			if (!result.empty())
			{
				int hwId = atoi(result[0][0].c_str());
				safe_query("UPDATE DeviceStatus SET HardwareID=%d WHERE (HardwareID=1000)", hwId);
			}
		}
		if (dbversion < 53)
		{
			query("ALTER TABLE Floorplans ADD COLUMN [ScaleFactor] Float default 1.0");
		}
		if (dbversion < 54)
		{
			query("ALTER TABLE Temperature ADD COLUMN [SetPoint] FLOAT default 0");
			query("ALTER TABLE Temperature_Calendar ADD COLUMN [SetPoint_Min] FLOAT default 0");
			query("ALTER TABLE Temperature_Calendar ADD COLUMN [SetPoint_Max] FLOAT default 0");
			query("ALTER TABLE Temperature_Calendar ADD COLUMN [SetPoint_Avg] FLOAT default 0");
		}
		if (dbversion < 55)
		{
			query("DROP TABLE IF EXISTS [CustomImages]");
			query(sqlCreateCustomImages);
		}
		if (dbversion < 56)
		{
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result2;
			std::vector<std::vector<std::string> >::const_iterator itt;
			std::string pHash;
			szQuery2 << "SELECT sValue FROM Preferences WHERE (Key='WebPassword')";
			result2 = query(szQuery2.str());
			if (result2.size() > 0)
			{
				std::string pwd = result2[0][0];
				if (pwd.size() != 32)
				{
					pHash = GenerateMD5Hash(base64_decode(pwd));
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Preferences SET sValue='" << pHash << "' WHERE (Key='WebPassword')";
					query(szQuery2.str());
				}
			}

			szQuery2.clear();
			szQuery2.str("");
			szQuery2 << "SELECT sValue FROM Preferences WHERE (Key='SecPassword')";
			result2 = query(szQuery2.str());
			if (result2.size() > 0)
			{
				std::string pwd = result2[0][0];
				if (pwd.size() != 32)
				{
					pHash = GenerateMD5Hash(base64_decode(pwd));
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Preferences SET sValue='" << pHash << "' WHERE (Key='SecPassword')";
					query(szQuery2.str());
				}
			}

			szQuery2.clear();
			szQuery2.str("");
			szQuery2 << "SELECT sValue FROM Preferences WHERE (Key='ProtectionPassword')";
			result2 = query(szQuery2.str());
			if (result2.size() > 0)
			{
				std::string pwd = result2[0][0];
				if (pwd.size() != 32)
				{
					pHash = GenerateMD5Hash(base64_decode(pwd));
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Preferences SET sValue='" << pHash << "' WHERE (Key='ProtectionPassword')";
					query(szQuery2.str());
				}
			}
			szQuery2.clear();
			szQuery2.str("");
			szQuery2 << "SELECT ID, Password FROM Users";
			result2 = query(szQuery2.str());
			for (itt = result2.begin(); itt != result2.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				std::string pwd = sd[1];
				if (pwd.size() != 32)
				{
					pHash = GenerateMD5Hash(base64_decode(pwd));
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Users SET Password='" << pHash << "' WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}

			szQuery2.clear();
			szQuery2.str("");
			szQuery2 << "SELECT ID, Password FROM Hardware WHERE ([Type]==" << HTYPE_Domoticz << ")";
			result2 = query(szQuery2.str());
			for (itt = result2.begin(); itt != result2.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				std::string pwd = sd[1];
				if (pwd.size() != 32)
				{
					pHash = GenerateMD5Hash(pwd);
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Hardware SET Password='" << pHash << "' WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 57)
		{
			//S0 Meter patch
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, Mode1, Mode2, Mode3, Mode4 FROM HARDWARE WHERE([Type]==" << HTYPE_S0SmartMeterUSB << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::stringstream szAddress;
					szAddress
						<< sd[1] << ";" << sd[2] << ";"
						<< sd[3] << ";" << sd[4] << ";"
						<< sd[1] << ";" << sd[2] << ";"
						<< sd[1] << ";" << sd[2] << ";"
						<< sd[1] << ";" << sd[2];
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Hardware SET Address='" << szAddress.str() << "', Mode1=0, Mode2=0, Mode3=0, Mode4=0 WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 58)
		{
			//Patch for new ZWave light sensor type
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			result = query("SELECT ID FROM Hardware WHERE ([Type] = 9) OR ([Type] = 21)");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					szQuery2.clear();
					szQuery2.str("");
					//#define sTypeZWaveSwitch 0xA1
					szQuery2 << "UPDATE DeviceStatus SET SubType=" << 0xA1 << " WHERE ([Type]=" << pTypeLighting2 << ") AND (SubType=" << sTypeAC << ") AND (HardwareID=" << result[0][0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 60)
		{
			query("ALTER TABLE SceneDevices ADD COLUMN [OnDelay] INTEGER default 0");
			query("ALTER TABLE SceneDevices ADD COLUMN [OffDelay] INTEGER default 0");
		}
		if (dbversion < 61)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [Description] VARCHAR(200) DEFAULT ''");
		}
		if (dbversion < 62)
		{
			//Fix for Teleinfo hardware, where devices where created with Hardware_ID=0
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			result = query("SELECT ID FROM Hardware WHERE ([Type] = 19)");
			if (result.size() > 0)
			{
				int TeleInfoHWID = atoi(result[0][0].c_str());
				szQuery2 << "UPDATE DeviceStatus SET HardwareID=" << TeleInfoHWID << " WHERE ([HardwareID]=0)";
				query(szQuery2.str());
			}
		}
		if (dbversion < 63)
		{
			query("DROP TABLE IF EXISTS [TempVars]");
		}
		if (dbversion < 64)
		{
			FixDaylightSaving();
		}
		if (dbversion < 65)
		{
			//Patch for Toon, reverse counters
			std::stringstream szQuery;
			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> > result2;
			std::vector<std::vector<std::string> > result3;
			std::vector<std::vector<std::string> >::const_iterator itt;
			std::vector<std::vector<std::string> >::const_iterator itt2;
			std::vector<std::vector<std::string> >::const_iterator itt3;
			szQuery << "SELECT ID FROM HARDWARE WHERE([Type]==" << HTYPE_TOONTHERMOSTAT << ")";
			result = query(szQuery.str());
			for (itt = result.begin(); itt != result.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				int hwid = atoi(sd[0].c_str());

				szQuery.clear();
				szQuery.str("");
				szQuery << "SELECT ID FROM DeviceStatus WHERE (Type=" << pTypeP1Power << ") AND (HardwareID=" << hwid << ")";
				result2 = query(szQuery.str());
				for (itt2 = result2.begin(); itt2 != result2.end(); ++itt2)
				{
					std::vector<std::string> sd = *itt2;

					//First the shortlog
					szQuery.clear();
					szQuery.str("");
					szQuery << "SELECT ROWID, Value1, Value2, Value3, Value4, Value5, Value6 FROM MultiMeter WHERE (DeviceRowID==" << sd[0] << ")";
					result3 = query(szQuery.str());
					for (itt3 = result3.begin(); itt3 != result3.end(); ++itt3)
					{
						std::vector<std::string> sd = *itt3;
						//value1 = powerusage1;
						//value2 = powerdeliv1;
						//value5 = powerusage2;
						//value6 = powerdeliv2;
						//value3 = usagecurrent;
						//value4 = delivcurrent;
						szQuery.clear();
						szQuery.str("");
						szQuery << "UPDATE MultiMeter SET Value1=" << sd[5] << ", Value2=" << sd[6] << ", Value5=" << sd[1] << ", Value6=" << sd[2] << " WHERE (ROWID=" << sd[0] << ")";
						query(szQuery.str());
					}
					//Next for the calendar
					szQuery.clear();
					szQuery.str("");
					szQuery << "SELECT ROWID, Value1, Value2, Value3, Value4, Value5, Value6, Counter1, Counter2, Counter3, Counter4 FROM MultiMeter_Calendar WHERE (DeviceRowID==" << sd[0] << ")";
					result3 = query(szQuery.str());
					for (itt3 = result3.begin(); itt3 != result3.end(); ++itt3)
					{
						std::vector<std::string> sd = *itt3;
						szQuery.clear();
						szQuery.str("");
						szQuery << "UPDATE MultiMeter_Calendar SET Value1=" << sd[5] << ", Value2=" << sd[6] << ", Value5=" << sd[1] << ", Value6=" << sd[2] << ", Counter1=" << sd[9] << ", Counter2=" << sd[10] << ", Counter3=" << sd[7] << ", Counter4=" << sd[8] << " WHERE (ROWID=" << sd[0] << ")";
						query(szQuery.str());
					}
				}
			}
		}
		if (dbversion < 66)
		{
			query("ALTER TABLE Hardware ADD COLUMN [Mode6] CHAR default 0");
		}
		if (dbversion < 67)
		{
			//Enable all notification systems
			UpdatePreferencesVar("NMAEnabled", 1);
			UpdatePreferencesVar("ProwlEnabled", 1);
			UpdatePreferencesVar("PushALotEnabled", 1);
			UpdatePreferencesVar("PushoverEnabled", 1);
			UpdatePreferencesVar("PushsaferEnabled", 1);
			UpdatePreferencesVar("ClickatellEnabled", 1);
		}
		if (dbversion < 68)
		{
			query("ALTER TABLE Notifications ADD COLUMN [CustomMessage] VARCHAR(300) DEFAULT ('')");
			query("ALTER TABLE Notifications ADD COLUMN [ActiveSystems] VARCHAR(300) DEFAULT ('')");
		}
		if (dbversion < 69)
		{
			//Serial Port patch (using complete port paths now)
			query("ALTER TABLE Hardware ADD COLUMN [SerialPort] VARCHAR(50) DEFAULT ('')");

			//Convert all serial hardware to use the new column
			std::stringstream szQuery;
			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> >::const_iterator itt;
			szQuery << "SELECT ID,[Type],Port FROM Hardware";
			result = query(szQuery.str());
			if (!result.empty())
			{
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					int hwId = atoi(sd[0].c_str());
					_eHardwareTypes hwtype = (_eHardwareTypes)atoi(sd[1].c_str());
					size_t Port = (size_t)atoi(sd[2].c_str());

					if (IsSerialDevice(hwtype))
					{
						char szSerialPort[50];
#if defined WIN32
						sprintf(szSerialPort, "COM%d", (int)Port);
#else
						bool bUseDirectPath = false;
						std::vector<std::string> serialports = GetSerialPorts(bUseDirectPath);
						if (bUseDirectPath)
						{
							if (Port >= serialports.size())
							{
								_log.Log(LOG_ERROR, "Serial Port out of range!...");
								continue;
							}
							strcpy(szSerialPort, serialports[Port].c_str());
						}
						else
						{
							sprintf(szSerialPort, "/dev/ttyUSB%d", (int)Port);
						}
#endif
						safe_query("UPDATE Hardware SET Port=0, SerialPort='%q' WHERE (ID=%d)",
							szSerialPort, hwId);
					}
				}
			}
		}
		if (dbversion < 70)
		{
			query("ALTER TABLE [WOLNodes] ADD COLUMN [Timeout] INTEGER DEFAULT 5");
		}
		if (dbversion < 71)
		{
			//Dropping debug cleanup triggers
			query("DROP TRIGGER IF EXISTS onTemperatureDelete");
			query("DROP TRIGGER IF EXISTS onRainDelete");
			query("DROP TRIGGER IF EXISTS onWindDelete");
			query("DROP TRIGGER IF EXISTS onUVDelete");
			query("DROP TRIGGER IF EXISTS onMeterDelete");
			query("DROP TRIGGER IF EXISTS onMultiMeterDelete");
			query("DROP TRIGGER IF EXISTS onPercentageDelete");
			query("DROP TRIGGER IF EXISTS onFanDelete");
		}
		if (dbversion < 72)
		{
			query("ALTER TABLE [Notifications] ADD COLUMN [SendAlways] INTEGER DEFAULT 0");
		}
		if (dbversion < 73)
		{
			if (!DoesColumnExistsInTable("Description", "DeviceStatus"))
			{
				query("ALTER TABLE DeviceStatus ADD COLUMN [Description] VARCHAR(200) DEFAULT ''");
			}
			query("ALTER TABLE Scenes ADD COLUMN [Description] VARCHAR(200) DEFAULT ''");
		}
		if (dbversion < 74)
		{
			if (!DoesColumnExistsInTable("Description", "DeviceStatus"))
			{
				query("ALTER TABLE DeviceStatus ADD COLUMN [Description] VARCHAR(200) DEFAULT ''");
			}
		}
		if (dbversion < 75)
		{
			safe_query("UPDATE Hardware SET Username='%q', Password='%q' WHERE ([Type]=%d)",
				"Change_user_pass", "", HTYPE_THERMOSMART);
			if (!DoesColumnExistsInTable("Description", "DeviceStatus"))
			{
				query("ALTER TABLE DeviceStatus ADD COLUMN [Description] VARCHAR(200) DEFAULT ''");
			}
		}
		if (dbversion < 76)
		{
			if (!DoesColumnExistsInTable("Name", "MySensorsChilds"))
			{
				query("ALTER TABLE MySensorsChilds ADD COLUMN [Name] VARCHAR(100) DEFAULT ''");
			}
			if (!DoesColumnExistsInTable("UseAck", "MySensorsChilds"))
			{
				query("ALTER TABLE MySensorsChilds ADD COLUMN [UseAck] INTEGER DEFAULT 0");
			}
		}
		if (dbversion < 77)
		{
			//Simplify Scenes table, and add support for multiple activators
			query("ALTER TABLE Scenes ADD COLUMN [Activators] VARCHAR(200) DEFAULT ''");
			std::vector<std::vector<std::string> > result, result2;
			std::vector<std::vector<std::string> >::const_iterator itt;
			result = safe_query("SELECT ID, HardwareID, DeviceID, Unit, [Type], SubType, SceneType, ListenCmd FROM Scenes");
			if (!result.empty())
			{
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string Activator("");
					result2 = safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID==%q) AND (DeviceID=='%q') AND (Unit==%q) AND ([Type]==%q) AND (SubType==%q)",
						sd[1].c_str(), sd[2].c_str(), sd[3].c_str(), sd[4].c_str(), sd[5].c_str());
					if (!result2.empty())
					{
						Activator = result2[0][0];
						if (sd[6] == "0") { //Scene
							Activator += ":" + sd[7];
						}
					}
					safe_query("UPDATE Scenes SET Activators='%q' WHERE (ID==%q)", Activator.c_str(), sd[0].c_str());
				}
			}
			//create a backup
			query("ALTER TABLE Scenes RENAME TO tmp_Scenes");
			//Create the new table
			query(sqlCreateScenes);
			//Copy values from tmp_Scenes back into our new table
			query(
				"INSERT INTO Scenes ([ID],[Name],[Favorite],[Order],[nValue],[SceneType],[LastUpdate],[Protected],[OnAction],[OffAction],[Description],[Activators])"
				"SELECT [ID],[Name],[Favorite],[Order],[nValue],[SceneType],[LastUpdate],[Protected],[OnAction],[OffAction],[Description],[Activators] FROM tmp_Scenes");
			//Drop the tmp table
			query("DROP TABLE tmp_Scenes");
		}
		if (dbversion < 78)
		{
			//Patch for soil moisture to use large ID
			result = safe_query("SELECT ID, DeviceID FROM DeviceStatus WHERE (Type=%d) AND (SubType=%d)", pTypeGeneral, sTypeSoilMoisture);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string idx = sd[0];
					int lid = atoi(sd[1].c_str());
					char szTmp[20];
					sprintf(szTmp, "%08X", lid);
					safe_query("UPDATE DeviceStatus SET DeviceID='%q' WHERE (ID='%q')", szTmp, idx.c_str());
				}
			}
		}
		if (dbversion < 79)
		{
			//MQTT filename for ca file
			query("ALTER TABLE Hardware ADD COLUMN [Extra] VARCHAR(200) DEFAULT ('')");
		}
		if (dbversion < 81)
		{
			// MQTT set default mode
			std::stringstream szQuery2;
			szQuery2 << "UPDATE Hardware SET Mode1=1 WHERE  ([Type]==" << HTYPE_MQTT << " )";
			query(szQuery2.str());
		}
		if (dbversion < 82)
		{
			//pTypeEngery sensor to new kWh sensor
			std::vector<std::vector<std::string> > result2;
			std::vector<std::vector<std::string> >::const_iterator itt2	;
			result2 = safe_query("SELECT ID, DeviceID FROM DeviceStatus WHERE ([Type] = %d)", pTypeENERGY);
			for (itt2 = result2.begin(); itt2 != result2.end(); ++itt2)
			{
				std::vector<std::string> sd2 = *itt2;

				//Change type to new sensor, and update ID
				int oldID = atoi(sd2[1].c_str());
				char szTmp[20];
				sprintf(szTmp, "%08X", oldID);
				safe_query("UPDATE DeviceStatus SET [DeviceID]='%s', [Type]=%d, [SubType]=%d, [Unit]=1 WHERE (ID==%s)", szTmp, pTypeGeneral, sTypeKwh, sd2[0].c_str());

				//meter table
				safe_query("UPDATE Meter SET Value=Value/100, Usage=Usage*10 WHERE DeviceRowID=%s", sd2[0].c_str());
				//meter_calendar table
				safe_query("UPDATE Meter_Calendar SET Value=Value/100, Counter=Counter/100 WHERE (DeviceRowID==%s)", sd2[0].c_str());
			}
		}
		if (dbversion < 83)
		{
			//Add hardware monitor as normal hardware class (if not already added)
			std::vector<std::vector<std::string> > result;
			result = safe_query("SELECT ID FROM Hardware WHERE (Type==%d)", HTYPE_System);
			if (result.size() < 1)
			{
				m_sql.safe_query("INSERT INTO Hardware (Name, Enabled, Type, Address, Port, Username, Password, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6) VALUES ('Motherboard',1, %d,'',1,'','',0,0,0,0,0,0)", HTYPE_System);
			}
		}
		if (dbversion < 85)
		{
			//MySensors, default use ACK for Childs
			safe_query("UPDATE MySensorsChilds SET[UseAck] = 1 WHERE(ChildID != 255)");
		}
		if (dbversion < 86)
		{
			//MySensors add Name field
			query("ALTER TABLE MySensors ADD COLUMN [Name] VARCHAR(100) DEFAULT ''");
			safe_query("UPDATE MySensors SET [Name] = [SketchName]");
		}
		if (dbversion < 87)
		{
			//MySensors change waterflow percentage sensor to a real waterflow sensor
			std::stringstream szQuery;
			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> >::const_iterator itt;
			szQuery << "SELECT HardwareID,NodeID,ChildID FROM MySensorsChilds WHERE ([Type]==" << 21 << ")";
			result = query(szQuery.str());
			for (itt = result.begin(); itt != result.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				int hwid = atoi(sd[0].c_str());
				int nodeid = atoi(sd[1].c_str());
				//int childid = atoi(sd[2].c_str());

				szQuery.clear();
				szQuery.str("");

				char szID[20];
				sprintf(szID, "%08X", nodeid);

				szQuery << "UPDATE DeviceStatus SET SubType=" << sTypeWaterflow << " WHERE ([Type]=" << pTypeGeneral << ") AND (SubType=" << sTypePercentage << ") AND (HardwareID=" << hwid << ") AND (DeviceID='" << szID << "')";
				query(szQuery.str());
			}
		}
		if (dbversion < 88)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [Options] VARCHAR(1024) DEFAULT null");
		}
		if (dbversion < 89)
		{
			std::stringstream szQuery;
			szQuery << "UPDATE DeviceStatus SET [DeviceID]='0' || DeviceID WHERE ([Type]=" << pTypeGeneralSwitch << ") AND (SubType=" << sSwitchTypeSelector << ") AND length(DeviceID) = 7";
			query(szQuery.str());
		}
		if (dbversion < 90)
		{
			if (!DoesColumnExistsInTable("Interpreter", "EventMaster"))
			{
				query("ALTER TABLE EventMaster ADD COLUMN [Interpreter] VARCHAR(10) DEFAULT 'Blockly'");
			}
			if (!DoesColumnExistsInTable("Type", "EventMaster"))
			{
				query("ALTER TABLE EventMaster ADD COLUMN [Type] VARCHAR(10) DEFAULT 'All'");
			}
		}
		if (dbversion < 91)
		{
			//Add DomoticzInternal as normal hardware class (if not already added)
			int hwdID = -1;
			std::string securityPanelDeviceID = "148702"; // 0x00148702
			std::vector<std::vector<std::string> > result;
			result = safe_query("SELECT ID FROM Hardware WHERE (Type==%d)", HTYPE_DomoticzInternal);
			if (result.size() < 1) {
				m_sql.safe_query("INSERT INTO Hardware (Name, Enabled, Type, Address, Port, Username, Password, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6) VALUES ('Domoticz Internal',1, %d,'',1,'','',0,0,0,0,0,0)", HTYPE_DomoticzInternal);
				result = safe_query("SELECT ID FROM Hardware WHERE (Type==%d)", HTYPE_DomoticzInternal);
			}
			if (result.size() > 0) {
				hwdID = atoi(result[0][0].c_str());
			}
			if (hwdID > 0) {
				// Update HardwareID for Security Panel device
				int oldHwdID = 1000;
				result = safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID='%q') AND (Type=%d) AND (SubType=%d)", oldHwdID, securityPanelDeviceID.c_str(), pTypeSecurity1, sTypeDomoticzSecurity);
				if (result.size() > 0)
				{
					m_sql.safe_query("UPDATE DeviceStatus SET HardwareID=%d WHERE (HardwareID==%d) AND (DeviceID='%q') AND (Type=%d) AND (SubType=%d)", hwdID, oldHwdID, securityPanelDeviceID.c_str(), pTypeSecurity1, sTypeDomoticzSecurity);
				}
				// Update Name for Security Panel device
				result = safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID='%q') AND (Type=%d) AND (SubType=%d) AND Name='Unknown'", hwdID, securityPanelDeviceID.c_str(), pTypeSecurity1, sTypeDomoticzSecurity);
				if (result.size() > 0)
				{
					m_sql.safe_query("UPDATE DeviceStatus SET Name='Domoticz Security Panel' WHERE (HardwareID==%d) AND (DeviceID='%q') AND (Type=%d) AND (SubType=%d) AND Name='Unknown'", hwdID, securityPanelDeviceID.c_str(), pTypeSecurity1, sTypeDomoticzSecurity);
				}
			}
		}
		if (dbversion < 92) {
			// Change DeviceStatus.Options datatype from VARCHAR(1024) to TEXT
			std::string tableName = "DeviceStatus";
			std::string fieldList = "[ID],[HardwareID],[DeviceID],[Unit],[Name],[Used],[Type],[SubType],[SwitchType],[Favorite],[SignalLevel],[BatteryLevel],[nValue],[sValue],[LastUpdate],[Order],[AddjValue],[AddjMulti],[AddjValue2],[AddjMulti2],[StrParam1],[StrParam2],[LastLevel],[Protected],[CustomImage],[Description],[Options]";
			std::stringstream szQuery;

			sqlite3_exec(m_dbase, "PRAGMA foreign_keys=off", NULL, NULL, NULL);
			sqlite3_exec(m_dbase, "BEGIN TRANSACTION", NULL, NULL, NULL);

			// Drop indexes and trigger
			safe_query("DROP TRIGGER IF EXISTS devicestatusupdate");
			// Save all table rows
			szQuery.clear();
			szQuery.str("");
			szQuery << "ALTER TABLE " << tableName << " RENAME TO _" << tableName << "_old";
			safe_query(szQuery.str().c_str());
			// Create new table
			safe_query(sqlCreateDeviceStatus);
			// Restore all table rows
			szQuery.clear();
			szQuery.str("");
			szQuery << "INSERT INTO " << tableName << " (" << fieldList << ") SELECT " << fieldList << " FROM _" << tableName << "_old";
			safe_query(szQuery.str().c_str());
			// Restore indexes and triggers
			safe_query(sqlCreateDeviceStatusTrigger);
			// Delete old table
			szQuery.clear();
			szQuery.str("");
			szQuery << "DROP TABLE IF EXISTS _" << tableName << "_old";
			safe_query(szQuery.str().c_str());

			sqlite3_exec(m_dbase, "END TRANSACTION", NULL, NULL, NULL);
			sqlite3_exec(m_dbase, "PRAGMA foreign_keys=on", NULL, NULL, NULL);
		}
		if (dbversion < 93)
		{
			if (!DoesColumnExistsInTable("Month", "Timers"))
			{
				query("ALTER TABLE Timers ADD COLUMN [Month] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("MDay", "Timers"))
			{
				query("ALTER TABLE Timers ADD COLUMN [MDay] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("Occurence", "Timers"))
			{
				query("ALTER TABLE Timers ADD COLUMN [Occurence] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("Month", "SceneTimers"))
			{
				query("ALTER TABLE SceneTimers ADD COLUMN [Month] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("MDay", "SceneTimers"))
			{
				query("ALTER TABLE SceneTimers ADD COLUMN [MDay] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("Occurence", "SceneTimers"))
			{
				query("ALTER TABLE SceneTimers ADD COLUMN [Occurence] INTEGER DEFAULT 0");
			}
		}
		if (dbversion < 94)
		{
			std::stringstream szQuery;
			szQuery << "UPDATE Timers SET [Type]=[Type]+2 WHERE ([Type]>" << TTYPE_FIXEDDATETIME << ")";
			query(szQuery.str());
			szQuery.clear();
			szQuery.str("");
			szQuery << "UPDATE SceneTimers SET [Type]=[Type]+2 WHERE ([Type]>" << TTYPE_FIXEDDATETIME << ")";
			query(szQuery.str());
		}
		if (dbversion < 95)
		{
			if (!DoesColumnExistsInTable("Month", "SetpointTimers"))
			{
				query("ALTER TABLE SetpointTimers ADD COLUMN [Month] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("MDay", "SetpointTimers"))
			{
				query("ALTER TABLE SetpointTimers ADD COLUMN [MDay] INTEGER DEFAULT 0");
			}
			if (!DoesColumnExistsInTable("Occurence", "SetpointTimers"))
			{
				query("ALTER TABLE SetpointTimers ADD COLUMN [Occurence] INTEGER DEFAULT 0");
			}
		}
		if (dbversion < 96)
		{
			if (!DoesColumnExistsInTable("Name", "MobileDevices"))
			{
				query("ALTER TABLE MobileDevices ADD COLUMN [Name] VARCHAR(100) DEFAULT ''");
			}
		}
		if (dbversion < 97)
		{
			//Patch for pTypeLighting2/sTypeZWaveSwitch to pTypeGeneralSwitch/sSwitchGeneralSwitch
			std::stringstream szQuery,szQuery2;
			std::vector<std::vector<std::string> > result, result2;
			std::vector<std::string> sd;
			result = query("SELECT ID FROM Hardware WHERE ([Type] = 9) OR ([Type] = 21)");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					sd = *itt;
					szQuery.clear();
					szQuery.str("");
					szQuery << "SELECT ID, DeviceID FROM DeviceStatus WHERE ([Type]=" << pTypeLighting2 << ") AND (SubType=" << 0xA1 << ") AND (HardwareID=" << sd[0] << ")";
					result2 = query(szQuery.str());
					if (result2.size() > 0)
					{
						std::vector<std::vector<std::string> >::const_iterator itt2;
						for (itt2 = result2.begin(); itt2 != result2.end(); ++itt2)
						{
							sd = *itt2;
							std::string ndeviceid = "0" + sd[1];
							szQuery2.clear();
							szQuery2.str("");
							//#define sTypeZWaveSwitch 0xA1
							szQuery2 << "UPDATE DeviceStatus SET DeviceID='" << ndeviceid << "', [Type]=" << pTypeGeneralSwitch << ", SubType=" << sSwitchGeneralSwitch << " WHERE (ID=" << sd[0] << ")";
							query(szQuery2.str());
						}
					}
				}
			}
		}
		if (dbversion < 98)
		{
			// Shorthen cookies validity to 30 days
			query("UPDATE UserSessions SET ExpirationDate = datetime(ExpirationDate, '-335 days')");
		}
		if (dbversion < 99)
		{
			//Convert depricated CounterType 'Time' to type Counter with options ValueQuantity='Time' & ValueUnits='Min'
			//Add options ValueQuantity='Count' to existing CounterType 'Counter'
			const unsigned char charNTYPE_TODAYTIME = 'm';
			std::stringstream szQuery, szQuery1, szQuery2, szQuery3;
			std::vector<std::vector<std::string> > result, result1;
			std::vector<std::string> sd;
			szQuery.clear();
			szQuery.str("");
			szQuery << "SELECT ID FROM Hardware"
				" WHERE (([Type] = " << HTYPE_Dummy << ") OR ([Type] = " << HTYPE_YouLess << "))";
			result = query(szQuery.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					sd = *itt;
					szQuery1.clear();
					szQuery1.str("");
					szQuery1 << "SELECT ID, DeviceID, SwitchType FROM DeviceStatus"
						" WHERE ((([Type]=" << pTypeRFXMeter << ") AND (SubType=" << sTypeRFXMeterCount << "))"
						" OR (([Type]=" << pTypeGeneral << ") AND (SubType=" << sTypeCounterIncremental << "))"
						" OR ([Type]=" << pTypeYouLess << "))"
						" AND ((SwitchType=" << MTYPE_COUNTER << ") OR (SwitchType=" << MTYPE_TIME << "))"
						" AND (HardwareID=" << sd[0] << ")";
					result1 = query(szQuery1.str());
					if (result1.size() > 0)
					{
						std::vector<std::vector<std::string> >::const_iterator itt2;
						for (itt2 = result1.begin(); itt2 != result1.end(); ++itt2)
						{
							sd = *itt2;
							uint64_t devidx = atoi(sd[0].c_str());
							_eMeterType switchType = (_eMeterType)atoi(sd[2].c_str());

							if (switchType == MTYPE_COUNTER)
							{
								//Add options to existing SwitchType 'Counter'
								m_sql.SetDeviceOptions(devidx, m_sql.BuildDeviceOptions("ValueQuantity:Count;ValueUnits:", false));
							}
							else if (switchType == MTYPE_TIME)
							{
								//Set default options
								m_sql.SetDeviceOptions(devidx, m_sql.BuildDeviceOptions("ValueQuantity:Time;ValueUnits:Min", false));

								//Convert to Counter
								szQuery2.clear();
								szQuery2.str("");
								szQuery2 << "UPDATE DeviceStatus"
									" SET SwitchType=" << MTYPE_COUNTER << " WHERE (ID=" << devidx << ")";
								query(szQuery2.str());

								//Update notifications 'Time' -> 'Counter'
								szQuery3.clear();
								szQuery3.str("");
								szQuery3 << "UPDATE Notifications"
									" SET Params=REPLACE(Params, '" << charNTYPE_TODAYTIME  << ";', '" << Notification_Type_Desc(NTYPE_TODAYCOUNTER, 1) << ";')"
									" WHERE (DeviceRowID=" << devidx << ")";
								query(szQuery3.str());
							}
						}
					}
				}
			}
		}
		if (dbversion < 100)
		{
			//Convert temperature sensor type sTypeTEMP10 to sTypeTEMP5 for specified hardware classes
			std::stringstream szQuery, szQuery2;
			std::vector<std::vector<std::string> > result;
			std::vector<std::string> sd;
			szQuery << "SELECT ID FROM Hardware WHERE ( "
				<< "([Type] = " << HTYPE_OpenThermGateway << ") OR "
				<< "([Type] = " << HTYPE_OpenThermGatewayTCP << ") OR "
				<< "([Type] = " << HTYPE_DavisVantage << ") OR "
				<< "([Type] = " << HTYPE_System << ") OR "
				<< "([Type] = " << HTYPE_ICYTHERMOSTAT << ") OR "
				<< "([Type] = " << HTYPE_Meteostick << ") OR "
				<< "([Type] = " << HTYPE_PVOUTPUT_INPUT << ") OR "
				<< "([Type] = " << HTYPE_SBFSpot << ") OR "
				<< "([Type] = " << HTYPE_SolarEdgeTCP << ") OR "
				<< "([Type] = " << HTYPE_TE923 << ") OR "
				<< "([Type] = " << HTYPE_TOONTHERMOSTAT << ") OR "
				<< "([Type] = " << HTYPE_Wunderground << ") OR "
				<< "([Type] = " << HTYPE_DarkSky << ") OR "
				<< "([Type] = " << HTYPE_AccuWeather << ") OR "
				<< "([Type] = " << HTYPE_OpenZWave << ")"
				<< ")";
			result = query(szQuery.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE DeviceStatus SET SubType=" << sTypeTEMP5 << " WHERE ([Type]==" << pTypeTEMP << ") AND (SubType==" << sTypeTEMP10 << ") AND (HardwareID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 101)
		{
			//Convert TempHum sensor type sTypeTH2 to sTypeHUM1 for specified hardware classes
			std::stringstream szQuery, szQuery2;
			std::vector<std::vector<std::string> > result;
			std::vector<std::string> sd;
			szQuery << "SELECT ID FROM Hardware WHERE ( "
				<< "([Type] = " << HTYPE_DavisVantage << ") OR "
				<< "([Type] = " << HTYPE_TE923 << ") OR "
				<< "([Type] = " << HTYPE_OpenZWave << ")"
				<< ")";
			result = query(szQuery.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE DeviceStatus SET SubType=" << sTypeHUM1 << " WHERE ([Type]==" << pTypeHUM << ") AND (SubType==" << sTypeTH2 << ") AND (HardwareID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 102)
		{
			// Remove old indexes
			query("drop index if exists f_idx;");
			query("drop index if exists fc_idx;");
			query("drop index if exists l_idx;");
			query("drop index if exists s_idx;");
			query("drop index if exists m_idx;");
			query("drop index if exists mc_idx;");
			query("drop index if exists mm_idx;");
			query("drop index if exists mmc_idx;");
			query("drop index if exists p_idx;");
			query("drop index if exists pc_idx;");
			query("drop index if exists r_idx;");
			query("drop index if exists rc_idx;");
			query("drop index if exists t_idx;");
			query("drop index if exists tc_idx;");
			query("drop index if exists u_idx;");
			query("drop index if exists uv_idx;");
			query("drop index if exists w_idx;");
			query("drop index if exists wc_idx;");
			// Add new indexes
			query("create index if not exists ds_hduts_idx    on DeviceStatus(HardwareID, DeviceID, Unit, Type, SubType);");
			query("create index if not exists f_id_idx        on Fan(DeviceRowID);");
			query("create index if not exists f_id_date_idx   on Fan(DeviceRowID, Date);");
			query("create index if not exists fc_id_idx       on Fan_Calendar(DeviceRowID);");
			query("create index if not exists fc_id_date_idx  on Fan_Calendar(DeviceRowID, Date);");
			query("create index if not exists ll_id_idx       on LightingLog(DeviceRowID);");
			query("create index if not exists ll_id_date_idx  on LightingLog(DeviceRowID, Date);");
			query("create index if not exists sl_id_idx       on SceneLog(SceneRowID);");
			query("create index if not exists sl_id_date_idx  on SceneLog(SceneRowID, Date);");
			query("create index if not exists m_id_idx        on Meter(DeviceRowID);");
			query("create index if not exists m_id_date_idx   on Meter(DeviceRowID, Date);");
			query("create index if not exists mc_id_idx       on Meter_Calendar(DeviceRowID);");
			query("create index if not exists mc_id_date_idx  on Meter_Calendar(DeviceRowID, Date);");
			query("create index if not exists mm_id_idx       on MultiMeter(DeviceRowID);");
			query("create index if not exists mm_id_date_idx  on MultiMeter(DeviceRowID, Date);");
			query("create index if not exists mmc_id_idx      on MultiMeter_Calendar(DeviceRowID);");
			query("create index if not exists mmc_id_date_idx on MultiMeter_Calendar(DeviceRowID, Date);");
			query("create index if not exists p_id_idx        on Percentage(DeviceRowID);");
			query("create index if not exists p_id_date_idx   on Percentage(DeviceRowID, Date);");
			query("create index if not exists pc_id_idx       on Percentage_Calendar(DeviceRowID);");
			query("create index if not exists pc_id_date_idx  on Percentage_Calendar(DeviceRowID, Date);");
			query("create index if not exists r_id_idx        on Rain(DeviceRowID);");
			query("create index if not exists r_id_date_idx   on Rain(DeviceRowID, Date);");
			query("create index if not exists rc_id_idx       on Rain_Calendar(DeviceRowID);");
			query("create index if not exists rc_id_date_idx  on Rain_Calendar(DeviceRowID, Date);");
			query("create index if not exists t_id_idx        on Temperature(DeviceRowID);");
			query("create index if not exists t_id_date_idx   on Temperature(DeviceRowID, Date);");
			query("create index if not exists tc_id_idx       on Temperature_Calendar(DeviceRowID);");
			query("create index if not exists tc_id_date_idx  on Temperature_Calendar(DeviceRowID, Date);");
			query("create index if not exists u_id_idx        on UV(DeviceRowID);");
			query("create index if not exists u_id_date_idx   on UV(DeviceRowID, Date);");
			query("create index if not exists uv_id_idx       on UV_Calendar(DeviceRowID);");
			query("create index if not exists uv_id_date_idx  on UV_Calendar(DeviceRowID, Date);");
			query("create index if not exists w_id_idx        on Wind(DeviceRowID);");
			query("create index if not exists w_id_date_idx   on Wind(DeviceRowID, Date);");
			query("create index if not exists wc_id_idx       on Wind_Calendar(DeviceRowID);");
			query("create index if not exists wc_id_date_idx  on Wind_Calendar(DeviceRowID, Date);");
		}
		if (dbversion < 103)
		{
			FixDaylightSaving();
		}
		if (dbversion < 104)
		{
			//S0 Meter patch
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, Address FROM Hardware WHERE([Type]==" << HTYPE_S0SmartMeterUSB << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Hardware SET Extra='" << sd[1] << "', Address='' WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 105)
		{
			if (!DoesColumnExistsInTable("AckTimeout", "MySensorsChilds"))
			{
				query("ALTER TABLE MySensorsChilds ADD COLUMN [AckTimeout] INTEGER DEFAULT 1200");
			}
		}
		if (dbversion < 106)
		{
			//Adjust Limited device id's to uppercase HEX
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, DeviceID FROM DeviceStatus WHERE([Type]==" << pTypeColorSwitch << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					uint32_t lID;
					std::stringstream s_strid;
					s_strid << std::hex << sd[1];
					s_strid >> lID;

					if (lID > 9)
					{
						char szTmp[10];
						sprintf(szTmp, "%08X", lID);
						szQuery2 << "UPDATE DeviceStatus SET DeviceID='" << szTmp << "' WHERE (ID=" << sd[0] << ")";
						query(szQuery2.str());
					}
				}
			}
		}
		if (dbversion < 107)
		{
			if (!DoesColumnExistsInTable("User", "LightingLog"))
			{
				query("ALTER TABLE LightingLog ADD COLUMN [User] VARCHAR(100) DEFAULT ('')");
			}
		}
		if (dbversion < 108)
		{
			//Fix possible HTTP notifier problem
			std::string sValue;
			GetPreferencesVar("HTTPPostContentType", sValue);
			if ((sValue.size()>100)||(sValue.empty()))
			{
				sValue = "application/json";
				std::string sencoded = base64_encode((const unsigned char*)sValue.c_str(), sValue.size());
				UpdatePreferencesVar("HTTPPostContentType", sencoded);
			}
		}
		if (dbversion < 109)
		{
			query("INSERT INTO TimerPlans (ID, Name) VALUES (0, 'default')");
			query("INSERT INTO TimerPlans (ID, Name) VALUES (1, 'Holiday')");
		}
		if (dbversion < 110)
		{
			query("ALTER TABLE Hardware RENAME TO tmp_Hardware;");
			query(sqlCreateHardware);
			query("INSERT INTO Hardware(ID, Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout) SELECT ID, Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout FROM tmp_Hardware;");
			query("DROP TABLE tmp_Hardware;");

			result = safe_query("SELECT ID, Extra FROM Hardware WHERE Type=%d", HTYPE_HTTPPOLLER);
			if (result.size() > 0)
			{
				std::stringstream szQuery2;
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string id = sd[0];
					std::string extra = sd[1];
					std::string extraBase64=base64_encode((const unsigned char *)extra.c_str(), extra.length());
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Hardware SET Mode1=0, Extra='%s' WHERE (ID=" << id << ")";
					safe_query(szQuery2.str().c_str(), extraBase64.c_str());
				}
			}
		}
		if (dbversion < 111)
		{
			//SolarEdge API, no need for Serial Number anymore
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, Password FROM Hardware WHERE([Type]==" << HTYPE_SolarEdgeAPI << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					safe_query("UPDATE Hardware SET Username='%q', Password='' WHERE (ID=%s)", sd[1].c_str(), sd[0].c_str());
				}
			}
		}
		if (dbversion < 112)
		{
			//Fix for MySensor general switch with wrong subtype
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID FROM Hardware WHERE ([Type]==" << HTYPE_MySensorsTCP << ") OR ([Type]==" << HTYPE_MySensorsUSB << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE DeviceStatus SET SubType=" << sSwitchTypeAC << " WHERE ([Type]==" << pTypeGeneralSwitch << ") AND (SubType==" << sTypeAC << ") AND (HardwareID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}
		}
		if (dbversion < 113)
		{
			//Patch for new 1-Wire subtypes
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID FROM Hardware WHERE([Type]==" << HTYPE_1WIRE << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					safe_query("UPDATE DeviceStatus SET SubType='%d' WHERE ([Type]==%d) AND (SubType==%d) AND (HardwareID=%s)",
						sTypeTEMP5, pTypeTEMP, sTypeTEMP10, sd[0].c_str());

					safe_query("UPDATE DeviceStatus SET SubType='%d' WHERE ([Type]==%d) AND (SubType==%d) AND (HardwareID=%s)",
						sTypeHUM1, pTypeHUM, sTypeHUM2, sd[0].c_str());
				}
			}
		}
		if (dbversion < 114)
		{
			//Set default values for new parameters in EcoDevices and Teleinfo EDF
			std::stringstream szQuery1, szQuery2;
			szQuery1 << "UPDATE Hardware SET Mode1 = 0, Mode2 = 60 WHERE Type =" << HTYPE_ECODEVICES;
			query(szQuery1.str());
			szQuery2 << "UPDATE Hardware SET Mode1 = 0, Mode2 = 0, Mode3 = 60 WHERE Type =" << HTYPE_TeleinfoMeter;
			query(szQuery2.str());
		}
		if (dbversion < 115)
		{
			//Patch for Evohome Web
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, Name, Mode1, Mode2, Mode3, Mode4, Mode5 FROM Hardware WHERE([Type]==" << HTYPE_EVOHOME_WEB << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					std::string lowerName="fitbit";
					for (uint8_t i = 0; i < 6; i++)
						lowerName[i] = sd[1][i] | 0x20;
					if (lowerName == "fitbit")
						safe_query("DELETE FROM Hardware WHERE ID=%s", sd[0].c_str());
					else
					{
						int newParams = (sd[3]=="0") ? 0 : 1;
						if (sd[4]=="1")
							newParams += 2;
						if (sd[5]=="1")
							newParams += 4;
						m_sql.safe_query("UPDATE Hardware SET Mode2=%d, Mode3=%s, Mode4=0, Mode5=0 WHERE ID=%s", newParams, sd[6].c_str(), sd[0].c_str());
						m_sql.safe_query("UPDATE DeviceStatus SET StrParam1='' WHERE HardwareID=%s", sd[0].c_str());
					}
				}
			}
		}
		if (dbversion < 116)
		{
			//Patch for GCM/FCM
			safe_query("UPDATE MobileDevices SET Active=1");
			if (!DoesColumnExistsInTable("DeviceType", "MobileDevices"))
			{
				query("ALTER TABLE MobileDevices ADD COLUMN [DeviceType] VARCHAR(100) DEFAULT ('')");
			}
		}
		if (dbversion < 117)
		{
			//Add Protocol for Camera (HTTP/HTTPS/...)
			if (!DoesColumnExistsInTable("Protocol", "Cameras"))
			{
				query("ALTER TABLE Cameras ADD COLUMN [Protocol] INTEGER DEFAULT 0");
			}
		}
		if (dbversion < 118)
		{
			//Delete script that could crash domoticz (maybe the dev can have a look?)
			std::string sfile = szUserDataFolder + "scripts/python/script_device_PIRsmarter.py";
			std::remove(sfile.c_str());
		}
		if (dbversion < 119)
		{
			//change Disable Event System to Enable Event System
			int nValue = 0;
			if (GetPreferencesVar("DisableEventScriptSystem", nValue))
			{
				UpdatePreferencesVar("EnableEventScriptSystem", !nValue);
			}
			DeletePreferencesVar("DisableEventScriptSystem");
		}
		if (dbversion < 120)
		{
			// remove old dzVents dirs
			std::string dzv_Dir, dzv_scripts;
#ifdef WIN32
			dzv_Dir = szUserDataFolder + "scripts\\dzVents\\generated_scripts\\";
			dzv_scripts = szUserDataFolder + "scripts\\dzVents\\";
#else
			dzv_Dir = szUserDataFolder + "scripts/dzVents/generated_scripts/";
			dzv_scripts = szUserDataFolder + "scripts/dzVents/";
#endif
			const std::string
			dzv_rm_Dir1 = dzv_scripts + "runtime",
			dzv_rm_Dir2 = dzv_scripts + "documentation";

			if ((file_exist(dzv_rm_Dir1.c_str()) || file_exist(dzv_rm_Dir2.c_str())) &&
				!szUserDataFolder.empty())
			{
				std::string errorPath;
				if (int returncode = RemoveDir(dzv_rm_Dir1 + "|" + dzv_rm_Dir2, errorPath))
					_log.Log(LOG_ERROR, "EventSystem: (%d) Could not remove %s, please remove manually!", returncode, errorPath.c_str());
			}
		}
		if (dbversion < 121)
		{
			// incorrect call to hardware class from mainworker: move Evohome installation parameters from Mode5 to unused Mode3
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			szQuery2 << "SELECT ID, Mode5 FROM Hardware WHERE([Type]==" << HTYPE_EVOHOME_WEB << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					safe_query("UPDATE Hardware SET Mode3='%q', Mode5='' WHERE (ID=%s)", sd[1].c_str(), sd[0].c_str());
				}
			}
		}
		if (dbversion < 122)
		{
			//Patch for Darksky ozone sensor
			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> > result2;
			std::vector<std::vector<std::string> > result3;
			szQuery2 << "SELECT ID FROM Hardware WHERE([Type]==" << HTYPE_DarkSky << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "SELECT ID FROM DeviceStatus WHERE ([Type]=" << (int)pTypeGeneral << ") AND (SubType=" << (int)sTypeSolarRadiation << ") AND (HardwareID=" << sd[0] << ")";
					result2 = query(szQuery2.str());

					std::vector<std::vector<std::string> >::const_iterator itt2;
					for (itt2 = result2.begin(); itt2 != result2.end(); ++itt2)
					{
						sd = *itt2;

						//Change device type
						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "UPDATE DeviceStatus SET SubType=" << (int)sTypeCustom << ", DeviceID='00000100', Options='1;DU' WHERE (ID=" << sd[0] << ")";
						query(szQuery2.str());

						//change log
						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "SELECT Value, Date FROM Meter WHERE ([DeviceRowID]=" << sd[0] << ")";
						result3 = query(szQuery2.str());

						std::vector<std::vector<std::string> >::const_iterator itt3;
						for (itt3 = result3.begin(); itt3 != result3.end(); ++itt3)
						{
							std::vector<std::string> sd3 = *itt3;
							szQuery2.clear();
							szQuery2.str("");
							szQuery2 << "INSERT INTO Percentage (DeviceRowID, Percentage, Date) VALUES (" << sd[0] << ", " << (float)atof(sd3[0].c_str())/10.0f << ", '" << sd3[1] << "')";
							query(szQuery2.str());
						}
						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "DELETE FROM Meter WHERE ([DeviceRowID]=" << sd[0] << ")";
						query(szQuery2.str());

						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "SELECT Value1, Value2, Value3, Date FROM MultiMeter_Calendar WHERE ([DeviceRowID]=" << sd[0] << ")";
						result3 = query(szQuery2.str());

						for (itt3 = result3.begin(); itt3 != result3.end(); ++itt3)
						{
							std::vector<std::string> sd3 = *itt3;
							szQuery2.clear();
							szQuery2.str("");
							float percentage_min = (float)atof(sd3[0].c_str()) / 10.0f;
							float percentage_max = (float)atof(sd3[1].c_str()) / 10.0f;
							float percentage_avg = (float)atof(sd3[2].c_str()) / 10.0f;
							szQuery2 << "INSERT INTO Percentage_Calendar (DeviceRowID, Percentage_Min, Percentage_Max, Percentage_Avg, Date) VALUES (" << sd[0] << ", " << percentage_min << ", " << percentage_max << ", " << percentage_avg << ", '" << sd3[3] << "')";
							query(szQuery2.str());
						}
						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "DELETE FROM Meter_Calendar WHERE ([DeviceRowID]=" << sd[0] << ")";
						query(szQuery2.str());
					}
				}
			}
		}
		if (dbversion < 123)
		{
			safe_query("UPDATE Hardware SET Mode1 = 5000 WHERE Type = %d", HTYPE_DenkoviSmartdenIPInOut);
			safe_query("UPDATE Hardware SET Mode1 = 5000 WHERE Type = %d", HTYPE_DenkoviSmartdenLan);
		}
		if (dbversion < 124)
		{
			query("ALTER TABLE DeviceStatus ADD COLUMN [Color] TEXT DEFAULT NULL");
			query("ALTER TABLE Timers ADD COLUMN [Color] TEXT DEFAULT NULL");
			query("ALTER TABLE SceneDevices ADD COLUMN [Color] TEXT DEFAULT NULL");

			std::stringstream szQuery2;
			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> > result2;

			//Convert stored Hue in Timers to color
			result = query("SELECT ID, Hue FROM Timers WHERE(Hue!=0)");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					int r, g, b;

					//convert hue to RGB
					float iHue = float(atof(sd[1].c_str()));
					hsb2rgb(iHue, 1.0f, 1.0f, r, g, b, 255);

					_tColor color = _tColor(r, g, b, 0, 0, ColorModeRGB);
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE Timers SET Color='" << color.toJSONString() << "' WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}

			//Convert stored Hue in SceneDevices to color
			result = query("SELECT ID, Hue FROM SceneDevices WHERE(Hue!=0)");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					int r, g, b;

					//convert hue to RGB
					float iHue = float(atof(sd[1].c_str()));
					hsb2rgb(iHue, 1.0f, 1.0f, r, g, b, 255);

					_tColor color = _tColor(r, g, b, 0, 0, ColorModeRGB);
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "UPDATE SceneDevices SET Color='" << color.toJSONString() << "' WHERE (ID=" << sd[0] << ")";
					query(szQuery2.str());
				}
			}

			//Patch for ZWave, change device type from sTypeColor_RGB_W to sTypeColor_RGB_W_Z
			szQuery2.clear();
			szQuery2.str("");
			szQuery2 << "SELECT ID FROM Hardware WHERE([Type]==" << HTYPE_OpenZWave << ")";
			result = query(szQuery2.str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					szQuery2.clear();
					szQuery2.str("");
					szQuery2 << "SELECT ID FROM DeviceStatus WHERE ([Type]=" << (int)pTypeColorSwitch << ") AND (SubType=" << (int)sTypeColor_RGB_W << ") AND (HardwareID=" << sd[0] << ")";
					result2 = query(szQuery2.str());

					std::vector<std::vector<std::string> >::const_iterator itt2;
					for (itt2 = result2.begin(); itt2 != result2.end(); ++itt2)
					{
						sd = *itt2;

						//Change device type
						szQuery2.clear();
						szQuery2.str("");
						szQuery2 << "UPDATE DeviceStatus SET SubType=" << (int)sTypeColor_RGB_W_Z << " WHERE (ID=" << sd[0] << ")";
						query(szQuery2.str());
					}
				}
			}
		}
		if (dbversion < 125)
		{
			std::string sFile = szWWWFolder + "/js/domoticz.js.gz";
			std::remove(sFile.c_str());
		}
		if (dbversion < 126)
		{
			std::string sFile = szWWWFolder + "/js/domoticzdevices.js.gz";
			std::remove(sFile.c_str());
		}
		if (dbversion < 127)
		{
			safe_query("UPDATE Hardware SET Mode2 = 3 WHERE Type = %d", HTYPE_Philips_Hue);
		}
		if (dbversion < 128)
		{
			safe_query("UPDATE Hardware SET Mode2 = 3 WHERE Type = %d", HTYPE_Philips_Hue);
		}
	}
	else if (bNewInstall)
	{
		//place here actions that needs to be performed on new databases
		query("INSERT INTO Plans (Name) VALUES ('$Hidden Devices')");
		// Add hardware for internal use
		m_sql.safe_query("INSERT INTO Hardware (Name, Enabled, Type, Address, Port, Username, Password, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6) VALUES ('Domoticz Internal',1, %d,'',1,'','',0,0,0,0,0,0)", HTYPE_DomoticzInternal);
	}
	UpdatePreferencesVar("DB_Version", DB_VERSION);

	//Make sure we have some default preferences
	int nValue=10;
	std::string sValue;
	if (!GetPreferencesVar("Title", sValue))
        {
                UpdatePreferencesVar("Title", "Domoticz");
        }
        if ((!GetPreferencesVar("LightHistoryDays", nValue)) || (nValue==0))
	{
		UpdatePreferencesVar("LightHistoryDays", 30);
	}
	if ((!GetPreferencesVar("MeterDividerEnergy", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("MeterDividerEnergy", 1000);
	}
	else if (nValue == 0)
	{
		//Sanity check!
		UpdatePreferencesVar("MeterDividerEnergy", 1000);
	}
	if ((!GetPreferencesVar("MeterDividerGas", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("MeterDividerGas", 100);
	}
	else if (nValue == 0)
	{
		//Sanity check!
		UpdatePreferencesVar("MeterDividerGas", 100);
	}
	if ((!GetPreferencesVar("MeterDividerWater", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("MeterDividerWater", 100);
	}
	else if (nValue == 0)
	{
		//Sanity check!
		UpdatePreferencesVar("MeterDividerWater", 100);
	}
	if ((!GetPreferencesVar("RandomTimerFrame", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("RandomTimerFrame", 15);
	}
	if ((!GetPreferencesVar("ElectricVoltage", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("ElectricVoltage", 230);
	}
	if (!GetPreferencesVar("CM113DisplayType", nValue))
	{
		UpdatePreferencesVar("CM113DisplayType", 0);
	}
	if ((!GetPreferencesVar("5MinuteHistoryDays", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("5MinuteHistoryDays", 1);
	}
	if ((!GetPreferencesVar("SensorTimeout", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("SensorTimeout", 60);
	}
	if (!GetPreferencesVar("SensorTimeoutNotification", nValue))
	{
		UpdatePreferencesVar("SensorTimeoutNotification", 0); //default disabled
	}

	if (!GetPreferencesVar("UseAutoUpdate", nValue))
	{
		UpdatePreferencesVar("UseAutoUpdate", 1);
	}

	if (!GetPreferencesVar("UseAutoBackup", nValue))
	{
		UpdatePreferencesVar("UseAutoBackup", 0);
	}

	if (GetPreferencesVar("Rego6XXType", nValue))
	{
        // The setting is no longer here. It has moved to the hardware table (Mode1)
        // Copy the setting so no data is lost - if the rego is used. (zero is the default
        // so it's no point copying this value...)
        // THIS SETTING CAN BE REMOVED WHEN ALL CAN BE ASSUMED TO HAVE UPDATED (summer 2013?)
        if(nValue > 0)
        {
        	std::stringstream szQuery;

			szQuery.clear();
			szQuery.str("");
			szQuery << "SELECT ID,Mode1 FROM Hardware WHERE (Type=" << HTYPE_Rego6XX << ")";
        	result=query(szQuery.str());
        	if (result.size()>0)
            {
                if(atoi(result[0][1].c_str()) != nValue)
                {
					UpdateRFXCOMHardwareDetails(atoi(result[0][0].c_str()), nValue, 0, 0, 0, 0, 0);
                }
            }
		    UpdatePreferencesVar("Rego6XXType", 0); // Set to zero to avoid another copy
        }
	}
	//Costs for Energy/Gas and Water (See your provider, try to include tax and other stuff)
	if ((!GetPreferencesVar("CostEnergy", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("CostEnergy", 2149);
	}
	if ((!GetPreferencesVar("CostEnergyT2", nValue)) || (nValue == 0))
	{
		GetPreferencesVar("CostEnergy", nValue);
		UpdatePreferencesVar("CostEnergyT2", nValue);
	}
	if ((!GetPreferencesVar("CostEnergyR1", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("CostEnergyR1", 800);
	}
	if ((!GetPreferencesVar("CostEnergyR2", nValue)) || (nValue == 0))
	{
		GetPreferencesVar("CostEnergyR1", nValue);
		UpdatePreferencesVar("CostEnergyR2", nValue);
	}

	if ((!GetPreferencesVar("CostGas", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("CostGas", 6218);
	}
	if ((!GetPreferencesVar("CostWater", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("CostWater", 16473);
	}
	if (!GetPreferencesVar("UseEmailInNotifications", nValue))
	{
		UpdatePreferencesVar("UseEmailInNotifications", 1);
	}
	if (!GetPreferencesVar("SendErrorNotifications", nValue))
	{
		UpdatePreferencesVar("SendErrorNotifications", 0);
	}
	if ((!GetPreferencesVar("EmailPort", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("EmailPort", 25);
	}
	if (!GetPreferencesVar("EmailAsAttachment", nValue))
	{
		UpdatePreferencesVar("EmailAsAttachment", 0);
	}
	if (!GetPreferencesVar("DoorbellCommand", nValue))
	{
		UpdatePreferencesVar("DoorbellCommand", 0);
	}
	if (!GetPreferencesVar("SmartMeterType", nValue))	//0=meter has decimals, 1=meter does not have decimals, need this for the day graph
	{
		UpdatePreferencesVar("SmartMeterType", 0);
	}
	if (!GetPreferencesVar("EnableTabLights", nValue))
	{
		UpdatePreferencesVar("EnableTabLights", 1);
	}
	if (!GetPreferencesVar("EnableTabTemp", nValue))
	{
		UpdatePreferencesVar("EnableTabTemp", 1);
	}
	if (!GetPreferencesVar("EnableTabWeather", nValue))
	{
		UpdatePreferencesVar("EnableTabWeather", 1);
	}
	if (!GetPreferencesVar("EnableTabUtility", nValue))
	{
		UpdatePreferencesVar("EnableTabUtility", 1);
	}
	if (!GetPreferencesVar("EnableTabCustom", nValue))
	{
		UpdatePreferencesVar("EnableTabCustom", 1);
	}
	if (!GetPreferencesVar("EnableTabScenes", nValue))
	{
		UpdatePreferencesVar("EnableTabScenes", 1);
	}
	if (!GetPreferencesVar("EnableTabFloorplans", nValue))
	{
		UpdatePreferencesVar("EnableTabFloorplans", 0);
	}
	if (!GetPreferencesVar("NotificationSensorInterval", nValue))
	{
		UpdatePreferencesVar("NotificationSensorInterval", 12*60*60);
	}
	else
	{
		if (nValue<60)
			UpdatePreferencesVar("NotificationSensorInterval", 60);
	}
	if (!GetPreferencesVar("NotificationSwitchInterval", nValue))
	{
		UpdatePreferencesVar("NotificationSwitchInterval", 0);
	}
	if ((!GetPreferencesVar("RemoteSharedPort", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("RemoteSharedPort", 6144);
	}
	if ((!GetPreferencesVar("Language", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("Language", "en");
	}
	if (!GetPreferencesVar("DashboardType", nValue))
	{
		UpdatePreferencesVar("DashboardType", 0);
	}
	if (!GetPreferencesVar("MobileType", nValue))
	{
		UpdatePreferencesVar("MobileType", 0);
	}
	if (!GetPreferencesVar("WindUnit", nValue))
	{
		UpdatePreferencesVar("WindUnit", (int)WINDUNIT_MS);
	}
	else
	{
		m_windunit=(_eWindUnit)nValue;
	}
	if (!GetPreferencesVar("TempUnit", nValue))
	{
		UpdatePreferencesVar("TempUnit", (int)TEMPUNIT_C);
	}
	else
	{
		m_tempunit=(_eTempUnit)nValue;

	}
	if (!GetPreferencesVar("WeightUnit", nValue))
	{
		UpdatePreferencesVar("WeightUnit", (int)WEIGHTUNIT_KG);
	}
	else
	{
		m_weightunit=(_eWeightUnit)nValue;

	}
	SetUnitsAndScale();

	if (!GetPreferencesVar("SecStatus", nValue))
	{
		UpdatePreferencesVar("SecStatus", 0);
	}
	if (!GetPreferencesVar("SecOnDelay", nValue))
	{
		UpdatePreferencesVar("SecOnDelay", 30);
	}

	if (!GetPreferencesVar("AuthenticationMethod", nValue))
	{
		UpdatePreferencesVar("AuthenticationMethod", 0);//AUTH_LOGIN=0, AUTH_BASIC=1
	}
	if (!GetPreferencesVar("ReleaseChannel", nValue))
	{
		UpdatePreferencesVar("ReleaseChannel", 0);//Stable=0, Beta=1
	}
	if ((!GetPreferencesVar("RaspCamParams", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("RaspCamParams", "-w 800 -h 600 -t 1"); //width/height/time2wait
	}
	if ((!GetPreferencesVar("UVCParams", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("UVCParams", "-S80 -B128 -C128 -G80 -x800 -y600 -q100"); //width/height/time2wait
	}
	if (sValue == "- -S80 -B128 -C128 -G80 -x800 -y600 -q100")
	{
		UpdatePreferencesVar("UVCParams", "-S80 -B128 -C128 -G80 -x800 -y600 -q100"); //fix a bug
	}

	nValue = 1;
	if (!GetPreferencesVar("AcceptNewHardware", nValue))
	{
		UpdatePreferencesVar("AcceptNewHardware", 1);
		nValue=1;
	}
	m_bAcceptNewHardware=(nValue==1);
	if ((!GetPreferencesVar("ZWavePollInterval", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("ZWavePollInterval", 60);
	}
	if (!GetPreferencesVar("ZWaveEnableDebug", nValue))
	{
		UpdatePreferencesVar("ZWaveEnableDebug", 0);
	}
	if ((!GetPreferencesVar("ZWaveNetworkKey", sValue)) || (sValue.empty()))
	{
		sValue = "0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10";
		UpdatePreferencesVar("ZWaveNetworkKey", sValue);
	}
	//Double check network_key
	std::vector<std::string> splitresults;
	StringSplit(sValue, ",", splitresults);
	if (splitresults.size() != 16)
	{
		sValue = "0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10";
		UpdatePreferencesVar("ZWaveNetworkKey", sValue);
	}

	if (!GetPreferencesVar("ZWaveEnableNightlyNetworkHeal", nValue))
	{
		UpdatePreferencesVar("ZWaveEnableNightlyNetworkHeal", 0);
	}
	if (!GetPreferencesVar("BatteryLowNotification", nValue))
	{
		UpdatePreferencesVar("BatteryLowNotification", 0); //default disabled
	}
	nValue = 1;
	if (!GetPreferencesVar("AllowWidgetOrdering", nValue))
	{
		UpdatePreferencesVar("AllowWidgetOrdering", 1); //default enabled
		nValue=1;
	}
	m_bAllowWidgetOrdering=(nValue==1);
	nValue = 0;
	if (!GetPreferencesVar("ActiveTimerPlan", nValue))
	{
		UpdatePreferencesVar("ActiveTimerPlan", 0); //default
		nValue=0;
	}
	m_ActiveTimerPlan=nValue;
	if (!GetPreferencesVar("HideDisabledHardwareSensors", nValue))
	{
		UpdatePreferencesVar("HideDisabledHardwareSensors", 1);
	}
	nValue = 0;
	if (!GetPreferencesVar("EnableEventScriptSystem", nValue))
	{
		UpdatePreferencesVar("EnableEventScriptSystem", 1);
		nValue = 1;
	}
	m_bEnableEventSystem = (nValue == 1);

	nValue = 0;
	if (!GetPreferencesVar("DisableDzVentsSystem", nValue))
	{
		UpdatePreferencesVar("DisableDzVentsSystem", 0);
		nValue = 0;
	}
	m_bDisableDzVentsSystem = (nValue == 1);

	if (!GetPreferencesVar("DzVentsLogLevel", nValue))
	{
		UpdatePreferencesVar("DzVentsLogLevel", 3);
	}

	nValue = 1;
	if (!GetPreferencesVar("LogEventScriptTrigger", nValue))
	{
		UpdatePreferencesVar("LogEventScriptTrigger", 1);
		nValue = 1;
	}
	m_bLogEventScriptTrigger = (nValue != 0);

	if ((!GetPreferencesVar("WebTheme", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("WebTheme", "default");
	}

	if ((!GetPreferencesVar("FloorplanPopupDelay", nValue)) || (nValue == 0))
	{
		UpdatePreferencesVar("FloorplanPopupDelay", 750);
	}
	if (!GetPreferencesVar("FloorplanFullscreenMode", nValue))
	{
		UpdatePreferencesVar("FloorplanFullscreenMode", 0);
	}
	if (!GetPreferencesVar("FloorplanAnimateZoom", nValue))
	{
		UpdatePreferencesVar("FloorplanAnimateZoom", 1);
	}
	if (!GetPreferencesVar("FloorplanShowSensorValues", nValue))
	{
		UpdatePreferencesVar("FloorplanShowSensorValues", 1);
	}
	if (!GetPreferencesVar("FloorplanShowSwitchValues", nValue))
	{
		UpdatePreferencesVar("FloorplanShowSwitchValues", 0);
	}
	if (!GetPreferencesVar("FloorplanShowSceneNames", nValue))
	{
		UpdatePreferencesVar("FloorplanShowSceneNames", 1);
	}
	if ((!GetPreferencesVar("FloorplanRoomColour", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("FloorplanRoomColour", "Blue");
	}
	if (!GetPreferencesVar("FloorplanActiveOpacity", nValue))
	{
		UpdatePreferencesVar("FloorplanActiveOpacity", 25);
	}
	if (!GetPreferencesVar("FloorplanInactiveOpacity", nValue))
	{
		UpdatePreferencesVar("FloorplanInactiveOpacity", 5);
	}
	if ((!GetPreferencesVar("TempHome", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("TempHome", "20");
	}
	if ((!GetPreferencesVar("TempAway", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("TempAway", "15");
	}
	if ((!GetPreferencesVar("TempComfort", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("TempComfort", "22.0");
	}
	if ((!GetPreferencesVar("DegreeDaysBaseTemperature", sValue)) || (sValue.empty()))
	{
		UpdatePreferencesVar("DegreeDaysBaseTemperature", "18.0");
	}
	if ((!GetPreferencesVar("HTTPURL", sValue)) || (sValue.empty()))
	{
		sValue = "https://www.somegateway.com/pushurl.php?username=#FIELD1&password=#FIELD2&apikey=#FIELD3&from=#FIELD4&to=#TO&message=#MESSAGE";
		std::string sencoded = base64_encode((const unsigned char*)sValue.c_str(), sValue.size());
		UpdatePreferencesVar("HTTPURL", sencoded);
	}
	if ((!GetPreferencesVar("HTTPPostContentType", sValue)) || (sValue.empty()))
	{
		sValue = "application/json";
		std::string sencoded = base64_encode((const unsigned char*)sValue.c_str(), sValue.size());
		UpdatePreferencesVar("HTTPPostContentType", sencoded);
	}
	if (!GetPreferencesVar("ShowUpdateEffect", nValue))
	{
		UpdatePreferencesVar("ShowUpdateEffect", 0);
	}
	nValue = 5;
	if (!GetPreferencesVar("ShortLogInterval", nValue))
	{
		UpdatePreferencesVar("ShortLogInterval", nValue);
	}
	if (nValue < 1)
		nValue = 5;
	m_ShortLogInterval = nValue;

	if (!GetPreferencesVar("SendErrorsAsNotification", nValue))
	{
		UpdatePreferencesVar("SendErrorsAsNotification", 0);
		nValue = 0;
	}
	_log.ForwardErrorsToNotificationSystem(nValue != 0);

	if (!GetPreferencesVar("IFTTTEnabled", nValue))
	{
		UpdatePreferencesVar("IFTTTEnabled", 0);
	}
	if (!GetPreferencesVar("EmailEnabled", nValue))
	{
		UpdatePreferencesVar("EmailEnabled", 1);
	}

	//Start background thread
	if (!StartThread())
		return false;
	return true;
}