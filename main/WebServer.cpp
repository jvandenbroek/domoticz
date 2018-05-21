#include "stdafx.h"
#include "WebServer.h"
#include "WebServerHelper.h"
#include "mainworker.h"
#include "localtime_r.h"
#include "dzVents.h"
#include "../hardware/OTGWBase.h"
#ifdef WITH_OPENZWAVE
#include "../hardware/OpenZWave.h"
#endif
#include "../hardware/MySensorsBase.h"
#include "../hardware/RFXBase.h"
#include "../hardware/RFLinkBase.h"
#ifdef WITH_GPIO
#include "../hardware/SysfsGpio.h"
#include "../hardware/Gpio.h"
#include "../hardware/GpioPin.h"
#endif // WITH_GPIO
#ifdef WITH_TELLDUSCORE
#include "../hardware/Tellstick.h"
#endif
#include "../webserver/Base64.h"
#include "../json/json.h"
#include "SQLHelper.h"
#ifdef ENABLE_PYTHON
#include "../hardware/plugins/Plugins.h"
#endif

#ifndef WIN32
#include <sys/utsname.h>
#include <dirent.h>
#else
#include "../msbuild/WindowsHelper.h"
#include "dirent_windows.h"
#endif
#include "../notifications/NotificationHelper.h"
#include "../main/LuaHandler.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define round(a) ( int ) ( a + .5 )

extern std::string szUserDataFolder;
extern std::string szWWWFolder;

extern std::string szAppVersion;
extern std::string szAppHash;
extern std::string szAppDate;
extern std::string szPyVersion;
extern bool g_bDontCacheWWW;

extern time_t m_StartTime;


struct _tGuiLanguage {
	const char* szShort;
	const char* szLong;
};

static const _tGuiLanguage guiLanguage[] =
{
	{ "en", "English" },
	{ "ar", "Arabic" },
	{ "bg", "Bulgarian" },
	{ "ca", "Catalan" },
	{ "zh", "Chinese" },
	{ "cs", "Czech" },
	{ "da", "Danish" },
	{ "nl", "Dutch" },
	{ "et", "Estonian" },
	{ "de", "German" },
	{ "el", "Greek" },
	{ "fr", "French" },
	{ "fi", "Finnish" },
	{ "he", "Hebrew" },
	{ "hu", "Hungarian" },
	{ "is", "Icelandic" },
	{ "it", "Italian" },
	{ "lt", "Lithuanian" },
	{ "lv", "Latvian" },
	{ "mk", "Macedonian" },
	{ "no", "Norwegian" },
	{ "fa", "Persian" },
	{ "pl", "Polish" },
	{ "pt", "Portuguese" },
	{ "ro", "Romanian" },
	{ "ru", "Russian" },
	{ "sr", "Serbian" },
	{ "sk", "Slovak" },
	{ "sl", "Slovenian" },
	{ "es", "Spanish" },
	{ "sv", "Swedish" },
	{ "tr", "Turkish" },
	{ "uk", "Ukrainian" },
	{ NULL, NULL }
};

extern http::server::CWebServerHelper m_webservers;

namespace http {
	namespace server {

		CWebServer::CWebServer(void) : session_store()
		{
			m_pWebEm = NULL;
			m_bDoStop = false;
#ifdef WITH_OPENZWAVE
			m_ZW_Hwidx = -1;
#endif
		}


		CWebServer::~CWebServer(void)
		{
			// RK, we call StopServer() instead of just deleting m_pWebEm. The Do_Work thread might still be accessing that object
			StopServer();
		}

		void CWebServer::Do_Work()
		{
			bool exception_thrown = false;
			while (!m_bDoStop)
			{
				exception_thrown = false;
				try {
					if (m_pWebEm) {
						m_pWebEm->Run();
					}
				}
				catch (std::exception& e) {
					_log.Log(LOG_ERROR, "WebServer(%s) exception occurred : '%s'", m_server_alias.c_str(), e.what());
					exception_thrown = true;
				}
				catch (...) {
					_log.Log(LOG_ERROR, "WebServer(%s) unknown exception occurred", m_server_alias.c_str());
					exception_thrown = true;
				}
				if (exception_thrown) {
					_log.Log(LOG_STATUS, "WebServer(%s) restart server in 5 seconds", m_server_alias.c_str());
					sleep_milliseconds(5000); // prevents from an exception flood
					continue;
				}
				break;
			}
			_log.Log(LOG_STATUS, "WebServer(%s) stopped", m_server_alias.c_str());
		}

		void CWebServer::ReloadCustomSwitchIcons()
		{
			m_custom_light_icons.clear();
			m_custom_light_icons_lookup.clear();
			std::string sLine = "";

			//First get them from the switch_icons.txt file
			std::ifstream infile;
			std::string switchlightsfile = szWWWFolder + "/switch_icons.txt";
			infile.open(switchlightsfile.c_str());
			if (infile.is_open())
			{
				int index = 0;
				while (!infile.eof())
				{
					getline(infile, sLine);
					if (sLine.size() != 0)
					{
						std::vector<std::string> results;
						StringSplit(sLine, ";", results);
						if (results.size() == 3)
						{
							_tCustomIcon cImage;
							cImage.idx = index++;
							cImage.RootFile = results[0];
							cImage.Title = results[1];
							cImage.Description = results[2];
							m_custom_light_icons.push_back(cImage);
							m_custom_light_icons_lookup[cImage.idx] = m_custom_light_icons.size() - 1;
						}
					}
				}
				infile.close();
			}
			//Now get them from the database (idx 100+)
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID,Base,Name,Description FROM CustomImages");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					int ID = atoi(sd[0].c_str());

					_tCustomIcon cImage;
					cImage.idx = 100 + ID;
					cImage.RootFile = sd[1];
					cImage.Title = sd[2];
					cImage.Description = sd[3];

					std::string IconFile16 = cImage.RootFile + ".png";
					std::string IconFile48On = cImage.RootFile + "48_On.png";
					std::string IconFile48Off = cImage.RootFile + "48_Off.png";

					std::map<std::string, std::string> _dbImageFiles;
					_dbImageFiles["IconSmall"] = szWWWFolder + "/images/" + IconFile16;
					_dbImageFiles["IconOn"] = szWWWFolder + "/images/" + IconFile48On;
					_dbImageFiles["IconOff"] = szWWWFolder + "/images/" + IconFile48Off;

					std::map<std::string, std::string>::const_iterator iItt;

					//Check if files are on disk, else add them
					for (iItt = _dbImageFiles.begin(); iItt != _dbImageFiles.end(); ++iItt)
					{
						std::string TableField = iItt->first;
						std::string IconFile = iItt->second;

						if (!file_exist(IconFile.c_str()))
						{
							//Does not exists, extract it from the database and add it
							std::vector<std::vector<std::string> > result2;
							result2 = m_sql.safe_queryBlob("SELECT %s FROM CustomImages WHERE ID=%d", TableField.c_str(), ID);
							if (result2.size() > 0)
							{
								std::ofstream file;
								file.open(IconFile.c_str(), std::ios::out | std::ios::binary);
								if (!file.is_open())
									return;

								file << result2[0][0];
								file.close();
							}
						}
					}

					m_custom_light_icons.push_back(cImage);
					m_custom_light_icons_lookup[cImage.idx] = m_custom_light_icons.size() - 1;
					ii++;
				}
			}
		}

		bool CWebServer::StartServer(server_settings & settings, const std::string & serverpath, const bool bIgnoreUsernamePassword)
		{
			m_server_alias = (settings.is_secure() == true) ? "SSL" : "HTTP";

			if (!settings.is_enabled())
				return true;

			ReloadCustomSwitchIcons();

			int tries = 0;
			bool exception = false;

			//_log.Log(LOG_STATUS, "CWebServer::StartServer() : settings : %s", settings.to_string().c_str());
			do {
				try {
					exception = false;
					m_pWebEm = new http::server::cWebem(settings, serverpath.c_str());
				}
				catch (std::exception& e) {
					exception = true;
					switch (tries) {
					case 0:
						settings.listening_address = "::";
						break;
					case 1:
						settings.listening_address = "0.0.0.0";
						break;
					case 2:
						_log.Log(LOG_ERROR, "WebServer(%s) startup failed on address %s with port: %s: %s", m_server_alias.c_str(), settings.listening_address.c_str(), settings.listening_port.c_str(), e.what());
						if (atoi(settings.listening_port.c_str()) < 1024)
							_log.Log(LOG_ERROR, "WebServer(%s) check privileges for opening ports below 1024", m_server_alias.c_str());
						else
							_log.Log(LOG_ERROR, "WebServer(%s) check if no other application is using port: %s", m_server_alias.c_str(), settings.listening_port.c_str());
						return false;
					}
					tries++;
				}
			} while (exception);

			_log.Log(LOG_STATUS, "WebServer(%s) started on address: %s with port %s", m_server_alias.c_str(), settings.listening_address.c_str(), settings.listening_port.c_str());

			m_pWebEm->SetDigistRealm("Domoticz.com");
			m_pWebEm->SetSessionStore(this);

			if (!bIgnoreUsernamePassword)
			{
				LoadUsers();
				std::string WebLocalNetworks;
				int nValue;
				if (m_sql.GetPreferencesVar("WebLocalNetworks", nValue, WebLocalNetworks))
				{
					std::vector<std::string> strarray;
					StringSplit(WebLocalNetworks, ";", strarray);
					std::vector<std::string>::const_iterator itt;
					for (itt = strarray.begin(); itt != strarray.end(); ++itt)
					{
						std::string network = *itt;
						m_pWebEm->AddLocalNetworks(network);
					}
					//add local hostname
					m_pWebEm->AddLocalNetworks("");
				}
			}

			std::string WebRemoteProxyIPs;
			int nValue;
			if (m_sql.GetPreferencesVar("WebRemoteProxyIPs", nValue, WebRemoteProxyIPs))
			{
				std::vector<std::string> strarray;
				StringSplit(WebRemoteProxyIPs, ";", strarray);
				std::vector<std::string>::const_iterator itt;
				for (itt = strarray.begin(); itt != strarray.end(); ++itt)
				{
					std::string ipaddr = *itt;
					m_pWebEm->AddRemoteProxyIPs(ipaddr);
				}
			}

			//register callbacks
			m_pWebEm->RegisterIncludeCode("switchtypes", boost::bind(&CWebServer::DisplaySwitchTypesCombo, this, _1));
			m_pWebEm->RegisterIncludeCode("metertypes", boost::bind(&CWebServer::DisplayMeterTypesCombo, this, _1));
			m_pWebEm->RegisterIncludeCode("timertypes", boost::bind(&CWebServer::DisplayTimerTypesCombo, this, _1));
			m_pWebEm->RegisterIncludeCode("combolanguage", boost::bind(&CWebServer::DisplayLanguageCombo, this, _1));

			m_pWebEm->RegisterPageCode("/json.htm", boost::bind(&CWebServer::GetJSonPage, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/uploadcustomicon", boost::bind(&CWebServer::Post_UploadCustomIcon, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/html5.appcache", boost::bind(&CWebServer::GetAppCache, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/camsnapshot.jpg", boost::bind(&CWebServer::GetCameraSnapshot, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/backupdatabase.php", boost::bind(&CWebServer::GetDatabaseBackup, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/raspberry.cgi", boost::bind(&CWebServer::GetInternalCameraSnapshot, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/uvccapture.cgi", boost::bind(&CWebServer::GetInternalCameraSnapshot, this, _1, _2, _3)); //TODO: fix me double

			m_pWebEm->RegisterActionCode("storesettings", boost::bind(&CWebServer::PostSettings, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("setrfxcommode", boost::bind(&CWebServer::SetRFXCOMMode, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("rfxupgradefirmware", boost::bind(&CWebServer::RFXComUpgradeFirmware, this, _1, _2, _3));
			RegisterCommandCode("rfxfirmwaregetpercentage", boost::bind(&CWebServer::Cmd_RFXComGetFirmwarePercentage, this, _1, _2, _3), true);
			m_pWebEm->RegisterActionCode("setrego6xxtype", boost::bind(&CWebServer::SetRego6XXType, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("sets0metertype", boost::bind(&CWebServer::SetS0MeterType, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("setlimitlesstype", boost::bind(&CWebServer::SetLimitlessType, this, _1, _2, _3));

			m_pWebEm->RegisterActionCode("setopenthermsettings", boost::bind(&CWebServer::SetOpenThermSettings, this, _1, _2, _3));
			RegisterCommandCode("sendopenthermcommand", boost::bind(&CWebServer::Cmd_SendOpenThermCommand, this, _1, _2, _3), true);

			m_pWebEm->RegisterActionCode("reloadpiface", boost::bind(&CWebServer::ReloadPiFace, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("setcurrentcostmetertype", boost::bind(&CWebServer::SetCurrentCostUSBType, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("restoredatabase", boost::bind(&CWebServer::RestoreDatabase, this, _1, _2, _3));
			m_pWebEm->RegisterActionCode("sbfspotimportolddata", boost::bind(&CWebServer::SBFSpotImportOldData, this, _1, _2, _3));

			m_pWebEm->RegisterActionCode("event_create", boost::bind(&CWebServer::EventCreate, this, _1, _2, _3));

			RegisterCommandCode("getlanguage", boost::bind(&CWebServer::Cmd_GetLanguage, this, _1, _2, _3), true);
			RegisterCommandCode("getthemes", boost::bind(&CWebServer::Cmd_GetThemes, this, _1, _2, _3), true);
			RegisterCommandCode("gettitle", boost::bind(&CWebServer::Cmd_GetTitle, this, _1, _2, _3), true);

			RegisterCommandCode("logincheck", boost::bind(&CWebServer::Cmd_LoginCheck, this, _1, _2, _3), true);
			RegisterCommandCode("getversion", boost::bind(&CWebServer::Cmd_GetVersion, this, _1, _2, _3), true);
			RegisterCommandCode("getlog", boost::bind(&CWebServer::Cmd_GetLog, this, _1, _2, _3));
			RegisterCommandCode("clearlog", boost::bind(&CWebServer::Cmd_ClearLog, this, _1, _2, _3));
			RegisterCommandCode("getauth", boost::bind(&CWebServer::Cmd_GetAuth, this, _1, _2, _3), true);
			RegisterCommandCode("getuptime", boost::bind(&CWebServer::Cmd_GetUptime, this, _1, _2, _3), true);


			RegisterCommandCode("gethardwaretypes", boost::bind(&CWebServer::Cmd_GetHardwareTypes, this, _1, _2, _3));
			RegisterCommandCode("addhardware", boost::bind(&CWebServer::Cmd_AddHardware, this, _1, _2, _3));
			RegisterCommandCode("updatehardware", boost::bind(&CWebServer::Cmd_UpdateHardware, this, _1, _2, _3));
			RegisterCommandCode("deletehardware", boost::bind(&CWebServer::Cmd_DeleteHardware, this, _1, _2, _3));

			RegisterCommandCode("addcamera", boost::bind(&CWebServer::Cmd_AddCamera, this, _1, _2, _3));
			RegisterCommandCode("updatecamera", boost::bind(&CWebServer::Cmd_UpdateCamera, this, _1, _2, _3));
			RegisterCommandCode("deletecamera", boost::bind(&CWebServer::Cmd_DeleteCamera, this, _1, _2, _3));

			RegisterCommandCode("wolgetnodes", boost::bind(&CWebServer::Cmd_WOLGetNodes, this, _1, _2, _3));
			RegisterCommandCode("woladdnode", boost::bind(&CWebServer::Cmd_WOLAddNode, this, _1, _2, _3));
			RegisterCommandCode("wolupdatenode", boost::bind(&CWebServer::Cmd_WOLUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("wolremovenode", boost::bind(&CWebServer::Cmd_WOLRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("wolclearnodes", boost::bind(&CWebServer::Cmd_WOLClearNodes, this, _1, _2, _3));

			RegisterCommandCode("mysensorsgetnodes", boost::bind(&CWebServer::Cmd_MySensorsGetNodes, this, _1, _2, _3));
			RegisterCommandCode("mysensorsgetchilds", boost::bind(&CWebServer::Cmd_MySensorsGetChilds, this, _1, _2, _3));
			RegisterCommandCode("mysensorsupdatenode", boost::bind(&CWebServer::Cmd_MySensorsUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("mysensorsremovenode", boost::bind(&CWebServer::Cmd_MySensorsRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("mysensorsremovechild", boost::bind(&CWebServer::Cmd_MySensorsRemoveChild, this, _1, _2, _3));
			RegisterCommandCode("mysensorsupdatechild", boost::bind(&CWebServer::Cmd_MySensorsUpdateChild, this, _1, _2, _3));

			RegisterCommandCode("pingersetmode", boost::bind(&CWebServer::Cmd_PingerSetMode, this, _1, _2, _3));
			RegisterCommandCode("pingergetnodes", boost::bind(&CWebServer::Cmd_PingerGetNodes, this, _1, _2, _3));
			RegisterCommandCode("pingeraddnode", boost::bind(&CWebServer::Cmd_PingerAddNode, this, _1, _2, _3));
			RegisterCommandCode("pingerupdatenode", boost::bind(&CWebServer::Cmd_PingerUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("pingerremovenode", boost::bind(&CWebServer::Cmd_PingerRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("pingerclearnodes", boost::bind(&CWebServer::Cmd_PingerClearNodes, this, _1, _2, _3));

			RegisterCommandCode("kodisetmode", boost::bind(&CWebServer::Cmd_KodiSetMode, this, _1, _2, _3));
			RegisterCommandCode("kodigetnodes", boost::bind(&CWebServer::Cmd_KodiGetNodes, this, _1, _2, _3));
			RegisterCommandCode("kodiaddnode", boost::bind(&CWebServer::Cmd_KodiAddNode, this, _1, _2, _3));
			RegisterCommandCode("kodiupdatenode", boost::bind(&CWebServer::Cmd_KodiUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("kodiremovenode", boost::bind(&CWebServer::Cmd_KodiRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("kodiclearnodes", boost::bind(&CWebServer::Cmd_KodiClearNodes, this, _1, _2, _3));
			RegisterCommandCode("kodimediacommand", boost::bind(&CWebServer::Cmd_KodiMediaCommand, this, _1, _2, _3));

			RegisterCommandCode("panasonicsetmode", boost::bind(&CWebServer::Cmd_PanasonicSetMode, this, _1, _2, _3));
			RegisterCommandCode("panasonicgetnodes", boost::bind(&CWebServer::Cmd_PanasonicGetNodes, this, _1, _2, _3));
			RegisterCommandCode("panasonicaddnode", boost::bind(&CWebServer::Cmd_PanasonicAddNode, this, _1, _2, _3));
			RegisterCommandCode("panasonicupdatenode", boost::bind(&CWebServer::Cmd_PanasonicUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("panasonicremovenode", boost::bind(&CWebServer::Cmd_PanasonicRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("panasonicclearnodes", boost::bind(&CWebServer::Cmd_PanasonicClearNodes, this, _1, _2, _3));
			RegisterCommandCode("panasonicmediacommand", boost::bind(&CWebServer::Cmd_PanasonicMediaCommand, this, _1, _2, _3));

			RegisterCommandCode("heossetmode", boost::bind(&CWebServer::Cmd_HEOSSetMode, this, _1, _2, _3));
			RegisterCommandCode("heosmediacommand", boost::bind(&CWebServer::Cmd_HEOSMediaCommand, this, _1, _2, _3));

			RegisterCommandCode("onkyoeiscpcommand", boost::bind(&CWebServer::Cmd_OnkyoEiscpCommand, this, _1, _2, _3));

			RegisterCommandCode("bleboxsetmode", boost::bind(&CWebServer::Cmd_BleBoxSetMode, this, _1, _2, _3));
			RegisterCommandCode("bleboxgetnodes", boost::bind(&CWebServer::Cmd_BleBoxGetNodes, this, _1, _2, _3));
			RegisterCommandCode("bleboxaddnode", boost::bind(&CWebServer::Cmd_BleBoxAddNode, this, _1, _2, _3));
			RegisterCommandCode("bleboxremovenode", boost::bind(&CWebServer::Cmd_BleBoxRemoveNode, this, _1, _2, _3));
			RegisterCommandCode("bleboxclearnodes", boost::bind(&CWebServer::Cmd_BleBoxClearNodes, this, _1, _2, _3));
			RegisterCommandCode("bleboxautosearchingnodes", boost::bind(&CWebServer::Cmd_BleBoxAutoSearchingNodes, this, _1, _2, _3));
			RegisterCommandCode("bleboxupdatefirmware", boost::bind(&CWebServer::Cmd_BleBoxUpdateFirmware, this, _1, _2, _3));

			RegisterCommandCode("lmssetmode", boost::bind(&CWebServer::Cmd_LMSSetMode, this, _1, _2, _3));
			RegisterCommandCode("lmsgetnodes", boost::bind(&CWebServer::Cmd_LMSGetNodes, this, _1, _2, _3));
			RegisterCommandCode("lmsgetplaylists", boost::bind(&CWebServer::Cmd_LMSGetPlaylists, this, _1, _2, _3));
			RegisterCommandCode("lmsmediacommand", boost::bind(&CWebServer::Cmd_LMSMediaCommand, this, _1, _2, _3));
			RegisterCommandCode("lmsdeleteunuseddevices", boost::bind(&CWebServer::Cmd_LMSDeleteUnusedDevices, this, _1, _2, _3));

			RegisterCommandCode("savefibarolinkconfig", boost::bind(&CWebServer::Cmd_SaveFibaroLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getfibarolinkconfig", boost::bind(&CWebServer::Cmd_GetFibaroLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getfibarolinks", boost::bind(&CWebServer::Cmd_GetFibaroLinks, this, _1, _2, _3));
			RegisterCommandCode("savefibarolink", boost::bind(&CWebServer::Cmd_SaveFibaroLink, this, _1, _2, _3));
			RegisterCommandCode("deletefibarolink", boost::bind(&CWebServer::Cmd_DeleteFibaroLink, this, _1, _2, _3));

			RegisterCommandCode("saveinfluxlinkconfig", boost::bind(&CWebServer::Cmd_SaveInfluxLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getinfluxlinkconfig", boost::bind(&CWebServer::Cmd_GetInfluxLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getinfluxlinks", boost::bind(&CWebServer::Cmd_GetInfluxLinks, this, _1, _2, _3));
			RegisterCommandCode("saveinfluxlink", boost::bind(&CWebServer::Cmd_SaveInfluxLink, this, _1, _2, _3));
			RegisterCommandCode("deleteinfluxlink", boost::bind(&CWebServer::Cmd_DeleteInfluxLink, this, _1, _2, _3));

			RegisterCommandCode("savehttplinkconfig", boost::bind(&CWebServer::Cmd_SaveHttpLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("gethttplinkconfig", boost::bind(&CWebServer::Cmd_GetHttpLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("gethttplinks", boost::bind(&CWebServer::Cmd_GetHttpLinks, this, _1, _2, _3));
			RegisterCommandCode("savehttplink", boost::bind(&CWebServer::Cmd_SaveHttpLink, this, _1, _2, _3));
			RegisterCommandCode("deletehttplink", boost::bind(&CWebServer::Cmd_DeleteHttpLink, this, _1, _2, _3));

			RegisterCommandCode("savegooglepubsublinkconfig", boost::bind(&CWebServer::Cmd_SaveGooglePubSubLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getgooglepubsublinkconfig", boost::bind(&CWebServer::Cmd_GetGooglePubSubLinkConfig, this, _1, _2, _3));
			RegisterCommandCode("getgooglepubsublinks", boost::bind(&CWebServer::Cmd_GetGooglePubSubLinks, this, _1, _2, _3));
			RegisterCommandCode("savegooglepubsublink", boost::bind(&CWebServer::Cmd_SaveGooglePubSubLink, this, _1, _2, _3));
			RegisterCommandCode("deletegooglepubsublink", boost::bind(&CWebServer::Cmd_DeleteGooglePubSubLink, this, _1, _2, _3));

			RegisterCommandCode("getdevicevalueoptions", boost::bind(&CWebServer::Cmd_GetDeviceValueOptions, this, _1, _2, _3));
			RegisterCommandCode("getdevicevalueoptionwording", boost::bind(&CWebServer::Cmd_GetDeviceValueOptionWording, this, _1, _2, _3));

			RegisterCommandCode("deleteuservariable", boost::bind(&CWebServer::Cmd_DeleteUserVariable, this, _1, _2, _3));
			RegisterCommandCode("saveuservariable", boost::bind(&CWebServer::Cmd_SaveUserVariable, this, _1, _2, _3));
			RegisterCommandCode("updateuservariable", boost::bind(&CWebServer::Cmd_UpdateUserVariable, this, _1, _2, _3));
			RegisterCommandCode("getuservariables", boost::bind(&CWebServer::Cmd_GetUserVariables, this, _1, _2, _3));
			RegisterCommandCode("getuservariable", boost::bind(&CWebServer::Cmd_GetUserVariable, this, _1, _2, _3));

			RegisterCommandCode("allownewhardware", boost::bind(&CWebServer::Cmd_AllowNewHardware, this, _1, _2, _3));

			RegisterCommandCode("addplan", boost::bind(&CWebServer::Cmd_AddPlan, this, _1, _2, _3));
			RegisterCommandCode("updateplan", boost::bind(&CWebServer::Cmd_UpdatePlan, this, _1, _2, _3));
			RegisterCommandCode("deleteplan", boost::bind(&CWebServer::Cmd_DeletePlan, this, _1, _2, _3));
			RegisterCommandCode("getunusedplandevices", boost::bind(&CWebServer::Cmd_GetUnusedPlanDevices, this, _1, _2, _3));
			RegisterCommandCode("addplanactivedevice", boost::bind(&CWebServer::Cmd_AddPlanActiveDevice, this, _1, _2, _3));
			RegisterCommandCode("getplandevices", boost::bind(&CWebServer::Cmd_GetPlanDevices, this, _1, _2, _3));
			RegisterCommandCode("deleteplandevice", boost::bind(&CWebServer::Cmd_DeletePlanDevice, this, _1, _2, _3));
			RegisterCommandCode("setplandevicecoords", boost::bind(&CWebServer::Cmd_SetPlanDeviceCoords, this, _1, _2, _3));
			RegisterCommandCode("deleteallplandevices", boost::bind(&CWebServer::Cmd_DeleteAllPlanDevices, this, _1, _2, _3));
			RegisterCommandCode("changeplanorder", boost::bind(&CWebServer::Cmd_ChangePlanOrder, this, _1, _2, _3));
			RegisterCommandCode("changeplandeviceorder", boost::bind(&CWebServer::Cmd_ChangePlanDeviceOrder, this, _1, _2, _3));

			RegisterCommandCode("gettimerplans", boost::bind(&CWebServer::Cmd_GetTimerPlans, this, _1, _2, _3));
			RegisterCommandCode("addtimerplan", boost::bind(&CWebServer::Cmd_AddTimerPlan, this, _1, _2, _3));
			RegisterCommandCode("updatetimerplan", boost::bind(&CWebServer::Cmd_UpdateTimerPlan, this, _1, _2, _3));
			RegisterCommandCode("deletetimerplan", boost::bind(&CWebServer::Cmd_DeleteTimerPlan, this, _1, _2, _3));

			RegisterCommandCode("getactualhistory", boost::bind(&CWebServer::Cmd_GetActualHistory, this, _1, _2, _3));
			RegisterCommandCode("getnewhistory", boost::bind(&CWebServer::Cmd_GetNewHistory, this, _1, _2, _3));

			RegisterCommandCode("getconfig", boost::bind(&CWebServer::Cmd_GetConfig, this, _1, _2, _3), true);
			RegisterCommandCode("sendnotification", boost::bind(&CWebServer::Cmd_SendNotification, this, _1, _2, _3));
			RegisterCommandCode("emailcamerasnapshot", boost::bind(&CWebServer::Cmd_EmailCameraSnapshot, this, _1, _2, _3));
			RegisterCommandCode("udevice", boost::bind(&CWebServer::Cmd_UpdateDevice, this, _1, _2, _3));
			RegisterCommandCode("udevices", boost::bind(&CWebServer::Cmd_UpdateDevices, this, _1, _2, _3));
			RegisterCommandCode("thermostatstate", boost::bind(&CWebServer::Cmd_SetThermostatState, this, _1, _2, _3));
			RegisterCommandCode("system_shutdown", boost::bind(&CWebServer::Cmd_SystemShutdown, this, _1, _2, _3));
			RegisterCommandCode("system_reboot", boost::bind(&CWebServer::Cmd_SystemReboot, this, _1, _2, _3));
			RegisterCommandCode("execute_script", boost::bind(&CWebServer::Cmd_ExcecuteScript, this, _1, _2, _3));
			RegisterCommandCode("getcosts", boost::bind(&CWebServer::Cmd_GetCosts, this, _1, _2, _3));
			RegisterCommandCode("checkforupdate", boost::bind(&CWebServer::Cmd_CheckForUpdate, this, _1, _2, _3));
			RegisterCommandCode("downloadupdate", boost::bind(&CWebServer::Cmd_DownloadUpdate, this, _1, _2, _3));
			RegisterCommandCode("downloadready", boost::bind(&CWebServer::Cmd_DownloadReady, this, _1, _2, _3));
			RegisterCommandCode("deletedatapoint", boost::bind(&CWebServer::Cmd_DeleteDatePoint, this, _1, _2, _3));

			RegisterCommandCode("setactivetimerplan", boost::bind(&CWebServer::Cmd_SetActiveTimerPlan, this, _1, _2, _3));
			RegisterCommandCode("addtimer", boost::bind(&CWebServer::Cmd_AddTimer, this, _1, _2, _3));
			RegisterCommandCode("updatetimer", boost::bind(&CWebServer::Cmd_UpdateTimer, this, _1, _2, _3));
			RegisterCommandCode("deletetimer", boost::bind(&CWebServer::Cmd_DeleteTimer, this, _1, _2, _3));
			RegisterCommandCode("enabletimer", boost::bind(&CWebServer::Cmd_EnableTimer, this, _1, _2, _3));
			RegisterCommandCode("disabletimer", boost::bind(&CWebServer::Cmd_DisableTimer, this, _1, _2, _3));
			RegisterCommandCode("cleartimers", boost::bind(&CWebServer::Cmd_ClearTimers, this, _1, _2, _3));

			RegisterCommandCode("addscenetimer", boost::bind(&CWebServer::Cmd_AddSceneTimer, this, _1, _2, _3));
			RegisterCommandCode("updatescenetimer", boost::bind(&CWebServer::Cmd_UpdateSceneTimer, this, _1, _2, _3));
			RegisterCommandCode("deletescenetimer", boost::bind(&CWebServer::Cmd_DeleteSceneTimer, this, _1, _2, _3));
			RegisterCommandCode("enablescenetimer", boost::bind(&CWebServer::Cmd_EnableSceneTimer, this, _1, _2, _3));
			RegisterCommandCode("disablescenetimer", boost::bind(&CWebServer::Cmd_DisableSceneTimer, this, _1, _2, _3));
			RegisterCommandCode("clearscenetimers", boost::bind(&CWebServer::Cmd_ClearSceneTimers, this, _1, _2, _3));
			RegisterCommandCode("getsceneactivations", boost::bind(&CWebServer::Cmd_GetSceneActivations, this, _1, _2, _3));
			RegisterCommandCode("addscenecode", boost::bind(&CWebServer::Cmd_AddSceneCode, this, _1, _2, _3));
			RegisterCommandCode("removescenecode", boost::bind(&CWebServer::Cmd_RemoveSceneCode, this, _1, _2, _3));
			RegisterCommandCode("clearscenecodes", boost::bind(&CWebServer::Cmd_ClearSceneCodes, this, _1, _2, _3));
			RegisterCommandCode("renamescene", boost::bind(&CWebServer::Cmd_RenameScene, this, _1, _2, _3));

			RegisterCommandCode("setsetpoint", boost::bind(&CWebServer::Cmd_SetSetpoint, this, _1, _2, _3));
			RegisterCommandCode("addsetpointtimer", boost::bind(&CWebServer::Cmd_AddSetpointTimer, this, _1, _2, _3));
			RegisterCommandCode("updatesetpointtimer", boost::bind(&CWebServer::Cmd_UpdateSetpointTimer, this, _1, _2, _3));
			RegisterCommandCode("deletesetpointtimer", boost::bind(&CWebServer::Cmd_DeleteSetpointTimer, this, _1, _2, _3));
			RegisterCommandCode("enablesetpointtimer", boost::bind(&CWebServer::Cmd_EnableSetpointTimer, this, _1, _2, _3));
			RegisterCommandCode("disablesetpointtimer", boost::bind(&CWebServer::Cmd_DisableSetpointTimer, this, _1, _2, _3));
			RegisterCommandCode("clearsetpointtimers", boost::bind(&CWebServer::Cmd_ClearSetpointTimers, this, _1, _2, _3));

			RegisterCommandCode("serial_devices", boost::bind(&CWebServer::Cmd_GetSerialDevices, this, _1, _2, _3));
			RegisterCommandCode("devices_list", boost::bind(&CWebServer::Cmd_GetDevicesList, this, _1, _2, _3));
			RegisterCommandCode("devices_list_onoff", boost::bind(&CWebServer::Cmd_GetDevicesListOnOff, this, _1, _2, _3));

			RegisterCommandCode("registerhue", boost::bind(&CWebServer::Cmd_PhilipsHueRegister, this, _1, _2, _3));

			RegisterCommandCode("getcustomiconset", boost::bind(&CWebServer::Cmd_GetCustomIconSet, this, _1, _2, _3));
			RegisterCommandCode("deletecustomicon", boost::bind(&CWebServer::Cmd_DeleteCustomIcon, this, _1, _2, _3));
			RegisterCommandCode("updatecustomicon", boost::bind(&CWebServer::Cmd_UpdateCustomIcon, this, _1, _2, _3));

			RegisterCommandCode("renamedevice", boost::bind(&CWebServer::Cmd_RenameDevice, this, _1, _2, _3));
			RegisterCommandCode("setunused", boost::bind(&CWebServer::Cmd_SetUnused, this, _1, _2, _3));

			RegisterCommandCode("addlogmessage", boost::bind(&CWebServer::Cmd_AddLogMessage, this, _1, _2, _3));
			RegisterCommandCode("clearshortlog", boost::bind(&CWebServer::Cmd_ClearShortLog, this, _1, _2, _3));
			RegisterCommandCode("vacuumdatabase", boost::bind(&CWebServer::Cmd_VacuumDatabase, this, _1, _2, _3));

			RegisterCommandCode("addmobiledevice", boost::bind(&CWebServer::Cmd_AddMobileDevice, this, _1, _2, _3));
			RegisterCommandCode("updatemobiledevice", boost::bind(&CWebServer::Cmd_UpdateMobileDevice, this, _1, _2, _3));
			RegisterCommandCode("deletemobiledevice", boost::bind(&CWebServer::Cmd_DeleteMobileDevice, this, _1, _2, _3));

			RegisterCommandCode("addyeelight", boost::bind(&CWebServer::Cmd_AddYeeLight, this, _1, _2, _3));

			RegisterCommandCode("addArilux", boost::bind(&CWebServer::Cmd_AddArilux, this, _1, _2, _3));

			RegisterRType("graph", boost::bind(&CWebServer::RType_HandleGraph, this, _1, _2, _3));
			RegisterRType("lightlog", boost::bind(&CWebServer::RType_LightLog, this, _1, _2, _3));
			RegisterRType("textlog", boost::bind(&CWebServer::RType_TextLog, this, _1, _2, _3));
			RegisterRType("scenelog", boost::bind(&CWebServer::RType_SceneLog, this, _1, _2, _3));
			RegisterRType("settings", boost::bind(&CWebServer::RType_Settings, this, _1, _2, _3));
			RegisterRType("events", boost::bind(&CWebServer::RType_Events, this, _1, _2, _3));

			RegisterRType("hardware", boost::bind(&CWebServer::RType_Hardware, this, _1, _2, _3));
			RegisterRType("devices", boost::bind(&CWebServer::RType_Devices, this, _1, _2, _3));
			RegisterRType("deletedevice", boost::bind(&CWebServer::RType_DeleteDevice, this, _1, _2, _3));
			RegisterRType("cameras", boost::bind(&CWebServer::RType_Cameras, this, _1, _2, _3));
			RegisterRType("users", boost::bind(&CWebServer::RType_Users, this, _1, _2, _3));
			RegisterRType("mobiles", boost::bind(&CWebServer::RType_Mobiles, this, _1, _2, _3));

			RegisterRType("timers", boost::bind(&CWebServer::RType_Timers, this, _1, _2, _3));
			RegisterRType("scenetimers", boost::bind(&CWebServer::RType_SceneTimers, this, _1, _2, _3));
			RegisterRType("setpointtimers", boost::bind(&CWebServer::RType_SetpointTimers, this, _1, _2, _3));

			RegisterRType("gettransfers", boost::bind(&CWebServer::RType_GetTransfers, this, _1, _2, _3));
			RegisterRType("transferdevice", boost::bind(&CWebServer::RType_TransferDevice, this, _1, _2, _3));
			RegisterRType("notifications", boost::bind(&CWebServer::RType_Notifications, this, _1, _2, _3));
			RegisterRType("schedules", boost::bind(&CWebServer::RType_Schedules, this, _1, _2, _3));
			RegisterRType("getshareduserdevices", boost::bind(&CWebServer::RType_GetSharedUserDevices, this, _1, _2, _3));
			RegisterRType("setshareduserdevices", boost::bind(&CWebServer::RType_SetSharedUserDevices, this, _1, _2, _3));
			RegisterRType("setused", boost::bind(&CWebServer::RType_SetUsed, this, _1, _2, _3));
			RegisterRType("scenes", boost::bind(&CWebServer::RType_Scenes, this, _1, _2, _3));
			RegisterRType("addscene", boost::bind(&CWebServer::RType_AddScene, this, _1, _2, _3));
			RegisterRType("deletescene", boost::bind(&CWebServer::RType_DeleteScene, this, _1, _2, _3));
			RegisterRType("updatescene", boost::bind(&CWebServer::RType_UpdateScene, this, _1, _2, _3));
			RegisterRType("createvirtualsensor", boost::bind(&CWebServer::RType_CreateMappedSensor, this, _1, _2, _3));
			RegisterRType("createdevice", boost::bind(&CWebServer::RType_CreateDevice, this, _1, _2, _3));

			RegisterRType("createevohomesensor", boost::bind(&CWebServer::RType_CreateEvohomeSensor, this, _1, _2, _3));
			RegisterRType("bindevohome", boost::bind(&CWebServer::RType_BindEvohome, this, _1, _2, _3));
			RegisterRType("createrflinkdevice", boost::bind(&CWebServer::RType_CreateRFLinkDevice, this, _1, _2, _3));

			RegisterRType("custom_light_icons", boost::bind(&CWebServer::RType_CustomLightIcons, this, _1, _2, _3));
			RegisterRType("plans", boost::bind(&CWebServer::RType_Plans, this, _1, _2, _3));
			RegisterRType("floorplans", boost::bind(&CWebServer::RType_FloorPlans, this, _1, _2, _3));
#ifdef WITH_OPENZWAVE
			//ZWave
			RegisterCommandCode("updatezwavenode", boost::bind(&CWebServer::Cmd_ZWaveUpdateNode, this, _1, _2, _3));
			RegisterCommandCode("deletezwavenode", boost::bind(&CWebServer::Cmd_ZWaveDeleteNode, this, _1, _2, _3));
			RegisterCommandCode("zwaveinclude", boost::bind(&CWebServer::Cmd_ZWaveInclude, this, _1, _2, _3));
			RegisterCommandCode("zwaveexclude", boost::bind(&CWebServer::Cmd_ZWaveExclude, this, _1, _2, _3));

			RegisterCommandCode("zwaveisnodeincluded", boost::bind(&CWebServer::Cmd_ZWaveIsNodeIncluded, this, _1, _2, _3));
			RegisterCommandCode("zwaveisnodeexcluded", boost::bind(&CWebServer::Cmd_ZWaveIsNodeExcluded, this, _1, _2, _3));

			RegisterCommandCode("zwavesoftreset", boost::bind(&CWebServer::Cmd_ZWaveSoftReset, this, _1, _2, _3));
			RegisterCommandCode("zwavehardreset", boost::bind(&CWebServer::Cmd_ZWaveHardReset, this, _1, _2, _3));
			RegisterCommandCode("zwavenetworkheal", boost::bind(&CWebServer::Cmd_ZWaveNetworkHeal, this, _1, _2, _3));
			RegisterCommandCode("zwavenodeheal", boost::bind(&CWebServer::Cmd_ZWaveNodeHeal, this, _1, _2, _3));
			RegisterCommandCode("zwavenetworkinfo", boost::bind(&CWebServer::Cmd_ZWaveNetworkInfo, this, _1, _2, _3));
			RegisterCommandCode("zwaveremovegroupnode", boost::bind(&CWebServer::Cmd_ZWaveRemoveGroupNode, this, _1, _2, _3));
			RegisterCommandCode("zwaveaddgroupnode", boost::bind(&CWebServer::Cmd_ZWaveAddGroupNode, this, _1, _2, _3));
			RegisterCommandCode("zwavegroupinfo", boost::bind(&CWebServer::Cmd_ZWaveGroupInfo, this, _1, _2, _3));
			RegisterCommandCode("zwavecancel", boost::bind(&CWebServer::Cmd_ZWaveCancel, this, _1, _2, _3));
			RegisterCommandCode("applyzwavenodeconfig", boost::bind(&CWebServer::Cmd_ApplyZWaveNodeConfig, this, _1, _2, _3));
			RegisterCommandCode("requestzwavenodeconfig", boost::bind(&CWebServer::Cmd_ZWaveRequestNodeConfig, this, _1, _2, _3));
			RegisterCommandCode("zwavestatecheck", boost::bind(&CWebServer::Cmd_ZWaveStateCheck, this, _1, _2, _3));
			RegisterCommandCode("zwavereceiveconfigurationfromothercontroller", boost::bind(&CWebServer::Cmd_ZWaveReceiveConfigurationFromOtherController, this, _1, _2, _3));
			RegisterCommandCode("zwavesendconfigurationtosecondcontroller", boost::bind(&CWebServer::Cmd_ZWaveSendConfigurationToSecondaryController, this, _1, _2, _3));
			RegisterCommandCode("zwavetransferprimaryrole", boost::bind(&CWebServer::Cmd_ZWaveTransferPrimaryRole, this, _1, _2, _3));
			RegisterCommandCode("zwavestartusercodeenrollmentmode", boost::bind(&CWebServer::Cmd_ZWaveSetUserCodeEnrollmentMode, this, _1, _2, _3));
			RegisterCommandCode("zwavegetusercodes", boost::bind(&CWebServer::Cmd_ZWaveGetNodeUserCodes, this, _1, _2, _3));
			RegisterCommandCode("zwaveremoveusercode", boost::bind(&CWebServer::Cmd_ZWaveRemoveUserCode, this, _1, _2, _3));

			m_pWebEm->RegisterPageCode("/zwavegetconfig.php", boost::bind(&CWebServer::ZWaveGetConfigFile, this, _1, _2, _3));

			m_pWebEm->RegisterPageCode("/ozwcp/poll.xml", boost::bind(&CWebServer::ZWaveCPPollXml, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/cp.html", boost::bind(&CWebServer::ZWaveCPIndex, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/confparmpost.html", boost::bind(&CWebServer::ZWaveCPNodeGetConf, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/refreshpost.html", boost::bind(&CWebServer::ZWaveCPNodeGetValues, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/valuepost.html", boost::bind(&CWebServer::ZWaveCPNodeSetValue, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/buttonpost.html", boost::bind(&CWebServer::ZWaveCPNodeSetButton, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/admpost.html", boost::bind(&CWebServer::ZWaveCPAdminCommand, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/nodepost.html", boost::bind(&CWebServer::ZWaveCPNodeChange, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/savepost.html", boost::bind(&CWebServer::ZWaveCPSaveConfig, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/thpost.html", boost::bind(&CWebServer::ZWaveCPTestHeal, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/topopost.html", boost::bind(&CWebServer::ZWaveCPGetTopo, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/statpost.html", boost::bind(&CWebServer::ZWaveCPGetStats, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/grouppost.html", boost::bind(&CWebServer::ZWaveCPSetGroup, this, _1, _2, _3));
			m_pWebEm->RegisterPageCode("/ozwcp/scenepost.html", boost::bind(&CWebServer::ZWaveCPSceneCommand, this, _1, _2, _3));
			//
			//pollpost.html
			//scenepost.html
			//thpost.html
			RegisterRType("openzwavenodes", boost::bind(&CWebServer::RType_OpenZWaveNodes, this, _1, _2, _3));
#endif

#ifdef WITH_TELLDUSCORE
			RegisterCommandCode("tellstickApplySettings", boost::bind(&CWebServer::Cmd_TellstickApplySettings, this, _1, _2, _3));
#endif

			m_pWebEm->RegisterWhitelistURLString("/html5.appcache");

			//Start normal worker thread
			m_bDoStop = false;
			m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CWebServer::Do_Work, shared_from_this())));

			return (m_thread != NULL);
		}

		void CWebServer::StopServer()
		{
			m_bDoStop = true;
			try
			{
				if (m_pWebEm == NULL)
					return;
				m_pWebEm->Stop();
				if (m_thread != NULL) {
					m_thread->join();
					m_thread.reset();
				}
				delete m_pWebEm;
				m_pWebEm = NULL;
			}
			catch (...)
			{

			}
		}

		void CWebServer::SetAuthenticationMethod(int amethod)
		{
			if (m_pWebEm == NULL)
				return;
			m_pWebEm->SetAuthenticationMethod((_eAuthenticationMethod)amethod);
		}

		void CWebServer::SetWebTheme(const std::string &themename)
		{
			if (m_pWebEm == NULL)
				return;
			m_pWebEm->SetWebTheme(themename);
		}

		void CWebServer::SetWebRoot(const std::string &webRoot)
		{
			if (m_pWebEm == NULL)
				return;
			m_pWebEm->SetWebRoot(webRoot);
		}

		void CWebServer::RegisterCommandCode(const char* idname, webserver_response_function ResponseFunction, bool bypassAuthentication)
		{
			m_webcommands.insert(std::pair<std::string, webserver_response_function >(std::string(idname), ResponseFunction));
			if (bypassAuthentication)
			{
				m_pWebEm->RegisterWhitelistURLString(idname);
			}
		}

		void CWebServer::RegisterRType(const char* idname, webserver_response_function ResponseFunction)
		{
			m_webrtypes.insert(std::pair<std::string, webserver_response_function >(std::string(idname), ResponseFunction));
		}

		void CWebServer::HandleRType(const std::string &rtype, WebEmSession & session, const request& req, Json::Value &root)
		{
			std::map < std::string, webserver_response_function >::iterator pf = m_webrtypes.find(rtype);
			if (pf != m_webrtypes.end())
			{
				pf->second(session, req, root);
			}
		}

		int GetDirFilesRecursive(const std::string &DirPath, std::map<std::string, int> &_Files)
		{
			DIR* dir;
			struct dirent *ent;
			if ((dir = opendir(DirPath.c_str())) != NULL)
			{
				while ((ent = readdir(dir)) != NULL)
				{
					if (dirent_is_directory(DirPath, ent))
					{
						if ((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0) && (strcmp(ent->d_name, ".svn") != 0))
						{
							std::string nextdir = DirPath + ent->d_name + "/";
							if (GetDirFilesRecursive(nextdir.c_str(), _Files))
							{
								closedir(dir);
								return 1;
							}
						}
					}
					else
					{
						std::string fname = DirPath + CURLEncode::URLEncode(ent->d_name);
						_Files[fname] = 1;
					}
				}
			}
			closedir(dir);
			return 0;
		}

		void CWebServer::GetAppCache(WebEmSession & session, const request& req, reply & rep)
		{
			std::string response = "";
			if (g_bDontCacheWWW)
			{
				return;
			}
			//Return the appcache file (dynamicly generated)
			std::string sLine;
			std::string filename = szWWWFolder + "/html5.appcache";


			std::string sWebTheme = "default";
			m_sql.GetPreferencesVar("WebTheme", sWebTheme);

			//Get Dynamic Theme Files
			std::map<std::string, int> _ThemeFiles;
			GetDirFilesRecursive(szWWWFolder + "/styles/" + sWebTheme + "/", _ThemeFiles);

			//Get Dynamic Floorplan Files
			std::map<std::string, int> _FloorplanFiles;
			GetDirFilesRecursive(szWWWFolder + "/images/floorplans/", _FloorplanFiles);

			std::ifstream is(filename.c_str());
			if (is)
			{
				while (!is.eof())
				{
					getline(is, sLine);
					if (sLine != "")
					{
						if (sLine.find("#ThemeFiles") != std::string::npos)
						{
							response += "#Theme=" + sWebTheme + "\n";
							//Add all theme files
							std::map<std::string, int>::const_iterator itt;
							for (itt = _ThemeFiles.begin(); itt != _ThemeFiles.end(); ++itt)
							{
								std::string tfname = (itt->first).substr(szWWWFolder.size() + 1);
								stdreplace(tfname, "styles/" + sWebTheme, "acttheme");
								response += tfname + "\n";
							}
							continue;
						}
						else if (sLine.find("#Floorplans") != std::string::npos)
						{
							//Add all floorplans
							std::map<std::string, int>::const_iterator itt;
							for (itt = _FloorplanFiles.begin(); itt != _FloorplanFiles.end(); ++itt)
							{
								std::string tfname = (itt->first).substr(szWWWFolder.size() + 1);
								response += tfname + "\n";
							}
							continue;
						}
						else if (sLine.find("#SwitchIcons") != std::string::npos)
						{
							//Add database switch icons
							std::vector<_tCustomIcon>::const_iterator itt;
							for (itt = m_custom_light_icons.begin(); itt != m_custom_light_icons.end(); ++itt)
							{
								if (itt->idx >= 100)
								{
									std::string IconFile16 = itt->RootFile + ".png";
									std::string IconFile48On = itt->RootFile + "48_On.png";
									std::string IconFile48Off = itt->RootFile + "48_Off.png";

									response += "images/" + CURLEncode::URLEncode(IconFile16) + "\n";
									response += "images/" + CURLEncode::URLEncode(IconFile48On) + "\n";
									response += "images/" + CURLEncode::URLEncode(IconFile48Off) + "\n";
								}
							}
						}
					}
					response += sLine + "\n";
				}
			}
			reply::set_content(&rep, response);
		}

		void CWebServer::Cmd_GetLanguage(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sValue;
			if (m_sql.GetPreferencesVar("Language", sValue))
			{
				root["status"] = "OK";
				root["title"] = "GetLanguage";
				root["language"] = sValue;
			}
		}

		void CWebServer::Cmd_GetThemes(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetThemes";
			m_mainworker.GetAvailableWebThemes();
			std::vector<std::string>::const_iterator itt;
			int ii = 0;
			for (itt = m_mainworker.m_webthemes.begin(); itt != m_mainworker.m_webthemes.end(); ++itt)
			{
				root["result"][ii]["theme"] = *itt;
				ii++;
			}
		}

		void CWebServer::Cmd_GetTitle(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sValue;
			root["status"] = "OK";
			root["title"] = "GetTitle";
			if (m_sql.GetPreferencesVar("Title", sValue))
				root["Title"] = sValue;
			else
				root["Title"] = "Domoticz";
		}

		void CWebServer::Cmd_LoginCheck(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string tmpusrname = request::findValue(&req, "username");
			std::string tmpusrpass = request::findValue(&req, "password");
			if (
				(tmpusrname.empty()) ||
				(tmpusrpass.empty())
				)
				return;

			std::string rememberme = request::findValue(&req, "rememberme");

			std::string usrname;
			std::string usrpass;
			if (request_handler::url_decode(tmpusrname, usrname))
			{
				if (request_handler::url_decode(tmpusrpass, usrpass))
				{
					usrname = base64_decode(usrname);
					int iUser = FindUser(usrname.c_str());
					if (iUser == -1) {
						// log brute force attack
						_log.Log(LOG_ERROR, "Failed login attempt from %s for user '%s' !", session.remote_host.c_str(), usrname.c_str());
						return;
					}
					if (m_users[iUser].Password != usrpass) {
						// log brute force attack
						_log.Log(LOG_ERROR, "Failed login attempt from %s for '%s' !", session.remote_host.c_str(), m_users[iUser].Username.c_str());
						return;
					}
					_log.Log(LOG_STATUS, "Login successful from %s for user '%s'", session.remote_host.c_str(), m_users[iUser].Username.c_str());
					root["status"] = "OK";
					root["version"] = szAppVersion;
					root["title"] = "logincheck";
					session.isnew = true;
					session.username = m_users[iUser].Username;
					session.rights = m_users[iUser].userrights;
					session.rememberme = (rememberme == "true");
					root["user"] = session.username;
					root["rights"] = session.rights;
				}
			}
		}

		void CWebServer::Cmd_GetHardwareTypes(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			root["status"] = "OK";
			root["title"] = "GetHardwareTypes";
			std::map<std::string, int> _htypes;
			for (int ii = 0; ii < HTYPE_END; ii++)
			{
				bool bDoAdd = true;
#ifndef _DEBUG
#ifdef WIN32
				if (
					(ii == HTYPE_RaspberryBMP085) ||
					(ii == HTYPE_RaspberryHTU21D) ||
					(ii == HTYPE_RaspberryTSL2561) ||
					(ii == HTYPE_RaspberryPCF8574) ||
					(ii == HTYPE_RaspberryBME280) ||
					(ii == HTYPE_RaspberryMCP23017)
					)
				{
					bDoAdd = false;
				}
				else
				{
#ifndef WITH_LIBUSB
					if (
						(ii == HTYPE_VOLCRAFTCO20) ||
						(ii == HTYPE_TE923)
						)
					{
						bDoAdd = false;
					}
#endif

		}
#endif
#endif
#ifndef WITH_OPENZWAVE
				if (ii == HTYPE_OpenZWave)
					bDoAdd = false;
#endif
#ifndef WITH_GPIO
				if (ii == HTYPE_RaspberryGPIO)
				{
					bDoAdd = false;
				}

				if (ii == HTYPE_SysfsGpio)
				{
					bDoAdd = false;
				}
#endif
				if (ii == HTYPE_PythonPlugin)
					bDoAdd = false;
				if (bDoAdd)
					_htypes[Hardware_Type_Desc(ii)] = ii;
	}
			//return a sorted hardware list
			std::map<std::string, int>::const_iterator itt;
			int ii = 0;
			for (itt = _htypes.begin(); itt != _htypes.end(); ++itt)
			{
				root["result"][ii]["idx"] = itt->second;
				root["result"][ii]["name"] = itt->first;
				ii++;
			}

#ifdef ENABLE_PYTHON
			// Append Plugin list as well
			PluginList(root["result"]);
#endif
}

		void CWebServer::Cmd_AddHardware(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string name = CURLEncode::URLDecode(request::findValue(&req, "name"));
			std::string senabled = request::findValue(&req, "enabled");
			std::string shtype = request::findValue(&req, "htype");
			std::string address = request::findValue(&req, "address");
			std::string sport = request::findValue(&req, "port");
			std::string username = CURLEncode::URLDecode(request::findValue(&req, "username"));
			std::string password = CURLEncode::URLDecode(request::findValue(&req, "password"));
			std::string extra = CURLEncode::URLDecode(request::findValue(&req, "extra"));
			std::string sdatatimeout = request::findValue(&req, "datatimeout");
			if (
				(name.empty()) ||
				(senabled.empty()) ||
				(shtype.empty())
				)
				return;
			_eHardwareTypes htype = (_eHardwareTypes)atoi(shtype.c_str());

			int iDataTimeout = atoi(sdatatimeout.c_str());
			int mode1 = 0;
			int mode2 = 0;
			int mode3 = 0;
			int mode4 = 0;
			int mode5 = 0;
			int mode6 = 0;
			int port = atoi(sport.c_str());
			std::string mode1Str = request::findValue(&req, "Mode1");
			if (!mode1Str.empty()) {
				mode1 = atoi(mode1Str.c_str());
			}
			std::string mode2Str = request::findValue(&req, "Mode2");
			if (!mode2Str.empty()) {
				mode2 = atoi(mode2Str.c_str());
			}
			std::string mode3Str = request::findValue(&req, "Mode3");
			if (!mode3Str.empty()) {
				mode3 = atoi(mode3Str.c_str());
			}
			std::string mode4Str = request::findValue(&req, "Mode4");
			if (!mode4Str.empty()) {
				mode4 = atoi(mode4Str.c_str());
			}
			std::string mode5Str = request::findValue(&req, "Mode5");
			if (!mode5Str.empty()) {
				mode5 = atoi(mode5Str.c_str());
			}
			std::string mode6Str = request::findValue(&req, "Mode6");
			if (!mode6Str.empty()) {
				mode6 = atoi(mode6Str.c_str());
			}

			if (IsSerialDevice(htype))
			{
				//USB/System
				if (sport.empty())
					return; //need to have a serial port

				if (htype == HTYPE_TeleinfoMeter) {
					// Teleinfo always has decimals. Chances to have a P1 and a Teleinfo device on the same
					// Domoticz instance are very low as both are national standards (NL and FR)
					m_sql.UpdatePreferencesVar("SmartMeterType", 0);
				}
			}
			else if (IsNetworkDevice(htype))
			{
				//Lan
				if (address.empty() || port == 0)
					return;

				if (htype == HTYPE_MySensorsMQTT || htype == HTYPE_MQTT) {
					std::string modeqStr = request::findValue(&req, "mode1");
					if (!modeqStr.empty()) {
						mode1 = atoi(modeqStr.c_str());
					}
				}

				if (htype == HTYPE_ECODEVICES) {
					// EcoDevices always have decimals. Chances to have a P1 and a EcoDevice/Teleinfo device on the same
					// Domoticz instance are very low as both are national standards (NL and FR)
					m_sql.UpdatePreferencesVar("SmartMeterType", 0);
				}
			}
			else if (htype == HTYPE_DomoticzInternal) {
				// DomoticzInternal cannot be added manually
				return;
			}
			else if (htype == HTYPE_Domoticz) {
				//Remote Domoticz
				if (address.empty() || port == 0)
					return;
			}
			else if (htype == HTYPE_TE923) {
				//all fine here!
			}
			else if (htype == HTYPE_VOLCRAFTCO20) {
				//all fine here!
			}
			else if (htype == HTYPE_System) {
				//There should be only one
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT ID FROM Hardware WHERE (Type==%d)", HTYPE_System);
				if (!result.empty())
					return;
			}
			else if (htype == HTYPE_1WIRE) {
				//all fine here!
			}
			else if (htype == HTYPE_Rtl433) {
				//all fine here!
			}
			else if (htype == HTYPE_Pinger) {
				//all fine here!
			}
			else if (htype == HTYPE_Kodi) {
				//all fine here!
			}
			else if (htype == HTYPE_PanasonicTV) {
				// all fine here!
			}
			else if (htype == HTYPE_LogitechMediaServer) {
				//all fine here!
			}
			else if (htype == HTYPE_RaspberryBMP085) {
				//all fine here!
			}
			else if (htype == HTYPE_RaspberryHTU21D) {
				//all fine here!
			}
			else if (htype == HTYPE_RaspberryTSL2561) {
				//all fine here!
			}
			else if (htype == HTYPE_RaspberryBME280) {
				//all fine here!
			}
			else if (htype == HTYPE_RaspberryMCP23017) {
				//all fine here!
			}
			else if (htype == HTYPE_Dummy) {
				//all fine here!
			}
			else if (htype == HTYPE_Tellstick) {
				//all fine here!
			}
			else if (htype == HTYPE_EVOHOME_SCRIPT || htype == HTYPE_EVOHOME_SERIAL || htype == HTYPE_EVOHOME_WEB || htype == HTYPE_EVOHOME_TCP) {
				//all fine here!
			}
			else if (htype == HTYPE_PiFace) {
				//all fine here!
			}
			else if (htype == HTYPE_HTTPPOLLER) {
				//all fine here!
			}
			else if (htype == HTYPE_BleBox) {
				//all fine here!
			}
			else if (htype == HTYPE_HEOS) {
				//all fine here!
			}
			else if (htype == HTYPE_Yeelight) {
				//all fine here!
			}
			else if (htype == HTYPE_XiaomiGateway) {
				//all fine here!
			}
			else if (htype == HTYPE_Arilux) {
				//all fine here!
			}
			else if (htype == HTYPE_USBtinGateway) {
				//All fine here
			}
			else if (
				(htype == HTYPE_Wunderground) ||
				(htype == HTYPE_DarkSky) ||
				(htype == HTYPE_AccuWeather) ||
				(htype == HTYPE_OpenWeatherMap) ||
				(htype == HTYPE_ICYTHERMOSTAT) ||
				(htype == HTYPE_TOONTHERMOSTAT) ||
				(htype == HTYPE_AtagOne) ||
				(htype == HTYPE_PVOUTPUT_INPUT) ||
				(htype == HTYPE_NEST) ||
				(htype == HTYPE_ANNATHERMOSTAT) ||
				(htype == HTYPE_THERMOSMART) ||
				(htype == HTYPE_Tado) ||
				(htype == HTYPE_Netatmo)
				)
			{
				if (
					(username.empty()) ||
					(password.empty())
					)
					return;
			}
			else if (htype == HTYPE_SolarEdgeAPI)
			{
				if (
					(username.empty())
					)
					return;
			}
			else if (htype == HTYPE_Nest_OAuthAPI) {
				if (
					(username == "") &&
					(extra == "||")
					)
					return;
			}
			else if (htype == HTYPE_SBFSpot) {
				if (username.empty())
					return;
			}
			else if (htype == HTYPE_HARMONY_HUB) {
				if (
					(address.empty() || port == 0)
					)
					return;
			}
			else if (htype == HTYPE_Philips_Hue) {
				if (
					(username.empty()) ||
					(address.empty() || port == 0)
					)
					return;
				if (port == 0)
					port = 80;
			}
			else if (htype == HTYPE_WINDDELEN) {
				std::string mill_id = request::findValue(&req, "Mode1");
				if (
					(mill_id.empty()) ||
					(sport.empty())
					)

					return;
				mode1 = atoi(mill_id.c_str());
			}
			else if (htype == HTYPE_Honeywell) {
				//all fine here!
			}
#ifndef WITH_GPIO
			else if (htype == HTYPE_RaspberryGPIO) {
				//all fine here!
			}
			else if (htype == HTYPE_SysfsGpio) {
				//all fine here!
			}
#endif
			else if (htype == HTYPE_OpenWebNetTCP) {
				//All fine here
			}
			else if (htype == HTYPE_Daikin) {
				//All fine here
			}
			else if (htype == HTYPE_GoodweAPI) {
				if (username.empty())
					return;
			}
			else if (htype == HTYPE_PythonPlugin) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryPCF8574) {
				//All fine here
			}
			else if (htype == HTYPE_OpenWebNetUSB) {
				//All fine here
			}
			else if (htype == HTYPE_IntergasInComfortLAN2RF) {
				//All fine here
			}
			else if (htype == HTYPE_EnphaseAPI) {
				//All fine here
			}
			else if (htype == HTYPE_EcoCompteur) {
				//all fine here!
			}
			else
				return;

			root["status"] = "OK";
			root["title"] = "AddHardware";

			std::vector<std::vector<std::string> > result;

			if (htype == HTYPE_Domoticz)
			{
				if (password.size() != 32)
				{
					password = GenerateMD5Hash(password);
				}
			}
			else if ((htype == HTYPE_S0SmartMeterUSB) || (htype == HTYPE_S0SmartMeterTCP))
			{
				extra = "0;1000;0;1000;0;1000;0;1000;0;1000";
			}
			else if (htype == HTYPE_Pinger)
			{
				mode1 = 30;
				mode2 = 1000;
			}
			else if (htype == HTYPE_Kodi)
			{
				mode1 = 30;
				mode2 = 1000;
			}
			else if (htype == HTYPE_PanasonicTV)
			{
				mode1 = 30;
				mode2 = 1000;
			}
			else if (htype == HTYPE_LogitechMediaServer)
			{
				mode1 = 30;
				mode2 = 1000;
			}
			else if (htype == HTYPE_HEOS)
			{
				mode1 = 30;
				mode2 = 1000;
			}
			else if (htype == HTYPE_Tellstick)
			{
				mode1 = 4;
				mode2 = 500;
			}

			if (htype == HTYPE_HTTPPOLLER) {
				m_sql.safe_query(
					"INSERT INTO Hardware (Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout) VALUES ('%q',%d, %d,'%q',%d,'%q','%q','%q','%q','%q','%q', '%q', '%q', '%q', '%q', %d)",
					name.c_str(),
					(senabled == "true") ? 1 : 0,
					htype,
					address.c_str(),
					port,
					sport.c_str(),
					username.c_str(),
					password.c_str(),
					extra.c_str(),
					mode1Str.c_str(), mode2Str.c_str(), mode3Str.c_str(), mode4Str.c_str(), mode5Str.c_str(), mode6Str.c_str(),
					iDataTimeout
				);
			}
			else if (htype == HTYPE_PythonPlugin) {
				sport = request::findValue(&req, "serialport");
				m_sql.safe_query(
					"INSERT INTO Hardware (Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout) VALUES ('%q',%d, %d,'%q',%d,'%q','%q','%q','%q','%q','%q', '%q', '%q', '%q', '%q', %d)",
					name.c_str(),
					(senabled == "true") ? 1 : 0,
					htype,
					address.c_str(),
					port,
					sport.c_str(),
					username.c_str(),
					password.c_str(),
					extra.c_str(),
					mode1Str.c_str(), mode2Str.c_str(), mode3Str.c_str(), mode4Str.c_str(), mode5Str.c_str(), mode6Str.c_str(),
					iDataTimeout
				);
			}
			else {
				m_sql.safe_query(
					"INSERT INTO Hardware (Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout) VALUES ('%q',%d, %d,'%q',%d,'%q','%q','%q','%q',%d,%d,%d,%d,%d,%d,%d)",
					name.c_str(),
					(senabled == "true") ? 1 : 0,
					htype,
					address.c_str(),
					port,
					sport.c_str(),
					username.c_str(),
					password.c_str(),
					extra.c_str(),
					mode1, mode2, mode3, mode4, mode5, mode6,
					iDataTimeout
				);
			}

			//add the device for real in our system
			result = m_sql.safe_query("SELECT MAX(ID) FROM Hardware");
			if (result.size() > 0)
			{
				std::vector<std::string> sd = result[0];
				int ID = atoi(sd[0].c_str());

				root["idx"] = sd[0].c_str(); // OTO output the created ID for easier management on the caller side (if automated)

				m_mainworker.AddHardwareFromParams(ID, name, (senabled == "true") ? true : false, htype, address, port, sport, username, password, extra, mode1, mode2, mode3, mode4, mode5, mode6, iDataTimeout, true);
			}
		}

		void CWebServer::Cmd_UpdateHardware(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string name = CURLEncode::URLDecode(request::findValue(&req, "name"));
			std::string senabled = request::findValue(&req, "enabled");
			std::string shtype = request::findValue(&req, "htype");
			std::string address = request::findValue(&req, "address");
			std::string sport = request::findValue(&req, "port");
			std::string username = CURLEncode::URLDecode(request::findValue(&req, "username"));
			std::string password = CURLEncode::URLDecode(request::findValue(&req, "password"));
			std::string extra = CURLEncode::URLDecode(request::findValue(&req, "extra"));
			std::string sdatatimeout = request::findValue(&req, "datatimeout");

			if (
				(name.empty()) ||
				(senabled.empty()) ||
				(shtype.empty())
				)
				return;

			int mode1 = atoi(request::findValue(&req, "Mode1").c_str());
			int mode2 = atoi(request::findValue(&req, "Mode2").c_str());
			int mode3 = atoi(request::findValue(&req, "Mode3").c_str());
			int mode4 = atoi(request::findValue(&req, "Mode4").c_str());
			int mode5 = atoi(request::findValue(&req, "Mode5").c_str());
			int mode6 = atoi(request::findValue(&req, "Mode6").c_str());

			bool bEnabled = (senabled == "true") ? true : false;

			_eHardwareTypes htype = (_eHardwareTypes)atoi(shtype.c_str());
			int iDataTimeout = atoi(sdatatimeout.c_str());

			int port = atoi(sport.c_str());

			bool bIsSerial = false;

			if (IsSerialDevice(htype))
			{
				//USB/System
				bIsSerial = true;
				if (bEnabled)
				{
					if (sport.empty())
						return; //need to have a serial port
				}
			}
			else if (
				(htype == HTYPE_RFXLAN) || (htype == HTYPE_P1SmartMeterLAN) ||
				(htype == HTYPE_YouLess) || (htype == HTYPE_RazberryZWave) || (htype == HTYPE_OpenThermGatewayTCP) || (htype == HTYPE_LimitlessLights) ||
				(htype == HTYPE_SolarEdgeTCP) || (htype == HTYPE_WOL) || (htype == HTYPE_S0SmartMeterTCP) || (htype == HTYPE_ECODEVICES) || (htype == HTYPE_Mochad) ||
				(htype == HTYPE_MySensorsTCP) || (htype == HTYPE_MySensorsMQTT) || (htype == HTYPE_MQTT) || (htype == HTYPE_FRITZBOX) || (htype == HTYPE_ETH8020) || (htype == HTYPE_Sterbox) ||
				(htype == HTYPE_KMTronicTCP) || (htype == HTYPE_KMTronicUDP) || (htype == HTYPE_SOLARMAXTCP) || (htype == HTYPE_RelayNet) || (htype == HTYPE_SatelIntegra) || (htype == HTYPE_eHouseTCP) || (htype == HTYPE_RFLINKTCP) ||
				(htype == HTYPE_Comm5TCP || (htype == HTYPE_Comm5SMTCP) || (htype == HTYPE_CurrentCostMeterLAN)) ||
				(htype == HTYPE_NefitEastLAN) || (htype == HTYPE_DenkoviSmartdenLan) || (htype == HTYPE_DenkoviSmartdenIPInOut) || (htype == HTYPE_Ec3kMeterTCP) || (htype == HTYPE_MultiFun) || (htype == HTYPE_ZIBLUETCP) || (htype == HTYPE_OnkyoAVTCP)
				) {
				//Lan
				if (address.empty())
					return;
			}
			else if (htype == HTYPE_DomoticzInternal) {
				// DomoticzInternal cannot be updated
				return;
			}
			else if (htype == HTYPE_Domoticz) {
				//Remote Domoticz
				if (address.empty())
					return;
			}
			else if (htype == HTYPE_System) {
				//There should be only one, and with this ID
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT ID FROM Hardware WHERE (Type==%d)", HTYPE_System);
				if (!result.empty())
				{
					int hID = atoi(result[0][0].c_str());
					int aID = atoi(idx.c_str());
					if (hID != aID)
						return;
				}
			}
			else if (htype == HTYPE_TE923) {
				//All fine here
			}
			else if (htype == HTYPE_VOLCRAFTCO20) {
				//All fine here
			}
			else if (htype == HTYPE_1WIRE) {
				//All fine here
			}
			else if (htype == HTYPE_Pinger) {
				//All fine here
			}
			else if (htype == HTYPE_Kodi) {
				//All fine here
			}
			else if (htype == HTYPE_PanasonicTV) {
				//All fine here
			}
			else if (htype == HTYPE_LogitechMediaServer) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryBMP085) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryHTU21D) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryTSL2561) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryBME280) {
				//All fine here
			}
			else if (htype == HTYPE_RaspberryMCP23017) {
				//all fine here!
			}
			else if (htype == HTYPE_Dummy) {
				//All fine here
			}
			else if (htype == HTYPE_EVOHOME_SCRIPT || htype == HTYPE_EVOHOME_SERIAL || htype == HTYPE_EVOHOME_WEB || htype == HTYPE_EVOHOME_TCP) {
				//All fine here
			}
			else if (htype == HTYPE_PiFace) {
				//All fine here
			}
			else if (htype == HTYPE_HTTPPOLLER) {
				//all fine here!
			}
			else if (htype == HTYPE_BleBox) {
				//All fine here
			}
			else if (htype == HTYPE_HEOS) {
				//All fine here
			}
			else if (htype == HTYPE_Yeelight) {
				//All fine here
			}
			else if (htype == HTYPE_XiaomiGateway) {
				//All fine here
			}
			else if (htype == HTYPE_Arilux) {
				//All fine here
			}
			else if (htype == HTYPE_USBtinGateway) {
				//All fine here
			}
			else if (
				(htype == HTYPE_Wunderground) ||
				(htype == HTYPE_DarkSky) ||
				(htype == HTYPE_AccuWeather) ||
				(htype == HTYPE_OpenWeatherMap) ||
				(htype == HTYPE_ICYTHERMOSTAT) ||
				(htype == HTYPE_TOONTHERMOSTAT) ||
				(htype == HTYPE_AtagOne) ||
				(htype == HTYPE_PVOUTPUT_INPUT) ||
				(htype == HTYPE_NEST) ||
				(htype == HTYPE_ANNATHERMOSTAT) ||
				(htype == HTYPE_THERMOSMART) ||
				(htype == HTYPE_Tado) ||
				(htype == HTYPE_Netatmo)
				)
			{
				if (
					(username.empty()) ||
					(password.empty())
					)
					return;
			}
			else if (htype == HTYPE_SolarEdgeAPI)
			{
				if (
					(username.empty())
					)
					return;
			}
			else if (htype == HTYPE_Nest_OAuthAPI) {
				if (
					(username == "") &&
					(extra == "||")
					)
					return;
			}
			else if (htype == HTYPE_HARMONY_HUB) {
				if (
					(address.empty())
					)
					return;
			}
			else if (htype == HTYPE_Philips_Hue) {
				if (
					(username.empty()) ||
					(address.empty())
					)
					return;
				if (port == 0)
					port = 80;
			}
#ifndef WITH_GPIO
			else if (htype == HTYPE_RaspberryGPIO) {
				//all fine here!
			}
			else if (htype == HTYPE_SysfsGpio) {
				//all fine here!
			}
#endif
			else if (htype == HTYPE_Rtl433) {
				//all fine here!
			}
			else if (htype == HTYPE_Daikin) {
				//all fine here!
			}
			else if (htype == HTYPE_SBFSpot) {
				if (username.empty())
					return;
			}
			else if (htype == HTYPE_WINDDELEN) {
				std::string mill_id = request::findValue(&req, "Mode1");
				if (
					(mill_id.empty()) ||
					(sport.empty())
					)
					return;
			}
			else if (htype == HTYPE_Honeywell) {
				//All fine here
			}
			else if (htype == HTYPE_OpenWebNetTCP) {
				//All fine here
			}
			else if (htype == HTYPE_PythonPlugin) {
				//All fine here
			}
			else if (htype == HTYPE_GoodweAPI) {
				if (username.empty()) {
					return;
				}
			}
			else if (htype == HTYPE_RaspberryPCF8574) {
				//All fine here
			}
			else if (htype == HTYPE_OpenWebNetUSB) {
				//All fine here
			}
			else if (htype == HTYPE_IntergasInComfortLAN2RF) {
				//All fine here
			}
			else if (htype == HTYPE_EnphaseAPI) {
				//all fine here!
			}
			else
				return;

			std::string mode1Str;
			std::string mode2Str;
			std::string mode3Str;
			std::string mode4Str;
			std::string mode5Str;
			std::string mode6Str;

			root["status"] = "OK";
			root["title"] = "UpdateHardware";

			if (htype == HTYPE_Domoticz)
			{
				if (password.size() != 32)
				{
					password = GenerateMD5Hash(password);
				}
			}

			if ((bIsSerial) && (!bEnabled) && (sport.empty()))
			{
				//just disable the device
				m_sql.safe_query(
					"UPDATE Hardware SET Enabled=%d WHERE (ID == '%q')",
					(bEnabled == true) ? 1 : 0,
					idx.c_str()
				);
			}
			else
			{
				if (htype == HTYPE_HTTPPOLLER) {
					m_sql.safe_query(
						"UPDATE Hardware SET Name='%q', Enabled=%d, Type=%d, Address='%q', Port=%d, SerialPort='%q', Username='%q', Password='%q', Extra='%q', DataTimeout=%d WHERE (ID == '%q')",
						name.c_str(),
						(senabled == "true") ? 1 : 0,
						htype,
						address.c_str(),
						port,
						sport.c_str(),
						username.c_str(),
						password.c_str(),
						extra.c_str(),
						iDataTimeout,
						idx.c_str()
					);
				}
				else if (htype == HTYPE_PythonPlugin) {
					mode1Str = request::findValue(&req, "Mode1");
					mode2Str = request::findValue(&req, "Mode2");
					mode3Str = request::findValue(&req, "Mode3");
					mode4Str = request::findValue(&req, "Mode4");
					mode5Str = request::findValue(&req, "Mode5");
					mode6Str = request::findValue(&req, "Mode6");
					sport = request::findValue(&req, "serialport");
					m_sql.safe_query(
						"UPDATE Hardware SET Name='%q', Enabled=%d, Type=%d, Address='%q', Port=%d, SerialPort='%q', Username='%q', Password='%q', Extra='%q', Mode1='%q', Mode2='%q', Mode3='%q', Mode4='%q', Mode5='%q', Mode6='%q', DataTimeout=%d WHERE (ID == '%q')",
						name.c_str(),
						(senabled == "true") ? 1 : 0,
						htype,
						address.c_str(),
						port,
						sport.c_str(),
						username.c_str(),
						password.c_str(),
						extra.c_str(),
						mode1Str.c_str(), mode2Str.c_str(), mode3Str.c_str(), mode4Str.c_str(), mode5Str.c_str(), mode6Str.c_str(),
						iDataTimeout,
						idx.c_str()
					);
				}
				else {
					m_sql.safe_query(
						"UPDATE Hardware SET Name='%q', Enabled=%d, Type=%d, Address='%q', Port=%d, SerialPort='%q', Username='%q', Password='%q', Extra='%q', Mode1=%d, Mode2=%d, Mode3=%d, Mode4=%d, Mode5=%d, Mode6=%d, DataTimeout=%d WHERE (ID == '%q')",
						name.c_str(),
						(bEnabled == true) ? 1 : 0,
						htype,
						address.c_str(),
						port,
						sport.c_str(),
						username.c_str(),
						password.c_str(),
						extra.c_str(),
						mode1, mode2, mode3, mode4, mode5, mode6,
						iDataTimeout,
						idx.c_str()
					);
				}
			}

			//re-add the device in our system
			int ID = atoi(idx.c_str());
			m_mainworker.AddHardwareFromParams(ID, name, bEnabled, htype, address, port, sport, username, password, extra, mode1, mode2, mode3, mode4, mode5, mode6, iDataTimeout, true);
		}

		void CWebServer::Cmd_GetDeviceValueOptions(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::vector<std::string> result;
			result = CBasePush::DropdownOptions(atoi(idx.c_str()));
			if ((result.size() == 1) && result[0] == "Status") {
				root["result"][0]["Value"] = 0;
				root["result"][0]["Wording"] = result[0];
			}
			else {
				std::vector<std::string>::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::string ddOption = *itt;
					root["result"][ii]["Value"] = ii + 1;
					root["result"][ii]["Wording"] = ddOption.c_str();
					ii++;
				}

			}
			root["status"] = "OK";
			root["title"] = "GetDeviceValueOptions";
		}

		void CWebServer::Cmd_GetDeviceValueOptionWording(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			std::string pos = request::findValue(&req, "pos");
			if ((idx.empty()) || (pos.empty()))
				return;
			std::string wording;
			wording = CBasePush::DropdownOptionsValue(atoi(idx.c_str()), atoi(pos.c_str()));
			root["wording"] = wording;
			root["status"] = "OK";
			root["title"] = "GetDeviceValueOptions";
		}


		void CWebServer::Cmd_DeleteUserVariable(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			root["status"] = m_sql.DeleteUserVariable(idx);
			root["title"] = "DeleteUserVariable";
		}

		void CWebServer::Cmd_SaveUserVariable(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string variablename = request::findValue(&req, "vname");
			std::string variablevalue = request::findValue(&req, "vvalue");
			std::string variabletype = request::findValue(&req, "vtype");
			if (
				(variablename.empty()) ||
				(variabletype.empty()) ||
				((variablevalue.empty()) && (variabletype != "2"))
				)
				return;

			root["status"] = m_sql.SaveUserVariable(variablename, variabletype, variablevalue);
			root["title"] = "SaveUserVariable";
		}

		void CWebServer::Cmd_UpdateUserVariable(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			std::string variablename = request::findValue(&req, "vname");
			std::string variablevalue = request::findValue(&req, "vvalue");
			std::string variabletype = request::findValue(&req, "vtype");

			if (idx.empty())
			{
				//Get idx from valuename
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT ID FROM UserVariables WHERE Name='%q'",
					variablename.c_str());
				if (result.empty())
					return;
				idx = result[0][0];
			}

			if (
				(idx.empty()) ||
				(variablename.empty()) ||
				(variabletype.empty()) ||
				((variablevalue.empty()) && (variabletype != "2"))
				)
				return;

			root["status"] = m_sql.UpdateUserVariable(idx, variablename, variabletype, variablevalue, true);
			root["title"] = "UpdateUserVariable";
		}


		void CWebServer::Cmd_GetUserVariables(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::stringstream szQuery;
			std::vector<std::vector<std::string> > result;
			szQuery << "SELECT ID,Name,ValueType,Value,LastUpdate FROM UserVariables";
			result = m_sql.GetUserVariables();
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					root["result"][ii]["Type"] = sd[2];
					root["result"][ii]["Value"] = sd[3];
					root["result"][ii]["LastUpdate"] = sd[4];
					ii++;
				}
			}
			root["status"] = "OK";
			root["title"] = "GetUserVariables";
		}

		void CWebServer::Cmd_GetUserVariable(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			int iVarID = atoi(idx.c_str());

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID,Name,ValueType,Value,LastUpdate FROM UserVariables WHERE (ID==%d)",
				iVarID);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					root["result"][ii]["Type"] = sd[2];
					root["result"][ii]["Value"] = sd[3];
					root["result"][ii]["LastUpdate"] = sd[4];
					ii++;
				}
			}
			root["status"] = "OK";
			root["title"] = "GetUserVariable";
		}


		void CWebServer::Cmd_AllowNewHardware(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string sTimeout = request::findValue(&req, "timeout");
			if (sTimeout.empty())
				return;
			root["status"] = "OK";
			root["title"] = "AllowNewHardware";

			m_sql.AllowNewHardwareTimer(atoi(sTimeout.c_str()));
		}


		void CWebServer::Cmd_DeleteHardware(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			int hwID = atoi(idx.c_str());

			CDomoticzHardwareBase *pBaseHardware = m_mainworker.GetHardware(hwID);
			if ((pBaseHardware != NULL) && (pBaseHardware->HwdType == HTYPE_DomoticzInternal)) {
				// DomoticzInternal cannot be removed
				return;
			}

			root["status"] = "OK";
			root["title"] = "DeleteHardware";

			m_mainworker.RemoveDomoticzHardware(hwID);
			m_sql.DeleteHardware(idx);
		}

		void CWebServer::Cmd_GetLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetLog";

			time_t lastlogtime = 0;
			std::string slastlogtime = request::findValue(&req, "lastlogtime");
			if (slastlogtime != "")
			{
				std::stringstream s_str(slastlogtime);
				s_str >> lastlogtime;
			}

			_eLogLevel lLevel = LOG_NORM;
			std::string sloglevel = request::findValue(&req, "loglevel");
			if (!sloglevel.empty())
			{
				lLevel = (_eLogLevel)atoi(sloglevel.c_str());
			}

			std::list<CLogger::_tLogLineStruct> logmessages = _log.GetLog(lLevel);
			std::list<CLogger::_tLogLineStruct>::const_iterator itt;
			int ii = 0;
			for (itt = logmessages.begin(); itt != logmessages.end(); ++itt)
			{
				if (itt->logtime > lastlogtime)
				{
					std::stringstream szLogTime;
					szLogTime << itt->logtime;
					root["LastLogTime"] = szLogTime.str();
					root["result"][ii]["level"] = static_cast<int>(itt->level);
					root["result"][ii]["message"] = itt->logmessage;
					ii++;
				}
			}
		}

		void CWebServer::Cmd_ClearLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "ClearLog";
			_log.ClearLog();
		}

		//Plan Functions
		void CWebServer::Cmd_AddPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string name = request::findValue(&req, "name");
			root["status"] = "OK";
			root["title"] = "AddPlan";
			m_sql.safe_query(
				"INSERT INTO Plans (Name) VALUES ('%q')",
				name.c_str()
			);
		}

		void CWebServer::Cmd_UpdatePlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string name = request::findValue(&req, "name");
			if (
				(name.empty())
				)
				return;

			root["status"] = "OK";
			root["title"] = "UpdatePlan";

			m_sql.safe_query(
				"UPDATE Plans SET Name='%q' WHERE (ID == '%q')",
				name.c_str(),
				idx.c_str()
			);
		}

		void CWebServer::Cmd_DeletePlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "DeletePlan";
			m_sql.safe_query(
				"DELETE FROM DeviceToPlansMap WHERE (PlanID == '%q')",
				idx.c_str()
			);
			m_sql.safe_query(
				"DELETE FROM Plans WHERE (ID == '%q')",
				idx.c_str()
			);
		}

		void CWebServer::Cmd_GetUnusedPlanDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetUnusedPlanDevices";
			std::string sunique = request::findValue(&req, "unique");
			if (sunique.empty())
				return;
			int iUnique = (sunique == "true") ? 1 : 0;
			int ii = 0;

			std::vector<std::vector<std::string> > result;
			std::vector<std::vector<std::string> > result2;
			result = m_sql.safe_query("SELECT T1.[ID], T1.[Name], T1.[Type], T1.[SubType], T2.[Name] AS HardwareName FROM DeviceStatus as T1, Hardware as T2 WHERE (T1.[Used]==1) AND (T2.[ID]==T1.[HardwareID]) ORDER BY T2.[Name], T1.[Name]");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					bool bDoAdd = true;
					if (iUnique)
					{
						result2 = m_sql.safe_query("SELECT ID FROM DeviceToPlansMap WHERE (DeviceRowID=='%q') AND (DevSceneType==0)",
							sd[0].c_str());
						bDoAdd = (result2.size() == 0);
					}
					if (bDoAdd)
					{
						int _dtype = atoi(sd[2].c_str());
						std::string Name = "[" + sd[4] + "] " + sd[1] + " (" + RFX_Type_Desc(_dtype, 1) + "/" + RFX_Type_SubType_Desc(_dtype, atoi(sd[3].c_str())) + ")";
						root["result"][ii]["type"] = 0;
						root["result"][ii]["idx"] = sd[0];
						root["result"][ii]["Name"] = Name;
						ii++;
					}
				}
			}
			//Add Scenes
			result = m_sql.safe_query("SELECT ID, Name FROM Scenes ORDER BY Name");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					bool bDoAdd = true;
					if (iUnique)
					{
						result2 = m_sql.safe_query("SELECT ID FROM DeviceToPlansMap WHERE (DeviceRowID=='%q') AND (DevSceneType==1)",
							sd[0].c_str());
						bDoAdd = (result2.size() == 0);
					}
					if (bDoAdd)
					{
						root["result"][ii]["type"] = 1;
						root["result"][ii]["idx"] = sd[0];
						std::string sname = "[Scene] " + sd[1];
						root["result"][ii]["Name"] = sname;
						ii++;
					}
				}
			}
		}

		void CWebServer::Cmd_AddPlanActiveDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string sactivetype = request::findValue(&req, "activetype");
			std::string activeidx = request::findValue(&req, "activeidx");
			if (
				(idx.empty()) ||
				(sactivetype.empty()) ||
				(activeidx.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "AddPlanActiveDevice";

			int activetype = atoi(sactivetype.c_str());

			//check if it is not already there
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID FROM DeviceToPlansMap WHERE (DeviceRowID=='%q') AND (DevSceneType==%d) AND (PlanID=='%q')",
				activeidx.c_str(), activetype, idx.c_str());
			if (result.size() == 0)
			{
				m_sql.safe_query(
					"INSERT INTO DeviceToPlansMap (DevSceneType,DeviceRowID, PlanID) VALUES (%d,'%q','%q')",
					activetype,
					activeidx.c_str(),
					idx.c_str()
				);
			}
		}

		void CWebServer::Cmd_GetPlanDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "GetPlanDevices";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, DevSceneType, DeviceRowID, [Order] FROM DeviceToPlansMap WHERE (PlanID=='%q') ORDER BY [Order]",
				idx.c_str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					std::string ID = sd[0];
					int DevSceneType = atoi(sd[1].c_str());
					std::string DevSceneRowID = sd[2];

					std::string Name = "";
					if (DevSceneType == 0)
					{
						std::vector<std::vector<std::string> > result2;
						result2 = m_sql.safe_query("SELECT Name FROM DeviceStatus WHERE (ID=='%q')",
							DevSceneRowID.c_str());
						if (result2.size() > 0)
						{
							Name = result2[0][0];
						}
					}
					else
					{
						std::vector<std::vector<std::string> > result2;
						result2 = m_sql.safe_query("SELECT Name FROM Scenes WHERE (ID=='%q')",
							DevSceneRowID.c_str());
						if (result2.size() > 0)
						{
							Name = "[Scene] " + result2[0][0];
						}
					}
					if (Name != "")
					{
						root["result"][ii]["idx"] = ID;
						root["result"][ii]["devidx"] = DevSceneRowID;
						root["result"][ii]["type"] = DevSceneType;
						root["result"][ii]["DevSceneRowID"] = DevSceneRowID;
						root["result"][ii]["order"] = sd[3];
						root["result"][ii]["Name"] = Name;
						ii++;
					}
				}
			}
		}

		void CWebServer::Cmd_DeletePlanDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "DeletePlanDevice";
			m_sql.safe_query("DELETE FROM DeviceToPlansMap WHERE (ID == '%q')", idx.c_str());
		}

		void CWebServer::Cmd_SetPlanDeviceCoords(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			std::string planidx = request::findValue(&req, "planidx");
			std::string xoffset = request::findValue(&req, "xoffset");
			std::string yoffset = request::findValue(&req, "yoffset");
			std::string type = request::findValue(&req, "DevSceneType");
			if ((idx.empty()) || (planidx.empty()) || (xoffset.empty()) || (yoffset.empty()))
				return;
			if (type != "1") type = "0";  // 0 = Device, 1 = Scene/Group
			root["status"] = "OK";
			root["title"] = "SetPlanDeviceCoords";
			m_sql.safe_query("UPDATE DeviceToPlansMap SET [XOffset] = '%q', [YOffset] = '%q' WHERE (DeviceRowID='%q') and (PlanID='%q') and (DevSceneType='%q')",
				xoffset.c_str(), yoffset.c_str(), idx.c_str(), planidx.c_str(), type.c_str());
			_log.Log(LOG_STATUS, "(Floorplan) Device '%s' coordinates set to '%s,%s' in plan '%s'.", idx.c_str(), xoffset.c_str(), yoffset.c_str(), planidx.c_str());
		}

		void CWebServer::Cmd_DeleteAllPlanDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "DeleteAllPlanDevices";
			m_sql.safe_query("DELETE FROM DeviceToPlansMap WHERE (PlanID == '%q')", idx.c_str());
		}

		void CWebServer::Cmd_ChangePlanOrder(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string sway = request::findValue(&req, "way");
			if (sway.empty())
				return;
			bool bGoUp = (sway == "0");

			std::string aOrder, oID, oOrder;

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT [Order] FROM Plans WHERE (ID=='%q')",
				idx.c_str());
			if (result.size() < 1)
				return;
			aOrder = result[0][0];

			if (!bGoUp)
			{
				//Get next device order
				result = m_sql.safe_query("SELECT ID, [Order] FROM Plans WHERE ([Order]>'%q') ORDER BY [Order] ASC",
					aOrder.c_str());
				if (result.size() < 1)
					return;
				oID = result[0][0];
				oOrder = result[0][1];
			}
			else
			{
				//Get previous device order
				result = m_sql.safe_query("SELECT ID, [Order] FROM Plans WHERE ([Order]<'%q') ORDER BY [Order] DESC",
					aOrder.c_str());
				if (result.size() < 1)
					return;
				oID = result[0][0];
				oOrder = result[0][1];
			}
			//Swap them
			root["status"] = "OK";
			root["title"] = "ChangePlanOrder";

			m_sql.safe_query("UPDATE Plans SET [Order] = '%q' WHERE (ID='%q')",
				oOrder.c_str(), idx.c_str());
			m_sql.safe_query("UPDATE Plans SET [Order] = '%q' WHERE (ID='%q')",
				aOrder.c_str(), oID.c_str());
		}

		void CWebServer::Cmd_ChangePlanDeviceOrder(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string planid = request::findValue(&req, "planid");
			std::string idx = request::findValue(&req, "idx");
			std::string sway = request::findValue(&req, "way");
			if (
				(planid.empty()) ||
				(idx.empty()) ||
				(sway.empty())
				)
				return;
			bool bGoUp = (sway == "0");

			std::string aOrder, oID, oOrder;

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT [Order] FROM DeviceToPlansMap WHERE ((ID=='%q') AND (PlanID=='%q'))",
				idx.c_str(), planid.c_str());
			if (result.size() < 1)
				return;
			aOrder = result[0][0];

			if (!bGoUp)
			{
				//Get next device order
				result = m_sql.safe_query("SELECT ID, [Order] FROM DeviceToPlansMap WHERE (([Order]>'%q') AND (PlanID=='%q')) ORDER BY [Order] ASC",
					aOrder.c_str(), planid.c_str());
				if (result.size() < 1)
					return;
				oID = result[0][0];
				oOrder = result[0][1];
			}
			else
			{
				//Get previous device order
				result = m_sql.safe_query("SELECT ID, [Order] FROM DeviceToPlansMap WHERE (([Order]<'%q') AND (PlanID=='%q')) ORDER BY [Order] DESC",
					aOrder.c_str(), planid.c_str());
				if (result.size() < 1)
					return;
				oID = result[0][0];
				oOrder = result[0][1];
			}
			//Swap them
			root["status"] = "OK";
			root["title"] = "ChangePlanOrder";

			m_sql.safe_query("UPDATE DeviceToPlansMap SET [Order] = '%q' WHERE (ID='%q')",
				oOrder.c_str(), idx.c_str());
			m_sql.safe_query("UPDATE DeviceToPlansMap SET [Order] = '%q' WHERE (ID='%q')",
				aOrder.c_str(), oID.c_str());
		}

		void CWebServer::Cmd_GetTimerPlans(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			root["status"] = "OK";
			root["title"] = "GetTimerPlans";
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Name FROM TimerPlans ORDER BY Name COLLATE NOCASE ASC");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					ii++;
				}
			}
		}

		void CWebServer::Cmd_AddTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string name = request::findValue(&req, "name");
			root["status"] = "OK";
			root["title"] = "AddTimerPlan";
			m_sql.safe_query("INSERT INTO TimerPlans (Name) VALUES ('%q')", name.c_str());
		}

		void CWebServer::Cmd_UpdateTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string name = request::findValue(&req, "name");
			if (
				(name.empty())
				)
				return;

			root["status"] = "OK";
			root["title"] = "UpdateTimerPlan";

			m_sql.safe_query("UPDATE TimerPlans SET Name='%q' WHERE (ID == '%q')", name.c_str(), idx.c_str());
		}

		void CWebServer::Cmd_DeleteTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			int iPlan = atoi(idx.c_str());
			if (iPlan < 1)
				return;

			root["status"] = "OK";
			root["title"] = "DeletePlan";
			m_sql.safe_query("DELETE FROM Timers WHERE (TimerPlan == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM TimerPlans WHERE (ID == '%q')", idx.c_str());

			if (m_sql.m_ActiveTimerPlan == iPlan)
			{
				//Set active timer plan to default
				m_sql.UpdatePreferencesVar("ActiveTimerPlan", 0);
				m_sql.m_ActiveTimerPlan = 0;
				m_mainworker.m_scheduler.ReloadSchedules();
			}
		}

		void CWebServer::Cmd_GetVersion(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetVersion";
			root["version"] = szAppVersion;
			root["hash"] = szAppHash;
			root["build_time"] = szAppDate;
			CdzVents* dzvents = CdzVents::GetInstance();
			root["dzvents_version"] = dzvents->GetVersion();
			root["python_version"] = szPyVersion;

			if (session.rights != 2)
			{
				//only admin users will receive the update notification
				root["HaveUpdate"] = false;
			}
			else
			{
				root["HaveUpdate"] = m_mainworker.IsUpdateAvailable(false);
				root["DomoticzUpdateURL"] = m_mainworker.m_szDomoticzUpdateURL;
				root["SystemName"] = m_mainworker.m_szSystemName;
				root["Revision"] = m_mainworker.m_iRevision;
			}
		}

		void CWebServer::Cmd_GetAuth(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetAuth";
			if (session.rights != -1)
			{
				root["version"] = szAppVersion;
			}
			root["user"] = session.username;
			root["rights"] = session.rights;
		}

		void CWebServer::Cmd_GetUptime(WebEmSession & session, const request& req, Json::Value &root)
		{
			//this is used in the about page, we are going to round the seconds a bit to display nicer
			time_t atime = mytime(NULL);
			time_t tuptime = atime - m_StartTime;
			//round to 5 seconds (nicer in about page)
			tuptime = ((tuptime / 5) * 5) + 5;
			int days, hours, minutes, seconds;
			days = (int)(tuptime / 86400);
			tuptime -= (days * 86400);
			hours = (int)(tuptime / 3600);
			tuptime -= (hours * 3600);
			minutes = (int)(tuptime / 60);
			tuptime -= (minutes * 60);
			seconds = (int)tuptime;
			root["status"] = "OK";
			root["title"] = "GetUptime";
			root["days"] = days;
			root["hours"] = hours;
			root["minutes"] = minutes;
			root["seconds"] = seconds;
		}

		void CWebServer::Cmd_GetActualHistory(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetActualHistory";

			std::string historyfile = szUserDataFolder + "History.txt";

			std::ifstream infile;
			int ii = 0;
			infile.open(historyfile.c_str());
			std::string sLine;
			if (infile.is_open())
			{
				while (!infile.eof())
				{
					getline(infile, sLine);
					root["LastLogTime"] = "";
					if (sLine.find("Version ") == 0)
						root["result"][ii]["level"] = 1;
					else
						root["result"][ii]["level"] = 0;
					root["result"][ii]["message"] = sLine;
					ii++;
				}
			}
		}

		void CWebServer::Cmd_GetNewHistory(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetNewHistory";

			std::string historyfile;
			int nValue;
			m_sql.GetPreferencesVar("ReleaseChannel", nValue);
			bool bIsBetaChannel = (nValue != 0);

			std::string szHistoryURL = "http://www.domoticz.com/download.php?channel=stable&type=history";
			if (bIsBetaChannel)
			{
				utsname my_uname;
				if (uname(&my_uname) < 0)
					return;

				std::string systemname = my_uname.sysname;
				std::string machine = my_uname.machine;
				std::transform(systemname.begin(), systemname.end(), systemname.begin(), ::tolower);

				if (machine == "armv6l")
				{
					//Seems like old arm systems can also use the new arm build
					machine = "armv7l";
				}

				if (((machine != "armv6l") && (machine != "armv7l") && (systemname != "windows") && (machine != "x86_64") && (machine != "aarch64")) || (strstr(my_uname.release, "ARCH+") != NULL))
					szHistoryURL = "http://www.domoticz.com/download.php?channel=beta&type=history";
				else
					szHistoryURL = "http://www.domoticz.com/download.php?channel=beta&type=history&system=" + systemname + "&machine=" + machine;
			}
			if (!HTTPClient::GET(szHistoryURL, historyfile))
			{
				historyfile = "Unable to get Online History document !!";
			}

			std::istringstream stream(historyfile);
			std::string sLine;
			int ii = 0;
			while (std::getline(stream, sLine))
			{
				root["LastLogTime"] = "";
				if (sLine.find("Version ") == 0)
					root["result"][ii]["level"] = 1;
				else
					root["result"][ii]["level"] = 0;
				root["result"][ii]["message"] = sLine;
				ii++;
			}
		}

		void CWebServer::Cmd_GetConfig(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights == -1)
			{
				session.reply_status = reply::forbidden;
				return;//Only auth user allowed
			}

			root["status"] = "OK";
			root["title"] = "GetConfig";

			bool bHaveUser = (session.username != "");
			int urights = 3;
			unsigned long UserID = 0;
			if (bHaveUser)
			{
				int iUser = FindUser(session.username.c_str());
				if (iUser != -1)
				{
					urights = static_cast<int>(m_users[iUser].userrights);
					UserID = m_users[iUser].ID;
				}

			}

			int nValue;
			std::string sValue;

			if (m_sql.GetPreferencesVar("Language", sValue))
			{
				root["language"] = sValue;
			}
			if (m_sql.GetPreferencesVar("DegreeDaysBaseTemperature", sValue))
			{
				root["DegreeDaysBaseTemperature"] = atof(sValue.c_str());
			}

			nValue = 0;
			int iDashboardType = 0;
			m_sql.GetPreferencesVar("DashboardType", iDashboardType);
			root["DashboardType"] = iDashboardType;
			m_sql.GetPreferencesVar("MobileType", nValue);
			root["MobileType"] = nValue;

			nValue = 1;
			m_sql.GetPreferencesVar("5MinuteHistoryDays", nValue);
			root["FiveMinuteHistoryDays"] = nValue;

			nValue = 1;
			m_sql.GetPreferencesVar("ShowUpdateEffect", nValue);
			root["result"]["ShowUpdatedEffect"] = (nValue == 1);

			root["AllowWidgetOrdering"] = m_sql.m_bAllowWidgetOrdering;

			root["WindScale"] = m_sql.m_windscale*10.0f;
			root["WindSign"] = m_sql.m_windsign;
			root["TempScale"] = m_sql.m_tempscale;
			root["TempSign"] = m_sql.m_tempsign;

			std::string Latitude = "1";
			std::string Longitude = "1";
			if (m_sql.GetPreferencesVar("Location", nValue, sValue))
			{
				std::vector<std::string> strarray;
				StringSplit(sValue, ";", strarray);

				if (strarray.size() == 2)
				{
					Latitude = strarray[0];
					Longitude = strarray[1];
				}
			}
			root["Latitude"] = Latitude;
			root["Longitude"] = Longitude;

#ifndef NOCLOUD
			bool bEnableTabProxy = request::get_req_header(&req, "X-From-MyDomoticz") != NULL;
#else
			bool bEnableTabProxy = false;
#endif
			int bEnableTabDashboard = 1;
			int bEnableTabFloorplans = 1;
			int bEnableTabLight = 1;
			int bEnableTabScenes = 1;
			int bEnableTabTemp = 1;
			int bEnableTabWeather = 1;
			int bEnableTabUtility = 1;
			int bEnableTabCustom = 1;

			std::vector<std::vector<std::string> > result;

			if ((UserID != 0) && (UserID != 10000))
			{
				result = m_sql.safe_query("SELECT TabsEnabled FROM Users WHERE (ID==%lu)",
					UserID);
				if (result.size() > 0)
				{
					int TabsEnabled = atoi(result[0][0].c_str());
					bEnableTabLight = (TabsEnabled&(1 << 0));
					bEnableTabScenes = (TabsEnabled&(1 << 1));
					bEnableTabTemp = (TabsEnabled&(1 << 2));
					bEnableTabWeather = (TabsEnabled&(1 << 3));
					bEnableTabUtility = (TabsEnabled&(1 << 4));
					bEnableTabCustom = (TabsEnabled&(1 << 5));
					bEnableTabFloorplans = (TabsEnabled&(1 << 6));
				}
			}
			else
			{
				m_sql.GetPreferencesVar("EnableTabFloorplans", bEnableTabFloorplans);
				m_sql.GetPreferencesVar("EnableTabLights", bEnableTabLight);
				m_sql.GetPreferencesVar("EnableTabScenes", bEnableTabScenes);
				m_sql.GetPreferencesVar("EnableTabTemp", bEnableTabTemp);
				m_sql.GetPreferencesVar("EnableTabWeather", bEnableTabWeather);
				m_sql.GetPreferencesVar("EnableTabUtility", bEnableTabUtility);
				m_sql.GetPreferencesVar("EnableTabCustom", bEnableTabCustom);
			}
			if (iDashboardType == 3)
			{
				//Floorplan , no need to show a tab floorplan
				bEnableTabFloorplans = 0;
			}
			root["result"]["EnableTabProxy"] = bEnableTabProxy;
			root["result"]["EnableTabDashboard"] = bEnableTabDashboard != 0;
			root["result"]["EnableTabFloorplans"] = bEnableTabFloorplans != 0;
			root["result"]["EnableTabLights"] = bEnableTabLight != 0;
			root["result"]["EnableTabScenes"] = bEnableTabScenes != 0;
			root["result"]["EnableTabTemp"] = bEnableTabTemp != 0;
			root["result"]["EnableTabWeather"] = bEnableTabWeather != 0;
			root["result"]["EnableTabUtility"] = bEnableTabUtility != 0;
			root["result"]["EnableTabCustom"] = bEnableTabCustom != 0;

			if (bEnableTabCustom)
			{
				//Add custom templates
				DIR *lDir;
				struct dirent *ent;
				std::string templatesFolder = szWWWFolder + "/templates";
				int iFile = 0;
				if ((lDir = opendir(templatesFolder.c_str())) != NULL)
				{
					while ((ent = readdir(lDir)) != NULL)
					{
						std::string filename = ent->d_name;
						size_t pos = filename.find(".htm");
						if (pos != std::string::npos)
						{
							std::string shortfile = filename.substr(0, pos);
							root["result"]["templates"][iFile++] = shortfile;
						}
					}
					closedir(lDir);
				}
			}
		}

		void CWebServer::Cmd_SendNotification(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string subject = request::findValue(&req, "subject");
			std::string body = request::findValue(&req, "body");
			std::string subsystem = request::findValue(&req, "subsystem");
			if (
				(subject.empty()) ||
				(body.empty())
				)
				return;
			if (subsystem.empty()) subsystem = NOTIFYALL;
			//Add to queue
			if (m_notifications.SendMessage(0, std::string(""), subsystem, subject, body, std::string(""), 1, std::string(""), false)) {
				root["status"] = "OK";
			}
			root["title"] = "SendNotification";
		}

		void CWebServer::Cmd_EmailCameraSnapshot(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string camidx = request::findValue(&req, "camidx");
			std::string subject = request::findValue(&req, "subject");
			if (
				(camidx.empty()) ||
				(subject.empty())
				)
				return;
			//Add to queue
			m_sql.AddTaskItem(_tTaskItem::EmailCameraSnapshot(1, camidx, subject));
			root["status"] = "OK";
			root["title"] = "Email Camera Snapshot";
		}

		void CWebServer::Cmd_UpdateDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights < 1)
			{
				session.reply_status = reply::forbidden;
				return; //only user or higher allowed
			}

			std::string idx = request::findValue(&req, "idx");

			if (!IsIdxForUser(&session, atoi(idx.c_str())))
			{
				_log.Log(LOG_ERROR, "User: %s tried to update an Unauthorized device!", session.username.c_str());
				session.reply_status = reply::forbidden;
				return;
			}

			std::string hid = request::findValue(&req, "hid");
			std::string did = request::findValue(&req, "did");
			std::string dunit = request::findValue(&req, "dunit");
			std::string dtype = request::findValue(&req, "dtype");
			std::string dsubtype = request::findValue(&req, "dsubtype");

			std::string nvalue = request::findValue(&req, "nvalue");
			std::string svalue = request::findValue(&req, "svalue");

			if ((nvalue.empty() && svalue.empty()))
			{
				return;
			}

			int signallevel = 12;
			int batterylevel = 255;

			if (idx.empty())
			{
				//No index supplied, check if raw parameters where supplied
				if (
					(hid.empty()) ||
					(did.empty()) ||
					(dunit.empty()) ||
					(dtype.empty()) ||
					(dsubtype.empty())
					)
					return;
			}
			else
			{
				//Get the raw device parameters
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT HardwareID, DeviceID, Unit, Type, SubType FROM DeviceStatus WHERE (ID=='%q')",
					idx.c_str());
				if (result.empty())
					return;
				hid = result[0][0];
				did = result[0][1];
				dunit = result[0][2];
				dtype = result[0][3];
				dsubtype = result[0][4];
			}

			int HardwareID = atoi(hid.c_str());
			std::string DeviceID = did;
			int unit = atoi(dunit.c_str());
			int devType = atoi(dtype.c_str());
			int subType = atoi(dsubtype.c_str());

			std::stringstream sstr;

			uint64_t ulIdx;
			sstr << idx;
			sstr >> ulIdx;

			int invalue = atoi(nvalue.c_str());

			std::string sSignalLevel = request::findValue(&req, "rssi");
			if (sSignalLevel != "")
			{
				signallevel = atoi(sSignalLevel.c_str());
			}
			std::string sBatteryLevel = request::findValue(&req, "battery");
			if (sBatteryLevel != "")
			{
				batterylevel = atoi(sBatteryLevel.c_str());
			}
			if (m_mainworker.UpdateDevice(HardwareID, DeviceID, unit, devType, subType, invalue, svalue, signallevel, batterylevel))
			{
				root["status"] = "OK";
				root["title"] = "Update Device";
			}
		}

		void CWebServer::Cmd_UpdateDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string script = request::findValue(&req, "script");
			if (script.empty())
			{
				return;
			}
			std::string content = req.content;

			std::vector<std::string> allParameters;

			// Keep the url content on the right of the '?'
			std::vector<std::string> allParts;
			StringSplit(req.uri, "?", allParts);
			if (!allParts.empty())
			{
				// Split all url parts separated by a '&'
				StringSplit(allParts[1], "&", allParameters);
			}

			CLuaHandler luaScript;
			bool ret = luaScript.executeLuaScript(script, content, allParameters);
			if (ret)
			{
				root["status"] = "OK";
				root["title"] = "Update Device";
			}
		}

		void CWebServer::Cmd_SetThermostatState(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sstate = request::findValue(&req, "state");
			std::string idx = request::findValue(&req, "idx");
			std::string name = request::findValue(&req, "name");
			_log.Log(LOG_TRACE, "WEBS SetThermostatState  State cmd Id:%s Name:%s State:%s", idx.c_str(), name.c_str(), sstate.c_str());

			if (
				(idx.empty()) ||
				(sstate.empty())
				)
				return;
			int iState = atoi(sstate.c_str());

			int urights = 3;
			bool bHaveUser = (session.username != "");
			if (bHaveUser)
			{
				int iUser = FindUser(session.username.c_str());
				if (iUser != -1)
				{
					urights = static_cast<int>(m_users[iUser].userrights);
					_log.Log(LOG_STATUS, "User: %s initiated a Thermostat State change command", m_users[iUser].Username.c_str());
				}
			}
			if (urights < 1)
				return;

			root["status"] = "OK";
			root["title"] = "Set Thermostat State";
			_log.Log(LOG_NORM, "Setting Thermostat State....");
			m_mainworker.SetThermostatState(idx, iState);
		}

		void CWebServer::Cmd_SystemShutdown(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
#ifdef WIN32
			int ret = system("shutdown -s -f -t 1 -d up:125:1");
#else
			int ret = system("sudo shutdown -h now");
#endif
			if (ret != 0)
			{
				_log.Log(LOG_ERROR, "Error executing shutdown command. returned: %d", ret);
				return;
			}
			root["title"] = "SystemShutdown";
			root["status"] = "OK";
		}

		void CWebServer::Cmd_SystemReboot(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
#ifdef WIN32
			int ret = system("shutdown -r -f -t 1 -d up:125:1");
#else
			int ret = system("sudo shutdown -r now");
#endif
			if (ret != 0)
			{
				_log.Log(LOG_ERROR, "Error executing reboot command. returned: %d", ret);
				return;
			}
			root["title"] = "SystemReboot";
			root["status"] = "OK";
		}

		void CWebServer::Cmd_ExcecuteScript(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string scriptname = request::findValue(&req, "scriptname");
			if (scriptname.empty())
				return;
			if (scriptname.find("..") != std::string::npos)
				return;
#ifdef WIN32
			scriptname = szUserDataFolder + "scripts\\" + scriptname;
#else
			scriptname = szUserDataFolder + "scripts/" + scriptname;
#endif
			if (!file_exist(scriptname.c_str()))
				return;
			std::string script_params = request::findValue(&req, "scriptparams");
			std::string strparm = szUserDataFolder;
			if (!script_params.empty())
			{
				if (strparm.size() > 0)
					strparm += " " + script_params;
				else
					strparm = script_params;
			}
			std::string sdirect = request::findValue(&req, "direct");
			if (sdirect == "true")
			{
				_log.Log(LOG_STATUS, "Executing script: %s", scriptname.c_str());
#ifdef WIN32
				ShellExecute(NULL, "open", scriptname.c_str(), strparm.c_str(), NULL, SW_SHOWNORMAL);
#else
				std::string lscript = scriptname + " " + strparm;
				int ret = system(lscript.c_str());
				if (ret != 0)
				{
					_log.Log(LOG_ERROR, "Error executing script command (%s). returned: %d", lscript.c_str(), ret);
					return;
			}
#endif
		}
			else
			{
				//add script to background worker
				m_sql.AddTaskItem(_tTaskItem::ExecuteScript(0.2f, scriptname, strparm));
			}
			root["title"] = "ExecuteScript";
			root["status"] = "OK";
		}

		void CWebServer::Cmd_GetCosts(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			char szTmp[100];
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Type, SubType, nValue, sValue FROM DeviceStatus WHERE (ID=='%q')",
				idx.c_str());
			if (result.size() > 0)
			{
				std::vector<std::string> sd = result[0];

				int nValue = 0;
				root["status"] = "OK";
				root["title"] = "GetElectraCosts";
				m_sql.GetPreferencesVar("CostEnergy", nValue);
				root["CostEnergy"] = nValue;
				m_sql.GetPreferencesVar("CostEnergyT2", nValue);
				root["CostEnergyT2"] = nValue;
				m_sql.GetPreferencesVar("CostEnergyR1", nValue);
				root["CostEnergyR1"] = nValue;
				m_sql.GetPreferencesVar("CostEnergyR2", nValue);
				root["CostEnergyR2"] = nValue;
				m_sql.GetPreferencesVar("CostGas", nValue);
				root["CostGas"] = nValue;
				m_sql.GetPreferencesVar("CostWater", nValue);
				root["CostWater"] = nValue;

				int tValue = 1000;
				if (m_sql.GetPreferencesVar("MeterDividerWater", tValue))
				{
					root["DividerWater"] = float(tValue);
				}

				unsigned char dType = atoi(sd[0].c_str());
				//unsigned char subType = atoi(sd[1].c_str());
				//nValue = (unsigned char)atoi(sd[2].c_str());
				std::string sValue = sd[3];

				if (dType == pTypeP1Power)
				{
					//also provide the counter values

					std::vector<std::string> splitresults;
					StringSplit(sValue, ";", splitresults);
					if (splitresults.size() != 6)
						return;

					float EnergyDivider = 1000.0f;
					if (m_sql.GetPreferencesVar("MeterDividerEnergy", tValue))
					{
						EnergyDivider = float(tValue);
					}

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

					powerdeliv1 = (powerdeliv1 < 10) ? 0 : powerdeliv1;
					powerdeliv2 = (powerdeliv2 < 10) ? 0 : powerdeliv2;

					sprintf(szTmp, "%.03f", float(powerusage1) / EnergyDivider);
					root["CounterT1"] = szTmp;
					sprintf(szTmp, "%.03f", float(powerusage2) / EnergyDivider);
					root["CounterT2"] = szTmp;
					sprintf(szTmp, "%.03f", float(powerdeliv1) / EnergyDivider);
					root["CounterR1"] = szTmp;
					sprintf(szTmp, "%.03f", float(powerdeliv2) / EnergyDivider);
					root["CounterR2"] = szTmp;
				}
			}
		}

		void CWebServer::Cmd_CheckForUpdate(WebEmSession & session, const request& req, Json::Value &root)
		{
			bool bHaveUser = (session.username != "");
			int urights = 3;
			if (bHaveUser)
			{
				int iUser = FindUser(session.username.c_str());
				if (iUser != -1)
					urights = static_cast<int>(m_users[iUser].userrights);
			}
			root["statuscode"] = urights;

			root["status"] = "OK";
			root["title"] = "CheckForUpdate";
			root["HaveUpdate"] = false;
			root["Revision"] = m_mainworker.m_iRevision;

			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin users may update
			}

			bool bIsForced = (request::findValue(&req, "forced") == "true");

			if (!bIsForced)
			{
				int nValue = 0;
				m_sql.GetPreferencesVar("UseAutoUpdate", nValue);
				if (nValue != 1)
				{
					return;
				}
			}

			root["HaveUpdate"] = m_mainworker.IsUpdateAvailable(bIsForced);
			root["DomoticzUpdateURL"] = m_mainworker.m_szDomoticzUpdateURL;
			root["SystemName"] = m_mainworker.m_szSystemName;
			root["Revision"] = m_mainworker.m_iRevision;
		}

		void CWebServer::Cmd_DownloadUpdate(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (!m_mainworker.StartDownloadUpdate())
				return;
			root["status"] = "OK";
			root["title"] = "DownloadUpdate";
		}

		void CWebServer::Cmd_DownloadReady(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (!m_mainworker.m_bHaveDownloadedDomoticzUpdate)
				return;
			root["status"] = "OK";
			root["title"] = "DownloadReady";
			root["downloadok"] = (m_mainworker.m_bHaveDownloadedDomoticzUpdateSuccessFull) ? true : false;
		}

		void CWebServer::Cmd_DeleteDatePoint(WebEmSession & session, const request& req, Json::Value &root)
		{
			const std::string idx = request::findValue(&req, "idx");
			const std::string Date = request::findValue(&req, "date");
			if (
				(idx.empty()) ||
				(Date.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "deletedatapoint";
			m_sql.DeleteDataPoint(idx.c_str(), Date);
		}

		bool CWebServer::IsIdxForUser(const WebEmSession *pSession, const int Idx)
		{
			if (pSession->rights == 2)
				return true;
			if (pSession->rights == 0)
				return false; //viewer
			//User
			int iUser = FindUser(pSession->username.c_str());
			if ((iUser < 0) || (iUser >= (int)m_users.size()))
				return false;

			if (m_users[iUser].TotSensors == 0)
				return true; // all sensors

			std::vector<std::vector<std::string> > result = m_sql.safe_query("SELECT DeviceRowID FROM SharedDevices WHERE (SharedUserID == '%d') AND (DeviceRowID == '%d')", m_users[iUser].ID, Idx);
			return (!result.empty());
		}




		void CWebServer::DisplaySwitchTypesCombo(std::string & content_part)
		{
			char szTmp[200];

			std::map<std::string, int> _switchtypes;

			for (int ii = 0; ii < STYPE_END; ii++)
			{
				_switchtypes[Switch_Type_Desc((_eSwitchType)ii)] = ii;
			}
			//return a sorted list
			std::map<std::string, int>::const_iterator itt;
			for (itt = _switchtypes.begin(); itt != _switchtypes.end(); ++itt)
			{
				sprintf(szTmp, "<option value=\"%d\">%s</option>\n", itt->second, itt->first.c_str());
				content_part += szTmp;
			}
		}

		void CWebServer::DisplayMeterTypesCombo(std::string & content_part)
		{
			char szTmp[200];
			for (int ii = 0; ii < MTYPE_END; ii++)
			{
				sprintf(szTmp, "<option value=\"%d\">%s</option>\n", ii, Meter_Type_Desc((_eMeterType)ii));
				content_part += szTmp;
			}
		}

		void CWebServer::DisplayLanguageCombo(std::string & content_part)
		{
			//return a sorted list
			std::map<std::string, std::string> _ltypes;
			std::map<std::string, std::string>::const_iterator itt;
			char szTmp[200];
			int ii = 0;
			while (guiLanguage[ii].szShort != NULL)
			{
				_ltypes[guiLanguage[ii].szLong] = guiLanguage[ii].szShort;
				ii++;
			}
			for (itt = _ltypes.begin(); itt != _ltypes.end(); ++itt)
			{
				sprintf(szTmp, "<option value=\"%s\">%s</option>\n", itt->second.c_str(), itt->first.c_str());
				content_part += szTmp;
			}
		}

		void CWebServer::DisplayTimerTypesCombo(std::string & content_part)
		{
			char szTmp[200];
			for (int ii = 0; ii < TTYPE_END; ii++)
			{
				sprintf(szTmp, "<option data-i18n=\"%s\" value=\"%d\">%s</option>\n", Timer_Type_Desc(ii), ii, Timer_Type_Desc(ii));
				content_part += szTmp;
			}
		}

		void CWebServer::LoadUsers()
		{
			ClearUserPasswords();
			std::string WebUserName, WebPassword;
			int nValue = 0;
			if (m_sql.GetPreferencesVar("WebUserName", nValue, WebUserName))
			{
				if (m_sql.GetPreferencesVar("WebPassword", nValue, WebPassword))
				{
					if ((WebUserName != "") && (WebPassword != ""))
					{
						WebUserName = base64_decode(WebUserName);
						//WebPassword = WebPassword;
						AddUser(10000, WebUserName, WebPassword, URIGHTS_ADMIN, 0xFFFF);

						std::vector<std::vector<std::string> > result;
						result = m_sql.safe_query("SELECT ID, Active, Username, Password, Rights, TabsEnabled FROM Users");
						if (result.size() > 0)
						{
							std::vector<std::vector<std::string> >::const_iterator itt;
							int ii = 0;
							for (itt = result.begin(); itt != result.end(); ++itt)
							{
								std::vector<std::string> sd = *itt;

								int bIsActive = static_cast<int>(atoi(sd[1].c_str()));
								if (bIsActive)
								{
									unsigned long ID = (unsigned long)atol(sd[0].c_str());

									std::string username = base64_decode(sd[2]);
									std::string password = sd[3];

									_eUserRights rights = (_eUserRights)atoi(sd[4].c_str());
									int activetabs = atoi(sd[5].c_str());

									AddUser(ID, username, password, rights, activetabs);
								}
							}
						}
					}
				}
			}
			m_mainworker.LoadSharedUsers();
		}

		void CWebServer::AddUser(const unsigned long ID, const std::string &username, const std::string &password, const int userrights, const int activetabs)
		{
			std::vector<std::vector<std::string> > result = m_sql.safe_query("SELECT COUNT(*) FROM SharedDevices WHERE (SharedUserID == '%d')", ID);
			if (result.empty())
				return;

			_tWebUserPassword wtmp;
			wtmp.ID = ID;
			wtmp.Username = username;
			wtmp.Password = password;
			wtmp.userrights = (_eUserRights)userrights;
			wtmp.ActiveTabs = activetabs;
			wtmp.TotSensors = atoi(result[0][0].c_str());
			m_users.push_back(wtmp);

			m_pWebEm->AddUserPassword(ID, username, password, (_eUserRights)userrights, activetabs);
		}

		void CWebServer::ClearUserPasswords()
		{
			m_users.clear();
			m_pWebEm->ClearUserPasswords();
		}

		int CWebServer::FindUser(const char* szUserName)
		{
			int iUser = 0;
			std::vector<_tWebUserPassword>::const_iterator itt;
			for (itt = m_users.begin(); itt != m_users.end(); ++itt)
			{
				if (itt->Username == szUserName)
					return iUser;
				iUser++;
			}
			return -1;
		}

		bool CWebServer::FindAdminUser()
		{
			std::vector<_tWebUserPassword>::const_iterator itt;
			for (itt = m_users.begin(); itt != m_users.end(); ++itt)
			{
				if (itt->userrights == URIGHTS_ADMIN)
					return true;
			}
			return false;
		}

		void CWebServer::PostSettings(WebEmSession & session, const request& req, std::string & redirect_uri)
		{
			redirect_uri = "/index.html";

			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string Latitude = request::findValue(&req, "Latitude");
			std::string Longitude = request::findValue(&req, "Longitude");
			_log.SetLogPreference(CURLEncode::URLDecode(request::findValue(&req, "LogFilter")),
				CURLEncode::URLDecode(request::findValue(&req, "LogFileName")),
				CURLEncode::URLDecode(request::findValue(&req, "LogLevel")));
			if ((Latitude != "") && (Longitude != ""))
			{
				std::string LatLong = Latitude + ";" + Longitude;
				m_sql.UpdatePreferencesVar("Location", LatLong.c_str());
				m_mainworker.GetSunSettings();
			}
			m_notifications.ConfigFromGetvars(req, true);
			std::string DashboardType = request::findValue(&req, "DashboardType");
			m_sql.UpdatePreferencesVar("DashboardType", atoi(DashboardType.c_str()));
			std::string MobileType = request::findValue(&req, "MobileType");
			m_sql.UpdatePreferencesVar("MobileType", atoi(MobileType.c_str()));

			int nUnit = atoi(request::findValue(&req, "WindUnit").c_str());
			m_sql.UpdatePreferencesVar("WindUnit", nUnit);
			m_sql.m_windunit = (_eWindUnit)nUnit;

			nUnit = atoi(request::findValue(&req, "TempUnit").c_str());
			m_sql.UpdatePreferencesVar("TempUnit", nUnit);
			m_sql.m_tempunit = (_eTempUnit)nUnit;

			nUnit = atoi(request::findValue(&req, "WeightUnit").c_str());
			m_sql.UpdatePreferencesVar("WeightUnit", nUnit);
			m_sql.m_weightunit = (_eWeightUnit)nUnit;


			m_sql.SetUnitsAndScale();

			std::string AuthenticationMethod = request::findValue(&req, "AuthenticationMethod");
			_eAuthenticationMethod amethod = (_eAuthenticationMethod)atoi(AuthenticationMethod.c_str());
			m_sql.UpdatePreferencesVar("AuthenticationMethod", static_cast<int>(amethod));
			m_pWebEm->SetAuthenticationMethod(amethod);

			std::string ReleaseChannel = request::findValue(&req, "ReleaseChannel");
			m_sql.UpdatePreferencesVar("ReleaseChannel", atoi(ReleaseChannel.c_str()));

			std::string LightHistoryDays = request::findValue(&req, "LightHistoryDays");
			m_sql.UpdatePreferencesVar("LightHistoryDays", atoi(LightHistoryDays.c_str()));

			std::string s5MinuteHistoryDays = request::findValue(&req, "ShortLogDays");
			m_sql.UpdatePreferencesVar("5MinuteHistoryDays", atoi(s5MinuteHistoryDays.c_str()));

			int iShortLogInterval = atoi(request::findValue(&req, "ShortLogInterval").c_str());
			if (iShortLogInterval < 1)
				iShortLogInterval = 5;
			m_sql.UpdatePreferencesVar("ShortLogInterval", iShortLogInterval);
			m_sql.m_ShortLogInterval = iShortLogInterval;

			std::string sElectricVoltage = request::findValue(&req, "ElectricVoltage");
			m_sql.UpdatePreferencesVar("ElectricVoltage", atoi(sElectricVoltage.c_str()));

			std::string sCM113DisplayType = request::findValue(&req, "CM113DisplayType");
			m_sql.UpdatePreferencesVar("CM113DisplayType", atoi(sCM113DisplayType.c_str()));

			std::string WebUserName = request::findValue(&req, "WebUserName");
			std::string WebPassword = request::findValue(&req, "WebPassword");
			std::string WebLocalNetworks = request::findValue(&req, "WebLocalNetworks");
			std::string WebRemoteProxyIPs = request::findValue(&req, "WebRemoteProxyIPs");
			WebUserName = CURLEncode::URLDecode(WebUserName);
			WebPassword = CURLEncode::URLDecode(WebPassword);
			WebLocalNetworks = CURLEncode::URLDecode(WebLocalNetworks);
			WebRemoteProxyIPs = CURLEncode::URLDecode(WebRemoteProxyIPs);

			if ((WebUserName.empty()) || (WebPassword.empty()))
			{
				WebUserName = "";
				WebPassword = "";
			}
			WebUserName = base64_encode((const unsigned char*)WebUserName.c_str(), WebUserName.size());
			if (WebPassword.size() != 32)
			{
				WebPassword = GenerateMD5Hash(WebPassword);
			}

			// Invalid sessions of WebUser when his username or password has been changed
			int nUnusedValue = 0;
			std::string sOldWebLogin;
			std::string sOldWebPassword;
			if (((m_sql.GetPreferencesVar("WebUserName", nUnusedValue, sOldWebLogin)) && (sOldWebLogin != WebUserName))
				|| ((m_sql.GetPreferencesVar("WebPassword", nUnusedValue, sOldWebPassword)) && (sOldWebPassword != WebPassword)))
			{
				RemoveUsersSessions(sOldWebLogin, session);
			}

			m_sql.UpdatePreferencesVar("WebUserName", WebUserName.c_str());
			m_sql.UpdatePreferencesVar("WebPassword", WebPassword.c_str());
			m_sql.UpdatePreferencesVar("WebLocalNetworks", WebLocalNetworks.c_str());
			m_sql.UpdatePreferencesVar("WebRemoteProxyIPs", WebRemoteProxyIPs.c_str());

			LoadUsers();
			m_pWebEm->ClearLocalNetworks();
			std::vector<std::string> strarray;
			StringSplit(WebLocalNetworks, ";", strarray);
			for (std::vector<std::string>::const_iterator itt = strarray.begin(); itt != strarray.end(); ++itt)
			{
				std::string network = *itt;
				m_pWebEm->AddLocalNetworks(network);
			}
			//add local hostname
			m_pWebEm->AddLocalNetworks("");

			m_pWebEm->ClearRemoteProxyIPs();
			strarray.clear();
			StringSplit(WebRemoteProxyIPs, ";", strarray);
			for (std::vector<std::string>::const_iterator itt = strarray.begin(); itt != strarray.end(); ++itt)
			{
				std::string ipaddr = *itt;
				m_pWebEm->AddRemoteProxyIPs(ipaddr);
			}

			if (session.username.empty())
			{
				//Local network could be changed so lets for a check here
				session.rights = -1;
			}

			std::string SecPassword = request::findValue(&req, "SecPassword");
			SecPassword = CURLEncode::URLDecode(SecPassword);
			if (SecPassword.size() != 32)
			{
				SecPassword = GenerateMD5Hash(SecPassword);
			}
			m_sql.UpdatePreferencesVar("SecPassword", SecPassword.c_str());

			std::string ProtectionPassword = request::findValue(&req, "ProtectionPassword");
			ProtectionPassword = CURLEncode::URLDecode(ProtectionPassword);
			if (ProtectionPassword.size() != 32)
			{
				ProtectionPassword = GenerateMD5Hash(ProtectionPassword);
			}
			m_sql.UpdatePreferencesVar("ProtectionPassword", ProtectionPassword.c_str());

			int EnergyDivider = atoi(request::findValue(&req, "EnergyDivider").c_str());
			int GasDivider = atoi(request::findValue(&req, "GasDivider").c_str());
			int WaterDivider = atoi(request::findValue(&req, "WaterDivider").c_str());
			if (EnergyDivider < 1)
				EnergyDivider = 1000;
			if (GasDivider < 1)
				GasDivider = 100;
			if (WaterDivider < 1)
				WaterDivider = 100;
			m_sql.UpdatePreferencesVar("MeterDividerEnergy", EnergyDivider);
			m_sql.UpdatePreferencesVar("MeterDividerGas", GasDivider);
			m_sql.UpdatePreferencesVar("MeterDividerWater", WaterDivider);

			std::string scheckforupdates = request::findValue(&req, "checkforupdates");
			m_sql.UpdatePreferencesVar("UseAutoUpdate", (scheckforupdates == "on" ? 1 : 0));

			std::string senableautobackup = request::findValue(&req, "enableautobackup");
			m_sql.UpdatePreferencesVar("UseAutoBackup", (senableautobackup == "on" ? 1 : 0));

			float CostEnergy = static_cast<float>(atof(request::findValue(&req, "CostEnergy").c_str()));
			float CostEnergyT2 = static_cast<float>(atof(request::findValue(&req, "CostEnergyT2").c_str()));
			float CostEnergyR1 = static_cast<float>(atof(request::findValue(&req, "CostEnergyR1").c_str()));
			float CostEnergyR2 = static_cast<float>(atof(request::findValue(&req, "CostEnergyR2").c_str()));
			float CostGas = static_cast<float>(atof(request::findValue(&req, "CostGas").c_str()));
			float CostWater = static_cast<float>(atof(request::findValue(&req, "CostWater").c_str()));
			m_sql.UpdatePreferencesVar("CostEnergy", int(CostEnergy*10000.0f));
			m_sql.UpdatePreferencesVar("CostEnergyT2", int(CostEnergyT2*10000.0f));
			m_sql.UpdatePreferencesVar("CostEnergyR1", int(CostEnergyR1*10000.0f));
			m_sql.UpdatePreferencesVar("CostEnergyR2", int(CostEnergyR2*10000.0f));
			m_sql.UpdatePreferencesVar("CostGas", int(CostGas*10000.0f));
			m_sql.UpdatePreferencesVar("CostWater", int(CostWater*10000.0f));

			int rnOldvalue = 0;
			int rnvalue = 0;

			m_sql.GetPreferencesVar("ActiveTimerPlan", rnOldvalue);
			rnvalue = atoi(request::findValue(&req, "ActiveTimerPlan").c_str());
			if (rnOldvalue != rnvalue)
			{
				m_sql.UpdatePreferencesVar("ActiveTimerPlan", rnvalue);
				m_sql.m_ActiveTimerPlan = rnvalue;
				m_mainworker.m_scheduler.ReloadSchedules();
			}
			m_sql.UpdatePreferencesVar("DoorbellCommand", atoi(request::findValue(&req, "DoorbellCommand").c_str()));
			m_sql.UpdatePreferencesVar("SmartMeterType", atoi(request::findValue(&req, "SmartMeterType").c_str()));

			std::string EnableTabFloorplans = request::findValue(&req, "EnableTabFloorplans");
			m_sql.UpdatePreferencesVar("EnableTabFloorplans", (EnableTabFloorplans == "on" ? 1 : 0));
			std::string EnableTabLights = request::findValue(&req, "EnableTabLights");
			m_sql.UpdatePreferencesVar("EnableTabLights", (EnableTabLights == "on" ? 1 : 0));
			std::string EnableTabTemp = request::findValue(&req, "EnableTabTemp");
			m_sql.UpdatePreferencesVar("EnableTabTemp", (EnableTabTemp == "on" ? 1 : 0));
			std::string EnableTabWeather = request::findValue(&req, "EnableTabWeather");
			m_sql.UpdatePreferencesVar("EnableTabWeather", (EnableTabWeather == "on" ? 1 : 0));
			std::string EnableTabUtility = request::findValue(&req, "EnableTabUtility");
			m_sql.UpdatePreferencesVar("EnableTabUtility", (EnableTabUtility == "on" ? 1 : 0));
			std::string EnableTabScenes = request::findValue(&req, "EnableTabScenes");
			m_sql.UpdatePreferencesVar("EnableTabScenes", (EnableTabScenes == "on" ? 1 : 0));
			std::string EnableTabCustom = request::findValue(&req, "EnableTabCustom");
			m_sql.UpdatePreferencesVar("EnableTabCustom", (EnableTabCustom == "on" ? 1 : 0));

			m_sql.GetPreferencesVar("NotificationSensorInterval", rnOldvalue);
			rnvalue = atoi(request::findValue(&req, "NotificationSensorInterval").c_str());
			if (rnOldvalue != rnvalue)
			{
				m_sql.UpdatePreferencesVar("NotificationSensorInterval", rnvalue);
				m_notifications.ReloadNotifications();
			}
			m_sql.GetPreferencesVar("NotificationSwitchInterval", rnOldvalue);
			rnvalue = atoi(request::findValue(&req, "NotificationSwitchInterval").c_str());
			if (rnOldvalue != rnvalue)
			{
				m_sql.UpdatePreferencesVar("NotificationSwitchInterval", rnvalue);
				m_notifications.ReloadNotifications();
			}
			std::string RaspCamParams = request::findValue(&req, "RaspCamParams");
			if (RaspCamParams != "")
			{
				if (IsArgumentSecure(RaspCamParams))
					m_sql.UpdatePreferencesVar("RaspCamParams", RaspCamParams.c_str());
			}

			std::string UVCParams = request::findValue(&req, "UVCParams");
			if (UVCParams != "")
			{
				if (IsArgumentSecure(UVCParams))
					m_sql.UpdatePreferencesVar("UVCParams", UVCParams.c_str());
			}

			std::string EnableNewHardware = request::findValue(&req, "AcceptNewHardware");
			int iEnableNewHardware = (EnableNewHardware == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("AcceptNewHardware", iEnableNewHardware);
			m_sql.m_bAcceptNewHardware = (iEnableNewHardware == 1);

			std::string HideDisabledHardwareSensors = request::findValue(&req, "HideDisabledHardwareSensors");
			int iHideDisabledHardwareSensors = (HideDisabledHardwareSensors == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("HideDisabledHardwareSensors", iHideDisabledHardwareSensors);

			std::string ShowUpdateEffect = request::findValue(&req, "ShowUpdateEffect");
			int iShowUpdateEffect = (ShowUpdateEffect == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("ShowUpdateEffect", iShowUpdateEffect);

			std::string SendErrorsAsNotification = request::findValue(&req, "SendErrorsAsNotification");
			int iSendErrorsAsNotification = (SendErrorsAsNotification == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("SendErrorsAsNotification", iSendErrorsAsNotification);
			_log.ForwardErrorsToNotificationSystem(iSendErrorsAsNotification != 0);

			std::string DegreeDaysBaseTemperature = request::findValue(&req, "DegreeDaysBaseTemperature");
			m_sql.UpdatePreferencesVar("DegreeDaysBaseTemperature", DegreeDaysBaseTemperature);

			rnOldvalue = 0;
			m_sql.GetPreferencesVar("EnableEventScriptSystem", rnOldvalue);
			std::string EnableEventScriptSystem = request::findValue(&req, "EnableEventScriptSystem");
			int iEnableEventScriptSystem = (EnableEventScriptSystem == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("EnableEventScriptSystem", iEnableEventScriptSystem);
			m_sql.m_bEnableEventSystem = (iEnableEventScriptSystem == 1);
			if (iEnableEventScriptSystem != rnOldvalue)
			{
				m_mainworker.m_eventsystem.SetEnabled(m_sql.m_bEnableEventSystem);
				m_mainworker.m_eventsystem.StartEventSystem();
			}

			rnOldvalue = 0;
			m_sql.GetPreferencesVar("DisableDzVentsSystem", rnOldvalue);
			std::string DisableDzVentsSystem = request::findValue(&req, "DisableDzVentsSystem");
			int iDisableDzVentsSystem = (DisableDzVentsSystem == "on" ? 0 : 1);
			m_sql.UpdatePreferencesVar("DisableDzVentsSystem", iDisableDzVentsSystem);
			m_sql.m_bDisableDzVentsSystem = (iDisableDzVentsSystem == 1);
			if (m_sql.m_bEnableEventSystem && !iDisableDzVentsSystem && iDisableDzVentsSystem != rnOldvalue)
			{
				m_mainworker.m_eventsystem.LoadEvents();
				m_mainworker.m_eventsystem.GetCurrentStates();
			}
			m_sql.UpdatePreferencesVar("DzVentsLogLevel", atoi(request::findValue(&req, "DzVentsLogLevel").c_str()));

			std::string LogEventScriptTrigger = request::findValue(&req, "LogEventScriptTrigger");
			m_sql.m_bLogEventScriptTrigger = (LogEventScriptTrigger == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("LogEventScriptTrigger", m_sql.m_bLogEventScriptTrigger);

			std::string EnableWidgetOrdering = request::findValue(&req, "AllowWidgetOrdering");
			int iEnableAllowWidgetOrdering = (EnableWidgetOrdering == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("AllowWidgetOrdering", iEnableAllowWidgetOrdering);
			m_sql.m_bAllowWidgetOrdering = (iEnableAllowWidgetOrdering == 1);

			rnOldvalue = 0;
			m_sql.GetPreferencesVar("RemoteSharedPort", rnOldvalue);

			m_sql.UpdatePreferencesVar("RemoteSharedPort", atoi(request::findValue(&req, "RemoteSharedPort").c_str()));

			rnvalue = 0;
			m_sql.GetPreferencesVar("RemoteSharedPort", rnvalue);

			if (rnvalue != rnOldvalue)
			{
				m_mainworker.m_sharedserver.StopServer();
				if (rnvalue != 0)
				{
					char szPort[100];
					sprintf(szPort, "%d", rnvalue);
					m_mainworker.m_sharedserver.StartServer("::", szPort);
					m_mainworker.LoadSharedUsers();
				}
			}

			m_sql.UpdatePreferencesVar("Language", request::findValue(&req, "Language").c_str());
			std::string SelectedTheme = request::findValue(&req, "Themes");
			m_sql.UpdatePreferencesVar("WebTheme", SelectedTheme.c_str());
			m_pWebEm->SetWebTheme(SelectedTheme);
			std::string Title = request::findValue(&req, "Title").c_str();
			m_sql.UpdatePreferencesVar("Title", (Title.empty()) ? "Domoticz" : Title);

			m_sql.GetPreferencesVar("RandomTimerFrame", rnOldvalue);
			rnvalue = atoi(request::findValue(&req, "RandomSpread").c_str());
			if (rnOldvalue != rnvalue)
			{
				m_sql.UpdatePreferencesVar("RandomTimerFrame", rnvalue);
				m_mainworker.m_scheduler.ReloadSchedules();
			}

			m_sql.UpdatePreferencesVar("SecOnDelay", atoi(request::findValue(&req, "SecOnDelay").c_str()));

			int sensortimeout = atoi(request::findValue(&req, "SensorTimeout").c_str());
			if (sensortimeout < 10)
				sensortimeout = 10;
			m_sql.UpdatePreferencesVar("SensorTimeout", sensortimeout);

			int batterylowlevel = atoi(request::findValue(&req, "BatterLowLevel").c_str());
			if (batterylowlevel > 100)
				batterylowlevel = 100;
			m_sql.GetPreferencesVar("BatteryLowNotification", rnOldvalue);
			m_sql.UpdatePreferencesVar("BatteryLowNotification", batterylowlevel);
			if ((rnOldvalue != batterylowlevel) && (batterylowlevel != 0))
				m_sql.CheckBatteryLow();

			int nValue = 0;
			nValue = atoi(request::findValue(&req, "FloorplanPopupDelay").c_str());
			m_sql.UpdatePreferencesVar("FloorplanPopupDelay", nValue);
			std::string FloorplanFullscreenMode = request::findValue(&req, "FloorplanFullscreenMode");
			m_sql.UpdatePreferencesVar("FloorplanFullscreenMode", (FloorplanFullscreenMode == "on" ? 1 : 0));
			std::string FloorplanAnimateZoom = request::findValue(&req, "FloorplanAnimateZoom");
			m_sql.UpdatePreferencesVar("FloorplanAnimateZoom", (FloorplanAnimateZoom == "on" ? 1 : 0));
			std::string FloorplanShowSensorValues = request::findValue(&req, "FloorplanShowSensorValues");
			m_sql.UpdatePreferencesVar("FloorplanShowSensorValues", (FloorplanShowSensorValues == "on" ? 1 : 0));
			std::string FloorplanShowSwitchValues = request::findValue(&req, "FloorplanShowSwitchValues");
			m_sql.UpdatePreferencesVar("FloorplanShowSwitchValues", (FloorplanShowSwitchValues == "on" ? 1 : 0));
			std::string FloorplanShowSceneNames = request::findValue(&req, "FloorplanShowSceneNames");
			m_sql.UpdatePreferencesVar("FloorplanShowSceneNames", (FloorplanShowSceneNames == "on" ? 1 : 0));
			m_sql.UpdatePreferencesVar("FloorplanRoomColour", CURLEncode::URLDecode(request::findValue(&req, "FloorplanRoomColour").c_str()).c_str());
			m_sql.UpdatePreferencesVar("FloorplanActiveOpacity", atoi(request::findValue(&req, "FloorplanActiveOpacity").c_str()));
			m_sql.UpdatePreferencesVar("FloorplanInactiveOpacity", atoi(request::findValue(&req, "FloorplanInactiveOpacity").c_str()));

#ifndef NOCLOUD
			std::string md_userid, md_password, pf_userid, pf_password;
			int md_subsystems, pf_subsystems;
			m_sql.GetPreferencesVar("MyDomoticzUserId", pf_userid);
			m_sql.GetPreferencesVar("MyDomoticzPassword", pf_password);
			m_sql.GetPreferencesVar("MyDomoticzSubsystems", pf_subsystems);
			md_userid = CURLEncode::URLDecode(request::findValue(&req, "MyDomoticzUserId"));
			md_password = CURLEncode::URLDecode(request::findValue(&req, "MyDomoticzPassword"));
			md_subsystems = (request::findValue(&req, "SubsystemHttp").empty() ? 0 : 1) + (request::findValue(&req, "SubsystemShared").empty() ? 0 : 2) + (request::findValue(&req, "SubsystemApps").empty() ? 0 : 4);
			if (md_userid != pf_userid || md_password != pf_password || md_subsystems != pf_subsystems) {
				m_sql.UpdatePreferencesVar("MyDomoticzUserId", md_userid);
				if (md_password != pf_password) {
					md_password = base64_encode((unsigned char const*)md_password.c_str(), md_password.size());
					m_sql.UpdatePreferencesVar("MyDomoticzPassword", md_password);
				}
				m_sql.UpdatePreferencesVar("MyDomoticzSubsystems", md_subsystems);
				m_webservers.RestartProxy();
			}
#endif

			m_sql.UpdatePreferencesVar("OneWireSensorPollPeriod", atoi(request::findValue(&req, "OneWireSensorPollPeriod").c_str()));
			m_sql.UpdatePreferencesVar("OneWireSwitchPollPeriod", atoi(request::findValue(&req, "OneWireSwitchPollPeriod").c_str()));

			m_sql.UpdatePreferencesVar("OneWireSwitchPollPeriod", atoi(request::findValue(&req, "OneWireSwitchPollPeriod").c_str()));

			std::string IFTTTEnabled = request::findValue(&req, "IFTTTEnabled");
			int iIFTTTEnabled = (IFTTTEnabled == "on" ? 1 : 0);
			m_sql.UpdatePreferencesVar("IFTTTEnabled", iIFTTTEnabled);
			std::string szKey = request::findValue(&req, "IFTTTAPI");
			m_sql.UpdatePreferencesVar("IFTTTAPI", base64_encode((unsigned char const*)szKey.c_str(), szKey.size()));

			m_notifications.LoadConfig();
#ifdef ENABLE_PYTHON
			//Signal plugins to update Settings dictionary
			PluginLoadConfig();
#endif
		}

		void CWebServer::RestoreDatabase(WebEmSession & session, const request& req, std::string & redirect_uri)
		{
			redirect_uri = "/index.html";
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string dbasefile = request::findValue(&req, "dbasefile");
			if (dbasefile.empty()) {
				return;
			}

			m_mainworker.StopDomoticzHardware();

			m_sql.RestoreDatabase(dbasefile);
			m_mainworker.AddAllDomoticzHardware();
		}

		void CWebServer::GetDatabaseBackup(WebEmSession & session, const request& req, reply & rep)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
#ifdef WIN32
			std::string OutputFileName = szUserDataFolder + "backup.db";
#else
			std::string OutputFileName = "/tmp/backup.db";
#endif
			if (m_sql.BackupDatabase(OutputFileName))
			{
				std::string szAttachmentName = "domoticz.db";
				std::string szVar;
				if (m_sql.GetPreferencesVar("Title", szVar))
				{
					stdreplace(szVar, " ", "_");
					stdreplace(szVar, "/", "_");
					stdreplace(szVar, "\\", "_");
					if (!szVar.empty()) {
						szAttachmentName = szVar + ".db";
					}
				}
				reply::set_content_from_file(&rep, OutputFileName, szAttachmentName, true);
			}
		}

		void CWebServer::RType_DeleteDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			root["status"] = "OK";
			root["title"] = "DeleteDevice";
			m_sql.DeleteDevices(idx);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::RType_AddScene(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string name = request::findValue(&req, "name");
			if (name.empty())
			{
				root["status"] = "ERR";
				root["message"] = "No Scene Name specified!";
				return;
			}
			std::string stype = request::findValue(&req, "scenetype");
			if (stype.empty())
			{
				root["status"] = "ERR";
				root["message"] = "No Scene Type specified!";
				return;
			}
			if (m_sql.DoesSceneByNameExits(name) == true)
			{
				root["status"] = "ERR";
				root["message"] = "A Scene with this Name already Exits!";
				return;
			}
			root["status"] = "OK";
			root["title"] = "AddScene";
			m_sql.safe_query(
				"INSERT INTO Scenes (Name,SceneType) VALUES ('%q',%d)",
				name.c_str(),
				atoi(stype.c_str())
			);
			if (m_sql.m_bEnableEventSystem)
			{
				m_mainworker.m_eventsystem.GetCurrentScenesGroups();
			}
		}

		void CWebServer::RType_DeleteScene(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "DeleteScene";
			m_sql.safe_query("DELETE FROM Scenes WHERE (ID == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM SceneDevices WHERE (SceneRowID == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM SceneTimers WHERE (SceneRowID == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM SceneLog WHERE (SceneRowID=='%q')", idx.c_str());
			std::stringstream sstridx(idx);
			uint64_t ullidx;
			sstridx >> ullidx;
			m_mainworker.m_eventsystem.RemoveSingleState(ullidx, m_mainworker.m_eventsystem.REASON_SCENEGROUP);
		}

		void CWebServer::RType_UpdateScene(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string name = request::findValue(&req, "name");
			std::string description = request::findValue(&req, "description");
			if ((idx.empty()) || (name.empty()))
				return;
			std::string stype = request::findValue(&req, "scenetype");
			if (stype.empty())
			{
				root["status"] = "ERR";
				root["message"] = "No Scene Type specified!";
				return;
			}
			std::string tmpstr = request::findValue(&req, "protected");
			int iProtected = (tmpstr == "true") ? 1 : 0;

			std::string onaction = base64_decode(request::findValue(&req, "onaction"));
			std::string offaction = base64_decode(request::findValue(&req, "offaction"));


			root["status"] = "OK";
			root["title"] = "UpdateScene";
			m_sql.safe_query("UPDATE Scenes SET Name='%q', Description='%q', SceneType=%d, Protected=%d, OnAction='%q', OffAction='%q' WHERE (ID == '%q')",
				name.c_str(),
				description.c_str(),
				atoi(stype.c_str()),
				iProtected,
				onaction.c_str(),
				offaction.c_str(),
				idx.c_str()
			);
			std::stringstream sstridx(idx);
			uint64_t ullidx;
			sstridx >> ullidx;
			m_mainworker.m_eventsystem.WWWUpdateSingleState(ullidx, name, m_mainworker.m_eventsystem.REASON_SCENEGROUP);
		}

		bool compareIconsByName(const http::server::CWebServer::_tCustomIcon &a, const http::server::CWebServer::_tCustomIcon &b)
		{
			return a.Title < b.Title;
		}

		void CWebServer::RType_CustomLightIcons(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::vector<_tCustomIcon>::const_iterator itt;
			int ii = 0;

			std::vector<_tCustomIcon> temp_custom_light_icons = m_custom_light_icons;
			//Sort by name
			std::sort(temp_custom_light_icons.begin(), temp_custom_light_icons.end(), compareIconsByName);

			for (itt = temp_custom_light_icons.begin(); itt != temp_custom_light_icons.end(); ++itt)
			{
				root["result"][ii]["idx"] = itt->idx;
				root["result"][ii]["imageSrc"] = itt->RootFile;
				root["result"][ii]["text"] = itt->Title;
				root["result"][ii]["description"] = itt->Description;
				ii++;
			}
			root["status"] = "OK";
		}

		void CWebServer::RType_Plans(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "Plans";

			std::string sDisplayHidden = request::findValue(&req, "displayhidden");
			bool bDisplayHidden = (sDisplayHidden == "1");

			std::vector<std::vector<std::string> > result, result2;
			result = m_sql.safe_query("SELECT ID, Name, [Order] FROM Plans ORDER BY [Order]");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					std::string Name = sd[1];
					bool bIsHidden = (Name[0] == '$');

					if ((bDisplayHidden) || (!bIsHidden))
					{
						root["result"][ii]["idx"] = sd[0];
						root["result"][ii]["Name"] = Name;
						root["result"][ii]["Order"] = sd[2];

						unsigned int totDevices = 0;

						result2 = m_sql.safe_query("SELECT COUNT(*) FROM DeviceToPlansMap WHERE (PlanID=='%q')",
							sd[0].c_str());
						if (result2.size() > 0)
						{
							totDevices = (unsigned int)atoi(result2[0][0].c_str());
						}
						root["result"][ii]["Devices"] = totDevices;

						ii++;
					}
				}
			}
		}

		void CWebServer::RType_FloorPlans(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "Floorplans";

			std::vector<std::vector<std::string> > result, result2, result3;

			result = m_sql.safe_query("SELECT Key, nValue, sValue FROM Preferences WHERE Key LIKE 'Floorplan%%'");
			if (result.empty())
				return;

			std::vector<std::vector<std::string> >::const_iterator itt;
			for (itt = result.begin(); itt != result.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				std::string Key = sd[0];
				int nValue = atoi(sd[1].c_str());
				std::string sValue = sd[2];

				if (Key == "FloorplanPopupDelay")
				{
					root["PopupDelay"] = nValue;
				}
				if (Key == "FloorplanFullscreenMode")
				{
					root["FullscreenMode"] = nValue;
				}
				if (Key == "FloorplanAnimateZoom")
				{
					root["AnimateZoom"] = nValue;
				}
				if (Key == "FloorplanShowSensorValues")
				{
					root["ShowSensorValues"] = nValue;
				}
				if (Key == "FloorplanShowSwitchValues")
				{
					root["ShowSwitchValues"] = nValue;
				}
				if (Key == "FloorplanShowSceneNames")
				{
					root["ShowSceneNames"] = nValue;
				}
				if (Key == "FloorplanRoomColour")
				{
					root["RoomColour"] = sValue;
				}
				if (Key == "FloorplanActiveOpacity")
				{
					root["ActiveRoomOpacity"] = nValue;
				}
				if (Key == "FloorplanInactiveOpacity")
				{
					root["InactiveRoomOpacity"] = nValue;
				}
			}

			result2 = m_sql.safe_query("SELECT ID, Name, ImageFile, ScaleFactor, [Order] FROM Floorplans ORDER BY [Order]");
			if (result2.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result2.begin(); itt != result2.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					root["result"][ii]["Image"] = sd[2];
					root["result"][ii]["ScaleFactor"] = sd[3];
					root["result"][ii]["Order"] = sd[4];

					unsigned int totPlans = 0;

					result3 = m_sql.safe_query("SELECT COUNT(*) FROM Plans WHERE (FloorplanID=='%q')",
						sd[0].c_str());
					if (result3.size() > 0)
					{
						totPlans = (unsigned int)atoi(result3[0][0].c_str());
					}
					root["result"][ii]["Plans"] = totPlans;

					ii++;
				}
			}
		}

		void CWebServer::RType_Scenes(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "Scenes";
			root["AllowWidgetOrdering"] = m_sql.m_bAllowWidgetOrdering;

			std::string sDisplayHidden = request::findValue(&req, "displayhidden");
			bool bDisplayHidden = (sDisplayHidden == "1");

			std::string sLastUpdate = request::findValue(&req, "lastupdate");

			time_t LastUpdate = 0;
			if (sLastUpdate != "")
			{
				std::stringstream sstr;
				sstr << sLastUpdate;
				sstr >> LastUpdate;
			}

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			struct tm tLastUpdate;
			localtime_r(&now, &tLastUpdate);

			root["ActTime"] = static_cast<int>(now);

			std::vector<std::vector<std::string> > result, result2;
			result = m_sql.safe_query("SELECT ID, Name, Activators, Favorite, nValue, SceneType, LastUpdate, Protected, OnAction, OffAction, Description FROM Scenes ORDER BY [Order]");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					std::string sName = sd[1];
					if ((bDisplayHidden == false) && (sName[0] == '$'))
						continue;

					std::string sLastUpdate = sd[6].c_str();
					if (LastUpdate != 0)
					{
						time_t cLastUpdate;
						ParseSQLdatetime(cLastUpdate, tLastUpdate, sLastUpdate, tm1.tm_isdst);
						if (cLastUpdate <= LastUpdate)
							continue;
					}

					unsigned char nValue = atoi(sd[4].c_str());
					unsigned char scenetype = atoi(sd[5].c_str());
					int iProtected = atoi(sd[7].c_str());

					std::string onaction = base64_encode((const unsigned char*)sd[8].c_str(), sd[8].size());
					std::string offaction = base64_encode((const unsigned char*)sd[9].c_str(), sd[9].size());

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sName;
					root["result"][ii]["Description"] = sd[10];
					root["result"][ii]["Favorite"] = atoi(sd[3].c_str());
					root["result"][ii]["Protected"] = (iProtected != 0);
					root["result"][ii]["OnAction"] = onaction;
					root["result"][ii]["OffAction"] = offaction;

					if (scenetype == 0)
					{
						root["result"][ii]["Type"] = "Scene";
					}
					else
					{
						root["result"][ii]["Type"] = "Group";
					}

					root["result"][ii]["LastUpdate"] = sLastUpdate;

					if (nValue == 0)
						root["result"][ii]["Status"] = "Off";
					else if (nValue == 1)
						root["result"][ii]["Status"] = "On";
					else
						root["result"][ii]["Status"] = "Mixed";
					root["result"][ii]["Timers"] = (m_sql.HasSceneTimers(sd[0]) == true) ? "true" : "false";
					uint64_t camIDX = m_mainworker.m_cameras.IsDevSceneInCamera(1, sd[0]);
					root["result"][ii]["UsedByCamera"] = (camIDX != 0) ? true : false;
					if (camIDX != 0) {
						std::stringstream scidx;
						scidx << camIDX;
						root["result"][ii]["CameraIdx"] = scidx.str();
					}
					ii++;
				}
			}
			if (!m_mainworker.m_LastSunriseSet.empty())
			{
				std::vector<std::string> strarray;
				StringSplit(m_mainworker.m_LastSunriseSet, ";", strarray);
				if (strarray.size() == 10)
				{
					char szTmp[100];
					//strftime(szTmp, 80, "%b %d %Y %X", &tm1);
					strftime(szTmp, 80, "%Y-%m-%d %X", &tm1);
					root["ServerTime"] = szTmp;
					root["Sunrise"] = strarray[0];
					root["Sunset"] = strarray[1];
					root["SunAtSouth"] = strarray[2];
					root["CivTwilightStart"] = strarray[3];
					root["CivTwilightEnd"] = strarray[4];
					root["NautTwilightStart"] = strarray[5];
					root["NautTwilightEnd"] = strarray[6];
					root["AstrTwilightStart"] = strarray[7];
					root["AstrTwilightEnd"] = strarray[8];
					root["DayLength"] = strarray[9];
				}
			}
		}

		void CWebServer::RType_Hardware(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "Hardware";

#ifdef WITH_OPENZWAVE
			m_ZW_Hwidx = -1;
#endif

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Name, Enabled, Type, Address, Port, SerialPort, Username, Password, Extra, Mode1, Mode2, Mode3, Mode4, Mode5, Mode6, DataTimeout FROM Hardware ORDER BY ID ASC");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					_eHardwareTypes hType = (_eHardwareTypes)atoi(sd[3].c_str());
					if (hType == HTYPE_DomoticzInternal)
						continue;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					root["result"][ii]["Enabled"] = (sd[2] == "1") ? "true" : "false";
					root["result"][ii]["Type"] = hType;
					root["result"][ii]["Address"] = sd[4];
					root["result"][ii]["Port"] = atoi(sd[5].c_str());
					root["result"][ii]["SerialPort"] = sd[6];
					root["result"][ii]["Username"] = sd[7];
					root["result"][ii]["Password"] = sd[8];
					root["result"][ii]["Extra"] = sd[9];

					if (hType == HTYPE_PythonPlugin) {
						root["result"][ii]["Mode1"] = sd[10];  // Plugins can have non-numeric values in the Mode fields
						root["result"][ii]["Mode2"] = sd[11];
						root["result"][ii]["Mode3"] = sd[12];
						root["result"][ii]["Mode4"] = sd[13];
						root["result"][ii]["Mode5"] = sd[14];
						root["result"][ii]["Mode6"] = sd[15];
					}
					else {
						root["result"][ii]["Mode1"] = atoi(sd[10].c_str());
						root["result"][ii]["Mode2"] = atoi(sd[11].c_str());
						root["result"][ii]["Mode3"] = atoi(sd[12].c_str());
						root["result"][ii]["Mode4"] = atoi(sd[13].c_str());
						root["result"][ii]["Mode5"] = atoi(sd[14].c_str());
						root["result"][ii]["Mode6"] = atoi(sd[15].c_str());
					}
					root["result"][ii]["DataTimeout"] = atoi(sd[16].c_str());

					//Special case for openzwave (status for nodes queried)
					CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(atoi(sd[0].c_str()));
					if (pHardware != NULL)
					{
						if (
							(pHardware->HwdType == HTYPE_RFXtrx315) ||
							(pHardware->HwdType == HTYPE_RFXtrx433) ||
							(pHardware->HwdType == HTYPE_RFXtrx868) ||
							(pHardware->HwdType == HTYPE_RFXLAN)
							)
						{
							CRFXBase *pMyHardware = reinterpret_cast<CRFXBase*>(pHardware);
							if (!pMyHardware->m_Version.empty())
								root["result"][ii]["version"] = pMyHardware->m_Version;
							else
								root["result"][ii]["version"] = sd[11];
						}
						else if ((pHardware->HwdType == HTYPE_MySensorsUSB) || (pHardware->HwdType == HTYPE_MySensorsTCP) || (pHardware->HwdType == HTYPE_MySensorsMQTT))
						{
							MySensorsBase *pMyHardware = reinterpret_cast<MySensorsBase*>(pHardware);
							root["result"][ii]["version"] = pMyHardware->GetGatewayVersion();
						}
						else if ((pHardware->HwdType == HTYPE_OpenThermGateway) || (pHardware->HwdType == HTYPE_OpenThermGatewayTCP))
						{
							OTGWBase *pMyHardware = reinterpret_cast<OTGWBase*>(pHardware);
							root["result"][ii]["version"] = pMyHardware->m_Version;
						}
						else if ((pHardware->HwdType == HTYPE_RFLINKUSB) || (pHardware->HwdType == HTYPE_RFLINKTCP))
						{
							CRFLinkBase *pMyHardware = reinterpret_cast<CRFLinkBase*>(pHardware);
							root["result"][ii]["version"] = pMyHardware->m_Version;
						}
						else
						{
#ifdef WITH_OPENZWAVE
							if (pHardware->HwdType == HTYPE_OpenZWave)
							{
								COpenZWave *pOZWHardware = reinterpret_cast<COpenZWave*>(pHardware);
								root["result"][ii]["version"] = pOZWHardware->GetVersionLong();
								root["result"][ii]["NodesQueried"] = (pOZWHardware->m_awakeNodesQueried || pOZWHardware->m_allNodesQueried);
							}
#endif
						}
					}
					ii++;
				}
			}
		}

		void CWebServer::RType_Devices(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string rfilter = request::findValue(&req, "filter");
			std::string order = request::findValue(&req, "order");
			std::string rused = request::findValue(&req, "used");
			std::string rid = request::findValue(&req, "rid");
			std::string planid = request::findValue(&req, "plan");
			std::string floorid = request::findValue(&req, "floor");
			std::string sDisplayHidden = request::findValue(&req, "displayhidden");
			std::string sFetchFavorites = request::findValue(&req, "favorite");
			std::string sDisplayDisabled = request::findValue(&req, "displaydisabled");
			bool bDisplayHidden = (sDisplayHidden == "1");
			bool bFetchFavorites = (sFetchFavorites == "1");

			int HideDisabledHardwareSensors = 0;
			m_sql.GetPreferencesVar("HideDisabledHardwareSensors", HideDisabledHardwareSensors);
			bool bDisabledDisabled = (HideDisabledHardwareSensors == 0);
			if (sDisplayDisabled == "1")
				bDisabledDisabled = true;

			std::string sLastUpdate = request::findValue(&req, "lastupdate");
			std::string hwidx = request::findValue(&req, "hwidx"); // OTO

			time_t LastUpdate = 0;
			if (sLastUpdate != "")
			{
				std::stringstream sstr;
				sstr << sLastUpdate;
				sstr >> LastUpdate;
			}

			root["status"] = "OK";
			root["title"] = "Devices";

			GetJSonDevices(root, rused, rfilter, order, rid, planid, floorid, bDisplayHidden, bDisabledDisabled, bFetchFavorites, LastUpdate, session.username, hwidx);
		}

		void CWebServer::RType_Users(WebEmSession & session, const request& req, Json::Value &root)
		{
			bool bHaveUser = (session.username != "");
			int urights = 3;
			if (bHaveUser)
			{
				int iUser = FindUser(session.username.c_str());
				if (iUser != -1)
					urights = static_cast<int>(m_users[iUser].userrights);
			}
			if (urights < 2)
				return;

			root["status"] = "OK";
			root["title"] = "Users";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Active, Username, Password, Rights, RemoteSharing, TabsEnabled FROM USERS ORDER BY ID ASC");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Enabled"] = (sd[1] == "1") ? "true" : "false";
					root["result"][ii]["Username"] = base64_decode(sd[2]);
					root["result"][ii]["Password"] = sd[3];
					root["result"][ii]["Rights"] = atoi(sd[4].c_str());
					root["result"][ii]["RemoteSharing"] = atoi(sd[5].c_str());
					root["result"][ii]["TabsEnabled"] = atoi(sd[6].c_str());
					ii++;
				}
			}
		}

		void CWebServer::RType_Mobiles(WebEmSession & session, const request& req, Json::Value &root)
		{
			bool bHaveUser = (session.username != "");
			int urights = 3;
			if (bHaveUser)
			{
				int iUser = FindUser(session.username.c_str());
				if (iUser != -1)
					urights = static_cast<int>(m_users[iUser].userrights);
			}
			if (urights < 2)
				return;

			root["status"] = "OK";
			root["title"] = "Mobiles";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Active, Name, UUID, LastUpdate, DeviceType FROM MobileDevices ORDER BY Name ASC");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Enabled"] = (sd[1] == "1") ? "true" : "false";
					root["result"][ii]["Name"] = sd[2];
					root["result"][ii]["UUID"] = sd[3];
					root["result"][ii]["LastUpdate"] = sd[4];
					root["result"][ii]["DeviceType"] = sd[5];
					ii++;
				}
			}
		}

		void CWebServer::Cmd_SetSetpoint(WebEmSession & session, const request& req, Json::Value &root)
		{
			bool bHaveUser = (session.username != "");
			int iUser = -1;
			int urights = 3;
			if (bHaveUser)
			{
				iUser = FindUser(session.username.c_str());
				if (iUser != -1)
				{
					urights = static_cast<int>(m_users[iUser].userrights);
				}
			}
			if (urights < 1)
				return;

			std::string idx = request::findValue(&req, "idx");
			std::string setpoint = request::findValue(&req, "setpoint");
			if (
				(idx.empty()) ||
				(setpoint.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "SetSetpoint";
			if (iUser != -1)
			{
				_log.Log(LOG_STATUS, "User: %s initiated a SetPoint command", m_users[iUser].Username.c_str());
			}
			m_mainworker.SetSetPoint(idx, static_cast<float>(atof(setpoint.c_str())));
		}

		void CWebServer::Cmd_GetSceneActivations(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;

			root["status"] = "OK";
			root["title"] = "GetSceneActivations";

			std::vector<std::vector<std::string> > result, result2;
			result = m_sql.safe_query("SELECT Activators, SceneType FROM Scenes WHERE (ID==%q)", idx.c_str());
			if (result.empty())
				return;
			int ii = 0;
			std::string Activators = result[0][0];
			int SceneType = atoi(result[0][1].c_str());
			if (!Activators.empty())
			{
				//Get Activator device names
				std::vector<std::string> arrayActivators;
				StringSplit(Activators, ";", arrayActivators);
				std::vector<std::string>::const_iterator ittAct;
				for (ittAct = arrayActivators.begin(); ittAct != arrayActivators.end(); ++ittAct)
				{
					std::string sCodeCmd = *ittAct;

					std::vector<std::string> arrayCode;
					StringSplit(sCodeCmd, ":", arrayCode);

					std::string sID = arrayCode[0];
					int sCode = 0;
					if (arrayCode.size() == 2)
					{
						sCode = atoi(arrayCode[1].c_str());
					}


					result2 = m_sql.safe_query("SELECT Name, [Type], SubType, SwitchType FROM DeviceStatus WHERE (ID==%q)", sID.c_str());
					if (result2.size() > 0)
					{
						std::vector<std::string> sd = result2[0];
						std::string lstatus = "-";
						if ((SceneType == 0) && (arrayCode.size() == 2))
						{
							unsigned char devType = (unsigned char)atoi(sd[1].c_str());
							unsigned char subType = (unsigned char)atoi(sd[2].c_str());
							_eSwitchType switchtype = (_eSwitchType)atoi(sd[3].c_str());
							int nValue = sCode;
							std::string sValue = "";
							int llevel = 0;
							bool bHaveDimmer = false;
							bool bHaveGroupCmd = false;
							int maxDimLevel = 0;
							GetLightStatus(devType, subType, switchtype, nValue, sValue, lstatus, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);
						}
						std::stringstream sstr;
						uint64_t dID;
						sstr << sID;
						sstr >> dID;
						root["result"][ii]["idx"] = dID;
						root["result"][ii]["name"] = sd[0];
						root["result"][ii]["code"] = sCode;
						root["result"][ii]["codestr"] = lstatus;
						ii++;
					}
				}
			}
		}

		void CWebServer::Cmd_AddSceneCode(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sceneidx = request::findValue(&req, "sceneidx");
			std::string idx = request::findValue(&req, "idx");
			std::string cmnd = request::findValue(&req, "cmnd");
			if (
				(sceneidx.empty()) ||
				(idx.empty()) ||
				(cmnd.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "AddSceneCode";

			//First check if we do not already have this device as activation code
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Activators, SceneType FROM Scenes WHERE (ID==%q)", sceneidx.c_str());
			if (result.empty())
				return;
			std::string Activators = result[0][0];
			unsigned char scenetype = atoi(result[0][1].c_str());

			if (!Activators.empty())
			{
				//Get Activator device names
				std::vector<std::string> arrayActivators;
				StringSplit(Activators, ";", arrayActivators);
				std::vector<std::string>::const_iterator ittAct;
				for (ittAct = arrayActivators.begin(); ittAct != arrayActivators.end(); ++ittAct)
				{
					std::string sCodeCmd = *ittAct;

					std::vector<std::string> arrayCode;
					StringSplit(sCodeCmd, ":", arrayCode);

					std::string sID = arrayCode[0];
					std::string sCode = "";
					if (arrayCode.size() == 2)
					{
						sCode = arrayCode[1];
					}

					if (sID == idx)
					{
						if (scenetype == 1)
							return; //Group does not work with separate codes, so already there
						if (sCode == cmnd)
							return; //same code, already there!
					}
				}
			}
			if (!Activators.empty())
				Activators += ";";
			Activators += idx;
			if (scenetype == 0)
			{
				Activators += ":" + cmnd;
			}
			m_sql.safe_query("UPDATE Scenes SET Activators='%q' WHERE (ID==%q)", Activators.c_str(), sceneidx.c_str());
		}

		void CWebServer::Cmd_RemoveSceneCode(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sceneidx = request::findValue(&req, "sceneidx");
			std::string idx = request::findValue(&req, "idx");
			std::string code = request::findValue(&req, "code");
			if (
				(idx.empty()) ||
				(sceneidx.empty()) ||
				(code.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "RemoveSceneCode";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Activators, SceneType FROM Scenes WHERE (ID==%q)", sceneidx.c_str());
			if (result.empty())
				return;
			std::string Activators = result[0][0];
			int SceneType = atoi(result[0][1].c_str());
			if (!Activators.empty())
			{
				//Get Activator device names
				std::vector<std::string> arrayActivators;
				StringSplit(Activators, ";", arrayActivators);
				std::vector<std::string>::const_iterator ittAct;
				std::string newActivation = "";
				for (ittAct = arrayActivators.begin(); ittAct != arrayActivators.end(); ++ittAct)
				{
					std::string sCodeCmd = *ittAct;

					std::vector<std::string> arrayCode;
					StringSplit(sCodeCmd, ":", arrayCode);

					std::string sID = arrayCode[0];
					std::string sCode = "";
					if (arrayCode.size() == 2)
					{
						sCode = arrayCode[1];
					}
					bool bFound = false;
					if (sID == idx)
					{
						if ((SceneType == 1) || (sCode.empty()))
						{
							bFound = true;
						}
						else
						{
							//Also check the code
							bFound = (sCode == code);
						}
					}
					if (!bFound)
					{
						if (!newActivation.empty())
							newActivation += ";";
						newActivation += sID;
						if ((SceneType == 0) && (!sCode.empty()))
						{
							newActivation += ":" + sCode;
						}
					}
				}
				if (Activators != newActivation)
				{
					m_sql.safe_query("UPDATE Scenes SET Activators='%q' WHERE (ID==%q)", newActivation.c_str(), sceneidx.c_str());
				}
			}
		}

		void CWebServer::Cmd_ClearSceneCodes(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sceneidx = request::findValue(&req, "sceneidx");
			if (sceneidx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "ClearSceneCode";

			m_sql.safe_query("UPDATE Scenes SET Activators='' WHERE (ID==%q)", sceneidx.c_str());
		}

		void CWebServer::Cmd_GetSerialDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetSerialDevices";

			bool bUseDirectPath = false;
			std::vector<std::string> serialports = GetSerialPorts(bUseDirectPath);
			std::vector<std::string>::iterator itt;
			int ii = 0;
			for (itt = serialports.begin(); itt != serialports.end(); ++itt)
			{
				root["result"][ii]["name"] = *itt;
				root["result"][ii]["value"] = ii;
				ii++;
			}
		}

		void CWebServer::Cmd_GetDevicesList(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetDevicesList";
			int ii = 0;
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Name FROM DeviceStatus WHERE (Used == 1) ORDER BY Name");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["name"] = sd[1];
					root["result"][ii]["value"] = sd[0];
					ii++;
				}
			}
		}

		void CWebServer::Post_UploadCustomIcon(WebEmSession & session, const request& req, reply & rep)
		{
			Json::Value root;
			root["title"] = "UploadCustomIcon";
			root["status"] = "ERROR";
			root["error"] = "Invalid";
			//Only admin user allowed
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string zipfile = request::findValue(&req, "file");
			if (zipfile != "")
			{
				std::string ErrorMessage;
				bool bOK = m_sql.InsertCustomIconFromZip(zipfile, ErrorMessage);
				if (bOK)
				{
					root["status"] = "OK";
				}
				else
				{
					root["status"] = "ERROR";
					root["error"] = ErrorMessage;
				}
			}
			std::string jcallback = request::findValue(&req, "jsoncallback");
			if (jcallback.size() == 0) {
				reply::set_content(&rep, root.toStyledString());
				return;
			}
			reply::set_content(&rep, "var data=" + root.toStyledString() + "\n" + jcallback + "(data);");
		}

		void CWebServer::Cmd_GetCustomIconSet(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetCustomIconSet";
			int ii = 0;
			std::vector<_tCustomIcon>::const_iterator itt;
			for (itt = m_custom_light_icons.begin(); itt != m_custom_light_icons.end(); ++itt)
			{
				if (itt->idx >= 100)
				{
					std::string IconFile16 = "images/" + itt->RootFile + ".png";
					std::string IconFile48On = "images/" + itt->RootFile + "48_On.png";
					std::string IconFile48Off = "images/" + itt->RootFile + "48_Off.png";

					root["result"][ii]["idx"] = itt->idx - 100;
					root["result"][ii]["Title"] = itt->Title;
					root["result"][ii]["Description"] = itt->Description;
					root["result"][ii]["IconFile16"] = IconFile16;
					root["result"][ii]["IconFile48On"] = IconFile48On;
					root["result"][ii]["IconFile48Off"] = IconFile48Off;
					ii++;
				}
			}
		}

		void CWebServer::Cmd_DeleteCustomIcon(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sidx = request::findValue(&req, "idx");
			if (sidx.empty())
				return;
			int idx = atoi(sidx.c_str());
			root["status"] = "OK";
			root["title"] = "DeleteCustomIcon";

			m_sql.safe_query("DELETE FROM CustomImages WHERE (ID == %d)", idx);

			//Delete icons file from disk
			std::vector<_tCustomIcon>::const_iterator itt;
			for (itt = m_custom_light_icons.begin(); itt != m_custom_light_icons.end(); ++itt)
			{
				if (itt->idx == idx + 100)
				{
					std::string IconFile16 = szWWWFolder + "/images/" + itt->RootFile + ".png";
					std::string IconFile48On = szWWWFolder + "/images/" + itt->RootFile + "48_On.png";
					std::string IconFile48Off = szWWWFolder + "/images/" + itt->RootFile + "48_Off.png";
					std::remove(IconFile16.c_str());
					std::remove(IconFile48On.c_str());
					std::remove(IconFile48Off.c_str());
					break;
				}
			}
			ReloadCustomSwitchIcons();
		}

		void CWebServer::Cmd_UpdateCustomIcon(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sidx = request::findValue(&req, "idx");
			std::string sname = request::findValue(&req, "name");
			std::string sdescription = request::findValue(&req, "description");
			if (
				(sidx.empty()) ||
				(sname.empty()) ||
				(sdescription.empty())
				)
				return;

			int idx = atoi(sidx.c_str());
			root["status"] = "OK";
			root["title"] = "UpdateCustomIcon";

			m_sql.safe_query("UPDATE CustomImages SET Name='%q', Description='%q' WHERE (ID == %d)", sname.c_str(), sdescription.c_str(), idx);
			ReloadCustomSwitchIcons();
		}

		void CWebServer::Cmd_RenameDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sidx = request::findValue(&req, "idx");
			std::string sname = request::findValue(&req, "name");
			if (
				(sidx.empty()) ||
				(sname.empty())
				)
				return;
			int idx = atoi(sidx.c_str());
			root["status"] = "OK";
			root["title"] = "RenameDevice";

			m_sql.safe_query("UPDATE DeviceStatus SET Name='%q' WHERE (ID == %d)", sname.c_str(), idx);
			std::stringstream sstridx(sidx);
			uint64_t ullidx;
			sstridx >> ullidx;
			m_mainworker.m_eventsystem.WWWUpdateSingleState(ullidx, sname, m_mainworker.m_eventsystem.REASON_DEVICE);

#ifdef ENABLE_PYTHON
			// Notify plugin framework about the change
			m_mainworker.m_pluginsystem.DeviceModified(idx);
#endif
		}

		void CWebServer::Cmd_RenameScene(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sidx = request::findValue(&req, "idx");
			std::string sname = request::findValue(&req, "name");
			if (
				(sidx.empty()) ||
				(sname.empty())
				)
				return;
			int idx = atoi(sidx.c_str());
			root["status"] = "OK";
			root["title"] = "RenameScene";

			m_sql.safe_query("UPDATE Scenes SET Name='%q' WHERE (ID == %d)", sname.c_str(), idx);
			std::stringstream sstridx(sidx);
			uint64_t ullidx;
			sstridx >> ullidx;
			m_mainworker.m_eventsystem.WWWUpdateSingleState(ullidx, sname, m_mainworker.m_eventsystem.REASON_SCENEGROUP);
		}

		void CWebServer::Cmd_SetUnused(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string sidx = request::findValue(&req, "idx");
			if (sidx.empty())
				return;
			int idx = atoi(sidx.c_str());
			root["status"] = "OK";
			root["title"] = "SetUnused";
			m_sql.safe_query("UPDATE DeviceStatus SET Used=0 WHERE (ID == %d)", idx);
			if (m_sql.m_bEnableEventSystem)
				m_mainworker.m_eventsystem.RemoveSingleState(idx, m_mainworker.m_eventsystem.REASON_DEVICE);

#ifdef ENABLE_PYTHON
			// Notify plugin framework about the change
			m_mainworker.m_pluginsystem.DeviceModified(idx);
#endif
		}

		void CWebServer::Cmd_AddLogMessage(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string smessage = request::findValue(&req, "message");
			if (smessage.empty())
				return;
			root["status"] = "OK";
			root["title"] = "AddLogMessage";

			_log.Log(LOG_STATUS, "%s", smessage.c_str());
		}

		void CWebServer::Cmd_ClearShortLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			root["status"] = "OK";
			root["title"] = "ClearShortLog";

			_log.Log(LOG_STATUS, "Clearing Short Log...");

			m_sql.ClearShortLog();

			_log.Log(LOG_STATUS, "Short Log Cleared!");
		}

		void CWebServer::Cmd_VacuumDatabase(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			root["status"] = "OK";
			root["title"] = "VacuumDatabase";

			m_sql.VacuumDatabase();
		}

		void CWebServer::Cmd_AddMobileDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string suuid = request::findValue(&req, "uuid");
			std::string ssenderid = request::findValue(&req, "senderid");
			std::string sname = request::findValue(&req, "name");
			std::string sdevtype = request::findValue(&req, "devicetype");
			std::string sactive = request::findValue(&req, "active");
			if (
				(suuid.empty()) ||
				(ssenderid.empty())
				)
				return;
			root["status"] = "OK";
			root["title"] = "AddMobileDevice";

			if (sactive.empty())
				sactive = "1";
			int iActive = (sactive == "1") ? 1 : 0;

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Name, DeviceType FROM MobileDevices WHERE (UUID=='%q')", suuid.c_str());
			if (result.empty())
			{
				//New
				m_sql.safe_query("INSERT INTO MobileDevices (Active,UUID,SenderID,Name,DeviceType) VALUES (%d,'%q','%q','%q','%q')",
					iActive,
					suuid.c_str(),
					ssenderid.c_str(),
					sname.c_str(),
					sdevtype.c_str());
			}
			else
			{
				//Update
				time_t now = mytime(NULL);
				struct tm ltime;
				localtime_r(&now, &ltime);
				m_sql.safe_query("UPDATE MobileDevices SET Active=%d, SenderID='%q', LastUpdate='%04d-%02d-%02d %02d:%02d:%02d' WHERE (UUID == '%q')",
					iActive,
					ssenderid.c_str(),
					ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec,
					suuid.c_str()
				);

				std::string dname = result[0][1];
				std::string ddevtype = result[0][2];
				if (dname.empty() || ddevtype.empty())
				{
					m_sql.safe_query("UPDATE MobileDevices SET Name='%q', DeviceType='%q' WHERE (UUID == '%q')",
						sname.c_str(), sdevtype.c_str(),
						suuid.c_str()
					);
				}
			}
		}

		void CWebServer::Cmd_UpdateMobileDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string sidx = request::findValue(&req, "idx");
			std::string enabled = request::findValue(&req, "enabled");
			std::string name = request::findValue(&req, "name");

			if (
				(sidx.empty()) ||
				(enabled.empty()) ||
				(name.empty())
				)
				return;
			uint64_t idx = 0;
			std::stringstream s_str(sidx);
			s_str >> idx;

			m_sql.safe_query("UPDATE MobileDevices SET Name='%q', Active=%d WHERE (ID==%" PRIu64 ")",
				name.c_str(), (enabled == "true") ? 1 : 0, idx);

			root["status"] = "OK";
			root["title"] = "UpdateMobile";
		}

		void CWebServer::Cmd_DeleteMobileDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string suuid = request::findValue(&req, "uuid");
			if (suuid.empty())
				return;
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID FROM MobileDevices WHERE (UUID=='%q')", suuid.c_str());
			if (result.empty())
				return;
			m_sql.safe_query("DELETE FROM MobileDevices WHERE (UUID == '%q')", suuid.c_str());
			root["status"] = "OK";
			root["title"] = "DeleteMobileDevice";
		}


		void CWebServer::RType_GetTransfers(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "GetTransfers";

			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				std::stringstream s_str(request::findValue(&req, "idx"));
				s_str >> idx;
			}

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Type, SubType FROM DeviceStatus WHERE (ID==%" PRIu64 ")",
				idx);
			if (result.size() > 0)
			{
				int dType = atoi(result[0][0].c_str());
				if (
					(dType == pTypeTEMP) ||
					(dType == pTypeTEMP_HUM)
					)
				{
					result = m_sql.safe_query(
						"SELECT ID, Name FROM DeviceStatus WHERE (Type=='%q') AND (ID!=%" PRIu64 ")",
						result[0][0].c_str(), idx);
				}
				else
				{
					result = m_sql.safe_query(
						"SELECT ID, Name FROM DeviceStatus WHERE (Type=='%q') AND (SubType=='%q') AND (ID!=%" PRIu64 ")",
						result[0][0].c_str(), result[0][1].c_str(), idx);
				}

				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					ii++;
				}
			}
		}

		//Will transfer Newest sensor log to OLD sensor,
		//then set the HardwareID/DeviceID/Unit/Name/Type/Subtype/Unit for the OLD sensor to the NEW sensor ID/Type/Subtype/Unit
		//then delete the NEW sensor
		void CWebServer::RType_TransferDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sidx = request::findValue(&req, "idx");
			if (sidx.empty())
				return;

			std::string newidx = request::findValue(&req, "newidx");
			if (newidx.empty())
				return;

			std::vector<std::vector<std::string> > result;

			//Check which device is newer

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			struct tm LastUpdateTime_A;
			struct tm LastUpdateTime_B;

			result = m_sql.safe_query(
				"SELECT A.LastUpdate, B.LastUpdate FROM DeviceStatus as A, DeviceStatus as B WHERE (A.ID == '%q') AND (B.ID == '%q')",
				sidx.c_str(), newidx.c_str());
			if (result.size() < 1)
				return;

			std::string sLastUpdate_A = result[0][0];
			std::string sLastUpdate_B = result[0][1];

			time_t timeA, timeB;
			ParseSQLdatetime(timeA, LastUpdateTime_A, sLastUpdate_A, tm1.tm_isdst);
			ParseSQLdatetime(timeB, LastUpdateTime_B, sLastUpdate_B, tm1.tm_isdst);

			if (timeA < timeB)
			{
				//Swap idx with newidx
				sidx.swap(newidx);
			}

			result = m_sql.safe_query(
				"SELECT HardwareID, DeviceID, Unit, Name, Type, SubType, SignalLevel, BatteryLevel, nValue, sValue FROM DeviceStatus WHERE (ID == '%q')",
				newidx.c_str());
			if (result.size() < 1)
				return;

			root["status"] = "OK";
			root["title"] = "TransferDevice";

			//transfer device logs (new to old)
			m_sql.TransferDevice(newidx, sidx);

			//now delete the NEW device
			m_sql.DeleteDevices(newidx);

			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::RType_Notifications(WebEmSession & session, const request& req, Json::Value &root)
		{
			root["status"] = "OK";
			root["title"] = "Notifications";

			int ii = 0;

			//Add known notification systems
			std::map<std::string, CNotificationBase*>::const_iterator ittNotifiers;
			for (ittNotifiers = m_notifications.m_notifiers.begin(); ittNotifiers != m_notifications.m_notifiers.end(); ++ittNotifiers)
			{
				root["notifiers"][ii]["name"] = ittNotifiers->first;
				root["notifiers"][ii]["description"] = ittNotifiers->first;
				ii++;
			}

			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				std::stringstream s_str(request::findValue(&req, "idx"));
				s_str >> idx;
			}
			std::vector<_tNotification> notifications = m_notifications.GetNotifications(idx);
			if (notifications.size() > 0)
			{
				std::vector<_tNotification>::const_iterator itt;
				ii = 0;
				for (itt = notifications.begin(); itt != notifications.end(); ++itt)
				{
					root["result"][ii]["idx"] = itt->ID;
					std::string sParams = itt->Params;
					if (sParams.empty()) {
						sParams = "S";
					}
					root["result"][ii]["Params"] = sParams;
					root["result"][ii]["Priority"] = itt->Priority;
					root["result"][ii]["SendAlways"] = itt->SendAlways;
					root["result"][ii]["CustomMessage"] = itt->CustomMessage;
					root["result"][ii]["ActiveSystems"] = itt->ActiveSystems;
					ii++;
				}
			}
		}

		void CWebServer::RType_GetSharedUserDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "GetSharedUserDevices";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT DeviceRowID FROM SharedDevices WHERE (SharedUserID == '%q')", idx.c_str());
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["DeviceRowIdx"] = sd[0];
					ii++;
				}
			}
		}

		void CWebServer::RType_SetSharedUserDevices(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = request::findValue(&req, "idx");
			std::string userdevices = request::findValue(&req, "devices");
			if (idx.empty())
				return;
			root["status"] = "OK";
			root["title"] = "SetSharedUserDevices";
			std::vector<std::string> strarray;
			StringSplit(userdevices, ";", strarray);

			//First delete all devices for this user, then add the (new) onces
			m_sql.safe_query("DELETE FROM SharedDevices WHERE (SharedUserID == '%q')", idx.c_str());

			int nDevices = static_cast<int>(strarray.size());
			for (int ii = 0; ii < nDevices; ii++)
			{
				m_sql.safe_query("INSERT INTO SharedDevices (SharedUserID,DeviceRowID) VALUES ('%q','%q')", idx.c_str(), strarray[ii].c_str());
			}
			LoadUsers();
		}

		void CWebServer::RType_SetUsed(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string deviceid = request::findValue(&req, "deviceid");
			std::string name = request::findValue(&req, "name");
			std::string description = request::findValue(&req, "description");
			std::string sused = request::findValue(&req, "used");
			std::string sswitchtype = request::findValue(&req, "switchtype");
			std::string maindeviceidx = request::findValue(&req, "maindeviceidx");
			std::string addjvalue = request::findValue(&req, "addjvalue");
			std::string addjmulti = request::findValue(&req, "addjmulti");
			std::string addjvalue2 = request::findValue(&req, "addjvalue2");
			std::string addjmulti2 = request::findValue(&req, "addjmulti2");
			std::string setPoint = request::findValue(&req, "setpoint");
			std::string state = request::findValue(&req, "state");
			std::string mode = request::findValue(&req, "mode");
			std::string until = request::findValue(&req, "until");
			std::string clock = request::findValue(&req, "clock");
			std::string tmode = request::findValue(&req, "tmode");
			std::string fmode = request::findValue(&req, "fmode");
			std::string sCustomImage = request::findValue(&req, "customimage");

			std::string strunit = request::findValue(&req, "unit");
			std::string strParam1 = base64_decode(request::findValue(&req, "strparam1"));
			std::string strParam2 = base64_decode(request::findValue(&req, "strparam2"));
			std::string tmpstr = request::findValue(&req, "protected");
			bool bHasstrParam1 = request::hasValue(&req, "strparam1");
			int iProtected = (tmpstr == "true") ? 1 : 0;

			std::string sOptions = CURLEncode::URLDecode(base64_decode(request::findValue(&req, "options")));
			std::string devoptions = CURLEncode::URLDecode(request::findValue(&req, "devoptions"));

			char szTmp[200];

			bool bHaveUser = (session.username != "");
			int iUser = -1;
			if (bHaveUser)
			{
				iUser = FindUser(session.username.c_str());
			}

			int switchtype = -1;
			if (sswitchtype != "")
				switchtype = atoi(sswitchtype.c_str());

			if ((idx.empty()) || (sused.empty()))
				return;
			int used = (sused == "true") ? 1 : 0;
			if (maindeviceidx != "")
				used = 0;

			int CustomImage = 0;
			if (sCustomImage != "")
				CustomImage = atoi(sCustomImage.c_str());

			//Strip trailing spaces in 'name'
			name = stdstring_trim(name);

			//Strip trailing spaces in 'description'
			description = stdstring_trim(description);

			std::vector<std::vector<std::string> > result;

			result = m_sql.safe_query("SELECT Type,SubType,HardwareID FROM DeviceStatus WHERE (ID == '%q')", idx.c_str());
			if (result.size() < 1)
				return;
			std::vector<std::string> sd = result[0];

			unsigned char dType = atoi(sd[0].c_str());
			//unsigned char dSubType=atoi(sd[1].c_str());
			int HwdID = atoi(sd[2].c_str());
			std::string sHwdID = sd[2];

			if (setPoint != "" || state != "")
			{
				double tempcelcius = atof(setPoint.c_str());
				if (m_sql.m_tempunit == TEMPUNIT_F)
				{
					//Convert back to Celsius
					tempcelcius = ConvertToCelsius(tempcelcius);
				}
				sprintf(szTmp, "%.2f", tempcelcius);

				if (dType != pTypeEvohomeZone && dType != pTypeEvohomeWater)//sql update now done in setsetpoint for evohome devices
				{
					m_sql.safe_query("UPDATE DeviceStatus SET Used=%d, sValue='%q' WHERE (ID == '%q')",
						used, szTmp, idx.c_str());
				}
			}
			if (name.empty())
			{
				m_sql.safe_query("UPDATE DeviceStatus SET Used=%d WHERE (ID == '%q')",
					used, idx.c_str());
			}
			else
			{
				if (switchtype == -1)
				{
					m_sql.safe_query("UPDATE DeviceStatus SET Used=%d, Name='%q', Description='%q' WHERE (ID == '%q')",
						used, name.c_str(), description.c_str(), idx.c_str());
				}
				else
				{
					m_sql.safe_query(
						"UPDATE DeviceStatus SET Used=%d, Name='%q', Description='%q', SwitchType=%d, CustomImage=%d WHERE (ID == '%q')",
						used, name.c_str(), description.c_str(), switchtype, CustomImage, idx.c_str());
				}
			}

			if (bHasstrParam1)
			{
				m_sql.safe_query("UPDATE DeviceStatus SET StrParam1='%q', StrParam2='%q' WHERE (ID == '%q')",
					strParam1.c_str(), strParam2.c_str(), idx.c_str());
			}

			m_sql.safe_query("UPDATE DeviceStatus SET Protected=%d WHERE (ID == '%q')", iProtected, idx.c_str());

			if (!setPoint.empty() || !state.empty())
			{
				int urights = 3;
				if (bHaveUser)
				{
					int iUser = FindUser(session.username.c_str());
					if (iUser != -1)
					{
						urights = static_cast<int>(m_users[iUser].userrights);
						_log.Log(LOG_STATUS, "User: %s initiated a SetPoint command", m_users[iUser].Username.c_str());
					}
				}
				if (urights < 1)
					return;
				if (dType == pTypeEvohomeWater)
					m_mainworker.SetSetPoint(idx, (state == "On") ? 1.0f : 0.0f, mode, until);//FIXME float not guaranteed precise?
				else if (dType == pTypeEvohomeZone)
					m_mainworker.SetSetPoint(idx, static_cast<float>(atof(setPoint.c_str())), mode, until);
				else
					m_mainworker.SetSetPoint(idx, static_cast<float>(atof(setPoint.c_str())));
			}
			else if (!clock.empty())
			{
				int urights = 3;
				if (bHaveUser)
				{
					int iUser = FindUser(session.username.c_str());
					if (iUser != -1)
					{
						urights = static_cast<int>(m_users[iUser].userrights);
						_log.Log(LOG_STATUS, "User: %s initiated a SetClock command", m_users[iUser].Username.c_str());
					}
				}
				if (urights < 1)
					return;
				m_mainworker.SetClock(idx, clock);
			}
			else if (!tmode.empty())
			{
				int urights = 3;
				if (bHaveUser)
				{
					int iUser = FindUser(session.username.c_str());
					if (iUser != -1)
					{
						urights = static_cast<int>(m_users[iUser].userrights);
						_log.Log(LOG_STATUS, "User: %s initiated a Thermostat Mode command", m_users[iUser].Username.c_str());
					}
				}
				if (urights < 1)
					return;
				m_mainworker.SetZWaveThermostatMode(idx, atoi(tmode.c_str()));
			}
			else if (!fmode.empty())
			{
				int urights = 3;
				if (bHaveUser)
				{
					int iUser = FindUser(session.username.c_str());
					if (iUser != -1)
					{
						urights = static_cast<int>(m_users[iUser].userrights);
						_log.Log(LOG_STATUS, "User: %s initiated a Thermostat Fan Mode command", m_users[iUser].Username.c_str());
					}
				}
				if (urights < 1)
					return;
				m_mainworker.SetZWaveThermostatFanMode(idx, atoi(fmode.c_str()));
			}

			if (!strunit.empty())
			{
				bool bUpdateUnit = true;
#ifdef ENABLE_PYTHON
				//check if HW is plugin
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT Type FROM Hardware WHERE (ID == %d)", HwdID);
				if (result.size() > 0)
				{
					std::vector<std::string> sd = result[0];
					_eHardwareTypes Type = (_eHardwareTypes)atoi(sd[0].c_str());
					if (Type == HTYPE_PythonPlugin)
					{
						bUpdateUnit = false;
						_log.Log(LOG_ERROR, "CWebServer::RType_SetUsed: Not allowed to change unit of device owned by plugin %u!", HwdID);
					}
				}
#endif
				if (bUpdateUnit)
				{
					m_sql.safe_query("UPDATE DeviceStatus SET Unit='%q' WHERE (ID == '%q')",
						strunit.c_str(), idx.c_str());
				}
			}
			//FIXME evohome ...we need the zone id to update the correct zone...but this should be ok as a generic call?
			if (!deviceid.empty())
			{
				m_sql.safe_query("UPDATE DeviceStatus SET DeviceID='%q' WHERE (ID == '%q')",
					deviceid.c_str(), idx.c_str());
			}
			if (!addjvalue.empty())
			{
				double faddjvalue = atof(addjvalue.c_str());
				m_sql.safe_query("UPDATE DeviceStatus SET AddjValue=%f WHERE (ID == '%q')",
					faddjvalue, idx.c_str());
			}
			if (!addjmulti.empty())
			{
				double faddjmulti = atof(addjmulti.c_str());
				if (faddjmulti == 0)
					faddjmulti = 1;
				m_sql.safe_query("UPDATE DeviceStatus SET AddjMulti=%f WHERE (ID == '%q')",
					faddjmulti, idx.c_str());
			}
			if (!addjvalue2.empty())
			{
				double faddjvalue2 = atof(addjvalue2.c_str());
				m_sql.safe_query("UPDATE DeviceStatus SET AddjValue2=%f WHERE (ID == '%q')",
					faddjvalue2, idx.c_str());
			}
			if (!addjmulti2.empty())
			{
				double faddjmulti2 = atof(addjmulti2.c_str());
				if (faddjmulti2 == 0)
					faddjmulti2 = 1;
				m_sql.safe_query("UPDATE DeviceStatus SET AddjMulti2=%f WHERE (ID == '%q')",
					faddjmulti2, idx.c_str());
			}
			if (!devoptions.empty())
			{
				m_sql.safe_query("UPDATE DeviceStatus SET Options='%q' WHERE (ID == '%q')", devoptions.c_str(), idx.c_str());
			}

			if (used == 0)
			{
				bool bRemoveSubDevices = (request::findValue(&req, "RemoveSubDevices") == "true");

				if (bRemoveSubDevices)
				{
					//if this device was a slave device, remove it
					m_sql.safe_query("DELETE FROM LightSubDevices WHERE (DeviceRowID == '%q')", idx.c_str());
				}
				m_sql.safe_query("DELETE FROM LightSubDevices WHERE (ParentID == '%q')", idx.c_str());

				m_sql.safe_query("DELETE FROM Timers WHERE (DeviceRowID == '%q')", idx.c_str());
			}

			// Save device options
			if (!sOptions.empty())
			{
				std::stringstream sstridx(idx);
				uint64_t ullidx;
				sstridx >> ullidx;
				m_sql.SetDeviceOptions(ullidx, m_sql.BuildDeviceOptions(sOptions, false));
			}

			if (maindeviceidx != "")
			{
				if (maindeviceidx != idx)
				{
					//this is a sub device for another light/switch
					//first check if it is not already a sub device
					result = m_sql.safe_query("SELECT ID FROM LightSubDevices WHERE (DeviceRowID=='%q') AND (ParentID =='%q')",
						idx.c_str(), maindeviceidx.c_str());
					if (result.size() == 0)
					{
						//no it is not, add it
						m_sql.safe_query(
							"INSERT INTO LightSubDevices (DeviceRowID, ParentID) VALUES ('%q','%q')",
							idx.c_str(),
							maindeviceidx.c_str()
						);
					}
				}
			}
			if ((used == 0) && (maindeviceidx.empty()))
			{
				//really remove it, including log etc
				m_sql.DeleteDevices(idx);
			}
			else
			{
#ifdef ENABLE_PYTHON
				// Notify plugin framework about the change
				m_mainworker.m_pluginsystem.DeviceModified(atoi(idx.c_str()));
#endif
			}
			if (result.size() > 0)
			{
				root["status"] = "OK";
				root["title"] = "SetUsed";
			}
			if (m_sql.m_bEnableEventSystem)
				m_mainworker.m_eventsystem.GetCurrentStates();
		}

		void CWebServer::RType_Settings(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::vector<std::vector<std::string> > result;
			char szTmp[100];

			result = m_sql.safe_query("SELECT Key, nValue, sValue FROM Preferences");
			if (result.empty())
				return;
			root["status"] = "OK";
			root["title"] = "settings";
#ifndef NOCLOUD
			root["cloudenabled"] = true;
#else
			root["cloudenabled"] = false;
#endif

			std::vector<std::vector<std::string> >::const_iterator itt;
			for (itt = result.begin(); itt != result.end(); ++itt)
			{
				std::vector<std::string> sd = *itt;
				std::string Key = sd[0];
				int nValue = atoi(sd[1].c_str());
				std::string sValue = sd[2];

				if (Key == "Location")
				{
					std::vector<std::string> strarray;
					StringSplit(sValue, ";", strarray);

					if (strarray.size() == 2)
					{
						root["Location"]["Latitude"] = strarray[0];
						root["Location"]["Longitude"] = strarray[1];
					}
				}
				/* RK: notification settings */
				if (m_notifications.IsInConfig(Key)) {
					if (sValue.empty() && nValue > 0) {
						root[Key] = nValue;
					}
					else {
						root[Key] = sValue;
					}
				}
				else if (Key == "DashboardType")
				{
					root["DashboardType"] = nValue;
				}
				else if (Key == "MobileType")
				{
					root["MobileType"] = nValue;
				}
				else if (Key == "LightHistoryDays")
				{
					root["LightHistoryDays"] = nValue;
				}
				else if (Key == "5MinuteHistoryDays")
				{
					root["ShortLogDays"] = nValue;
				}
				else if (Key == "ShortLogInterval")
				{
					root["ShortLogInterval"] = nValue;
				}
				else if (Key == "WebUserName")
				{
					root["WebUserName"] = base64_decode(sValue);
				}
				else if (Key == "WebPassword")
				{
					root["WebPassword"] = sValue;
				}
				else if (Key == "SecPassword")
				{
					root["SecPassword"] = sValue;
				}
				else if (Key == "ProtectionPassword")
				{
					root["ProtectionPassword"] = sValue;
				}
				else if (Key == "WebLocalNetworks")
				{
					root["WebLocalNetworks"] = sValue;
				}
				else if (Key == "WebRemoteProxyIPs")
				{
					root["WebRemoteProxyIPs"] = sValue;
				}
				else if (Key == "RandomTimerFrame")
				{
					root["RandomTimerFrame"] = nValue;
				}
				else if (Key == "MeterDividerEnergy")
				{
					root["EnergyDivider"] = nValue;
				}
				else if (Key == "MeterDividerGas")
				{
					root["GasDivider"] = nValue;
				}
				else if (Key == "MeterDividerWater")
				{
					root["WaterDivider"] = nValue;
				}
				else if (Key == "ElectricVoltage")
				{
					root["ElectricVoltage"] = nValue;
				}
				else if (Key == "CM113DisplayType")
				{
					root["CM113DisplayType"] = nValue;
				}
				else if (Key == "UseAutoUpdate")
				{
					root["UseAutoUpdate"] = nValue;
				}
				else if (Key == "UseAutoBackup")
				{
					root["UseAutoBackup"] = nValue;
				}
				else if (Key == "Rego6XXType")
				{
					root["Rego6XXType"] = nValue;
				}
				else if (Key == "CostEnergy")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostEnergy"] = szTmp;
				}
				else if (Key == "CostEnergyT2")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostEnergyT2"] = szTmp;
				}
				else if (Key == "CostEnergyR1")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostEnergyR1"] = szTmp;
				}
				else if (Key == "CostEnergyR2")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostEnergyR2"] = szTmp;
				}
				else if (Key == "CostGas")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostGas"] = szTmp;
				}
				else if (Key == "CostWater")
				{
					sprintf(szTmp, "%.4f", (float)(nValue) / 10000.0f);
					root["CostWater"] = szTmp;
				}
				else if (Key == "ActiveTimerPlan")
				{
					root["ActiveTimerPlan"] = nValue;
				}
				else if (Key == "DoorbellCommand")
				{
					root["DoorbellCommand"] = nValue;
				}
				else if (Key == "SmartMeterType")
				{
					root["SmartMeterType"] = nValue;
				}
				else if (Key == "EnableTabFloorplans")
				{
					root["EnableTabFloorplans"] = nValue;
				}
				else if (Key == "EnableTabLights")
				{
					root["EnableTabLights"] = nValue;
				}
				else if (Key == "EnableTabTemp")
				{
					root["EnableTabTemp"] = nValue;
				}
				else if (Key == "EnableTabWeather")
				{
					root["EnableTabWeather"] = nValue;
				}
				else if (Key == "EnableTabUtility")
				{
					root["EnableTabUtility"] = nValue;
				}
				else if (Key == "EnableTabScenes")
				{
					root["EnableTabScenes"] = nValue;
				}
				else if (Key == "EnableTabCustom")
				{
					root["EnableTabCustom"] = nValue;
				}
				else if (Key == "NotificationSensorInterval")
				{
					root["NotificationSensorInterval"] = nValue;
				}
				else if (Key == "NotificationSwitchInterval")
				{
					root["NotificationSwitchInterval"] = nValue;
				}
				else if (Key == "RemoteSharedPort")
				{
					root["RemoteSharedPort"] = nValue;
				}
				else if (Key == "Language")
				{
					root["Language"] = sValue;
				}
				else if (Key == "Title")
				{
					root["Title"] = sValue;
				}
				else if (Key == "WindUnit")
				{
					root["WindUnit"] = nValue;
				}
				else if (Key == "TempUnit")
				{
					root["TempUnit"] = nValue;
				}
				else if (Key == "WeightUnit")
				{
					root["WeightUnit"] = nValue;
				}
				else if (Key == "AuthenticationMethod")
				{
					root["AuthenticationMethod"] = nValue;
				}
				else if (Key == "ReleaseChannel")
				{
					root["ReleaseChannel"] = nValue;
				}
				else if (Key == "RaspCamParams")
				{
					root["RaspCamParams"] = sValue;
				}
				else if (Key == "UVCParams")
				{
					root["UVCParams"] = sValue;
				}
				else if (Key == "AcceptNewHardware")
				{
					root["AcceptNewHardware"] = nValue;
				}
				else if (Key == "HideDisabledHardwareSensors")
				{
					root["HideDisabledHardwareSensors"] = nValue;
				}
				else if (Key == "ShowUpdateEffect")
				{
					root["ShowUpdateEffect"] = nValue;
				}
				else if (Key == "DegreeDaysBaseTemperature")
				{
					root["DegreeDaysBaseTemperature"] = sValue;
				}
				else if (Key == "EnableEventScriptSystem")
				{
					root["EnableEventScriptSystem"] = nValue;
				}
				else if (Key == "DisableDzVentsSystem")
				{
					root["DisableDzVentsSystem"] = nValue;
				}
				else if (Key == "DzVentsLogLevel")
				{
					root["DzVentsLogLevel"] = nValue;
				}
				else if (Key == "LogEventScriptTrigger")
				{
					root["LogEventScriptTrigger"] = nValue;
				}
				else if (Key == "(1WireSensorPollPeriod")
				{
					root["1WireSensorPollPeriod"] = nValue;
				}
				else if (Key == "(1WireSwitchPollPeriod")
				{
					root["1WireSwitchPollPeriod"] = nValue;
				}
				else if (Key == "SecOnDelay")
				{
					root["SecOnDelay"] = nValue;
				}
				else if (Key == "AllowWidgetOrdering")
				{
					root["AllowWidgetOrdering"] = nValue;
				}
				else if (Key == "FloorplanPopupDelay")
				{
					root["FloorplanPopupDelay"] = nValue;
				}
				else if (Key == "FloorplanFullscreenMode")
				{
					root["FloorplanFullscreenMode"] = nValue;
				}
				else if (Key == "FloorplanAnimateZoom")
				{
					root["FloorplanAnimateZoom"] = nValue;
				}
				else if (Key == "FloorplanShowSensorValues")
				{
					root["FloorplanShowSensorValues"] = nValue;
				}
				else if (Key == "FloorplanShowSwitchValues")
				{
					root["FloorplanShowSwitchValues"] = nValue;
				}
				else if (Key == "FloorplanShowSceneNames")
				{
					root["FloorplanShowSceneNames"] = nValue;
				}
				else if (Key == "FloorplanRoomColour")
				{
					root["FloorplanRoomColour"] = sValue;
				}
				else if (Key == "FloorplanActiveOpacity")
				{
					root["FloorplanActiveOpacity"] = nValue;
				}
				else if (Key == "FloorplanInactiveOpacity")
				{
					root["FloorplanInactiveOpacity"] = nValue;
				}
				else if (Key == "SensorTimeout")
				{
					root["SensorTimeout"] = nValue;
				}
				else if (Key == "BatteryLowNotification")
				{
					root["BatterLowLevel"] = nValue;
				}
				else if (Key == "WebTheme")
				{
					root["WebTheme"] = sValue;
				}
#ifndef NOCLOUD
				else if (Key == "MyDomoticzInstanceId") {
					root["MyDomoticzInstanceId"] = sValue;
				}
				else if (Key == "MyDomoticzUserId") {
					root["MyDomoticzUserId"] = sValue;
				}
				else if (Key == "MyDomoticzPassword") {
					root["MyDomoticzPassword"] = sValue;
				}
				else if (Key == "MyDomoticzSubsystems") {
					root["MyDomoticzSubsystems"] = nValue;
				}
#endif
				else if (Key == "MyDomoticzSubsystems") {
					root["MyDomoticzSubsystems"] = nValue;
				}
				else if (Key == "SendErrorsAsNotification") {
					root["SendErrorsAsNotification"] = nValue;
				}
				else if (Key == "LogFilter") {
					root[Key] = sValue;
				}
				else if (Key == "LogFileName") {
					root[Key] = sValue;
				}
				else if (Key == "LogLevel") {
					root[Key] = sValue;
				}
				else if (Key == "DeltaTemperatureLog") {
					root[Key] = sValue;
				}
				else if (Key == "IFTTTEnabled") {
					root["IFTTTEnabled"] = nValue;
				}
				else if (Key == "IFTTTAPI") {
					root["IFTTTAPI"] = sValue;
				}
			}
		}

		void CWebServer::RType_LightLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				std::stringstream s_str(request::findValue(&req, "idx"));
				s_str >> idx;
			}
			std::vector<std::vector<std::string> > result;
			//First get Device Type/SubType
			result = m_sql.safe_query("SELECT Type, SubType, SwitchType, Options FROM DeviceStatus WHERE (ID == %" PRIu64 ")",
				idx);
			if (result.size() < 1)
				return;

			unsigned char dType = atoi(result[0][0].c_str());
			unsigned char dSubType = atoi(result[0][1].c_str());
			_eSwitchType switchtype = (_eSwitchType)atoi(result[0][2].c_str());
			std::map<std::string, std::string> options = m_sql.BuildDeviceOptions(result[0][3].c_str());

			if (
				(dType != pTypeLighting1) &&
				(dType != pTypeLighting2) &&
				(dType != pTypeLighting3) &&
				(dType != pTypeLighting4) &&
				(dType != pTypeLighting5) &&
				(dType != pTypeLighting6) &&
				(dType != pTypeFan) &&
				(dType != pTypeColorSwitch) &&
				(dType != pTypeSecurity1) &&
				(dType != pTypeSecurity2) &&
				(dType != pTypeEvohome) &&
				(dType != pTypeEvohomeRelay) &&
				(dType != pTypeCurtain) &&
				(dType != pTypeBlinds) &&
				(dType != pTypeRFY) &&
				(dType != pTypeRego6XXValue) &&
				(dType != pTypeChime) &&
				(dType != pTypeThermostat2) &&
				(dType != pTypeThermostat3) &&
				(dType != pTypeThermostat4) &&
				(dType != pTypeRemote) &&
				(dType != pTypeGeneralSwitch) &&
				(dType != pTypeHomeConfort) &&
				(!((dType == pTypeRadiator1) && (dSubType == sTypeSmartwaresSwitchRadiator)))
				)
				return; //no light device! we should not be here!

			root["status"] = "OK";
			root["title"] = "LightLog";

			result = m_sql.safe_query("SELECT ROWID, nValue, sValue, Date FROM LightingLog WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date DESC", idx);
			if (result.size() > 0)
			{
				std::map<std::string, std::string> selectorStatuses;
				if (switchtype == STYPE_Selector) {
					GetSelectorSwitchStatuses(options, selectorStatuses);
				}

				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					int nValue = atoi(sd[1].c_str());
					std::string sValue = sd[2];

					//skip 0-values in log for MediaPlayers
					if ((switchtype == STYPE_Media) && (sValue == "0")) continue;

					root["result"][ii]["idx"] = sd[0];

					//add light details
					std::string lstatus = "";
					std::string ldata = "";
					int llevel = 0;
					bool bHaveDimmer = false;
					bool bHaveSelector = false;
					bool bHaveGroupCmd = false;
					int maxDimLevel = 0;

					if (switchtype == STYPE_Media) {
						lstatus = sValue;
						ldata = lstatus;

					}
					else if ((switchtype == STYPE_Selector) && (selectorStatuses.size() > 0)) {
						if (ii == 0) {
							bHaveSelector = true;
							maxDimLevel = selectorStatuses.size();
						}
						ldata = selectorStatuses[sValue];
						lstatus = "Set Level: " + selectorStatuses[sValue];
						llevel = atoi(sValue.c_str());

					}
					else {
						GetLightStatus(dType, dSubType, switchtype, nValue, sValue, lstatus, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);
						ldata = lstatus;
					}

					if (ii == 0)
					{
						root["HaveDimmer"] = bHaveDimmer;
						root["result"][ii]["MaxDimLevel"] = maxDimLevel;
						root["HaveGroupCmd"] = bHaveGroupCmd;
						root["HaveSelector"] = bHaveSelector;
					}

					root["result"][ii]["Date"] = sd[3];
					root["result"][ii]["Data"] = ldata;
					root["result"][ii]["Status"] = lstatus;
					root["result"][ii]["Level"] = llevel;

					ii++;
				}
			}
		}

		void CWebServer::RType_TextLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				std::stringstream s_str(request::findValue(&req, "idx"));
				s_str >> idx;
			}
			std::vector<std::vector<std::string> > result;

			root["status"] = "OK";
			root["title"] = "TextLog";

			result = m_sql.safe_query("SELECT ROWID, sValue, Date FROM LightingLog WHERE (DeviceRowID==%" PRIu64 ") ORDER BY Date DESC",
				idx);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Date"] = sd[2];
					root["result"][ii]["Data"] = sd[1];
					ii++;
				}
			}
		}

		void CWebServer::RType_SceneLog(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				std::stringstream s_str(request::findValue(&req, "idx"));
				s_str >> idx;
			}
			std::vector<std::vector<std::string> > result;

			root["status"] = "OK";
			root["title"] = "SceneLog";

			result = m_sql.safe_query("SELECT ROWID, nValue, Date FROM SceneLog WHERE (SceneRowID==%" PRIu64 ") ORDER BY Date DESC", idx);
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;

					int nValue = atoi(sd[1].c_str());
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Date"] = sd[2];
					root["result"][ii]["Data"] = (nValue == 0) ? "Off" : "On";
					ii++;
				}
			}
		}



		/**
		 * Retrieve user session from store, without remote host.
		 */
		const WebEmStoredSession CWebServer::GetSession(const std::string & sessionId) {
			//_log.Log(LOG_STATUS, "SessionStore : get...");
			WebEmStoredSession session;

			if (sessionId.empty()) {
				_log.Log(LOG_ERROR, "SessionStore : cannot get session without id.");
			}
			else {
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT SessionID, Username, AuthToken, ExpirationDate FROM UserSessions WHERE SessionID = '%q'",
					sessionId.c_str());
				if (result.size() > 0) {
					session.id = result[0][0].c_str();
					session.username = base64_decode(result[0][1]);
					session.auth_token = result[0][2].c_str();

					std::string sExpirationDate = result[0][3];
					time_t now = mytime(NULL);
					struct tm tExpirationDate;
					ParseSQLdatetime(session.expires, tExpirationDate, sExpirationDate);
					// RemoteHost is not used to restore the session
					// LastUpdate is not used to restore the session
				}
			}

			return session;
		}

		/**
		 * Save user session.
		 */
		void CWebServer::StoreSession(const WebEmStoredSession & session) {
			//_log.Log(LOG_STATUS, "SessionStore : store...");
			if (session.id.empty()) {
				_log.Log(LOG_ERROR, "SessionStore : cannot store session without id.");
				return;
			}

			char szExpires[30];
			struct tm ltime;
			localtime_r(&session.expires, &ltime);
			strftime(szExpires, sizeof(szExpires), "%Y-%m-%d %H:%M:%S", &ltime);

			std::string remote_host = (session.remote_host.size() <= 50) ? // IPv4 : 15, IPv6 : (39|45)
				session.remote_host : session.remote_host.substr(0, 50);

			WebEmStoredSession storedSession = GetSession(session.id);
			if (storedSession.id.empty()) {
				m_sql.safe_query(
					"INSERT INTO UserSessions (SessionID, Username, AuthToken, ExpirationDate, RemoteHost) VALUES ('%q', '%q', '%q', '%q', '%q')",
					session.id.c_str(),
					base64_encode((const unsigned char*)session.username.c_str(), session.username.size()).c_str(),
					session.auth_token.c_str(),
					szExpires,
					remote_host.c_str());
			}
			else {
				m_sql.safe_query(
					"UPDATE UserSessions set AuthToken = '%q', ExpirationDate = '%q', RemoteHost = '%q', LastUpdate = datetime('now', 'localtime') WHERE SessionID = '%q'",
					session.auth_token.c_str(),
					szExpires,
					remote_host.c_str(),
					session.id.c_str());
			}
		}

		/**
		 * Remove user session and expired sessions.
		 */
		void CWebServer::RemoveSession(const std::string & sessionId) {
			//_log.Log(LOG_STATUS, "SessionStore : remove...");
			if (sessionId.empty()) {
				return;
			}
			m_sql.safe_query(
				"DELETE FROM UserSessions WHERE SessionID = '%q'",
				sessionId.c_str());
		}

		/**
		 * Remove all expired user sessions.
		 */
		void CWebServer::CleanSessions() {
			//_log.Log(LOG_STATUS, "SessionStore : clean...");
			m_sql.safe_query(
				"DELETE FROM UserSessions WHERE ExpirationDate < datetime('now', 'localtime')");
		}

		/**
		 * Delete all user's session, except the session used to modify the username or password.
		 * username must have been hashed
		 *
		 * Note : on the WebUserName modification, this method will not delete the session, but the session will be deleted anyway
		 * because the username will be unknown (see cWebemRequestHandler::checkAuthToken).
		 */
		void CWebServer::RemoveUsersSessions(const std::string& username, const WebEmSession & exceptSession) {
			m_sql.safe_query("DELETE FROM UserSessions WHERE (Username=='%q') and (SessionID!='%q')", username.c_str(), exceptSession.id.c_str());
		}

	} //server
}//http
