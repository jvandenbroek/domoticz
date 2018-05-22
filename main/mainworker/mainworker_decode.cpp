
#include "stdafx.h"
#include "../mainworker.h"
#include "../localtime_r.h"
#include "../WebServerHelper.h"
#include "../SQLHelper.h"

#include "../../webserver/Base64.h"

#include "../../hardware/RFXBase.h"
#include "../../hardware/YouLess.h"
#include "../../hardware/Rego6XXSerial.h"
#include "../../hardware/EvohomeWeb.h"

#include "../../notifications/NotificationHelper.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#ifdef PARSE_RFXCOM_DEVICE_LOG
#include <iostream>
#include <fstream>
#endif

#define round(a) ( int ) ( a + .5 )
#define HEX( x ) \
	std::setw(2) << std::setfill('0') << std::hex << std::uppercase << (int)( x )

void MainWorker::decode_InterfaceMessage(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];

	WriteMessageStart();

	switch (pResponse->IRESPONSE.subtype)
	{
	case sTypeInterfaceCommand:
	{
		int mlen = pResponse->IRESPONSE.packetlength;
		WriteMessage("subtype           = Interface Response");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->IRESPONSE.seqnbr);
		WriteMessage(szTmp);
		switch (pResponse->IRESPONSE.cmnd)
		{
		case cmdSTATUS:
		case cmdSETMODE:
		case trxType310:
		case trxType315:
		case recType43392:
		case trxType43392:
		case trxType868:
		{
			WriteMessage("response on cmnd  = ", false);
			switch (pResponse->IRESPONSE.cmnd)
			{
			case cmdSTATUS:
				WriteMessage("Get Status");
				break;
			case cmdSETMODE:
				WriteMessage("Set Mode");
				break;
			case trxType310:
				WriteMessage("Select 310MHz");
				break;
			case trxType315:
				WriteMessage("Select 315MHz");
				break;
			case recType43392:
				WriteMessage("Select 433.92MHz");
				break;
			case trxType43392:
				WriteMessage("Select 433.92MHz (E)");
				break;
			case trxType868:
				WriteMessage("Select 868.00MHz");
				break;
			default:
				WriteMessage("Error: unknown response");
				break;
			}

			m_sql.UpdateRFXCOMHardwareDetails(HwdID, pResponse->IRESPONSE.msg1, pResponse->IRESPONSE.msg2, pResponse->ICMND.msg3, pResponse->ICMND.msg4, pResponse->ICMND.msg5, pResponse->ICMND.msg6);

			switch (pResponse->IRESPONSE.msg1)
			{
			case trxType310:
				WriteMessage("Transceiver type  = 310MHz");
				break;
			case trxType315:
				WriteMessage("Receiver type     = 315MHz");
				break;
			case recType43392:
				WriteMessage("Receiver type     = 433.92MHz (receive only)");
				break;
			case trxType43392:
				WriteMessage("Transceiver type  = 433.92MHz");
				break;
			case trxType868:
				WriteMessage("Receiver type     = 868.00MHz");
				break;
			default:
				WriteMessage("Receiver type     = unknown");
				break;
			}
			int FWType = 0;
			int FWVersion = 0;
			if (mlen > 13)
			{
				FWType = pResponse->IRESPONSE.msg10;
				FWVersion = pResponse->IRESPONSE.msg2 + 1000;
			}
			else
			{
				FWVersion = pResponse->IRESPONSE.msg2;
				if ((pResponse->IRESPONSE.msg1 == recType43392) && (FWVersion < 162))
					FWType = 0; //Type1 RFXrec
				else if ((pResponse->IRESPONSE.msg1 == recType43392) && (FWVersion < 162))
					FWType = 1; //Type1
				else if ((pResponse->IRESPONSE.msg1 == recType43392) && ((FWVersion > 162) && (FWVersion < 225)))
					FWType = 2; //Type2
				else
					FWType = 3; //Ext
			}
			sprintf(szTmp, "Firmware version  = %d", FWVersion);
			WriteMessage(szTmp);

			if (
				(pResponse->IRESPONSE.msg1 == recType43392) ||
				(pResponse->IRESPONSE.msg1 == trxType43392)
				)
			{
				WriteMessage("Firmware type     = ", false);
				switch (FWType)
				{
				case 0:
					strcpy(szTmp, "Type1 RX");
					break;
				case 1:
					strcpy(szTmp, "Type1");
					break;
				case 2:
					strcpy(szTmp, "Type2");
					break;
				case 3:
					strcpy(szTmp, "Ext");
					break;
				case 4:
					strcpy(szTmp, "Ext2");
					break;
				case 5:
					strcpy(szTmp, "Pro1");
					break;
				default:
					strcpy(szTmp, "?");
					break;
				}
				WriteMessage(szTmp);
			}

			CRFXBase *pMyHardware = reinterpret_cast<CRFXBase*>(GetHardware(HwdID));
			if (pMyHardware)
			{
				std::stringstream sstr;
				sstr << szTmp << "/" << FWVersion;
				pMyHardware->m_Version = sstr.str();
			}


			sprintf(szTmp, "Hardware version  = %d.%d", pResponse->IRESPONSE.msg7, pResponse->IRESPONSE.msg8);
			WriteMessage(szTmp);

			if (pResponse->IRESPONSE.UNDECODEDenabled)
				WriteMessage("Undec             on");
			else
				WriteMessage("Undec             off");

			if (pResponse->IRESPONSE.X10enabled)
				WriteMessage("X10               enabled");
			else
				WriteMessage("X10               disabled");

			if (pResponse->IRESPONSE.ARCenabled)
				WriteMessage("ARC               enabled");
			else
				WriteMessage("ARC               disabled");

			if (pResponse->IRESPONSE.ACenabled)
				WriteMessage("AC                enabled");
			else
				WriteMessage("AC                disabled");

			if (pResponse->IRESPONSE.HEEUenabled)
				WriteMessage("HomeEasy EU       enabled");
			else
				WriteMessage("HomeEasy EU       disabled");

			if (pResponse->IRESPONSE.MEIANTECHenabled)
				WriteMessage("Meiantech/Atlantic enabled");
			else
				WriteMessage("Meiantech/Atlantic disabled");

			if (pResponse->IRESPONSE.OREGONenabled)
				WriteMessage("Oregon Scientific enabled");
			else
				WriteMessage("Oregon Scientific disabled");

			if (pResponse->IRESPONSE.ATIenabled)
				WriteMessage("ATI/Cartelectronic enabled");
			else
				WriteMessage("ATI/Cartelectronic disabled");

			if (pResponse->IRESPONSE.VISONICenabled)
				WriteMessage("Visonic           enabled");
			else
				WriteMessage("Visonic           disabled");

			if (pResponse->IRESPONSE.MERTIKenabled)
				WriteMessage("Mertik            enabled");
			else
				WriteMessage("Mertik            disabled");

			if (pResponse->IRESPONSE.LWRFenabled)
				WriteMessage("AD                enabled");
			else
				WriteMessage("AD                disabled");

			if (pResponse->IRESPONSE.HIDEKIenabled)
				WriteMessage("Hideki            enabled");
			else
				WriteMessage("Hideki            disabled");

			if (pResponse->IRESPONSE.LACROSSEenabled)
				WriteMessage("La Crosse         enabled");
			else
				WriteMessage("La Crosse         disabled");

			if (pResponse->IRESPONSE.FS20enabled)
				WriteMessage("FS20/Legrand      enabled");
			else
				WriteMessage("FS20/Legrand      disabled");

			if (pResponse->IRESPONSE.PROGUARDenabled)
				WriteMessage("ProGuard          enabled");
			else
				WriteMessage("ProGuard          disabled");

			if (pResponse->IRESPONSE.BLINDST0enabled)
				WriteMessage("BlindsT0          enabled");
			else
				WriteMessage("BlindsT0          disabled");

			if (pResponse->IRESPONSE.BLINDST1enabled)
				WriteMessage("BlindsT1          enabled");
			else
				WriteMessage("BlindsT1          disabled");

			if (pResponse->IRESPONSE.AEenabled)
				WriteMessage("AE                enabled");
			else
				WriteMessage("AE                disabled");

			if (pResponse->IRESPONSE.RUBICSONenabled)
				WriteMessage("RUBiCSON          enabled");
			else
				WriteMessage("RUBiCSON          disabled");

			if (pResponse->IRESPONSE.FINEOFFSETenabled)
				WriteMessage("FineOffset        enabled");
			else
				WriteMessage("FineOffset        disabled");

			if (pResponse->IRESPONSE.LIGHTING4enabled)
				WriteMessage("Lighting4         enabled");
			else
				WriteMessage("Lighting4         disabled");

			if (pResponse->IRESPONSE.RSLenabled)
				WriteMessage("Conrad RSL        enabled");
			else
				WriteMessage("Conrad RSL        disabled");

			if (pResponse->IRESPONSE.SXenabled)
				WriteMessage("ByronSX           enabled");
			else
				WriteMessage("ByronSX           disabled");

			if (pResponse->IRESPONSE.IMAGINTRONIXenabled)
				WriteMessage("IMAGINTRONIX      enabled");
			else
				WriteMessage("IMAGINTRONIX      disabled");

			if (pResponse->IRESPONSE.KEELOQenabled)
				WriteMessage("KEELOQ            enabled");
			else
				WriteMessage("KEELOQ            disabled");

			if (pResponse->IRESPONSE.HCEnabled)
				WriteMessage("Home Confort      enabled");
			else
				WriteMessage("Home Confort      disabled");
		}
		break;
		case cmdSAVE:
			WriteMessage("response on cmnd  = Save");
			break;
		}
		break;
	}
	break;
	case sTypeUnknownRFYremote:
		WriteMessage("subtype           = Unknown RFY remote! Use the Program command to create a remote in the RFXtrx433Ext");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->IRESPONSE.seqnbr);
		WriteMessage(szTmp);
		break;
	case sTypeExtError:
		WriteMessage("subtype           = No RFXtrx433E hardware detected");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->IRESPONSE.seqnbr);
		WriteMessage(szTmp);
		break;
	case sTypeRFYremoteList:
		if ((pResponse->ICMND.xmitpwr == 0) && (pResponse->ICMND.msg3 == 0) && (pResponse->ICMND.msg4 == 0) && (pResponse->ICMND.msg5 == 0))
		{
			sprintf(szTmp, "subtype           = RFY remote: %d is empty", pResponse->ICMND.freqsel);
			WriteMessage(szTmp);
		}
		else
		{
			sprintf(szTmp, "subtype           = RFY remote: %d, ID: %02d%02d%02d, unitnbr: %d",
				pResponse->ICMND.freqsel,
				pResponse->ICMND.xmitpwr,
				pResponse->ICMND.msg3,
				pResponse->ICMND.msg4,
				pResponse->ICMND.msg5);
			WriteMessage(szTmp);
		}
		break;
	case sTypeASAremoteList:
		if ((pResponse->ICMND.xmitpwr == 0) && (pResponse->ICMND.msg3 == 0) && (pResponse->ICMND.msg4 == 0) && (pResponse->ICMND.msg5 == 0))
		{
			sprintf(szTmp, "subtype           = ASA remote: %d is empty", pResponse->ICMND.freqsel);
			WriteMessage(szTmp);
		}
		else
		{
			sprintf(szTmp, "subtype           = ASA remote: %d, ID: %02d%02d%02d, unitnbr: %d",
				pResponse->ICMND.freqsel,
				pResponse->ICMND.xmitpwr,
				pResponse->ICMND.msg3,
				pResponse->ICMND.msg4,
				pResponse->ICMND.msg5);
			WriteMessage(szTmp);
		}
		break;
	case sTypeInterfaceWrongCommand:
		if (pResponse->IRESPONSE.cmnd == 0x07)
		{
			WriteMessage("Please upgrade your RFXTrx Firmware!");
		}
		else
		{
			sprintf(szTmp, "subtype          = Wrong command received from application (%d)", pResponse->IRESPONSE.cmnd);
			WriteMessage(szTmp);
			sprintf(szTmp, "Sequence nbr      = %d", pResponse->IRESPONSE.seqnbr);
			WriteMessage(szTmp);
		}
		break;
	}
	WriteMessageEnd();
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_InterfaceControl(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	WriteMessageStart();
	switch (pResponse->IRESPONSE.subtype)
	{
	case sTypeInterfaceCommand:
		WriteMessage("subtype           = Interface Command");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->IRESPONSE.seqnbr);
		WriteMessage(szTmp);
		switch (pResponse->IRESPONSE.cmnd)
		{
		case cmdRESET:
			WriteMessage("reset the receiver/transceiver");
			break;
		case cmdSTATUS:
			WriteMessage("return firmware versions and configuration of the interface");
			break;
		case cmdSETMODE:
			WriteMessage("set configuration of the interface");
			break;
		case cmdSAVE:
			WriteMessage("save receiving modes of the receiver/transceiver in non-volatile memory");
			break;
		case cmdStartRec:
			WriteMessage("start RFXtrx receiver");
			break;
		case trxType310:
			WriteMessage("select 310MHz in the 310/315 transceiver");
			break;
		case trxType315:
			WriteMessage("select 315MHz in the 310/315 transceiver");
			break;
		case recType43392:
		case trxType43392:
			WriteMessage("select 433.92MHz in the 433 transceiver");
			break;
		case trxType868:
			WriteMessage("select 868MHz in the 868 transceiver");
			break;
		}
		break;
	}
	WriteMessageEnd();
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_BateryLevel(bool bIsInPercentage, unsigned char level)
{
	if (bIsInPercentage)
	{
		switch (level)
		{
		case 0:
			WriteMessage("Battery       = 10%");
			break;
		case 1:
			WriteMessage("Battery       = 20%");
			break;
		case 2:
			WriteMessage("Battery       = 30%");
			break;
		case 3:
			WriteMessage("Battery       = 40%");
			break;
		case 4:
			WriteMessage("Battery       = 50%");
			break;
		case 5:
			WriteMessage("Battery       = 60%");
			break;
		case 6:
			WriteMessage("Battery       = 70%");
			break;
		case 7:
			WriteMessage("Battery       = 80%");
			break;
		case 8:
			WriteMessage("Battery       = 90%");
			break;
		case 9:
			WriteMessage("Battery       = 100%");
			break;
		}
	}
	else
	{
		if (level == 0)
		{
			WriteMessage("Battery       = Low");
		}
		else
		{
			WriteMessage("Battery       = OK");
		}
	}
}

void MainWorker::decode_Rain(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeRAIN;
	unsigned char subType = pResponse->RAIN.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->RAIN.id1 * 256) + pResponse->RAIN.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->RAIN.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, pResponse->RAIN.subtype == sTypeRAIN1, pResponse->RAIN.battery_level & 0x0F);

	int Rainrate = (pResponse->RAIN.rainrateh * 256) + pResponse->RAIN.rainratel;

	float TotalRain = float((pResponse->RAIN.raintotal1 * 65535) + (pResponse->RAIN.raintotal2 * 256) + pResponse->RAIN.raintotal3) / 10.0f;

	if (subType != sTypeRAINWU)
	{
		Rainrate = 0;
		//Calculate our own rainrate
		std::vector<std::vector<std::string> > result;

		//Get our index
		result = m_sql.safe_query(
			"SELECT ID FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)", HwdID, ID.c_str(), Unit, devType, subType);
		if (result.size() != 0)
		{
			uint64_t ulID;
			std::stringstream s_str(result[0][0]);
			s_str >> ulID;

			//Get Counter from one Hour ago
			time_t now = mytime(NULL);
			now -= 3600; //subtract one hour
			struct tm ltime;
			localtime_r(&now, &ltime);

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query(
				"SELECT MIN(Total) FROM Rain WHERE (DeviceRowID=%" PRIu64 " AND Date>='%04d-%02d-%02d %02d:%02d:%02d')",
				ulID, ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
			if (result.size() == 1)
			{
				float totalRainFallLastHour = TotalRain - static_cast<float>(atof(result[0][0].c_str()));
				Rainrate = round(totalRainFallLastHour*100.0f);
			}
		}
	}

	sprintf(szTmp, "%d;%.1f", Rainrate, TotalRain);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (subType)
		{
		case sTypeRAIN1:
			WriteMessage("subtype       = RAIN1 - RGR126/682/918/928");
			break;
		case sTypeRAIN2:
			WriteMessage("subtype       = RAIN2 - PCR800");
			break;
		case sTypeRAIN3:
			WriteMessage("subtype       = RAIN3 - TFA");
			break;
		case sTypeRAIN4:
			WriteMessage("subtype       = RAIN4 - UPM RG700");
			break;
		case sTypeRAIN5:
			WriteMessage("subtype       = RAIN5 - LaCrosse WS2300");
			break;
		case sTypeRAIN6:
			WriteMessage("subtype       = RAIN6 - LaCrosse TX5");
			break;
		case sTypeRAIN7:
			WriteMessage("subtype       = RAIN7 - Alecto");
			break;
		case sTypeRAIN8:
			WriteMessage("subtype       = RAIN8 - Davis");
			break;
		case sTypeRAIN9:
			WriteMessage("subtype       = RAIN9 - Alecto WCH2010");
			break;
		case sTypeRAINWU:
			WriteMessage("subtype       = Weather Underground (Total Rain)");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X : %02X", pResponse->RAIN.packettype, pResponse->RAIN.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->RAIN.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "ID            = %s", ID.c_str());
		WriteMessage(szTmp);

		if (pResponse->RAIN.subtype == sTypeRAIN1)
		{
			sprintf(szTmp, "Rain rate     = %d mm/h", Rainrate);
			WriteMessage(szTmp);
		}
		else if (pResponse->RAIN.subtype == sTypeRAIN2)
		{
			sprintf(szTmp, "Rain rate     = %d mm/h", Rainrate);
			WriteMessage(szTmp);
		}

		sprintf(szTmp, "Total rain    = %.1f mm", TotalRain);
		WriteMessage(szTmp);
		sprintf(szTmp, "Signal level  = %d", pResponse->RAIN.rssi);
		WriteMessage(szTmp);

		decode_BateryLevel(pResponse->RAIN.subtype == sTypeRAIN1, pResponse->RAIN.battery_level & 0x0F);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Wind(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[300];
	unsigned char devType = pTypeWIND;
	unsigned char subType = pResponse->WIND.subtype;
	unsigned short windID = (pResponse->WIND.id1 * 256) + pResponse->WIND.id2;
	sprintf(szTmp, "%d", windID);
	std::string ID = szTmp;
	unsigned char Unit = 0;

	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->WIND.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, pResponse->WIND.subtype == sTypeWIND3, pResponse->WIND.battery_level & 0x0F);

	double dDirection;
	dDirection = (double)(pResponse->WIND.directionh * 256) + pResponse->WIND.directionl;
	dDirection = m_wind_calculator[windID].AddValueAndReturnAvarage(dDirection);

	std::string strDirection;
	if (dDirection > 348.75 || dDirection < 11.26)
		strDirection = "N";
	else if (dDirection < 33.76)
		strDirection = "NNE";
	else if (dDirection < 56.26)
		strDirection = "NE";
	else if (dDirection < 78.76)
		strDirection = "ENE";
	else if (dDirection < 101.26)
		strDirection = "E";
	else if (dDirection < 123.76)
		strDirection = "ESE";
	else if (dDirection < 146.26)
		strDirection = "SE";
	else if (dDirection < 168.76)
		strDirection = "SSE";
	else if (dDirection < 191.26)
		strDirection = "S";
	else if (dDirection < 213.76)
		strDirection = "SSW";
	else if (dDirection < 236.26)
		strDirection = "SW";
	else if (dDirection < 258.76)
		strDirection = "WSW";
	else if (dDirection < 281.26)
		strDirection = "W";
	else if (dDirection < 303.76)
		strDirection = "WNW";
	else if (dDirection < 326.26)
		strDirection = "NW";
	else if (dDirection < 348.76)
		strDirection = "NNW";
	else
		strDirection = "---";

	dDirection = round(dDirection);

	int intSpeed = (pResponse->WIND.av_speedh * 256) + pResponse->WIND.av_speedl;
	int intGust = (pResponse->WIND.gusth * 256) + pResponse->WIND.gustl;

	if (pResponse->WIND.subtype == sTypeWIND6)
	{
		//LaCrosse WS2300
		//This sensor is only reporting gust, speed=gust
		intSpeed = intGust;
	}

	m_wind_calculator[windID].SetSpeedGust(intSpeed, intGust);

	float temp = 0, chill = 0;
	if (pResponse->WIND.subtype == sTypeWIND4)
	{
		if (!pResponse->WIND.tempsign)
		{
			temp = float((pResponse->WIND.temperatureh * 256) + pResponse->WIND.temperaturel) / 10.0f;
		}
		else
		{
			temp = -(float(((pResponse->WIND.temperatureh & 0x7F) * 256) + pResponse->WIND.temperaturel) / 10.0f);
		}
		if ((temp < -200) || (temp > 380))
		{
			WriteMessage(" Invalid Temperature");
			return;
		}

		float AddjValue = 0.0f;
		float AddjMulti = 1.0f;
		m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
		temp += AddjValue;

		if (!pResponse->WIND.chillsign)
		{
			chill = float((pResponse->WIND.chillh * 256) + pResponse->WIND.chilll) / 10.0f;
		}
		else
		{
			chill = -(float(((pResponse->WIND.chillh) & 0x7F) * 256 + pResponse->WIND.chilll) / 10.0f);
		}
		chill += AddjValue;
	}
	else if (pResponse->WIND.subtype == sTypeWINDNoTemp)
	{
		if (!pResponse->WIND.tempsign)
		{
			temp = float((pResponse->WIND.temperatureh * 256) + pResponse->WIND.temperaturel) / 10.0f;
		}
		else
		{
			temp = -(float(((pResponse->WIND.temperatureh & 0x7F) * 256) + pResponse->WIND.temperaturel) / 10.0f);
		}
		if ((temp < -200) || (temp > 380))
		{
			WriteMessage(" Invalid Temperature");
			return;
		}

		float AddjValue = 0.0f;
		float AddjMulti = 1.0f;
		m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
		temp += AddjValue;

		if (!pResponse->WIND.chillsign)
		{
			chill = float((pResponse->WIND.chillh * 256) + pResponse->WIND.chilll) / 10.0f;
		}
		else
		{
			chill = -(float(((pResponse->WIND.chillh) & 0x7F) * 256 + pResponse->WIND.chilll) / 10.0f);
		}
		chill += AddjValue;
	}
	if (chill == 0)
	{
		float wspeedms = float(intSpeed) / 10.0f;
		if ((temp < 10.0) && (wspeedms >= 1.4))
		{
			float chillJatTI = 13.12f + 0.6215f*temp - 11.37f*pow(wspeedms*3.6f, 0.16f) + 0.3965f*temp*pow(wspeedms*3.6f, 0.16f);
			chill = chillJatTI;
		}
	}

	sprintf(szTmp, "%.2f;%s;%d;%d;%.1f;%.1f", dDirection, strDirection.c_str(), intSpeed, intGust, temp, chill);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->WIND.subtype)
		{
		case sTypeWIND1:
			WriteMessage("subtype       = WIND1 - WTGR800");
			break;
		case sTypeWIND2:
			WriteMessage("subtype       = WIND2 - WGR800");
			break;
		case sTypeWIND3:
			WriteMessage("subtype       = WIND3 - STR918/928, WGR918");
			break;
		case sTypeWIND4:
			WriteMessage("subtype       = WIND4 - TFA");
			break;
		case sTypeWIND5:
			WriteMessage("subtype       = WIND5 - UPM WDS500");
			break;
		case sTypeWIND6:
			WriteMessage("subtype       = WIND6 - LaCrosse WS2300");
			break;
		case sTypeWIND7:
			WriteMessage("subtype       = WIND7 - Alecto WS4500");
			break;
		case sTypeWIND8:
			WriteMessage("subtype       = WIND8 - Alecto ACH2010");
			break;
		case sTypeWINDNoTemp:
			WriteMessage("subtype       = Weather Station");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->WIND.packettype, pResponse->WIND.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->WIND.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %s", ID.c_str());
		WriteMessage(szTmp);

		sprintf(szTmp, "Direction     = %d degrees %s", int(dDirection), strDirection.c_str());
		WriteMessage(szTmp);

		if (pResponse->WIND.subtype != sTypeWIND5)
		{
			sprintf(szTmp, "Average speed = %.1f mtr/sec, %.2f km/hr, %.2f mph", float(intSpeed) / 10.0f, (float(intSpeed) * 0.36f), (float(intSpeed) * 0.223693629f));
			WriteMessage(szTmp);
		}

		sprintf(szTmp, "Wind gust     = %.1f mtr/sec, %.2f km/hr, %.2f mph", float(intGust) / 10.0f, (float(intGust)* 0.36f), (float(intGust) * 0.223693629f));
		WriteMessage(szTmp);

		if (pResponse->WIND.subtype == sTypeWIND4)
		{
			sprintf(szTmp, "Temperature   = %.1f C", temp);
			WriteMessage(szTmp);

			sprintf(szTmp, "Chill         = %.1f C", chill);
		}
		if (pResponse->WIND.subtype == sTypeWINDNoTemp)
		{
			sprintf(szTmp, "Temperature   = %.1f C", temp);
			WriteMessage(szTmp);

			sprintf(szTmp, "Chill         = %.1f C", chill);
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->WIND.rssi);
		WriteMessage(szTmp);

		decode_BateryLevel(pResponse->WIND.subtype == sTypeWIND3, pResponse->WIND.battery_level & 0x0F);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Temp(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeTEMP;
	unsigned char subType = pResponse->TEMP.subtype;
	sprintf(szTmp, "%d", (pResponse->TEMP.id1 * 256) + pResponse->TEMP.id2);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->TEMP.id2;

	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->TEMP.rssi;
	unsigned char BatteryLevel = 0;
	if ((pResponse->TEMP.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;

	//Override battery level if hardware supports it
	if (HwdType == HTYPE_OpenZWave)
	{
		BatteryLevel = pResponse->TEMP.battery_level * 10;
	}
	else if ((HwdType == HTYPE_EnOceanESP2) || (HwdType == HTYPE_EnOceanESP3))
	{
		BatteryLevel = 255;
		SignalLevel = 12;
		Unit = (pResponse->TEMP.rssi << 4) | pResponse->TEMP.battery_level;
	}

	float temp;
	if (!pResponse->TEMP.tempsign)
	{
		temp = float((pResponse->TEMP.temperatureh * 256) + pResponse->TEMP.temperaturel) / 10.0f;
	}
	else
	{
		temp = -(float(((pResponse->TEMP.temperatureh & 0x7F) * 256) + pResponse->TEMP.temperaturel) / 10.0f);
	}
	if ((temp < -200) || (temp > 380))
	{
		WriteMessage(" Invalid Temperature");
		return;
	}

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	temp += AddjValue;

	sprintf(szTmp, "%.1f", temp);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	bool bHandledNotification = false;
	unsigned char humidity = 0;
	if (pResponse->TEMP.subtype == sTypeTEMP5)
	{
		//check if we already had a humidity for this device, if so, keep it!
		char szTmp[300];
		std::vector<std::vector<std::string> > result;

		result = m_sql.safe_query(
			"SELECT nValue,sValue FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
			HwdID, ID.c_str(), 1, pTypeHUM, sTypeHUM1);
		if (result.size() == 1)
		{
			m_sql.GetAddjustment(HwdID, ID.c_str(), 2, pTypeTEMP_HUM, sTypeTH_LC_TC, AddjValue, AddjMulti);
			temp += AddjValue;
			humidity = atoi(result[0][0].c_str());
			unsigned char humidity_status = atoi(result[0][1].c_str());
			sprintf(szTmp, "%.1f;%d;%d", temp, humidity, humidity_status);
			DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 2, pTypeTEMP_HUM, sTypeTH_LC_TC, SignalLevel, BatteryLevel, 0, szTmp, procResult.DeviceName);
			m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, pTypeTEMP_HUM, sTypeTH_LC_TC, szTmp);

			bHandledNotification = true;
		}
	}

	if (!bHandledNotification)
		m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, temp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->TEMP.subtype)
		{
		case sTypeTEMP1:
			WriteMessage("subtype       = TEMP1 - THR128/138, THC138");
			sprintf(szTmp, "                channel %d", pResponse->TEMP.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTEMP2:
			WriteMessage("subtype       = TEMP2 - THC238/268,THN132,THWR288,THRN122,THN122,AW129/131");
			sprintf(szTmp, "                channel %d", pResponse->TEMP.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTEMP3:
			WriteMessage("subtype       = TEMP3 - THWR800");
			break;
		case sTypeTEMP4:
			WriteMessage("subtype       = TEMP4 - RTHN318");
			sprintf(szTmp, "                channel %d", pResponse->TEMP.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTEMP5:
			WriteMessage("subtype       = TEMP5 - LaCrosse TX2, TX3, TX4, TX17");
			break;
		case sTypeTEMP6:
			WriteMessage("subtype       = TEMP6 - TS15C");
			break;
		case sTypeTEMP7:
			WriteMessage("subtype       = TEMP7 - Viking 02811, Proove TSS330");
			break;
		case sTypeTEMP8:
			WriteMessage("subtype       = TEMP8 - LaCrosse WS2300");
			break;
		case sTypeTEMP9:
			if (pResponse->TEMP.id2 & 0xFF)
				WriteMessage("subtype       = TEMP9 - RUBiCSON 48659 stektermometer");
			else
				WriteMessage("subtype       = TEMP9 - RUBiCSON 48695");
			break;
		case sTypeTEMP10:
			WriteMessage("subtype       = TEMP10 - TFA 30.3133");
			break;
		case sTypeTEMP11:
			WriteMessage("subtype       = WT0122 pool sensor");
			break;
		case sTypeTEMP_SYSTEM:
			WriteMessage("subtype       = System");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->TEMP.packettype, pResponse->TEMP.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->TEMP.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->TEMP.id1 * 256) + pResponse->TEMP.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Temperature   = %.1f C", temp);
		WriteMessage(szTmp);

		sprintf(szTmp, "Signal level  = %d", pResponse->TEMP.rssi);
		WriteMessage(szTmp);

		if ((pResponse->TEMP.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Hum(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeHUM;
	unsigned char subType = pResponse->HUM.subtype;
	sprintf(szTmp, "%d", (pResponse->HUM.id1 * 256) + pResponse->HUM.id2);
	std::string ID = szTmp;
	unsigned char Unit = 1;

	unsigned char SignalLevel = pResponse->HUM.rssi;
	unsigned char BatteryLevel = 0;
	if ((pResponse->HUM.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;
	//Override battery level if hardware supports it
	if (HwdType == HTYPE_OpenZWave)
	{
		BatteryLevel = pResponse->TEMP.battery_level;
	}

	unsigned char humidity = pResponse->HUM.humidity;
	if (humidity > 100)
	{
		WriteMessage(" Invalid Humidity");
		return;
	}

	sprintf(szTmp, "%d", pResponse->HUM.humidity_status);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, humidity, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	bool bHandledNotification = false;
	float temp = 0;
	if (pResponse->HUM.subtype == sTypeHUM1)
	{
		//check if we already had a humidity for this device, if so, keep it!
		char szTmp[300];
		std::vector<std::vector<std::string> > result;

		result = m_sql.safe_query(
			"SELECT sValue FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
			HwdID, ID.c_str(), 0, pTypeTEMP, sTypeTEMP5);
		if (result.size() == 1)
		{
			temp = static_cast<float>(atof(result[0][0].c_str()));
			float AddjValue = 0.0f;
			float AddjMulti = 1.0f;
			m_sql.GetAddjustment(HwdID, ID.c_str(), 2, pTypeTEMP_HUM, sTypeTH_LC_TC, AddjValue, AddjMulti);
			temp += AddjValue;
			sprintf(szTmp, "%.1f;%d;%d", temp, humidity, pResponse->HUM.humidity_status);
			DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 2, pTypeTEMP_HUM, sTypeTH_LC_TC, SignalLevel, BatteryLevel, 0, szTmp, procResult.DeviceName);
			m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, pTypeTEMP_HUM, sTypeTH_LC_TC, subType, szTmp);
			bHandledNotification = true;
		}
	}
	if (!bHandledNotification)
		m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, (const int)humidity);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->HUM.subtype)
		{
		case sTypeHUM1:
			WriteMessage("subtype       = HUM1 - LaCrosse TX3");
			break;
		case sTypeHUM2:
			WriteMessage("subtype       = HUM2 - LaCrosse WS2300");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->HUM.packettype, pResponse->HUM.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->HUM.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->HUM.id1 * 256) + pResponse->HUM.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Humidity      = %d %%", pResponse->HUM.humidity);
		WriteMessage(szTmp);

		switch (pResponse->HUM.humidity_status)
		{
		case humstat_normal:
			WriteMessage("Status        = Normal");
			break;
		case humstat_comfort:
			WriteMessage("Status        = Comfortable");
			break;
		case humstat_dry:
			WriteMessage("Status        = Dry");
			break;
		case humstat_wet:
			WriteMessage("Status        = Wet");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->HUM.rssi);
		WriteMessage(szTmp);

		if ((pResponse->HUM.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_TempHum(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeTEMP_HUM;
	unsigned char subType = pResponse->TEMP_HUM.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->TEMP_HUM.id1 * 256) + pResponse->TEMP_HUM.id2);
	ID = szTmp;
	unsigned char Unit = 0;

	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->TEMP_HUM.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, pResponse->TEMP_HUM.subtype == sTypeTH8, pResponse->TEMP_HUM.battery_level);

	//Get Channel(Unit)
	switch (pResponse->TEMP_HUM.subtype)
	{
	case sTypeTH1:
	case sTypeTH2:
	case sTypeTH3:
	case sTypeTH4:
	case sTypeTH6:
	case sTypeTH8:
	case sTypeTH10:
	case sTypeTH11:
	case sTypeTH12:
	case sTypeTH13:
	case sTypeTH14:
		Unit = pResponse->TEMP_HUM.id2;
		break;
	case sTypeTH5:
	case sTypeTH9:
		//no channel
		break;
	case sTypeTH7:
		if (pResponse->TEMP_HUM.id1 < 0x40)
			Unit = 1;
		else if (pResponse->TEMP_HUM.id1 < 0x60)
			Unit = 2;
		else if (pResponse->TEMP_HUM.id1 < 0x80)
			Unit = 3;
		else if ((pResponse->TEMP_HUM.id1 > 0x9F) && (pResponse->TEMP_HUM.id1 < 0xC0))
			Unit = 4;
		else if (pResponse->TEMP_HUM.id1 < 0xE0)
			Unit = 5;
		break;
	}

	float temp;
	if (!pResponse->TEMP_HUM.tempsign)
	{
		temp = float((pResponse->TEMP_HUM.temperatureh * 256) + pResponse->TEMP_HUM.temperaturel) / 10.0f;
	}
	else
	{
		temp = -(float(((pResponse->TEMP_HUM.temperatureh & 0x7F) * 256) + pResponse->TEMP_HUM.temperaturel) / 10.0f);
	}
	if ((temp < -200) || (temp > 380))
	{
		WriteMessage(" Invalid Temperature");
		return;
	}

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	temp += AddjValue;

	int Humidity = (int)pResponse->TEMP_HUM.humidity;
	unsigned char HumidityStatus = pResponse->TEMP_HUM.humidity_status;

	if (Humidity > 100)
	{
		WriteMessage(" Invalid Humidity");
		return;
	}
	/*
		AddjValue=0.0f;
		AddjMulti=1.0f;
		m_sql.GetAddjustment2(HwdID, ID.c_str(),Unit,devType,subType,AddjValue,AddjMulti);
		Humidity+=int(AddjValue);
		if (Humidity>100)
			Humidity=100;
		if (Humidity<0)
			Humidity=0;
	*/
	sprintf(szTmp, "%.1f;%d;%d", temp, Humidity, HumidityStatus);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->TEMP_HUM.subtype)
		{
		case sTypeTH1:
			WriteMessage("subtype       = TH1 - THGN122/123/132,THGR122/228/238/268");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH2:
			WriteMessage("subtype       = TH2 - THGR810,THGN800");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH3:
			WriteMessage("subtype       = TH3 - RTGR328");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH4:
			WriteMessage("subtype       = TH4 - THGR328");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH5:
			WriteMessage("subtype       = TH5 - WTGR800");
			break;
		case sTypeTH6:
			WriteMessage("subtype       = TH6 - THGR918/928,THGRN228,THGN500");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH7:
			WriteMessage("subtype       = TH7 - Cresta, TFA TS34C");
			if (pResponse->TEMP_HUM.id1 < 0x40)
				WriteMessage("                channel 1");
			else if (pResponse->TEMP_HUM.id1 < 0x60)
				WriteMessage("                channel 2");
			else if (pResponse->TEMP_HUM.id1 < 0x80)
				WriteMessage("                channel 3");
			else if ((pResponse->TEMP_HUM.id1 > 0x9F) && (pResponse->TEMP_HUM.id1 < 0xC0))
				WriteMessage("                channel 4");
			else if (pResponse->TEMP_HUM.id1 < 0xE0)
				WriteMessage("                channel 5");
			else
				WriteMessage("                channel ??");
			break;
		case sTypeTH8:
			WriteMessage("subtype       = TH8 - WT260,WT260H,WT440H,WT450,WT450H");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH9:
			WriteMessage("subtype       = TH9 - Viking 02038, 02035 (02035 has no humidity), TSS320");
			break;
		case sTypeTH10:
			WriteMessage("subtype       = TH10 - Rubicson/IW008T/TX95");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH11:
			WriteMessage("subtype       = TH11 - Oregon EW109");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH12:
			WriteMessage("subtype       = TH12 - Imagintronix/Opus TX300");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH13:
			WriteMessage("subtype       = TH13 - Alecto WS1700 and compatibles");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTH14:
			WriteMessage("subtype       = TH14 - Alecto");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM.id2);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->TEMP_HUM.packettype, pResponse->TEMP_HUM.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->TEMP_HUM.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %s", ID.c_str());
		WriteMessage(szTmp);

		double tvalue = ConvertTemperature(temp, m_sql.m_tempsign[0]);
		sprintf(szTmp, "Temperature   = %.1f C", tvalue);
		WriteMessage(szTmp);
		sprintf(szTmp, "Humidity      = %d %%", Humidity);
		WriteMessage(szTmp);

		switch (pResponse->TEMP_HUM.humidity_status)
		{
		case humstat_normal:
			WriteMessage("Status        = Normal");
			break;
		case humstat_comfort:
			WriteMessage("Status        = Comfortable");
			break;
		case humstat_dry:
			WriteMessage("Status        = Dry");
			break;
		case humstat_wet:
			WriteMessage("Status        = Wet");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->TEMP_HUM.rssi);
		WriteMessage(szTmp);

		decode_BateryLevel(pResponse->TEMP_HUM.subtype == sTypeTH8, pResponse->TEMP_HUM.battery_level);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_TempHumBaro(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeTEMP_HUM_BARO;
	unsigned char subType = pResponse->TEMP_HUM_BARO.subtype;
	sprintf(szTmp, "%d", (pResponse->TEMP_HUM_BARO.id1 * 256) + pResponse->TEMP_HUM_BARO.id2);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->TEMP_HUM_BARO.id2;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->TEMP_HUM_BARO.rssi;
	unsigned char BatteryLevel;
	if ((pResponse->TEMP_HUM_BARO.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;
	//Override battery level if hardware supports it
	if (HwdType == HTYPE_OpenZWave)
	{
		BatteryLevel = pResponse->TEMP.battery_level;
	}

	float temp;
	if (!pResponse->TEMP_HUM_BARO.tempsign)
	{
		temp = float((pResponse->TEMP_HUM_BARO.temperatureh * 256) + pResponse->TEMP_HUM_BARO.temperaturel) / 10.0f;
	}
	else
	{
		temp = -(float(((pResponse->TEMP_HUM_BARO.temperatureh & 0x7F) * 256) + pResponse->TEMP_HUM_BARO.temperaturel) / 10.0f);
	}
	if ((temp < -200) || (temp > 380))
	{
		WriteMessage(" Invalid Temperature");
		return;
	}

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	temp += AddjValue;

	unsigned char Humidity = pResponse->TEMP_HUM_BARO.humidity;
	unsigned char HumidityStatus = pResponse->TEMP_HUM_BARO.humidity_status;

	if (Humidity > 100)
	{
		WriteMessage(" Invalid Humidity");
		return;
	}

	int barometer = (pResponse->TEMP_HUM_BARO.baroh * 256) + pResponse->TEMP_HUM_BARO.barol;

	int forcast = pResponse->TEMP_HUM_BARO.forecast;
	float fbarometer = (float)barometer;

	m_sql.GetAddjustment2(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	barometer += int(AddjValue);

	if (pResponse->TEMP_HUM_BARO.subtype == sTypeTHBFloat)
	{
		if ((barometer < 8000) || (barometer > 12000))
		{
			WriteMessage(" Invalid Barometer");
			return;
		}
		fbarometer = float((pResponse->TEMP_HUM_BARO.baroh * 256) + pResponse->TEMP_HUM_BARO.barol) / 10.0f;
		fbarometer += AddjValue;
		sprintf(szTmp, "%.1f;%d;%d;%.1f;%d", temp, Humidity, HumidityStatus, fbarometer, forcast);
	}
	else
	{
		if ((barometer < 800) || (barometer > 1200))
		{
			WriteMessage(" Invalid Barometer");
			return;
		}
		sprintf(szTmp, "%.1f;%d;%d;%d;%d", temp, Humidity, HumidityStatus, barometer, forcast);
	}
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	//calculate Altitude
	//float seaLevelPressure=101325.0f;
	//float altitude = 44330.0f * (1.0f - pow(fbarometer / seaLevelPressure, 0.1903f));

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->TEMP_HUM_BARO.subtype)
		{
		case sTypeTHB1:
			WriteMessage("subtype       = THB1 - BTHR918, BTHGN129");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM_BARO.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTHB2:
			WriteMessage("subtype       = THB2 - BTHR918N, BTHR968");
			sprintf(szTmp, "                channel %d", pResponse->TEMP_HUM_BARO.id2);
			WriteMessage(szTmp);
			break;
		case sTypeTHBFloat:
			WriteMessage("subtype       = Weather Station");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->TEMP_HUM_BARO.packettype, pResponse->TEMP_HUM_BARO.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->TEMP_HUM_BARO.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "ID            = %d", (pResponse->TEMP_HUM_BARO.id1 * 256) + pResponse->TEMP_HUM_BARO.id2);
		WriteMessage(szTmp);

		double tvalue = ConvertTemperature(temp, m_sql.m_tempsign[0]);
		sprintf(szTmp, "Temperature   = %.1f C", tvalue);
		WriteMessage(szTmp);

		sprintf(szTmp, "Humidity      = %d %%", pResponse->TEMP_HUM_BARO.humidity);
		WriteMessage(szTmp);

		switch (pResponse->TEMP_HUM_BARO.humidity_status)
		{
		case humstat_normal:
			WriteMessage("Status        = Normal");
			break;
		case humstat_comfort:
			WriteMessage("Status        = Comfortable");
			break;
		case humstat_dry:
			WriteMessage("Status        = Dry");
			break;
		case humstat_wet:
			WriteMessage("Status        = Wet");
			break;
		}

		if (pResponse->TEMP_HUM_BARO.subtype == sTypeTHBFloat)
			sprintf(szTmp, "Barometer     = %.1f hPa", float((pResponse->TEMP_HUM_BARO.baroh * 256) + pResponse->TEMP_HUM_BARO.barol) / 10.0f);
		else
			sprintf(szTmp, "Barometer     = %d hPa", (pResponse->TEMP_HUM_BARO.baroh * 256) + pResponse->TEMP_HUM_BARO.barol);
		WriteMessage(szTmp);

		switch (pResponse->TEMP_HUM_BARO.forecast)
		{
		case baroForecastNoInfo:
			WriteMessage("Forecast      = No information available");
			break;
		case baroForecastSunny:
			WriteMessage("Forecast      = Sunny");
			break;
		case baroForecastPartlyCloudy:
			WriteMessage("Forecast      = Partly Cloudy");
			break;
		case baroForecastCloudy:
			WriteMessage("Forecast      = Cloudy");
			break;
		case baroForecastRain:
			WriteMessage("Forecast      = Rain");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->TEMP_HUM_BARO.rssi);
		WriteMessage(szTmp);

		if ((pResponse->TEMP_HUM_BARO.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_TempBaro(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeTEMP_BARO;
	unsigned char subType = sTypeBMP085;
	_tTempBaro *pTempBaro = (_tTempBaro*)pResponse;

	sprintf(szTmp, "%d", pTempBaro->id1);
	std::string ID = szTmp;
	unsigned char Unit = 1;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel;
	BatteryLevel = 100;

	float temp = pTempBaro->temp;
	if ((temp < -200) || (temp > 380))
	{
		WriteMessage(" Invalid Temperature");
		return;
	}

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	temp += AddjValue;

	float fbarometer = pTempBaro->baro;
	int forcast = pTempBaro->forecast;
	m_sql.GetAddjustment2(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	fbarometer += AddjValue;

	sprintf(szTmp, "%.1f;%.1f;%d;%.2f", temp, fbarometer, forcast, pTempBaro->altitude);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->TEMP_HUM_BARO.subtype)
		{
		case sTypeBMP085:
			WriteMessage("subtype       = BMP085 I2C");
			sprintf(szTmp, "                channel %d", pTempBaro->id1);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", devType, subType);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Temperature   = %.1f C", temp);
		WriteMessage(szTmp);

		sprintf(szTmp, "Barometer     = %.1f hPa", fbarometer);
		WriteMessage(szTmp);

		switch (pTempBaro->forecast)
		{
		case baroForecastNoInfo:
			WriteMessage("Forecast      = No information available");
			break;
		case baroForecastSunny:
			WriteMessage("Forecast      = Sunny");
			break;
		case baroForecastPartlyCloudy:
			WriteMessage("Forecast      = Partly Cloudy");
			break;
		case baroForecastCloudy:
			WriteMessage("Forecast      = Cloudy");
			break;
		case baroForecastRain:
			WriteMessage("Forecast      = Rain");
			break;
		}

		if (pResponse->TEMP_HUM_BARO.subtype == sTypeBMP085)
		{
			sprintf(szTmp, "Altitude   = %.2f meter", pTempBaro->altitude);
			WriteMessage(szTmp);
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_TempRain(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];

	//We are (also) going to split this device into two separate sensors (temp + rain)

	unsigned char devType = pTypeTEMP_RAIN;
	unsigned char subType = pResponse->TEMP_RAIN.subtype;

	sprintf(szTmp, "%d", (pResponse->TEMP_RAIN.id1 * 256) + pResponse->TEMP_RAIN.id2);
	std::string ID = szTmp;
	int Unit = pResponse->TEMP_RAIN.id2;
	int cmnd = 0;

	unsigned char SignalLevel = pResponse->TEMP_RAIN.rssi;
	unsigned char BatteryLevel = 0;
	if ((pResponse->TEMP_RAIN.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;

	float temp;
	if (!pResponse->TEMP_RAIN.tempsign)
	{
		temp = float((pResponse->TEMP_RAIN.temperatureh * 256) + pResponse->TEMP_RAIN.temperaturel) / 10.0f;
	}
	else
	{
		temp = -(float(((pResponse->TEMP_RAIN.temperatureh & 0x7F) * 256) + pResponse->TEMP_RAIN.temperaturel) / 10.0f);
	}

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, pTypeTEMP, sTypeTEMP3, AddjValue, AddjMulti);
	temp += AddjValue;

	if ((temp < -200) || (temp > 380))
	{
		WriteMessage(" Invalid Temperature");
		return;
	}
	float TotalRain = float((pResponse->TEMP_RAIN.raintotal1 * 256) + pResponse->TEMP_RAIN.raintotal2) / 10.0f;

	sprintf(szTmp, "%.1f;%.1f", temp, TotalRain);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	sprintf(szTmp, "%.1f", temp);
	uint64_t DevRowIdxTemp = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, pTypeTEMP, sTypeTEMP3, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, pTypeTEMP, sTypeTEMP3, temp);

	sprintf(szTmp, "%d;%.1f", 0, TotalRain);
	uint64_t DevRowIdxRain = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, pTypeRAIN, sTypeRAIN3, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, pTypeRAIN, sTypeRAIN3, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->TEMP_RAIN.subtype)
		{
		case sTypeTR1:
			WriteMessage("Subtype       = Alecto WS1200");
			break;
		}
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->TEMP_RAIN.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->TEMP_RAIN.id1 * 256) + pResponse->TEMP_RAIN.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Temperature   = %.1f C", temp);
		WriteMessage(szTmp);
		sprintf(szTmp, "Total Rain    = %.1f mm", TotalRain);
		WriteMessage(szTmp);

		sprintf(szTmp, "Signal level  = %d", pResponse->TEMP_RAIN.rssi);
		WriteMessage(szTmp);

		if ((pResponse->TEMP_RAIN.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}

	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_UV(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeUV;
	unsigned char subType = pResponse->UV.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->UV.id1 * 256) + pResponse->UV.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->UV.rssi;
	unsigned char BatteryLevel;
	if ((pResponse->UV.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;
	float Level = float(pResponse->UV.uv) / 10.0f;
	if (Level > 30)
	{
		WriteMessage(" Invalid UV");
		return;
	}
	float temp = 0;
	if (pResponse->UV.subtype == sTypeUV3)
	{
		if (!pResponse->UV.tempsign)
		{
			temp = float((pResponse->UV.temperatureh * 256) + pResponse->UV.temperaturel) / 10.0f;
		}
		else
		{
			temp = -(float(((pResponse->UV.temperatureh & 0x7F) * 256) + pResponse->UV.temperaturel) / 10.0f);
		}
		if ((temp < -200) || (temp > 380))
		{
			WriteMessage(" Invalid Temperature");
			return;
		}

		float AddjValue = 0.0f;
		float AddjMulti = 1.0f;
		m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
		temp += AddjValue;
	}

	sprintf(szTmp, "%.1f;%.1f", Level, temp);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->UV.subtype)
		{
		case sTypeUV1:
			WriteMessage("Subtype       = UV1 - UVN128, UV138");
			break;
		case sTypeUV2:
			WriteMessage("Subtype       = UV2 - UVN800");
			break;
		case sTypeUV3:
			WriteMessage("Subtype       = UV3 - TFA");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->UV.packettype, pResponse->UV.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->UV.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->UV.id1 * 256) + pResponse->UV.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Level         = %.1f UVI", Level);
		WriteMessage(szTmp);

		if (pResponse->UV.subtype == sTypeUV3)
		{
			sprintf(szTmp, "Temperature   = %.1f C", temp);
			WriteMessage(szTmp);
		}

		if (pResponse->UV.uv < 3)
			WriteMessage("Description = Low");
		else if (pResponse->UV.uv < 6)
			WriteMessage("Description = Medium");
		else if (pResponse->UV.uv < 8)
			WriteMessage("Description = High");
		else if (pResponse->UV.uv < 11)
			WriteMessage("Description = Very high");
		else
			WriteMessage("Description = Dangerous");

		sprintf(szTmp, "Signal level  = %d", pResponse->UV.rssi);
		WriteMessage(szTmp);

		if ((pResponse->UV.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Lighting1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeLighting1;
	unsigned char subType = pResponse->LIGHTING1.subtype;
	sprintf(szTmp, "%d", pResponse->LIGHTING1.housecode);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->LIGHTING1.unitcode;
	unsigned char cmnd = pResponse->LIGHTING1.cmnd;
	unsigned char SignalLevel = pResponse->LIGHTING1.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->LIGHTING1.subtype)
		{
		case sTypeX10:
			WriteMessage("subtype       = X10");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING1.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "housecode     = %c", pResponse->LIGHTING1.housecode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->LIGHTING1.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING1.cmnd)
			{
			case light1_sOff:
				WriteMessage("Off");
				break;
			case light1_sOn:
				WriteMessage("On");
				break;
			case light1_sDim:
				WriteMessage("Dim");
				break;
			case light1_sBright:
				WriteMessage("Bright");
				break;
			case light1_sAllOn:
				WriteMessage("All On");
				break;
			case light1_sAllOff:
				WriteMessage("All Off");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeARC:
			WriteMessage("subtype       = ARC");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING1.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "housecode     = %c", pResponse->LIGHTING1.housecode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->LIGHTING1.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING1.cmnd)
			{
			case light1_sOff:
				WriteMessage("Off");
				break;
			case light1_sOn:
				WriteMessage("On");
				break;
			case light1_sAllOn:
				WriteMessage("All On");
				break;
			case light1_sAllOff:
				WriteMessage("All Off");
				break;
			case light1_sChime:
				WriteMessage("Chime");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeAB400D:
		case sTypeWaveman:
		case sTypeEMW200:
		case sTypeIMPULS:
		case sTypeRisingSun:
		case sTypeEnergenie:
		case sTypeEnergenie5:
		case sTypeGDR2:
		case sTypeHQ:
			//decoding of these types is only implemented for use by simulate and verbose
			//these types are not received by the RFXtrx433
			switch (pResponse->LIGHTING1.subtype)
			{
			case sTypeAB400D:
				WriteMessage("subtype       = ELRO AB400");
				break;
			case sTypeWaveman:
				WriteMessage("subtype       = Waveman");
				break;
			case sTypeEMW200:
				WriteMessage("subtype       = EMW200");
				break;
			case sTypeIMPULS:
				WriteMessage("subtype       = IMPULS");
				break;
			case sTypeRisingSun:
				WriteMessage("subtype       = RisingSun");
				break;
			case sTypeEnergenie:
				WriteMessage("subtype       = Energenie-ENER010");
				break;
			case sTypeEnergenie5:
				WriteMessage("subtype       = Energenie 5-gang");
				break;
			case sTypeGDR2:
				WriteMessage("subtype       = COCO GDR2");
				break;
			case sTypeHQ:
				WriteMessage("subtype       = HQ COCO-20");
				break;
			}
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING1.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "housecode     = %c", pResponse->LIGHTING1.housecode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->LIGHTING1.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);

			switch (pResponse->LIGHTING1.cmnd)
			{
			case light1_sOff:
				WriteMessage("Off");
				break;
			case light1_sOn:
				WriteMessage("On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypePhilips:
			//decoding of this type is only implemented for use by simulate and verbose
			//this type is not received by the RFXtrx433
			WriteMessage("subtype       = Philips SBC");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING1.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "housecode     = %c", pResponse->LIGHTING1.housecode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->LIGHTING1.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);

			switch (pResponse->LIGHTING1.cmnd)
			{
			case light1_sOff:
				WriteMessage("Off");
				break;
			case light1_sOn:
				WriteMessage("On");
				break;
			case light1_sAllOff:
				WriteMessage("All Off");
				break;
			case light1_sAllOn:
				WriteMessage("All On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING1.packettype, pResponse->LIGHTING1.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING1.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Lighting2(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeLighting2;
	unsigned char subType = pResponse->LIGHTING2.subtype;
	sprintf(szTmp, "%X%02X%02X%02X", pResponse->LIGHTING2.id1, pResponse->LIGHTING2.id2, pResponse->LIGHTING2.id3, pResponse->LIGHTING2.id4);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->LIGHTING2.unitcode;
	unsigned char cmnd = pResponse->LIGHTING2.cmnd;
	unsigned char level = pResponse->LIGHTING2.level;
	unsigned char SignalLevel = pResponse->LIGHTING2.rssi;

	sprintf(szTmp, "%d", level);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);

	bool isGroupCommand = ((cmnd == light2_sGroupOff) || (cmnd == light2_sGroupOn));
	unsigned char single_cmnd = cmnd;

	if (isGroupCommand)
	{
		single_cmnd = (cmnd == light2_sGroupOff) ? light2_sOff : light2_sOn;

		// We write the GROUP_CMD into the log to differentiate between manual turn off/on and group_off/group_on
		m_sql.UpdateValueLighting2GroupCmd(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);

		//set the status of all lights with the same code to on/off
		m_sql.Lighting2GroupCmd(ID, subType, single_cmnd);
	}

	if (DevRowIdx == -1) {
		// not found nothing to do
		return;
	}
	CheckSceneCode(DevRowIdx, devType, subType, single_cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->LIGHTING2.subtype)
		{
		case sTypeAC:
		case sTypeHEU:
		case sTypeANSLUT:
			switch (pResponse->LIGHTING2.subtype)
			{
			case sTypeAC:
				WriteMessage("subtype       = AC");
				break;
			case sTypeHEU:
				WriteMessage("subtype       = HomeEasy EU");
				break;
			case sTypeANSLUT:
				WriteMessage("subtype       = ANSLUT");
				break;
			}
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING2.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %s", ID.c_str());
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", Unit);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING2.cmnd)
			{
			case light2_sOff:
				WriteMessage("Off");
				break;
			case light2_sOn:
				WriteMessage("On");
				break;
			case light2_sSetLevel:
				sprintf(szTmp, "Set Level: %d", level);
				WriteMessage(szTmp);
				break;
			case light2_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light2_sGroupOn:
				WriteMessage("Group On");
				break;
			case light2_sSetGroupLevel:
				sprintf(szTmp, "Set Group Level: %d", level);
				WriteMessage(szTmp);
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING2.packettype, pResponse->LIGHTING2.subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

//not in dbase yet
void MainWorker::decode_Lighting3(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessageStart();
	WriteMessage("");

	char szTmp[100];

	switch (pResponse->LIGHTING3.subtype)
	{
	case sTypeKoppla:
		WriteMessage("subtype       = Ikea Koppla");
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING3.seqnbr);
		WriteMessage(szTmp);
		WriteMessage("Command       = ", false);
		switch (pResponse->LIGHTING3.cmnd)
		{
		case 0x0:
			WriteMessage("Off");
			break;
		case 0x1:
			WriteMessage("On");
			break;
		case 0x20:
			sprintf(szTmp, "Set Level: %d", pResponse->LIGHTING3.channel10_9);
			WriteMessage(szTmp);
			break;
		case 0x21:
			WriteMessage("Program");
			break;
		default:
			if ((pResponse->LIGHTING3.cmnd >= 0x10) && (pResponse->LIGHTING3.cmnd < 0x18))
				WriteMessage("Dim");
			else if ((pResponse->LIGHTING3.cmnd >= 0x18) && (pResponse->LIGHTING3.cmnd < 0x20))
				WriteMessage("Bright");
			else
				WriteMessage("UNKNOWN");
			break;
		}
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING3.packettype, pResponse->LIGHTING3.subtype);
		WriteMessage(szTmp);
		break;
	}
	sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING3.rssi);
	WriteMessage(szTmp);
	WriteMessageEnd();
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Lighting4(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeLighting4;
	unsigned char subType = pResponse->LIGHTING4.subtype;
	sprintf(szTmp, "%02X%02X%02X", pResponse->LIGHTING4.cmd1, pResponse->LIGHTING4.cmd2, pResponse->LIGHTING4.cmd3);
	std::string ID = szTmp;
	int Unit = 0;
	unsigned char cmnd = 1; //only 'On' supported
	unsigned char SignalLevel = pResponse->LIGHTING4.rssi;
	sprintf(szTmp, "%d", (pResponse->LIGHTING4.pulseHigh * 256) + pResponse->LIGHTING4.pulseLow);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->LIGHTING4.subtype)
		{
		case sTypePT2262:
			WriteMessage("subtype       = PT2262");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING4.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "Code          = %02X%02X%02X", pResponse->LIGHTING4.cmd1, pResponse->LIGHTING4.cmd2, pResponse->LIGHTING4.cmd3);
			WriteMessage(szTmp);

			WriteMessage("S1- S24  = ", false);
			if ((pResponse->LIGHTING4.cmd1 & 0x80) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x40) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x20) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x10) == 0)
				WriteMessage("0 ", false);
			else
				WriteMessage("1 ", false);


			if ((pResponse->LIGHTING4.cmd1 & 0x08) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x04) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x02) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd1 & 0x01) == 0)
				WriteMessage("0 ", false);
			else
				WriteMessage("1 ", false);


			if ((pResponse->LIGHTING4.cmd2 & 0x80) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x40) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x20) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x10) == 0)
				WriteMessage("0 ", false);
			else
				WriteMessage("1 ", false);


			if ((pResponse->LIGHTING4.cmd2 & 0x08) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x04) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x02) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd2 & 0x01) == 0)
				WriteMessage("0 ", false);
			else
				WriteMessage("1 ", false);


			if ((pResponse->LIGHTING4.cmd3 & 0x80) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x40) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x20) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x10) == 0)
				WriteMessage("0 ", false);
			else
				WriteMessage("1 ", false);


			if ((pResponse->LIGHTING4.cmd3 & 0x08) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x04) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x02) == 0)
				WriteMessage("0", false);
			else
				WriteMessage("1", false);

			if ((pResponse->LIGHTING4.cmd3 & 0x01) == 0)
				WriteMessage("0");
			else
				WriteMessage("1");

			sprintf(szTmp, "Pulse         = %d usec", (pResponse->LIGHTING4.pulseHigh * 256) + pResponse->LIGHTING4.pulseLow);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING4.packettype, pResponse->LIGHTING4.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING4.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Lighting5(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeLighting5;
	unsigned char subType = pResponse->LIGHTING5.subtype;
	if ((subType != sTypeEMW100) && (subType != sTypeLivolo) && (subType != sTypeLivoloAppliance) && (subType != sTypeRGB432W) && (subType != sTypeKangtai))
		sprintf(szTmp, "%02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
	else
		sprintf(szTmp, "%02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->LIGHTING5.unitcode;
	unsigned char cmnd = pResponse->LIGHTING5.cmnd;
	float flevel;
	if (subType == sTypeLivolo)
		flevel = (100.0f / 7.0f)*float(pResponse->LIGHTING5.level);
	else if (subType == sTypeLightwaveRF)
		flevel = (100.0f / 31.0f)*float(pResponse->LIGHTING5.level);
	else
		flevel = (100.0f / 7.0f)*float(pResponse->LIGHTING5.level);
	unsigned char SignalLevel = pResponse->LIGHTING5.rssi;

	bool bDoUpdate = true;
	if ((subType == sTypeTRC02) || (subType == sTypeTRC02_2) || (subType == sTypeAoke) || (subType == sTypeEurodomest))
	{
		if (
			(pResponse->LIGHTING5.cmnd != light5_sOff) &&
			(pResponse->LIGHTING5.cmnd != light5_sOn) &&
			(pResponse->LIGHTING5.cmnd != light5_sGroupOff) &&
			(pResponse->LIGHTING5.cmnd != light5_sGroupOn)
			)
		{
			bDoUpdate = false;
		}
	}
	uint64_t DevRowIdx = -1;
	if (bDoUpdate)
	{
		sprintf(szTmp, "%d", pResponse->LIGHTING5.level);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
		CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);
	}

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->LIGHTING5.subtype)
		{
		case sTypeLightwaveRF:
			WriteMessage("subtype       = LightwaveRF");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sMood1:
				WriteMessage("Group Mood 1");
				break;
			case light5_sMood2:
				WriteMessage("Group Mood 2");
				break;
			case light5_sMood3:
				WriteMessage("Group Mood 3");
				break;
			case light5_sMood4:
				WriteMessage("Group Mood 4");
				break;
			case light5_sMood5:
				WriteMessage("Group Mood 5");
				break;
			case light5_sUnlock:
				WriteMessage("Unlock");
				break;
			case light5_sLock:
				WriteMessage("Lock");
				break;
			case light5_sAllLock:
				WriteMessage("All lock");
				break;
			case light5_sClose:
				WriteMessage("Close inline relay");
				break;
			case light5_sStop:
				WriteMessage("Stop inline relay");
				break;
			case light5_sOpen:
				WriteMessage("Open inline relay");
				break;
			case light5_sSetLevel:
				sprintf(szTmp, "Set dim level to: %.2f %%", flevel);
				WriteMessage(szTmp);
				break;
			case light5_sColourPalette:
				if (pResponse->LIGHTING5.level == 0)
					WriteMessage("Color Palette (Even command)");
				else
					WriteMessage("Color Palette (Odd command)");
				break;
			case light5_sColourTone:
				if (pResponse->LIGHTING5.level == 0)
					WriteMessage("Color Tone (Even command)");
				else
					WriteMessage("Color Tone (Odd command)");
				break;
			case light5_sColourCycle:
				if (pResponse->LIGHTING5.level == 0)
					WriteMessage("Color Cycle (Even command)");
				else
					WriteMessage("Color Cycle (Odd command)");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeEMW100:
			WriteMessage("subtype       = EMW100");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sLearn:
				WriteMessage("Learn");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeBBSB:
			WriteMessage("subtype       = BBSB new");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeRSL:
			WriteMessage("subtype       = Conrad RSL");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeLivolo:
			WriteMessage("subtype       = Livolo Dimmer");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeLivoloAppliance:
			WriteMessage("subtype       = Livolo On/Off module");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sLivoloAllOff:
				WriteMessage("Off");
				break;
			case light5_sLivoloGang1Toggle:
				WriteMessage("Toggle");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeRGB432W:
			WriteMessage("subtype       = RGB432W");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sRGBoff:
				WriteMessage("Off");
				break;
			case light5_sRGBon:
				WriteMessage("On");
				break;
			case light5_sRGBbright:
				WriteMessage("Bright+");
				break;
			case light5_sRGBdim:
				WriteMessage("Bright-");
				break;
			case light5_sRGBcolorplus:
				WriteMessage("Color+");
				break;
			case light5_sRGBcolormin:
				WriteMessage("Color-");
				break;
			default:
				sprintf(szTmp, "Color =          = %d", pResponse->LIGHTING5.cmnd);
				WriteMessage(szTmp);
				break;
			}
			break;
		case sTypeTRC02:
		case sTypeTRC02_2:
			if (pResponse->LIGHTING5.subtype == sTypeTRC02)
				WriteMessage("subtype       = TRC02 (RGB)");
			else
				WriteMessage("subtype       = TRC02_2 (RGB)");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sRGBbright:
				WriteMessage("Bright+");
				break;
			case light5_sRGBdim:
				WriteMessage("Bright-");
				break;
			case light5_sRGBcolorplus:
				WriteMessage("Color+");
				break;
			case light5_sRGBcolormin:
				WriteMessage("Color-");
				break;
			default:
				sprintf(szTmp, "Color = %d", pResponse->LIGHTING5.cmnd);
				WriteMessage(szTmp);
				break;
			}
			break;
		case sTypeAoke:
			WriteMessage("subtype       = Aoke");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeEurodomest:
			WriteMessage("subtype       = Eurodomest");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeLegrandCAD:
			WriteMessage("subtype       = Legrand CAD");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			WriteMessage("Command       = Toggle");
			break;
		case sTypeAvantek:
			WriteMessage("subtype       = Avantek");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("Unknown");
				break;
			}
			break;
		case sTypeIT:
			WriteMessage("subtype       = Intertek,FA500,PROmax");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			case light5_sSetLevel:
				sprintf(szTmp, "Set dim level to: %.2f %%", flevel);
				WriteMessage(szTmp);
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeKangtai:
			WriteMessage("subtype       = Kangtai / Cotech");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING5.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->LIGHTING5.id1, pResponse->LIGHTING5.id2, pResponse->LIGHTING5.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "Unit          = %d", pResponse->LIGHTING5.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING5.cmnd)
			{
			case light5_sOff:
				WriteMessage("Off");
				break;
			case light5_sOn:
				WriteMessage("On");
				break;
			case light5_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light5_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING5.packettype, pResponse->LIGHTING5.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING5.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Lighting6(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeLighting6;
	unsigned char subType = pResponse->LIGHTING6.subtype;
	sprintf(szTmp, "%02X%02X%02X", pResponse->LIGHTING6.id1, pResponse->LIGHTING6.id2, pResponse->LIGHTING6.groupcode);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->LIGHTING6.unitcode;
	unsigned char cmnd = pResponse->LIGHTING6.cmnd;
	unsigned char rfu = pResponse->LIGHTING6.seqnbr2;
	unsigned char SignalLevel = pResponse->LIGHTING6.rssi;

	sprintf(szTmp, "%d", rfu);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->LIGHTING6.subtype)
		{
		case sTypeBlyss:
			WriteMessage("subtype       = Blyss");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->LIGHTING6.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->LIGHTING6.id1, pResponse->LIGHTING6.id2);
			WriteMessage(szTmp);
			sprintf(szTmp, "groupcode     = %d", pResponse->LIGHTING6.groupcode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->LIGHTING6.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->LIGHTING6.cmnd)
			{
			case light6_sOff:
				WriteMessage("Off");
				break;
			case light6_sOn:
				WriteMessage("On");
				break;
			case light6_sGroupOff:
				WriteMessage("Group Off");
				break;
			case light6_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			sprintf(szTmp, "Command seqnbr= %d", pResponse->LIGHTING6.cmndseqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "seqnbr2       = %d", pResponse->LIGHTING6.seqnbr2);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING6.packettype, pResponse->LIGHTING6.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING6.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Fan(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeFan;
	unsigned char subType = pResponse->FAN.subtype;
	sprintf(szTmp, "%02X%02X%02X", pResponse->FAN.id1, pResponse->FAN.id2, pResponse->FAN.id3);
	std::string ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = pResponse->FAN.cmnd;
	unsigned char SignalLevel = pResponse->FAN.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->FAN.subtype)
		{
		case sTypeSiemensSF01:
			WriteMessage("subtype       = Siemens SF01");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->FAN.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->FAN.id2, pResponse->FAN.id3);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->FAN.cmnd)
			{
			case fan_sTimer:
				WriteMessage("Timer");
				break;
			case fan_sMin:
				WriteMessage("-");
				break;
			case fan_sLearn:
				WriteMessage("Learn");
				break;
			case fan_sPlus:
				WriteMessage("+");
				break;
			case fan_sConfirm:
				WriteMessage("Confirm");
				break;
			case fan_sLight:
				WriteMessage("Light");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeItho:
			WriteMessage("subtype       = Itho CVE RFT");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->FAN.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->FAN.id1, pResponse->FAN.id2, pResponse->FAN.id3);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->FAN.cmnd)
			{
			case fan_Itho1:
				WriteMessage("1");
				break;
			case fan_Itho2:
				WriteMessage("2");
				break;
			case fan_Itho3:
				WriteMessage("3");
				break;
			case fan_IthoTimer:
				WriteMessage("Timer");
				break;
			case fan_IthoNotAtHome:
				WriteMessage("Not at Home");
				break;
			case fan_IthoLearn:
				WriteMessage("Learn");
				break;
			case fan_IthoEraseAll:
				WriteMessage("Erase all remotes");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		case sTypeLucciAir:
			WriteMessage("subtype       = Lucci Air");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->LIGHTING6.packettype, pResponse->LIGHTING6.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->LIGHTING6.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_HomeConfort(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeHomeConfort;
	unsigned char subType = pResponse->HOMECONFORT.subtype;
	sprintf(szTmp, "%02X%02X%02X%02X", pResponse->HOMECONFORT.id1, pResponse->HOMECONFORT.id2, pResponse->HOMECONFORT.id3, pResponse->HOMECONFORT.housecode);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->HOMECONFORT.unitcode;
	unsigned char cmnd = pResponse->HOMECONFORT.cmnd;
	unsigned char SignalLevel = pResponse->HOMECONFORT.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);

	bool isGroupCommand = ((cmnd == HomeConfort_sGroupOff) || (cmnd == HomeConfort_sGroupOn));
	unsigned char single_cmnd = cmnd;

	if (isGroupCommand)
	{
		single_cmnd = (cmnd == HomeConfort_sGroupOff) ? HomeConfort_sOff : HomeConfort_sOn;

		// We write the GROUP_CMD into the log to differentiate between manual turn off/on and group_off/group_on
		m_sql.UpdateValueHomeConfortGroupCmd(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);

		//set the status of all lights with the same code to on/off
		m_sql.HomeConfortGroupCmd(ID, subType, single_cmnd);
	}

	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->HOMECONFORT.subtype)
		{
		case sTypeHomeConfortTEL010:
			WriteMessage("subtype       = TEL-010");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->HOMECONFORT.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->HOMECONFORT.id1, pResponse->HOMECONFORT.id2, pResponse->HOMECONFORT.id3);
			WriteMessage(szTmp);
			sprintf(szTmp, "housecode     = %d", pResponse->HOMECONFORT.housecode);
			WriteMessage(szTmp);
			sprintf(szTmp, "unitcode      = %d", pResponse->HOMECONFORT.unitcode);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->HOMECONFORT.cmnd)
			{
			case HomeConfort_sOff:
				WriteMessage("Off");
				break;
			case HomeConfort_sOn:
				WriteMessage("On");
				break;
			case HomeConfort_sGroupOff:
				WriteMessage("Group Off");
				break;
			case HomeConfort_sGroupOn:
				WriteMessage("Group On");
				break;
			default:
				WriteMessage("UNKNOWN");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->HOMECONFORT.packettype, pResponse->HOMECONFORT.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->HOMECONFORT.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_ColorSwitch(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[300];
	_tColorSwitch *pLed = (_tColorSwitch*)pResponse;

	unsigned char devType = pTypeColorSwitch;
	unsigned char subType = pLed->subtype;
	if (pLed->id == 1)
		sprintf(szTmp, "%d", 1);
	else
		sprintf(szTmp, "%08X", (unsigned int)pLed->id);
	std::string ID = szTmp;
	unsigned char Unit = pLed->dunit;
	unsigned char cmnd = pLed->command;
	uint32_t value = pLed->value;
	_tColor color = pLed->color;

	char szValueTmp[100];
	sprintf(szValueTmp, "%u", value);
	std::string sValue = szValueTmp;
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, 12, -1, cmnd, sValue.c_str(), procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	// TODO: Why don't we let database update be handled by CSQLHelper::UpdateValue?
	if (cmnd == Color_SetBrightnessLevel || cmnd == Color_SetColor)
	{
		std::vector<std::vector<std::string> > result;
		result = m_sql.safe_query(
			"SELECT ID,Name FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
			HwdID, ID.c_str(), Unit, devType, subType);
		if (result.size() != 0)
		{
			uint64_t ulID;
			std::stringstream s_str(result[0][0]);
			s_str >> ulID;

			//store light level
			m_sql.safe_query(
				"UPDATE DeviceStatus SET LastLevel='%d' WHERE (ID = %" PRIu64 ")",
				value,
				ulID);
		}

	}

	if (cmnd == Color_SetColor)
	{
		std::vector<std::vector<std::string> > result;
		result = m_sql.safe_query(
			"SELECT ID,Name FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
			HwdID, ID.c_str(), Unit, devType, subType);
		if (result.size() != 0)
		{
			uint64_t ulID;
			std::stringstream s_str(result[0][0]);
			s_str >> ulID;

			//store color in database
			m_sql.safe_query(
				"UPDATE DeviceStatus SET Color='%q' WHERE (ID = %" PRIu64 ")",
				color.toJSONString().c_str(),
				ulID);
		}

	}

	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Chime(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeChime;
	unsigned char subType = pResponse->CHIME.subtype;
	sprintf(szTmp, "%02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->CHIME.sound;
	unsigned char cmnd = pResponse->CHIME.sound;
	unsigned char SignalLevel = pResponse->CHIME.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->CHIME.subtype)
		{
		case sTypeByronSX:
			WriteMessage("subtype       = Byron SX");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->CHIME.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
			WriteMessage(szTmp);
			WriteMessage("Sound          = ", false);
			switch (pResponse->CHIME.sound)
			{
			case chime_sound0:
			case chime_sound4:
				WriteMessage("Tubular 3 notes");
				break;
			case chime_sound1:
			case chime_sound5:
				WriteMessage("Big Ben");
				break;
			case chime_sound2:
			case chime_sound6:
				WriteMessage("Tubular 3 notes");
				break;
			case chime_sound3:
			case chime_sound7:
				WriteMessage("Solo");
				break;
			default:
				WriteMessage("UNKNOWN?");
				break;
			}
			break;
		case sTypeByronMP001:
			WriteMessage("subtype       = Byron MP001");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->CHIME.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
			WriteMessage(szTmp);
			sprintf(szTmp, "Sound          = %d", pResponse->CHIME.sound);
			WriteMessage(szTmp);

			if ((pResponse->CHIME.id1 & 0x40) == 0x40)
				WriteMessage("Switch 1      = Off");
			else
				WriteMessage("Switch 1      = On");

			if ((pResponse->CHIME.id1 & 0x10) == 0x10)
				WriteMessage("Switch 2      = Off");
			else
				WriteMessage("Switch 2      = On");

			if ((pResponse->CHIME.id1 & 0x04) == 0x04)
				WriteMessage("Switch 3      = Off");
			else
				WriteMessage("Switch 3      = On");

			if ((pResponse->CHIME.id1 & 0x01) == 0x01)
				WriteMessage("Switch 4      = Off");
			else
				WriteMessage("Switch 4      = On");

			if ((pResponse->CHIME.id2 & 0x40) == 0x40)
				WriteMessage("Switch 5      = Off");
			else
				WriteMessage("Switch 5      = On");

			if ((pResponse->CHIME.id2 & 0x10) == 0x10)
				WriteMessage("Switch 6      = Off");
			else
				WriteMessage("Switch 6      = On");
			break;
		case sTypeSelectPlus:
			WriteMessage("subtype       = SelectPlus200689101");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->CHIME.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
			WriteMessage(szTmp);
			break;
		case sTypeSelectPlus3:
			WriteMessage("subtype       = SelectPlus200689103");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->CHIME.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
			WriteMessage(szTmp);
			break;
		case sTypeEnvivo:
			WriteMessage("subtype       = Envivo");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->CHIME.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %02X%02X", pResponse->CHIME.id1, pResponse->CHIME.id2);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->CHIME.packettype, pResponse->CHIME.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->CHIME.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}


void MainWorker::decode_UNDECODED(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];

	WriteMessage("UNDECODED ", false);

	switch (pResponse->UNDECODED.subtype)
	{
	case sTypeUac:
		WriteMessage("AC:", false);
		break;
	case sTypeUarc:
		WriteMessage("ARC:", false);
		break;
	case sTypeUati:
		WriteMessage("ATI:", false);
		break;
	case sTypeUhideki:
		WriteMessage("HIDEKI:", false);
		break;
	case sTypeUlacrosse:
		WriteMessage("LACROSSE:", false);
		break;
	case sTypeUad:
		WriteMessage("AD:", false);
		break;
	case sTypeUmertik:
		WriteMessage("MERTIK:", false);
		break;
	case sTypeUoregon1:
		WriteMessage("OREGON1:", false);
		break;
	case sTypeUoregon2:
		WriteMessage("OREGON2:", false);
		break;
	case sTypeUoregon3:
		WriteMessage("OREGON3:", false);
		break;
	case sTypeUproguard:
		WriteMessage("PROGUARD:", false);
		break;
	case sTypeUvisonic:
		WriteMessage("VISONIC:", false);
		break;
	case sTypeUnec:
		WriteMessage("NEC:", false);
		break;
	case sTypeUfs20:
		WriteMessage("FS20:", false);
		break;
	case sTypeUblinds:
		WriteMessage("Blinds:", false);
		break;
	case sTypeUrubicson:
		WriteMessage("RUBICSON:", false);
		break;
	case sTypeUae:
		WriteMessage("AE:", false);
		break;
	case sTypeUfineoffset:
		WriteMessage("FineOffset:", false);
		break;
	case sTypeUrgb:
		WriteMessage("RGB:", false);
		break;
	case sTypeUrfy:
		WriteMessage("RFY:", false);
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->UNDECODED.packettype, pResponse->UNDECODED.subtype);
		WriteMessage(szTmp);
		break;
	}
	std::stringstream sHexDump;
	unsigned char *pRXBytes = (unsigned char*)&pResponse->UNDECODED.msg1;
	for (int i = 0; i < pResponse->UNDECODED.packetlength - 3; i++)
	{
		sHexDump << HEX(pRXBytes[i]);
	}
	WriteMessage(sHexDump.str().c_str());
	procResult.DeviceRowIdx = -1;
}


void MainWorker::decode_RecXmitMessage(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];

	switch (pResponse->RXRESPONSE.subtype)
	{
	case sTypeReceiverLockError:
		WriteMessage("subtype           = Receiver lock error");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->RXRESPONSE.seqnbr);
		WriteMessage(szTmp);
		break;
	case sTypeTransmitterResponse:
		WriteMessage("subtype           = Transmitter Response");
		sprintf(szTmp, "Sequence nbr      = %d", pResponse->RXRESPONSE.seqnbr);
		WriteMessage(szTmp);

		switch (pResponse->RXRESPONSE.msg)
		{
		case 0x0:
			WriteMessage("response          = ACK, data correct transmitted");
			break;
		case 0x1:
			WriteMessage("response          = ACK, but transmit started after 6 seconds delay anyway with RF receive data detected");
			break;
		case 0x2:
			WriteMessage("response          = NAK, transmitter did not lock on the requested transmit frequency");
			break;
		case 0x3:
			WriteMessage("response          = NAK, AC address zero in id1-id4 not allowed");
			break;
		default:
			sprintf(szTmp, "ERROR: Unexpected message type= %02X", pResponse->RXRESPONSE.msg);
			WriteMessage(szTmp);
			break;
		}
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->RXRESPONSE.packettype, pResponse->RXRESPONSE.subtype);
		WriteMessage(szTmp);
		break;
	}
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Curtain(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeCurtain;
	unsigned char subType = pResponse->CURTAIN1.subtype;
	sprintf(szTmp, "%d", pResponse->CURTAIN1.housecode);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->CURTAIN1.unitcode;
	unsigned char cmnd = pResponse->CURTAIN1.cmnd;
	unsigned char SignalLevel = 9;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		char szTmp[100];

		switch (pResponse->CURTAIN1.subtype)
		{
		case sTypeHarrison:
			WriteMessage("subtype       = Harrison");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X:", pResponse->CURTAIN1.packettype, pResponse->CURTAIN1.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->CURTAIN1.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "Housecode         = %d", pResponse->CURTAIN1.housecode);
		WriteMessage(szTmp);

		sprintf(szTmp, "Unit          = %d", pResponse->CURTAIN1.unitcode);
		WriteMessage(szTmp);

		WriteMessage("Command       = ", false);

		switch (pResponse->CURTAIN1.cmnd)
		{
		case curtain_sOpen:
			WriteMessage("Open");
			break;
		case curtain_sStop:
			WriteMessage("Stop");
			break;
		case curtain_sClose:
			WriteMessage("Close");
			break;
		default:
			WriteMessage("UNKNOWN");
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_BLINDS1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeBlinds;
	unsigned char subType = pResponse->BLINDS1.subtype;

	sprintf(szTmp, "%02X%02X%02X%02X", pResponse->BLINDS1.id1, pResponse->BLINDS1.id2, pResponse->BLINDS1.id3, pResponse->BLINDS1.id4);

	std::string ID = szTmp;
	unsigned char Unit = pResponse->BLINDS1.unitcode;
	unsigned char cmnd = pResponse->BLINDS1.cmnd;
	unsigned char SignalLevel = pResponse->BLINDS1.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		char szTmp[100];

		switch (pResponse->BLINDS1.subtype)
		{
		case sTypeBlindsT0:
			WriteMessage("subtype       = Safy / RollerTrol / Hasta new");
			break;
		case sTypeBlindsT1:
			WriteMessage("subtype       = Hasta old");
			break;
		case sTypeBlindsT2:
			WriteMessage("subtype       = A-OK RF01");
			break;
		case sTypeBlindsT3:
			WriteMessage("subtype       = A-OK AC114");
			break;
		case sTypeBlindsT4:
			WriteMessage("subtype       = RAEX");
			break;
		case sTypeBlindsT5:
			WriteMessage("subtype       = Media Mount");
			break;
		case sTypeBlindsT6:
			WriteMessage("subtype       = DC106, YOOHA, Rohrmotor24 RMF");
			break;
		case sTypeBlindsT7:
			WriteMessage("subtype       = Forest");
			break;
		case sTypeBlindsT8:
			WriteMessage("subtype       = Chamberlain CS4330CN");
			break;
		case sTypeBlindsT9:
			WriteMessage("subtype       = Sunpery");
			break;
		case sTypeBlindsT10:
			WriteMessage("subtype       = Dolat DLM-1");
			break;
		case sTypeBlindsT11:
			WriteMessage("subtype       = ASP");
			break;
		case sTypeBlindsT12:
			WriteMessage("subtype       = Confexx");
			break;
		case sTypeBlindsT13:
			WriteMessage("subtype       = Screenline");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X:", pResponse->BLINDS1.packettype, pResponse->BLINDS1.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->BLINDS1.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "id1-3         = %02X%02X%02X", pResponse->BLINDS1.id1, pResponse->BLINDS1.id2, pResponse->BLINDS1.id3);
		WriteMessage(szTmp);

		if ((subType == sTypeBlindsT6) || (subType == sTypeBlindsT7))
		{
			sprintf(szTmp, "id4         = %02X", pResponse->BLINDS1.id4);
			WriteMessage(szTmp);
		}

		if (pResponse->BLINDS1.unitcode == 0)
			WriteMessage("Unit          = All");
		else
		{
			sprintf(szTmp, "Unit          = %d", pResponse->BLINDS1.unitcode);
			WriteMessage(szTmp);
		}

		WriteMessage("Command       = ", false);

		switch (pResponse->BLINDS1.cmnd)
		{
		case blinds_sOpen:
			WriteMessage("Open");
			break;
		case blinds_sStop:
			WriteMessage("Stop");
			break;
		case blinds_sClose:
			WriteMessage("Close");
			break;
		case blinds_sConfirm:
			WriteMessage("Confirm");
			break;
		case blinds_sLimit:
			WriteMessage("Set Limit");
			if (pResponse->BLINDS1.subtype == sTypeBlindsT4)
				WriteMessage("Set Upper Limit");
			else
				WriteMessage("Set Limit");
			break;
		case blinds_slowerLimit:
			WriteMessage("Set Lower Limit");
			break;
		case blinds_sDeleteLimits:
			WriteMessage("Delete Limits");
			break;
		case blinds_sChangeDirection:
			WriteMessage("Change Direction");
			break;
		case blinds_sLeft:
			WriteMessage("Left");
			break;
		case blinds_sRight:
			WriteMessage("Right");
			break;
		default:
			WriteMessage("UNKNOWN");
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->BLINDS1.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_RFY(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeRFY;
	unsigned char subType = pResponse->RFY.subtype;
	sprintf(szTmp, "%02X%02X%02X", pResponse->RFY.id1, pResponse->RFY.id2, pResponse->RFY.id3);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->RFY.unitcode;
	unsigned char cmnd = pResponse->RFY.cmnd;
	unsigned char SignalLevel = pResponse->RFY.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		char szTmp[100];

		switch (pResponse->RFY.subtype)
		{
		case sTypeRFY:
			WriteMessage("subtype       = RFY");
			break;
		case sTypeRFY2:
			WriteMessage("subtype       = RFY2");
			break;
		case sTypeRFYext:
			WriteMessage("subtype       = RFY-Ext");
			break;
		case sTypeASA:
			WriteMessage("subtype       = ASA");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X:", pResponse->RFY.packettype, pResponse->RFY.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFY.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "id1-3         = %02X%02X%02X", pResponse->RFY.id1, pResponse->RFY.id2, pResponse->RFY.id3);
		WriteMessage(szTmp);

		if (pResponse->RFY.unitcode == 0)
			WriteMessage("Unit          = All");
		else
		{
			sprintf(szTmp, "Unit          = %d", pResponse->RFY.unitcode);
			WriteMessage(szTmp);
		}

		sprintf(szTmp, "rfu1         = %02X", pResponse->RFY.rfu1);
		WriteMessage(szTmp);
		sprintf(szTmp, "rfu2         = %02X", pResponse->RFY.rfu2);
		WriteMessage(szTmp);
		sprintf(szTmp, "rfu3         = %02X", pResponse->RFY.rfu3);
		WriteMessage(szTmp);

		WriteMessage("Command       = ", false);

		switch (pResponse->RFY.cmnd)
		{
		case rfy_sStop:
			WriteMessage("Stop");
			break;
		case rfy_sUp:
			WriteMessage("Up");
			break;
		case rfy_sUpStop:
			WriteMessage("Up + Stop");
			break;
		case rfy_sDown:
			WriteMessage("Down");
			break;
		case rfy_sDownStop:
			WriteMessage("Down + Stop");
			break;
		case rfy_sUpDown:
			WriteMessage("Up + Down");
			break;
		case rfy_sListRemotes:
			WriteMessage("List remotes");
		case rfy_sProgram:
			WriteMessage("Program");
			break;
		case rfy_s2SecProgram:
			WriteMessage("2 seconds: Program");
			break;
		case rfy_s7SecProgram:
			WriteMessage("7 seconds: Program");
			break;
		case rfy_s2SecStop:
			WriteMessage("2 seconds: Stop");
			break;
		case rfy_s5SecStop:
			WriteMessage("5 seconds: Stop");
			break;
		case rfy_s5SecUpDown:
			WriteMessage("5 seconds: Up + Down");
			break;
		case rfy_sEraseThis:
			WriteMessage("Erase this remote");
			break;
		case rfy_sEraseAll:
			WriteMessage("Erase all remotes");
			break;
		case rfy_s05SecUp:
			WriteMessage("< 0.5 seconds: up");
			break;
		case rfy_s05SecDown:
			WriteMessage("< 0.5 seconds: down");
			break;
		case rfy_s2SecUp:
			WriteMessage("> 2 seconds: up");
			break;
		case rfy_s2SecDown:
			WriteMessage("> 2 seconds: down");
			break;
		default:
			WriteMessage("UNKNOWN");
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->RFY.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_evohome2(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	const REVOBUF *pEvo = reinterpret_cast<const REVOBUF*>(pResponse);
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 255;//Unknown
	unsigned char BatteryLevel = 255;//Unknown

	//Get Device details
	std::vector<std::vector<std::string> > result;
	if (pEvo->EVOHOME2.type == pTypeEvohomeZone && pEvo->EVOHOME2.zone > 12) //Allow for additional Zone Temp devices which require DeviceID
	{
		result = m_sql.safe_query(
			"SELECT HardwareID, DeviceID,Unit,Type,SubType,sValue,BatteryLevel "
			"FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID == '%x') AND (Type==%d)",
			HwdID, (int)RFX_GETID3(pEvo->EVOHOME2.id1, pEvo->EVOHOME2.id2, pEvo->EVOHOME2.id3), (int)pEvo->EVOHOME2.type);
	}
	else if (pEvo->EVOHOME2.zone)//if unit number is available the id3 will be the controller device id
	{
		result = m_sql.safe_query(
			"SELECT HardwareID, DeviceID,Unit,Type,SubType,sValue,BatteryLevel "
			"FROM DeviceStatus WHERE (HardwareID==%d) AND (Unit == %d) AND (Type==%d)",
			HwdID, (int)pEvo->EVOHOME2.zone, (int)pEvo->EVOHOME2.type);
	}
	else//unit number not available then id3 should be the zone device id
	{
		result = m_sql.safe_query(
			"SELECT HardwareID, DeviceID,Unit,Type,SubType,sValue,BatteryLevel "
			"FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID == '%x') AND (Type==%d)",
			HwdID, (int)RFX_GETID3(pEvo->EVOHOME2.id1, pEvo->EVOHOME2.id2, pEvo->EVOHOME2.id3), (int)pEvo->EVOHOME2.type);
	}
	if (result.size() < 1 && !pEvo->EVOHOME2.zone)
		return;

	CEvohomeBase *pEvoHW = reinterpret_cast<CEvohomeBase*>(GetHardware(HwdID));
	bool bNewDev = false;
	std::string name, szDevID;
	std::stringstream szID;
	unsigned char Unit;
	unsigned char dType;
	unsigned char dSubType;
	std::string szUpdateStat;
	if (result.size() > 0)
	{
		std::vector<std::string> sd = result[0];
		szDevID = sd[1];
		Unit = atoi(sd[2].c_str());
		dType = atoi(sd[3].c_str());
		dSubType = atoi(sd[4].c_str());
		szUpdateStat = sd[5];
		BatteryLevel = atoi(sd[6].c_str());
	}
	else
	{
		bNewDev = true;
		Unit = pEvo->EVOHOME2.zone;//should always be non zero
		dType = pEvo->EVOHOME2.type;
		dSubType = pEvo->EVOHOME2.subtype;

		szID << std::hex << (int)RFX_GETID3(pEvo->EVOHOME2.id1, pEvo->EVOHOME2.id2, pEvo->EVOHOME2.id3);
		szDevID = szID.str();

		if (!pEvoHW)
			return;
		if (dType == pTypeEvohomeWater)
			name = "Hot Water";
		else if (dType == pTypeEvohomeZone && !szDevID.empty())
			name = "Zone Temp";
		else
			name = pEvoHW->GetZoneName(Unit - 1);
		if (name.empty())
			return;
		szUpdateStat = "0.0;0.0;Auto";
	}

	if (pEvo->EVOHOME2.updatetype == CEvohomeBase::updBattery)
		BatteryLevel = pEvo->EVOHOME2.battery_level;
	else
	{
		if (dType == pTypeEvohomeWater && pEvo->EVOHOME2.updatetype == pEvoHW->updSetPoint)
			sprintf(szTmp, "%s", pEvo->EVOHOME2.temperature ? "On" : "Off");
		else
			sprintf(szTmp, "%.2f", pEvo->EVOHOME2.temperature / 100.0f);

		std::vector<std::string> strarray;
		StringSplit(szUpdateStat, ";", strarray);
		if (strarray.size() >= 3)
		{
			if (pEvo->EVOHOME2.updatetype == pEvoHW->updSetPoint)//SetPoint
			{
				strarray[1] = szTmp;
				if (pEvo->EVOHOME2.mode <= pEvoHW->zmTmp)//for the moment only update this if we get a valid setpoint mode as we can now send setpoint on its own
				{
					int nControllerMode = pEvo->EVOHOME2.controllermode;
					if (dType == pTypeEvohomeWater && (nControllerMode == pEvoHW->cmEvoHeatingOff || nControllerMode == pEvoHW->cmEvoAutoWithEco || nControllerMode == pEvoHW->cmEvoCustom))//dhw has no economy mode and does not turn off for heating off also appears custom does not support the dhw zone
						nControllerMode = pEvoHW->cmEvoAuto;
					if (pEvo->EVOHOME2.mode == pEvoHW->zmAuto || nControllerMode == pEvoHW->cmEvoHeatingOff)//if zonemode is auto (followschedule) or controllermode is heatingoff
						strarray[2] = pEvoHW->GetWebAPIModeName(nControllerMode);//the web front end ultimately uses these names for images etc.
					else
						strarray[2] = pEvoHW->GetZoneModeName(pEvo->EVOHOME2.mode);
					if (pEvo->EVOHOME2.mode == pEvoHW->zmTmp)
					{
						std::string szISODate(CEvohomeDateTime::GetISODate(pEvo->EVOHOME2));
						if (strarray.size() < 4) //add or set until
							strarray.push_back(szISODate);
						else
							strarray[3] = szISODate;
					}
					else if ((pEvo->EVOHOME2.mode == pEvoHW->zmAuto) && (HwdType == HTYPE_EVOHOME_WEB))
					{
						strarray[2] = "FollowSchedule";
						if ((pEvo->EVOHOME2.year != 0) && (pEvo->EVOHOME2.year != 0xFFFF))
						{
							std::string szISODate(CEvohomeDateTime::GetISODate(pEvo->EVOHOME2));
							if (strarray.size() < 4) //add or set until
								strarray.push_back(szISODate);
							else
								strarray[3] = szISODate;
						}

					}
					else
						if (strarray.size() >= 4) //remove until
							strarray.resize(3);
				}
			}
			else if (pEvo->EVOHOME2.updatetype == pEvoHW->updOverride)
			{
				strarray[2] = pEvoHW->GetZoneModeName(pEvo->EVOHOME2.mode);
				if (strarray.size() >= 4) //remove until
					strarray.resize(3);
			}
			else
				strarray[0] = szTmp;
			szUpdateStat = boost::algorithm::join(strarray, ";");
		}
	}
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, szDevID.c_str(), Unit, dType, dSubType, SignalLevel, BatteryLevel, cmnd, szUpdateStat.c_str(), procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	if (bNewDev)
	{
		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q' WHERE (ID == %" PRIu64 ")",
			name.c_str(), DevRowIdx);
		procResult.DeviceName = name;
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_evohome1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	const REVOBUF *pEvo = reinterpret_cast<const REVOBUF*>(pResponse);
	unsigned char devType = pTypeEvohome;
	unsigned char subType = pEvo->EVOHOME1.subtype;
	std::stringstream szID;
	if (HwdType == HTYPE_EVOHOME_SERIAL || HwdType == HTYPE_EVOHOME_TCP)
		szID << std::hex << (int)RFX_GETID3(pEvo->EVOHOME1.id1, pEvo->EVOHOME1.id2, pEvo->EVOHOME1.id3);
	else //GB3: web based evohome uses decimal device ID's
		szID << std::dec << (int)RFX_GETID3(pEvo->EVOHOME1.id1, pEvo->EVOHOME1.id2, pEvo->EVOHOME1.id3);
	std::string ID(szID.str());
	unsigned char Unit = 0;
	unsigned char cmnd = pEvo->EVOHOME1.status;
	unsigned char SignalLevel = 255;//Unknown
	unsigned char BatteryLevel = 255;//Unknown

	std::string szUntilDate;
	if (pEvo->EVOHOME1.mode == CEvohomeBase::cmTmp)//temporary
		szUntilDate = CEvohomeDateTime::GetISODate(pEvo->EVOHOME1);

	CEvohomeBase *pEvoHW = reinterpret_cast<CEvohomeBase*>(GetHardware(HwdID));

	//FIXME A similar check is also done in switchmodal do we want to forward the ooc flag and rely on this check entirely?
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query(
		"SELECT HardwareID, DeviceID,Unit,Type,SubType,SwitchType,StrParam1,nValue,sValue FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID == '%q')",
		HwdID, ID.c_str());
	bool bNewDev = false;
	std::string name;
	if (result.size() > 0)
	{
		std::vector<std::string> sd = result[0];
		if (atoi(sd[7].c_str()) == cmnd && sd[8] == szUntilDate)
			return;
	}
	else
	{
		bNewDev = true;
		if (!pEvoHW)
			return;
		name = pEvoHW->GetControllerName();
		if (name.empty())
			return;
	}

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szUntilDate.c_str(), procResult.DeviceName, pEvo->EVOHOME1.action != 0);
	if (DevRowIdx == -1)
		return;
	if (bNewDev)
	{
		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q' WHERE (ID == %" PRIu64 ")",
			name.c_str(), DevRowIdx);
		procResult.DeviceName = name;
	}

	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");
	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pEvo->EVOHOME1.subtype)
		{
		case sTypeEvohome:
			WriteMessage("subtype       = Evohome");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pEvo->EVOHOME1.type, pEvo->EVOHOME1.subtype);
			WriteMessage(szTmp);
			break;
		}

		if (HwdType == HTYPE_EVOHOME_SERIAL || HwdType == HTYPE_EVOHOME_TCP)
			sprintf(szTmp, "id            = %02X:%02X:%02X", pEvo->EVOHOME1.id1, pEvo->EVOHOME1.id2, pEvo->EVOHOME1.id3);
		else //GB3: web based evohome uses decimal device ID's
			sprintf(szTmp, "id            = %u", (int)RFX_GETID3(pEvo->EVOHOME1.id1, pEvo->EVOHOME1.id2, pEvo->EVOHOME1.id3));
		WriteMessage(szTmp);
		sprintf(szTmp, "action        = %d", (int)pEvo->EVOHOME1.action);
		WriteMessage(szTmp);
		WriteMessage("status        = ");
		WriteMessage(pEvoHW->GetControllerModeName(pEvo->EVOHOME1.status));

		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_evohome3(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	const REVOBUF *pEvo = reinterpret_cast<const REVOBUF*>(pResponse);
	unsigned char devType = pTypeEvohomeRelay;
	unsigned char subType = pEvo->EVOHOME1.subtype;
	std::stringstream szID;
	int nDevID = (int)RFX_GETID3(pEvo->EVOHOME3.id1, pEvo->EVOHOME3.id2, pEvo->EVOHOME3.id3);
	szID << std::hex << nDevID;
	std::string ID(szID.str());
	unsigned char Unit = pEvo->EVOHOME3.devno;
	unsigned char cmnd = (pEvo->EVOHOME3.demand > 0) ? light1_sOn : light1_sOff;
	sprintf(szTmp, "%d", pEvo->EVOHOME3.demand);
	std::string szDemand(szTmp);
	unsigned char SignalLevel = 255;//Unknown
	unsigned char BatteryLevel = 255;//Unknown

	if (Unit == 0xFF && nDevID == 0)
		return;
	//Get Device details (devno or devid not available)
	bool bNewDev = false;
	std::vector<std::vector<std::string> > result;
	if (Unit == 0xFF)
		result = m_sql.safe_query(
			"SELECT HardwareID,DeviceID,Unit,Type,SubType,nValue,sValue,BatteryLevel "
			"FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID == '%q')",
			HwdID, ID.c_str());
	else
		result = m_sql.safe_query(
			"SELECT HardwareID,DeviceID,Unit,Type,SubType,nValue,sValue,BatteryLevel "
			"FROM DeviceStatus WHERE (HardwareID==%d) AND (Unit == '%d') AND (Type==%d) AND (DeviceID == '%q')",
			HwdID, (int)Unit, (int)pEvo->EVOHOME3.type, ID.c_str());
	if (result.size() > 0)
	{
		if (pEvo->EVOHOME3.demand == 0xFF)//we sometimes get a 0418 message after the initial device creation but it will mess up the logging as we don't have a demand
			return;
		unsigned char cur_cmnd = atoi(result[0][5].c_str());
		BatteryLevel = atoi(result[0][7].c_str());

		if (pEvo->EVOHOME3.updatetype == CEvohomeBase::updBattery)
		{
			BatteryLevel = pEvo->EVOHOME3.battery_level;
			szDemand = result[0][6];
			cmnd = (atoi(szDemand.c_str()) > 0) ? light1_sOn : light1_sOff;
		}
		if (Unit == 0xFF)
		{
			Unit = atoi(result[0][2].c_str());
			szDemand = result[0][6];
			if (cmnd == cur_cmnd)
				return;
		}
		else if (nDevID == 0)
		{
			ID = result[0][1];
		}
	}
	else
	{
		if (Unit == 0xFF || (nDevID == 0 && Unit > 12))
			return;
		bNewDev = true;
		if (pEvo->EVOHOME3.demand == 0xFF)//0418 allows us to associate unit and deviceid but no state information other messages only contain one or the other
			szDemand = "0";
		if (pEvo->EVOHOME3.updatetype == CEvohomeBase::updBattery)
			BatteryLevel = pEvo->EVOHOME3.battery_level;
	}

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szDemand.c_str(), procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (bNewDev && (Unit == 0xF9 || Unit == 0xFA || Unit == 0xFC || Unit <= 12))
	{
		if (Unit == 0xF9)
			procResult.DeviceName = "CH Valve";
		else if (Unit == 0xFA)
			procResult.DeviceName = "DHW Valve";
		else if (Unit == 0xFC)
		{
			if (pEvo->EVOHOME3.id1 >> 2 == CEvohomeID::devBridge) // Evohome OT Bridge
				procResult.DeviceName = "Boiler (OT Bridge)";
			else
				procResult.DeviceName = "Boiler";
		}
		else if (Unit <= 12)
			procResult.DeviceName = "Zone";
		std::vector<std::vector<std::string> > result;
		result = m_sql.safe_query(
			"UPDATE DeviceStatus SET Name='%q' WHERE (ID == %" PRIu64 ")",
			procResult.DeviceName.c_str(), DevRowIdx);
	}

	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Security1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeSecurity1;
	unsigned char subType = pResponse->SECURITY1.subtype;
	std::string ID;
	sprintf(szTmp, "%02X%02X%02X", pResponse->SECURITY1.id1, pResponse->SECURITY1.id2, pResponse->SECURITY1.id3);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = pResponse->SECURITY1.status;
	unsigned char SignalLevel = pResponse->SECURITY1.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, false, pResponse->SECURITY1.battery_level & 0x0F);
	if (
		(pResponse->SECURITY1.subtype == sTypeKD101) ||
		(pResponse->SECURITY1.subtype == sTypeSA30) ||
		(pResponse->SECURITY1.subtype == sTypeDomoticzSecurity)
		)
	{
		//KD101 & SA30 do not support battery low indication
		BatteryLevel = 255;
	}

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->SECURITY1.subtype)
		{
		case sTypeSecX10:
			WriteMessage("subtype       = X10 security");
			break;
		case sTypeSecX10M:
			WriteMessage("subtype       = X10 security motion");
			break;
		case sTypeSecX10R:
			WriteMessage("subtype       = X10 security remote");
			break;
		case sTypeKD101:
			WriteMessage("subtype       = KD101 smoke detector");
			break;
		case sTypePowercodeSensor:
			WriteMessage("subtype       = Visonic PowerCode sensor - primary contact");
			break;
		case sTypePowercodeMotion:
			WriteMessage("subtype       = Visonic PowerCode motion");
			break;
		case sTypeCodesecure:
			WriteMessage("subtype       = Visonic CodeSecure");
			break;
		case sTypePowercodeAux:
			WriteMessage("subtype       = Visonic PowerCode sensor - auxiliary contact");
			break;
		case sTypeMeiantech:
			WriteMessage("subtype       = Meiantech/Atlantic/Aidebao");
			break;
		case sTypeSA30:
			WriteMessage("subtype       = Alecto SA30 smoke detector");
			break;
		case sTypeDomoticzSecurity:
			WriteMessage("subtype       = Security Panel");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->SECURITY1.packettype, pResponse->SECURITY1.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->SECURITY1.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "id1-3         = %02X:%02X:%02X", pResponse->SECURITY1.id1, pResponse->SECURITY1.id2, pResponse->SECURITY1.id3);
		WriteMessage(szTmp);

		WriteMessage("status        = ", false);

		switch (pResponse->SECURITY1.status)
		{
		case sStatusNormal:
			WriteMessage("Normal");
			break;
		case sStatusNormalDelayed:
			WriteMessage("Normal Delayed");
			break;
		case sStatusAlarm:
			WriteMessage("Alarm");
			break;
		case sStatusAlarmDelayed:
			WriteMessage("Alarm Delayed");
			break;
		case sStatusMotion:
			WriteMessage("Motion");
			break;
		case sStatusNoMotion:
			WriteMessage("No Motion");
			break;
		case sStatusPanic:
			WriteMessage("Panic");
			break;
		case sStatusPanicOff:
			WriteMessage("Panic End");
			break;
		case sStatusArmAway:
			if (pResponse->SECURITY1.subtype == sTypeMeiantech)
				WriteMessage("Group2 or ", false);
			WriteMessage("Arm Away");
			break;
		case sStatusArmAwayDelayed:
			WriteMessage("Arm Away Delayed");
			break;
		case sStatusArmHome:
			if (pResponse->SECURITY1.subtype == sTypeMeiantech)
				WriteMessage("Group3 or ", false);
			WriteMessage("Arm Home");
			break;
		case sStatusArmHomeDelayed:
			WriteMessage("Arm Home Delayed");
			break;
		case sStatusDisarm:
			if (pResponse->SECURITY1.subtype == sTypeMeiantech)
				WriteMessage("Group1 or ", false);
			WriteMessage("Disarm");
			break;
		case sStatusLightOff:
			WriteMessage("Light Off");
			break;
		case sStatusLightOn:
			WriteMessage("Light On");
			break;
		case sStatusLight2Off:
			WriteMessage("Light 2 Off");
			break;
		case sStatusLight2On:
			WriteMessage("Light 2 On");
			break;
		case sStatusDark:
			WriteMessage("Dark detected");
			break;
		case sStatusLight:
			WriteMessage("Light Detected");
			break;
		case sStatusBatLow:
			WriteMessage("Battery low MS10 or XX18 sensor");
			break;
		case sStatusPairKD101:
			WriteMessage("Pair KD101");
			break;
		case sStatusNormalTamper:
			WriteMessage("Normal + Tamper");
			break;
		case sStatusNormalDelayedTamper:
			WriteMessage("Normal Delayed + Tamper");
			break;
		case sStatusAlarmTamper:
			WriteMessage("Alarm + Tamper");
			break;
		case sStatusAlarmDelayedTamper:
			WriteMessage("Alarm Delayed + Tamper");
			break;
		case sStatusMotionTamper:
			WriteMessage("Motion + Tamper");
			break;
		case sStatusNoMotionTamper:
			WriteMessage("No Motion + Tamper");
			break;
		}

		if (
			(pResponse->SECURITY1.subtype != sTypeKD101) &&		//KD101 & SA30 does not support battery low indication
			(pResponse->SECURITY1.subtype != sTypeSA30)
			)
		{
			if ((pResponse->SECURITY1.battery_level & 0xF) == 0)
				WriteMessage("battery level = Low");
			else
				WriteMessage("battery level = OK");
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->SECURITY1.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Security2(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeSecurity2;
	unsigned char subType = pResponse->SECURITY2.subtype;
	std::string ID;
	sprintf(szTmp, "%02X%02X%02X%02X%02X%02X%02X%02X", pResponse->SECURITY2.id1, pResponse->SECURITY2.id2, pResponse->SECURITY2.id3, pResponse->SECURITY2.id4, pResponse->SECURITY2.id5, pResponse->SECURITY2.id6, pResponse->SECURITY2.id7, pResponse->SECURITY2.id8);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;// pResponse->SECURITY2.cmnd;
	unsigned char SignalLevel = pResponse->SECURITY2.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, false, pResponse->SECURITY2.battery_level & 0x0F);

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->SECURITY2.subtype)
		{
		case sTypeSec2Classic:
			WriteMessage("subtype       = Keeloq Classic");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->SECURITY2.packettype, pResponse->SECURITY2.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->SECURITY2.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "id1-8         = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", pResponse->SECURITY2.id1, pResponse->SECURITY2.id2, pResponse->SECURITY2.id3, pResponse->SECURITY2.id4, pResponse->SECURITY2.id5, pResponse->SECURITY2.id6, pResponse->SECURITY2.id7, pResponse->SECURITY2.id8);
		WriteMessage(szTmp);

		if ((pResponse->SECURITY2.battery_level & 0xF) == 0)
			WriteMessage("battery level = Low");
		else
			WriteMessage("battery level = OK");

		sprintf(szTmp, "Signal level  = %d", pResponse->SECURITY2.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

//not in dbase yet
void MainWorker::decode_Camera1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessage("");

	//unsigned char devType=pTypeCamera;

	char szTmp[100];

	switch (pResponse->CAMERA1.subtype)
	{
	case sTypeNinja:
		WriteMessage("subtype       = X10 Ninja/Robocam");
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->CAMERA1.seqnbr);
		WriteMessage(szTmp);

		WriteMessage("Command       = ", false);

		switch (pResponse->CAMERA1.cmnd)
		{
		case camera_sLeft:
			WriteMessage("Left");
			break;
		case camera_sRight:
			WriteMessage("Right");
			break;
		case camera_sUp:
			WriteMessage("Up");
			break;
		case camera_sDown:
			WriteMessage("Down");
			break;
		case camera_sPosition1:
			WriteMessage("Position 1");
			break;
		case camera_sProgramPosition1:
			WriteMessage("Position 1 program");
			break;
		case camera_sPosition2:
			WriteMessage("Position 2");
			break;
		case camera_sProgramPosition2:
			WriteMessage("Position 2 program");
			break;
		case camera_sPosition3:
			WriteMessage("Position 3");
			break;
		case camera_sProgramPosition3:
			WriteMessage("Position 3 program");
			break;
		case camera_sPosition4:
			WriteMessage("Position 4");
			break;
		case camera_sProgramPosition4:
			WriteMessage("Position 4 program");
			break;
		case camera_sCenter:
			WriteMessage("Center");
			break;
		case camera_sProgramCenterPosition:
			WriteMessage("Center program");
			break;
		case camera_sSweep:
			WriteMessage("Sweep");
			break;
		case camera_sProgramSweep:
			WriteMessage("Sweep program");
			break;
		default:
			WriteMessage("UNKNOWN");
			break;
		}
		sprintf(szTmp, "Housecode     = %d", pResponse->CAMERA1.housecode);
		WriteMessage(szTmp);
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->CAMERA1.packettype, pResponse->CAMERA1.subtype);
		WriteMessage(szTmp);
		break;
	}
	sprintf(szTmp, "Signal level  = %d", pResponse->CAMERA1.rssi);
	WriteMessage(szTmp);
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Remote(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeRemote;
	unsigned char subType = pResponse->REMOTE.subtype;
	sprintf(szTmp, "%d", pResponse->REMOTE.id);
	std::string ID = szTmp;
	unsigned char Unit = pResponse->REMOTE.cmnd;
	unsigned char cmnd = light2_sOn;
	unsigned char SignalLevel = pResponse->REMOTE.rssi;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->REMOTE.subtype)
		{
		case sTypeATI:
			WriteMessage("subtype       = ATI Remote Wonder");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->REMOTE.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->REMOTE.id);
			WriteMessage(szTmp);
			switch (pResponse->REMOTE.cmnd)
			{
			case 0x0:
				WriteMessage("Command       = A");
				break;
			case 0x1:
				WriteMessage("Command       = B");
				break;
			case 0x2:
				WriteMessage("Command       = power");
				break;
			case 0x3:
				WriteMessage("Command       = TV");
				break;
			case 0x4:
				WriteMessage("Command       = DVD");
				break;
			case 0x5:
				WriteMessage("Command       = ?");
				break;
			case 0x6:
				WriteMessage("Command       = Guide");
				break;
			case 0x7:
				WriteMessage("Command       = Drag");
				break;
			case 0x8:
				WriteMessage("Command       = VOL+");
				break;
			case 0x9:
				WriteMessage("Command       = VOL-");
				break;
			case 0xA:
				WriteMessage("Command       = MUTE");
				break;
			case 0xB:
				WriteMessage("Command       = CHAN+");
				break;
			case 0xC:
				WriteMessage("Command       = CHAN-");
				break;
			case 0xD:
				WriteMessage("Command       = 1");
				break;
			case 0xE:
				WriteMessage("Command       = 2");
				break;
			case 0xF:
				WriteMessage("Command       = 3");
				break;
			case 0x10:
				WriteMessage("Command       = 4");
				break;
			case 0x11:
				WriteMessage("Command       = 5");
				break;
			case 0x12:
				WriteMessage("Command       = 6");
				break;
			case 0x13:
				WriteMessage("Command       = 7");
				break;
			case 0x14:
				WriteMessage("Command       = 8");
				break;
			case 0x15:
				WriteMessage("Command       = 9");
				break;
			case 0x16:
				WriteMessage("Command       = txt");
				break;
			case 0x17:
				WriteMessage("Command       = 0");
				break;
			case 0x18:
				WriteMessage("Command       = snapshot ESC");
				break;
			case 0x19:
				WriteMessage("Command       = C");
				break;
			case 0x1A:
				WriteMessage("Command       = ^");
				break;
			case 0x1B:
				WriteMessage("Command       = D");
				break;
			case 0x1C:
				WriteMessage("Command       = TV/RADIO");
				break;
			case 0x1D:
				WriteMessage("Command       = <");
				break;
			case 0x1E:
				WriteMessage("Command       = OK");
				break;
			case 0x1F:
				WriteMessage("Command       = >");
				break;
			case 0x20:
				WriteMessage("Command       = <-");
				break;
			case 0x21:
				WriteMessage("Command       = E");
				break;
			case 0x22:
				WriteMessage("Command       = v");
				break;
			case 0x23:
				WriteMessage("Command       = F");
				break;
			case 0x24:
				WriteMessage("Command       = Rewind");
				break;
			case 0x25:
				WriteMessage("Command       = Play");
				break;
			case 0x26:
				WriteMessage("Command       = Fast forward");
				break;
			case 0x27:
				WriteMessage("Command       = Record");
				break;
			case 0x28:
				WriteMessage("Command       = Stop");
				break;
			case 0x29:
				WriteMessage("Command       = Pause");
				break;
			case 0x2C:
				WriteMessage("Command       = TV");
				break;
			case 0x2D:
				WriteMessage("Command       = VCR");
				break;
			case 0x2E:
				WriteMessage("Command       = RADIO");
				break;
			case 0x2F:
				WriteMessage("Command       = TV Preview");
				break;
			case 0x30:
				WriteMessage("Command       = Channel list");
				break;
			case 0x31:
				WriteMessage("Command       = Video Desktop");
				break;
			case 0x32:
				WriteMessage("Command       = red");
				break;
			case 0x33:
				WriteMessage("Command       = green");
				break;
			case 0x34:
				WriteMessage("Command       = yellow");
				break;
			case 0x35:
				WriteMessage("Command       = blue");
				break;
			case 0x36:
				WriteMessage("Command       = rename TAB");
				break;
			case 0x37:
				WriteMessage("Command       = Acquire image");
				break;
			case 0x38:
				WriteMessage("Command       = edit image");
				break;
			case 0x39:
				WriteMessage("Command       = Full screen");
				break;
			case 0x3A:
				WriteMessage("Command       = DVD Audio");
				break;
			case 0x70:
				WriteMessage("Command       = Cursor-left");
				break;
			case 0x71:
				WriteMessage("Command       = Cursor-right");
				break;
			case 0x72:
				WriteMessage("Command       = Cursor-up");
				break;
			case 0x73:
				WriteMessage("Command       = Cursor-down");
				break;
			case 0x74:
				WriteMessage("Command       = Cursor-up-left");
				break;
			case 0x75:
				WriteMessage("Command       = Cursor-up-right");
				break;
			case 0x76:
				WriteMessage("Command       = Cursor-down-right");
				break;
			case 0x77:
				WriteMessage("Command       = Cursor-down-left");
				break;
			case 0x78:
				WriteMessage("Command       = V");
				break;
			case 0x79:
				WriteMessage("Command       = V-End");
				break;
			case 0x7C:
				WriteMessage("Command       = X");
				break;
			case 0x7D:
				WriteMessage("Command       = X-End");
				break;
			default:
				WriteMessage("Command       = unknown");
				break;
			}
			break;
		case sTypeATIplus:
			WriteMessage("subtype       = ATI Remote Wonder Plus");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->REMOTE.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->REMOTE.id);
			WriteMessage(szTmp);

			WriteMessage("Command       = ", false);
			switch (pResponse->REMOTE.cmnd)
			{
			case 0x0:
				WriteMessage("A", false);
				break;
			case 0x1:
				WriteMessage("B", false);
				break;
			case 0x2:
				WriteMessage("power", false);
				break;
			case 0x3:
				WriteMessage("TV", false);
				break;
			case 0x4:
				WriteMessage("DVD", false);
				break;
			case 0x5:
				WriteMessage("?", false);
				break;
			case 0x6:
				WriteMessage("Guide", false);
				break;
			case 0x7:
				WriteMessage("Drag", false);
				break;
			case 0x8:
				WriteMessage("VOL+", false);
				break;
			case 0x9:
				WriteMessage("VOL-", false);
				break;
			case 0xA:
				WriteMessage("MUTE", false);
				break;
			case 0xB:
				WriteMessage("CHAN+", false);
				break;
			case 0xC:
				WriteMessage("CHAN-", false);
				break;
			case 0xD:
				WriteMessage("1", false);
				break;
			case 0xE:
				WriteMessage("2", false);
				break;
			case 0xF:
				WriteMessage("3", false);
				break;
			case 0x10:
				WriteMessage("4", false);
				break;
			case 0x11:
				WriteMessage("5", false);
				break;
			case 0x12:
				WriteMessage("6", false);
				break;
			case 0x13:
				WriteMessage("7", false);
				break;
			case 0x14:
				WriteMessage("8", false);
				break;
			case 0x15:
				WriteMessage("9", false);
				break;
			case 0x16:
				WriteMessage("txt", false);
				break;
			case 0x17:
				WriteMessage("0", false);
				break;
			case 0x18:
				WriteMessage("Open Setup Menu", false);
				break;
			case 0x19:
				WriteMessage("C", false);
				break;
			case 0x1A:
				WriteMessage("^", false);
				break;
			case 0x1B:
				WriteMessage("D", false);
				break;
			case 0x1C:
				WriteMessage("FM", false);
				break;
			case 0x1D:
				WriteMessage("<", false);
				break;
			case 0x1E:
				WriteMessage("OK", false);
				break;
			case 0x1F:
				WriteMessage(">", false);
				break;
			case 0x20:
				WriteMessage("Max/Restore window", false);
				break;
			case 0x21:
				WriteMessage("E", false);
				break;
			case 0x22:
				WriteMessage("v", false);
				break;
			case 0x23:
				WriteMessage("F", false);
				break;
			case 0x24:
				WriteMessage("Rewind", false);
				break;
			case 0x25:
				WriteMessage("Play", false);
				break;
			case 0x26:
				WriteMessage("Fast forward", false);
				break;
			case 0x27:
				WriteMessage("Record", false);
				break;
			case 0x28:
				WriteMessage("Stop", false);
				break;
			case 0x29:
				WriteMessage("Pause", false);
				break;
			case 0x2A:
				WriteMessage("TV2", false);
				break;
			case 0x2B:
				WriteMessage("Clock", false);
				break;
			case 0x2C:
				WriteMessage("i", false);
				break;
			case 0x2D:
				WriteMessage("ATI", false);
				break;
			case 0x2E:
				WriteMessage("RADIO", false);
				break;
			case 0x2F:
				WriteMessage("TV Preview", false);
				break;
			case 0x30:
				WriteMessage("Channel list", false);
				break;
			case 0x31:
				WriteMessage("Video Desktop", false);
				break;
			case 0x32:
				WriteMessage("red", false);
				break;
			case 0x33:
				WriteMessage("green", false);
				break;
			case 0x34:
				WriteMessage("yellow", false);
				break;
			case 0x35:
				WriteMessage("blue", false);
				break;
			case 0x36:
				WriteMessage("rename TAB", false);
				break;
			case 0x37:
				WriteMessage("Acquire image", false);
				break;
			case 0x38:
				WriteMessage("edit image", false);
				break;
			case 0x39:
				WriteMessage("Full screen", false);
				break;
			case 0x3A:
				WriteMessage("DVD Audio", false);
				break;
			case 0x70:
				WriteMessage("Cursor-left", false);
				break;
			case 0x71:
				WriteMessage("Cursor-right", false);
				break;
			case 0x72:
				WriteMessage("Cursor-up", false);
				break;
			case 0x73:
				WriteMessage("Cursor-down", false);
				break;
			case 0x74:
				WriteMessage("Cursor-up-left", false);
				break;
			case 0x75:
				WriteMessage("Cursor-up-right", false);
				break;
			case 0x76:
				WriteMessage("Cursor-down-right", false);
				break;
			case 0x77:
				WriteMessage("Cursor-down-left", false);
				break;
			case 0x78:
				WriteMessage("Left Mouse Button", false);
				break;
			case 0x79:
				WriteMessage("V-End", false);
				break;
			case 0x7C:
				WriteMessage("Right Mouse Button", false);
				break;
			case 0x7D:
				WriteMessage("X-End", false);
				break;
			default:
				WriteMessage("unknown", false);
				break;
			}
			if ((pResponse->REMOTE.toggle & 1) == 1)
				WriteMessage("  (button press = odd)");
			else
				WriteMessage("  (button press = even)");
			break;
		case sTypeATIrw2:
			WriteMessage("subtype       = ATI Remote Wonder II");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->REMOTE.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->REMOTE.id);
			WriteMessage(szTmp);
			WriteMessage("Command type  = ", false);

			switch (pResponse->REMOTE.cmndtype & 0x0E)
			{
			case 0x0:
				WriteMessage("PC");
				break;
			case 0x2:
				WriteMessage("AUX1");
				break;
			case 0x4:
				WriteMessage("AUX2");
				break;
			case 0x6:
				WriteMessage("AUX3");
				break;
			case 0x8:
				WriteMessage("AUX4");
				break;
			default:
				WriteMessage("unknown");
				break;
			}
			WriteMessage("Command       = ", false);
			switch (pResponse->REMOTE.cmnd)
			{
			case 0x0:
				WriteMessage("A", false);
				break;
			case 0x1:
				WriteMessage("B", false);
				break;
			case 0x2:
				WriteMessage("power", false);
				break;
			case 0x3:
				WriteMessage("TV", false);
				break;
			case 0x4:
				WriteMessage("DVD", false);
				break;
			case 0x5:
				WriteMessage("?", false);
				break;
			case 0x7:
				WriteMessage("Drag", false);
				break;
			case 0x8:
				WriteMessage("VOL+", false);
				break;
			case 0x9:
				WriteMessage("VOL-", false);
				break;
			case 0xA:
				WriteMessage("MUTE", false);
				break;
			case 0xB:
				WriteMessage("CHAN+", false);
				break;
			case 0xC:
				WriteMessage("CHAN-", false);
				break;
			case 0xD:
				WriteMessage("1", false);
				break;
			case 0xE:
				WriteMessage("2", false);
				break;
			case 0xF:
				WriteMessage("3", false);
				break;
			case 0x10:
				WriteMessage("4", false);
				break;
			case 0x11:
				WriteMessage("5", false);
				break;
			case 0x12:
				WriteMessage("6", false);
				break;
			case 0x13:
				WriteMessage("7", false);
				break;
			case 0x14:
				WriteMessage("8", false);
				break;
			case 0x15:
				WriteMessage("9", false);
				break;
			case 0x16:
				WriteMessage("txt", false);
				break;
			case 0x17:
				WriteMessage("0", false);
				break;
			case 0x18:
				WriteMessage("Open Setup Menu", false);
				break;
			case 0x19:
				WriteMessage("C", false);
				break;
			case 0x1A:
				WriteMessage("^", false);
				break;
			case 0x1B:
				WriteMessage("D", false);
				break;
			case 0x1C:
				WriteMessage("TV/RADIO", false);
				break;
			case 0x1D:
				WriteMessage("<", false);
				break;
			case 0x1E:
				WriteMessage("OK", false);
				break;
			case 0x1F:
				WriteMessage(">", false);
				break;
			case 0x20:
				WriteMessage("Max/Restore window", false);
				break;
			case 0x21:
				WriteMessage("E", false);
				break;
			case 0x22:
				WriteMessage("v", false);
				break;
			case 0x23:
				WriteMessage("F", false);
				break;
			case 0x24:
				WriteMessage("Rewind", false);
				break;
			case 0x25:
				WriteMessage("Play", false);
				break;
			case 0x26:
				WriteMessage("Fast forward", false);
				break;
			case 0x27:
				WriteMessage("Record", false);
				break;
			case 0x28:
				WriteMessage("Stop", false);
				break;
			case 0x29:
				WriteMessage("Pause", false);
				break;
			case 0x2C:
				WriteMessage("i", false);
				break;
			case 0x2D:
				WriteMessage("ATI", false);
				break;
			case 0x3B:
				WriteMessage("PC", false);
				break;
			case 0x3C:
				WriteMessage("AUX1", false);
				break;
			case 0x3D:
				WriteMessage("AUX2", false);
				break;
			case 0x3E:
				WriteMessage("AUX3", false);
				break;
			case 0x3F:
				WriteMessage("AUX4", false);
				break;
			case 0x70:
				WriteMessage("Cursor-left", false);
				break;
			case 0x71:
				WriteMessage("Cursor-right", false);
				break;
			case 0x72:
				WriteMessage("Cursor-up", false);
				break;
			case 0x73:
				WriteMessage("Cursor-down", false);
				break;
			case 0x74:
				WriteMessage("Cursor-up-left", false);
				break;
			case 0x75:
				WriteMessage("Cursor-up-right", false);
				break;
			case 0x76:
				WriteMessage("Cursor-down-right", false);
				break;
			case 0x77:
				WriteMessage("Cursor-down-left", false);
				break;
			case 0x78:
				WriteMessage("Left Mouse Button", false);
				break;
			case 0x7C:
				WriteMessage("Right Mouse Button", false);
				break;
			default:
				WriteMessage("unknown", false);
				break;
			}
			if ((pResponse->REMOTE.toggle & 1) == 1)
				WriteMessage("  (button press = odd)");
			else
				WriteMessage("  (button press = even)");
			break;
		case sTypeMedion:
			WriteMessage("subtype       = Medion Remote");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->REMOTE.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->REMOTE.id);
			WriteMessage(szTmp);

			WriteMessage("Command       = ", false);

			switch (pResponse->REMOTE.cmnd)
			{
			case 0x0:
				WriteMessage("Mute");
				break;
			case 0x1:
				WriteMessage("B");
				break;
			case 0x2:
				WriteMessage("power");
				break;
			case 0x3:
				WriteMessage("TV");
				break;
			case 0x4:
				WriteMessage("DVD");
				break;
			case 0x5:
				WriteMessage("Photo");
				break;
			case 0x6:
				WriteMessage("Music");
				break;
			case 0x7:
				WriteMessage("Drag");
				break;
			case 0x8:
				WriteMessage("VOL-");
				break;
			case 0x9:
				WriteMessage("VOL+");
				break;
			case 0xA:
				WriteMessage("MUTE");
				break;
			case 0xB:
				WriteMessage("CHAN+");
				break;
			case 0xC:
				WriteMessage("CHAN-");
				break;
			case 0xD:
				WriteMessage("1");
				break;
			case 0xE:
				WriteMessage("2");
				break;
			case 0xF:
				WriteMessage("3");
				break;
			case 0x10:
				WriteMessage("4");
				break;
			case 0x11:
				WriteMessage("5");
				break;
			case 0x12:
				WriteMessage("6");
				break;
			case 0x13:
				WriteMessage("7");
				break;
			case 0x14:
				WriteMessage("8");
				break;
			case 0x15:
				WriteMessage("9");
				break;
			case 0x16:
				WriteMessage("txt");
				break;
			case 0x17:
				WriteMessage("0");
				break;
			case 0x18:
				WriteMessage("snapshot ESC");
				break;
			case 0x19:
				WriteMessage("DVD MENU");
				break;
			case 0x1A:
				WriteMessage("^");
				break;
			case 0x1B:
				WriteMessage("Setup");
				break;
			case 0x1C:
				WriteMessage("TV/RADIO");
				break;
			case 0x1D:
				WriteMessage("<");
				break;
			case 0x1E:
				WriteMessage("OK");
				break;
			case 0x1F:
				WriteMessage(">");
				break;
			case 0x20:
				WriteMessage("<-");
				break;
			case 0x21:
				WriteMessage("E");
				break;
			case 0x22:
				WriteMessage("v");
				break;
			case 0x23:
				WriteMessage("F");
				break;
			case 0x24:
				WriteMessage("Rewind");
				break;
			case 0x25:
				WriteMessage("Play");
				break;
			case 0x26:
				WriteMessage("Fast forward");
				break;
			case 0x27:
				WriteMessage("Record");
				break;
			case 0x28:
				WriteMessage("Stop");
				break;
			case 0x29:
				WriteMessage("Pause");
				break;
			case 0x2C:
				WriteMessage("TV");
				break;
			case 0x2D:
				WriteMessage("VCR");
				break;
			case 0x2E:
				WriteMessage("RADIO");
				break;
			case 0x2F:
				WriteMessage("TV Preview");
				break;
			case 0x30:
				WriteMessage("Channel list");
				break;
			case 0x31:
				WriteMessage("Video Desktop");
				break;
			case 0x32:
				WriteMessage("red");
				break;
			case 0x33:
				WriteMessage("green");
				break;
			case 0x34:
				WriteMessage("yellow");
				break;
			case 0x35:
				WriteMessage("blue");
				break;
			case 0x36:
				WriteMessage("rename TAB");
				break;
			case 0x37:
				WriteMessage("Acquire image");
				break;
			case 0x38:
				WriteMessage("edit image");
				break;
			case 0x39:
				WriteMessage("Full screen");
				break;
			case 0x3A:
				WriteMessage("DVD Audio");
				break;
			case 0x70:
				WriteMessage("Cursor-left");
				break;
			case 0x71:
				WriteMessage("Cursor-right");
				break;
			case 0x72:
				WriteMessage("Cursor-up");
				break;
			case 0x73:
				WriteMessage("Cursor-down");
				break;
			case 0x74:
				WriteMessage("Cursor-up-left");
				break;
			case 0x75:
				WriteMessage("Cursor-up-right");
				break;
			case 0x76:
				WriteMessage("Cursor-down-right");
				break;
			case 0x77:
				WriteMessage("Cursor-down-left");
				break;
			case 0x78:
				WriteMessage("V");
				break;
			case 0x79:
				WriteMessage("V-End");
				break;
			case 0x7C:
				WriteMessage("X");
				break;
			case 0x7D:
				WriteMessage("X-End");
				break;
			default:
				WriteMessage("unknown");
				break;
			}
			break;
		case sTypePCremote:
			WriteMessage("subtype       = PC Remote");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->REMOTE.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->REMOTE.id);
			WriteMessage(szTmp);
			WriteMessage("Command       = ", false);
			switch (pResponse->REMOTE.cmnd)
			{
			case 0x2:
				WriteMessage("0");
				break;
			case 0x82:
				WriteMessage("1");
				break;
			case 0xD1:
				WriteMessage("MP3");
				break;
			case 0x42:
				WriteMessage("2");
				break;
			case 0xD2:
				WriteMessage("DVD");
				break;
			case 0xC2:
				WriteMessage("3");
				break;
			case 0xD3:
				WriteMessage("CD");
				break;
			case 0x22:
				WriteMessage("4");
				break;
			case 0xD4:
				WriteMessage("PC or SHIFT-4");
				break;
			case 0xA2:
				WriteMessage("5");
				break;
			case 0xD5:
				WriteMessage("SHIFT-5");
				break;
			case 0x62:
				WriteMessage("6");
				break;
			case 0xE2:
				WriteMessage("7");
				break;
			case 0x12:
				WriteMessage("8");
				break;
			case 0x92:
				WriteMessage("9");
				break;
			case 0xC0:
				WriteMessage("CH-");
				break;
			case 0x40:
				WriteMessage("CH+");
				break;
			case 0xE0:
				WriteMessage("VOL-");
				break;
			case 0x60:
				WriteMessage("VOL+");
				break;
			case 0xA0:
				WriteMessage("MUTE");
				break;
			case 0x3A:
				WriteMessage("INFO");
				break;
			case 0x38:
				WriteMessage("REW");
				break;
			case 0xB8:
				WriteMessage("FF");
				break;
			case 0xB0:
				WriteMessage("PLAY");
				break;
			case 0x64:
				WriteMessage("PAUSE");
				break;
			case 0x63:
				WriteMessage("STOP");
				break;
			case 0xB6:
				WriteMessage("MENU");
				break;
			case 0xFF:
				WriteMessage("REC");
				break;
			case 0xC9:
				WriteMessage("EXIT");
				break;
			case 0xD8:
				WriteMessage("TEXT");
				break;
			case 0xD9:
				WriteMessage("SHIFT-TEXT");
				break;
			case 0xF2:
				WriteMessage("TELETEXT");
				break;
			case 0xD7:
				WriteMessage("SHIFT-TELETEXT");
				break;
			case 0xBA:
				WriteMessage("A+B");
				break;
			case 0x52:
				WriteMessage("ENT");
				break;
			case 0xD6:
				WriteMessage("SHIFT-ENT");
				break;
			case 0x70:
				WriteMessage("Cursor-left");
				break;
			case 0x71:
				WriteMessage("Cursor-right");
				break;
			case 0x72:
				WriteMessage("Cursor-up");
				break;
			case 0x73:
				WriteMessage("Cursor-down");
				break;
			case 0x74:
				WriteMessage("Cursor-up-left");
				break;
			case 0x75:
				WriteMessage("Cursor-up-right");
				break;
			case 0x76:
				WriteMessage("Cursor-down-right");
				break;
			case 0x77:
				WriteMessage("Cursor-down-left");
				break;
			case 0x78:
				WriteMessage("Left mouse");
				break;
			case 0x79:
				WriteMessage("Left mouse-End");
				break;
			case 0x7B:
				WriteMessage("Drag");
				break;
			case 0x7C:
				WriteMessage("Right mouse");
				break;
			case 0x7D:
				WriteMessage("Right mouse-End");
				break;
			default:
				WriteMessage("unknown");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->REMOTE.packettype, pResponse->REMOTE.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->REMOTE.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Thermostat1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeThermostat1;
	unsigned char subType = pResponse->THERMOSTAT1.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->THERMOSTAT1.id1 * 256) + pResponse->THERMOSTAT1.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->THERMOSTAT1.rssi;
	unsigned char BatteryLevel = 255;

	unsigned char temp = pResponse->THERMOSTAT1.temperature;
	unsigned char set_point = pResponse->THERMOSTAT1.set_point;
	unsigned char mode = (pResponse->THERMOSTAT1.mode & 0x80);
	unsigned char status = (pResponse->THERMOSTAT1.status & 0x03);

	sprintf(szTmp, "%d;%d;%d;%d", temp, set_point, mode, status);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->THERMOSTAT1.subtype)
		{
		case sTypeDigimax:
			WriteMessage("subtype       = Digimax");
			break;
		case sTypeDigimaxShort:
			WriteMessage("subtype       = Digimax with short format");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->THERMOSTAT1.packettype, pResponse->THERMOSTAT1.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->THERMOSTAT1.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->THERMOSTAT1.id1 * 256) + pResponse->THERMOSTAT1.id2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Temperature   = %d C", pResponse->THERMOSTAT1.temperature);
		WriteMessage(szTmp);

		if (pResponse->THERMOSTAT1.subtype == sTypeDigimax)
		{
			sprintf(szTmp, "Set           = %d C", pResponse->THERMOSTAT1.set_point);
			WriteMessage(szTmp);

			if ((pResponse->THERMOSTAT1.mode & 0x80) == 0)
				WriteMessage("Mode          = heating");
			else
				WriteMessage("Mode          = Cooling");

			switch (pResponse->THERMOSTAT1.status & 0x03)
			{
			case 0:
				WriteMessage("Status        = no status available");
				break;
			case 1:
				WriteMessage("Status        = demand");
				break;
			case 2:
				WriteMessage("Status        = no demand");
				break;
			case 3:
				WriteMessage("Status        = initializing");
				break;
			}
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->THERMOSTAT1.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Thermostat2(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeThermostat2;
	unsigned char subType = pResponse->THERMOSTAT2.subtype;
	std::string ID;
	ID = "1";
	unsigned char Unit = pResponse->THERMOSTAT2.unitcode;
	unsigned char cmnd = pResponse->THERMOSTAT2.cmnd;
	unsigned char SignalLevel = pResponse->THERMOSTAT2.rssi;
	unsigned char BatteryLevel = 255;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->THERMOSTAT2.subtype)
		{
		case sTypeHE105:
			WriteMessage("subtype       = HE105");
			break;
		case sTypeRTS10:
			WriteMessage("subtype       = RTS10");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->THERMOSTAT2.packettype, pResponse->THERMOSTAT2.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->THERMOSTAT2.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "Unit code        = 0x%02X", pResponse->THERMOSTAT2.unitcode);
		WriteMessage(szTmp);

		switch (pResponse->THERMOSTAT2.cmnd)
		{
		case thermostat2_sOff:
			WriteMessage("Command       = Off");
			break;
		case thermostat2_sOn:
			WriteMessage("Command       = On");
			break;
		default:
			WriteMessage("Command       = unknown");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->THERMOSTAT2.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Thermostat3(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeThermostat3;
	unsigned char subType = pResponse->THERMOSTAT3.subtype;
	std::string ID;
	sprintf(szTmp, "%02X%02X%02X", pResponse->THERMOSTAT3.unitcode1, pResponse->THERMOSTAT3.unitcode2, pResponse->THERMOSTAT3.unitcode3);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = pResponse->THERMOSTAT3.cmnd;
	unsigned char SignalLevel = pResponse->THERMOSTAT3.rssi;
	unsigned char BatteryLevel = 255;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	CheckSceneCode(DevRowIdx, devType, subType, cmnd, "");

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->THERMOSTAT3.subtype)
		{
		case sTypeMertikG6RH4T1:
			WriteMessage("subtype       = Mertik G6R-H4T1");
			break;
		case sTypeMertikG6RH4TB:
			WriteMessage("subtype       = Mertik G6R-H4TB");
			break;
		case sTypeMertikG6RH4TD:
			WriteMessage("subtype       = Mertik G6R-H4TD");
			break;
		case sTypeMertikG6RH4S:
			WriteMessage("subtype       = Mertik G6R-H4S");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->THERMOSTAT3.packettype, pResponse->THERMOSTAT3.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->THERMOSTAT3.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "ID            = 0x%02X%02X%02X", pResponse->THERMOSTAT3.unitcode1, pResponse->THERMOSTAT3.unitcode2, pResponse->THERMOSTAT3.unitcode3);
		WriteMessage(szTmp);

		switch (pResponse->THERMOSTAT3.cmnd)
		{
		case thermostat3_sOff:
			WriteMessage("Command       = Off");
			break;
		case thermostat3_sOn:
			WriteMessage("Command       = On");
			break;
		case thermostat3_sUp:
			WriteMessage("Command       = Up");
			break;
		case thermostat3_sDown:
			WriteMessage("Command       = Down");
			break;
		case thermostat3_sRunUp:
			if (pResponse->THERMOSTAT3.subtype == sTypeMertikG6RH4T1)
				WriteMessage("Command       = Run Up");
			else
				WriteMessage("Command       = 2nd Off");
			break;
		case thermostat3_sRunDown:
			if (pResponse->THERMOSTAT3.subtype == sTypeMertikG6RH4T1)
				WriteMessage("Command       = Run Down");
			else
				WriteMessage("Command       = 2nd On");
			break;
		case thermostat3_sStop:
			if (pResponse->THERMOSTAT3.subtype == sTypeMertikG6RH4T1)
				WriteMessage("Command       = Stop");
			else
				WriteMessage("Command       = unknown");
		default:
			WriteMessage("Command       = unknown");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->THERMOSTAT3.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Thermostat4(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeThermostat4;
	unsigned char subType = pResponse->THERMOSTAT4.subtype;
	std::string ID;
	sprintf(szTmp, "%02X%02X%02X", pResponse->THERMOSTAT4.unitcode1, pResponse->THERMOSTAT4.unitcode2, pResponse->THERMOSTAT4.unitcode3);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char SignalLevel = pResponse->THERMOSTAT4.rssi;
	unsigned char BatteryLevel = 255;
	sprintf(szTmp, "%d;%d;%d;%d;%d;%d",
		pResponse->THERMOSTAT4.beep,
		pResponse->THERMOSTAT4.fan1_speed,
		pResponse->THERMOSTAT4.fan2_speed,
		pResponse->THERMOSTAT4.fan3_speed,
		pResponse->THERMOSTAT4.flame_power,
		pResponse->THERMOSTAT4.mode
	);

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->THERMOSTAT4.subtype)
		{
		case sTypeMCZ1:
			WriteMessage("subtype       = MCZ pellet stove 1 fan model");
			break;
		case sTypeMCZ2:
			WriteMessage("subtype       = MCZ pellet stove 2 fan model");
			break;
		case sTypeMCZ3:
			WriteMessage("subtype       = MCZ pellet stove 3 fan model");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->THERMOSTAT4.packettype, pResponse->THERMOSTAT4.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->THERMOSTAT4.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "ID            = 0x%02X%02X%02X", pResponse->THERMOSTAT4.unitcode1, pResponse->THERMOSTAT4.unitcode2, pResponse->THERMOSTAT4.unitcode3);
		WriteMessage(szTmp);

		if (pResponse->THERMOSTAT4.beep)
			WriteMessage("Beep          = Yes");
		else
			WriteMessage("Beep          = No");

		if (pResponse->THERMOSTAT4.fan1_speed == 6)
			strcpy(szTmp, "Fan 1 Speed   = Auto");
		else
			sprintf(szTmp, "Fan 1 Speed   = %d", pResponse->THERMOSTAT4.fan1_speed);
		WriteMessage(szTmp);

		if (pResponse->THERMOSTAT4.fan2_speed == 6)
			strcpy(szTmp, "Fan 2 Speed   = Auto");
		else
			sprintf(szTmp, "Fan 2 Speed   = %d", pResponse->THERMOSTAT4.fan2_speed);
		WriteMessage(szTmp);

		if (pResponse->THERMOSTAT4.fan3_speed == 6)
			strcpy(szTmp, "Fan 3 Speed   = Auto");
		else
			sprintf(szTmp, "Fan 3 Speed   = %d", pResponse->THERMOSTAT4.fan3_speed);
		WriteMessage(szTmp);

		sprintf(szTmp, "Flame power   = %d", pResponse->THERMOSTAT4.flame_power);
		WriteMessage(szTmp);

		switch (pResponse->THERMOSTAT4.mode)
		{
		case thermostat4_sOff:
			WriteMessage("Command       = Off");
			break;
		case thermostat4_sManual:
			WriteMessage("Command       = Manual");
			break;
		case thermostat4_sAuto:
			WriteMessage("Command       = Auto");
			break;
		case thermostat4_sEco:
			WriteMessage("Command       = Eco");
			break;
		default:
			WriteMessage("Command       = unknown");
			break;
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->THERMOSTAT3.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Radiator1(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeRadiator1;
	unsigned char subType = pResponse->RADIATOR1.subtype;
	std::string ID;
	sprintf(szTmp, "%X%02X%02X%02X", pResponse->RADIATOR1.id1, pResponse->RADIATOR1.id2, pResponse->RADIATOR1.id3, pResponse->RADIATOR1.id4);
	ID = szTmp;
	unsigned char Unit = pResponse->RADIATOR1.unitcode;
	unsigned char cmnd = pResponse->RADIATOR1.cmnd;
	unsigned char SignalLevel = pResponse->RADIATOR1.rssi;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%d.%d", pResponse->RADIATOR1.temperature, pResponse->RADIATOR1.tempPoint5);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->RADIATOR1.subtype)
		{
		case sTypeSmartwares:
			WriteMessage("subtype       = Smartwares");
			break;
		case sTypeSmartwaresSwitchRadiator:
			WriteMessage("subtype       = Smartwares Radiator Switch");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->RADIATOR1.packettype, pResponse->RADIATOR1.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->THERMOSTAT3.seqnbr);
		WriteMessage(szTmp);

		sprintf(szTmp, "ID            = %X%02X%02X%02X", pResponse->RADIATOR1.id1, pResponse->RADIATOR1.id2, pResponse->RADIATOR1.id3, pResponse->RADIATOR1.id4);
		WriteMessage(szTmp);
		sprintf(szTmp, "Unit          = %d", pResponse->RADIATOR1.unitcode);
		WriteMessage(szTmp);

		switch (pResponse->RADIATOR1.cmnd)
		{
		case Radiator1_sNight:
			WriteMessage("Command       = Night/Off");
			break;
		case Radiator1_sDay:
			WriteMessage("Command       = Day/On");
			break;
		case Radiator1_sSetTemp:
			WriteMessage("Command       = Set Temperature");
			break;
		default:
			WriteMessage("Command       = unknown");
			break;
		}

		sprintf(szTmp, "Temp          = %d.%d", pResponse->RADIATOR1.temperature, pResponse->RADIATOR1.tempPoint5);
		WriteMessage(szTmp);
		sprintf(szTmp, "Signal level  = %d", pResponse->RADIATOR1.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

//not in dbase yet
void MainWorker::decode_Baro(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessage("");

	//unsigned char devType=pTypeBARO;

	WriteMessage("Not implemented");

	procResult.DeviceRowIdx = -1;
}

//not in dbase yet
void MainWorker::decode_DateTime(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessage("");

	//unsigned char devType=pTypeDT;

	char szTmp[100];

	switch (pResponse->DT.subtype)
	{
	case sTypeDT1:
		WriteMessage("Subtype       = DT1 - RTGR328N");
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->DT.packettype, pResponse->DT.subtype);
		WriteMessage(szTmp);
		break;
	}
	sprintf(szTmp, "Sequence nbr  = %d", pResponse->DT.seqnbr);
	WriteMessage(szTmp);
	sprintf(szTmp, "ID            = %d", (pResponse->DT.id1 * 256) + pResponse->DT.id2);
	WriteMessage(szTmp);

	WriteMessage("Day of week   = ", false);

	switch (pResponse->DT.dow)
	{
	case 1:
		WriteMessage(" Sunday");
		break;
	case 2:
		WriteMessage(" Monday");
		break;
	case 3:
		WriteMessage(" Tuesday");
		break;
	case 4:
		WriteMessage(" Wednesday");
		break;
	case 5:
		WriteMessage(" Thursday");
		break;
	case 6:
		WriteMessage(" Friday");
		break;
	case 7:
		WriteMessage(" Saturday");
		break;
	}
	sprintf(szTmp, "Date yy/mm/dd = %02d/%02d/%02d", pResponse->DT.yy, pResponse->DT.mm, pResponse->DT.dd);
	WriteMessage(szTmp);
	sprintf(szTmp, "Time          = %02d:%02d:%02d", pResponse->DT.hr, pResponse->DT.min, pResponse->DT.sec);
	WriteMessage(szTmp);
	sprintf(szTmp, "Signal level  = %d", pResponse->DT.rssi);
	WriteMessage(szTmp);
	if ((pResponse->DT.battery_level & 0x0F) == 0)
		WriteMessage("Battery       = Low");
	else
		WriteMessage("Battery       = OK");
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Current(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeCURRENT;
	unsigned char subType = pResponse->CURRENT.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->CURRENT.id1 * 256) + pResponse->CURRENT.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->CURRENT.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, false, pResponse->CURRENT.battery_level & 0x0F);

	float CurrentChannel1 = float((pResponse->CURRENT.ch1h * 256) + pResponse->CURRENT.ch1l) / 10.0f;
	float CurrentChannel2 = float((pResponse->CURRENT.ch2h * 256) + pResponse->CURRENT.ch2l) / 10.0f;
	float CurrentChannel3 = float((pResponse->CURRENT.ch3h * 256) + pResponse->CURRENT.ch3l) / 10.0f;
	sprintf(szTmp, "%.1f;%.1f;%.1f", CurrentChannel1, CurrentChannel2, CurrentChannel3);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->CURRENT.subtype)
		{
		case sTypeELEC1:
			WriteMessage("subtype       = ELEC1 - OWL CM113, Electrisave, cent-a-meter");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->CURRENT.packettype, pResponse->CURRENT.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->CURRENT.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->CURRENT.id1 * 256) + pResponse->CURRENT.id2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Count         = %d", pResponse->CURRENT.id2);//m_rxbuffer[5]);
		WriteMessage(szTmp);
		sprintf(szTmp, "Channel 1     = %.1f ampere", CurrentChannel1);
		WriteMessage(szTmp);
		sprintf(szTmp, "Channel 2     = %.1f ampere", CurrentChannel2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Channel 3     = %.1f ampere", CurrentChannel3);
		WriteMessage(szTmp);

		sprintf(szTmp, "Signal level  = %d", pResponse->CURRENT.rssi);
		WriteMessage(szTmp);

		if ((pResponse->CURRENT.battery_level & 0xF) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Energy(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	unsigned char subType = pResponse->ENERGY.subtype;
	unsigned char SignalLevel = pResponse->ENERGY.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, false, pResponse->ENERGY.battery_level & 0x0F);

	long instant = (pResponse->ENERGY.instant1 * 0x1000000) + (pResponse->ENERGY.instant2 * 0x10000) + (pResponse->ENERGY.instant3 * 0x100) + pResponse->ENERGY.instant4;

	double total = (
		double(pResponse->ENERGY.total1) * 0x10000000000ULL +
		double(pResponse->ENERGY.total2) * 0x100000000ULL +
		double(pResponse->ENERGY.total3) * 0x1000000 +
		double(pResponse->ENERGY.total4) * 0x10000 +
		double(pResponse->ENERGY.total5) * 0x100 +
		double(pResponse->ENERGY.total6)
		) / 223.666;

	if (pResponse->ENERGY.subtype == sTypeELEC3)
	{
		if (total == 0)
		{
			char szTmp[20];
			std::string ID;
			sprintf(szTmp, "%08X", (pResponse->ENERGY.id1 * 256) + pResponse->ENERGY.id2);
			ID = szTmp;

			//Retrieve last total from current record
			int nValue;
			subType = sTypeKwh; // sensor type changed during recording
			unsigned char devType = pTypeGeneral; // Device reported as General and not Energy
			unsigned char Unit = 1; // in decode_general() Unit is set to 1
			std::string sValue;
			struct tm LastUpdateTime;
			if (!m_sql.GetLastValue(HwdID, ID.c_str(), Unit, devType, subType, nValue, sValue, LastUpdateTime))
				return;
			std::vector<std::string> strarray;
			StringSplit(sValue, ";", strarray);
			if (strarray.size() != 2)
				return;
			total = atof(strarray[1].c_str());
		}
	}

	//Translate this sensor type to the new kWh sensor type
	_tGeneralDevice gdevice;
	gdevice.intval1 = (pResponse->ENERGY.id1 * 256) + pResponse->ENERGY.id2;
	gdevice.subtype = sTypeKwh;
	gdevice.floatval1 = (float)instant;
	gdevice.floatval2 = (float)total;

	int voltage = 230;
	m_sql.GetPreferencesVar("ElectricVoltage", voltage);
	if (voltage != 230)
	{
		float mval = float(voltage) / 230.0f;
		gdevice.floatval1 *= mval;
		gdevice.floatval2 *= mval;
	}

	decode_General(HwdID, HwdType, (const tRBUF*)&gdevice, procResult, SignalLevel, BatteryLevel);
	procResult.bProcessBatteryValue = false;
}

void MainWorker::decode_Power(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypePOWER;
	unsigned char subType = pResponse->POWER.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->POWER.id1 * 256) + pResponse->POWER.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->POWER.rssi;
	unsigned char BatteryLevel = 255;

	float Voltage = (float)pResponse->POWER.voltage;
	double current = ((pResponse->POWER.currentH * 256) + pResponse->POWER.currentL) / 100.0;
	double instant = ((pResponse->POWER.powerH * 256) + pResponse->POWER.powerL) / 10.0;// Watt
	double usage = ((pResponse->POWER.energyH * 256) + pResponse->POWER.energyL) / 100.0; //kWh
	double powerfactor = pResponse->POWER.pf / 100.0;
	float frequency = (float)pResponse->POWER.freq; //Hz

	sprintf(szTmp, "%ld;%.2f", long(round(instant)), usage*1000.0);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	//Voltage
	sprintf(szTmp, "%.3f", Voltage);
	std::string tmpDevName;
	uint64_t DevRowIdxAlt = m_sql.UpdateValue(HwdID, ID.c_str(), 1, pTypeGeneral, sTypeVoltage, SignalLevel, BatteryLevel, cmnd, szTmp, tmpDevName);
	if (DevRowIdxAlt == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, tmpDevName, 1, pTypeGeneral, sTypeVoltage, Voltage);

	//Powerfactor
	sprintf(szTmp, "%.2f", (float)powerfactor);
	DevRowIdxAlt = m_sql.UpdateValue(HwdID, ID.c_str(), 2, pTypeGeneral, sTypePercentage, SignalLevel, BatteryLevel, cmnd, szTmp, tmpDevName);
	if (DevRowIdxAlt == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, tmpDevName, 2, pTypeGeneral, sTypePercentage, (float)powerfactor);

	//Frequency
	sprintf(szTmp, "%.2f", (float)frequency);
	DevRowIdxAlt = m_sql.UpdateValue(HwdID, ID.c_str(), 3, pTypeGeneral, sTypePercentage, SignalLevel, BatteryLevel, cmnd, szTmp, tmpDevName);
	if (DevRowIdxAlt == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, tmpDevName, 3, pTypeGeneral, sTypePercentage, frequency);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->POWER.subtype)
		{
		case sTypeELEC5:
			WriteMessage("subtype       = ELEC5 - Revolt");
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->POWER.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->POWER.id1 * 256) + pResponse->POWER.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Voltage       = %g Volt", Voltage);
		WriteMessage(szTmp);
		sprintf(szTmp, "Current       = %.2f Ampere", current);
		WriteMessage(szTmp);

		sprintf(szTmp, "Instant usage = %.2f Watt", instant);
		WriteMessage(szTmp);
		sprintf(szTmp, "total usage   = %.2f kWh", usage);
		WriteMessage(szTmp);

		sprintf(szTmp, "Power factor  = %.2f", powerfactor);
		WriteMessage(szTmp);
		sprintf(szTmp, "Frequency     = %g Hz", frequency);
		WriteMessage(szTmp);

		sprintf(szTmp, "Signal level  = %d", pResponse->POWER.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Current_Energy(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeCURRENTENERGY;
	unsigned char subType = pResponse->CURRENT_ENERGY.subtype;
	std::string ID;
	sprintf(szTmp, "%d", (pResponse->CURRENT_ENERGY.id1 * 256) + pResponse->CURRENT_ENERGY.id2);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->CURRENT_ENERGY.rssi;
	unsigned char BatteryLevel = get_BateryLevel(HwdType, false, pResponse->CURRENT_ENERGY.battery_level & 0x0F);

	float CurrentChannel1 = float((pResponse->CURRENT_ENERGY.ch1h * 256) + pResponse->CURRENT_ENERGY.ch1l) / 10.0f;
	float CurrentChannel2 = float((pResponse->CURRENT_ENERGY.ch2h * 256) + pResponse->CURRENT_ENERGY.ch2l) / 10.0f;
	float CurrentChannel3 = float((pResponse->CURRENT_ENERGY.ch3h * 256) + pResponse->CURRENT_ENERGY.ch3l) / 10.0f;

	double usage = 0;

	if (pResponse->CURRENT_ENERGY.count != 0)
	{
		//no usage provided, get the last usage
		std::vector<std::vector<std::string> > result2;
		result2 = m_sql.safe_query(
			"SELECT nValue,sValue FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
			HwdID, ID.c_str(), int(Unit), int(devType), int(subType));
		if (result2.size() > 0)
		{
			std::vector<std::string> strarray;
			StringSplit(result2[0][1], ";", strarray);
			if (strarray.size() == 4)
			{
				usage = atof(strarray[3].c_str());
			}
		}
	}
	else
	{
		usage = (
			double(pResponse->CURRENT_ENERGY.total1) * 0x10000000000ULL +
			double(pResponse->CURRENT_ENERGY.total2) * 0x100000000ULL +
			double(pResponse->CURRENT_ENERGY.total3) * 0x1000000 +
			double(pResponse->CURRENT_ENERGY.total4) * 0x10000 +
			double(pResponse->CURRENT_ENERGY.total5) * 0x100 +
			pResponse->CURRENT_ENERGY.total6
			) / 223.666;

		if (usage == 0)
		{
			//That should not be, let's get the previous value
			//no usage provided, get the last usage
			std::vector<std::vector<std::string> > result2;
			result2 = m_sql.safe_query(
				"SELECT nValue,sValue FROM DeviceStatus WHERE (HardwareID=%d AND DeviceID='%q' AND Unit=%d AND Type=%d AND SubType=%d)",
				HwdID, ID.c_str(), int(Unit), int(devType), int(subType));
			if (result2.size() > 0)
			{
				std::vector<std::string> strarray;
				StringSplit(result2[0][1], ";", strarray);
				if (strarray.size() == 4)
				{
					usage = atof(strarray[3].c_str());
				}
			}
		}

		int voltage = 230;
		m_sql.GetPreferencesVar("ElectricVoltage", voltage);

		sprintf(szTmp, "%ld;%.2f", (long)round((CurrentChannel1 + CurrentChannel2 + CurrentChannel3)*voltage), usage);
		m_sql.UpdateValue(HwdID, ID.c_str(), Unit, pTypeENERGY, sTypeELEC3, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	}
	sprintf(szTmp, "%.1f;%.1f;%.1f;%.3f", CurrentChannel1, CurrentChannel2, CurrentChannel3, usage);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->CURRENT_ENERGY.subtype)
		{
		case sTypeELEC4:
			WriteMessage("subtype       = ELEC4 - OWL CM180i");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->CURRENT_ENERGY.packettype, pResponse->CURRENT_ENERGY.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->CURRENT_ENERGY.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->CURRENT_ENERGY.id1 * 256) + pResponse->CURRENT_ENERGY.id2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Count         = %d", pResponse->CURRENT_ENERGY.count);
		WriteMessage(szTmp);
		float ampereChannel1, ampereChannel2, ampereChannel3;
		ampereChannel1 = float((pResponse->CURRENT_ENERGY.ch1h * 256) + pResponse->CURRENT_ENERGY.ch1l) / 10.0f;
		ampereChannel2 = float((pResponse->CURRENT_ENERGY.ch2h * 256) + pResponse->CURRENT_ENERGY.ch2l) / 10.0f;
		ampereChannel3 = float((pResponse->CURRENT_ENERGY.ch3h * 256) + pResponse->CURRENT_ENERGY.ch3l) / 10.0f;
		sprintf(szTmp, "Channel 1     = %.1f ampere", ampereChannel1);
		WriteMessage(szTmp);
		sprintf(szTmp, "Channel 2     = %.1f ampere", ampereChannel2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Channel 3     = %.1f ampere", ampereChannel3);
		WriteMessage(szTmp);

		if (pResponse->CURRENT_ENERGY.count == 0)
		{
			sprintf(szTmp, "total usage   = %.3f Wh", usage);
			WriteMessage(szTmp);
		}

		sprintf(szTmp, "Signal level  = %d", pResponse->CURRENT_ENERGY.rssi);
		WriteMessage(szTmp);

		if ((pResponse->CURRENT_ENERGY.battery_level & 0xF) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

//not in dbase yet
void MainWorker::decode_Gas(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessage("");

	//unsigned char devType=pTypeGAS;

	WriteMessage("Not implemented");

	procResult.DeviceRowIdx = -1;
}

//not in dbase yet
void MainWorker::decode_Water(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	WriteMessage("");

	//unsigned char devType=pTypeWATER;

	WriteMessage("Not implemented");

	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Weight(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeWEIGHT;
	unsigned char subType = pResponse->WEIGHT.subtype;
	unsigned short weightID = (pResponse->WEIGHT.id1 * 256) + pResponse->WEIGHT.id2;
	std::string ID;
	sprintf(szTmp, "%d", weightID);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->WEIGHT.rssi;
	unsigned char BatteryLevel = 255;
	float weight = (float(pResponse->WEIGHT.weighthigh) * 25.6f) + (float(pResponse->WEIGHT.weightlow) / 10.0f);

	float AddjValue = 0.0f;
	float AddjMulti = 1.0f;
	m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
	weight += AddjValue;

	sprintf(szTmp, "%.1f", weight);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, weight);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->WEIGHT.subtype)
		{
		case sTypeWEIGHT1:
			WriteMessage("subtype       = BWR102");
			break;
		case sTypeWEIGHT2:
			WriteMessage("subtype       = GR101");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type = %02X:%02X", pResponse->WEIGHT.packettype, pResponse->WEIGHT.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->WEIGHT.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->WEIGHT.id1 * 256) + pResponse->WEIGHT.id2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Weight        = %.1f kg", (float(pResponse->WEIGHT.weighthigh) * 25.6f) + (float(pResponse->WEIGHT.weightlow) / 10));
		WriteMessage(szTmp);
		sprintf(szTmp, "Signal level  = %d", pResponse->WEIGHT.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_RFXSensor(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeRFXSensor;
	unsigned char subType = pResponse->RFXSENSOR.subtype;
	std::string ID;
	sprintf(szTmp, "%d", pResponse->RFXSENSOR.id);
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->RFXSENSOR.rssi;
	unsigned char BatteryLevel = 255;

	if ((HwdType == HTYPE_EnOceanESP2) || (HwdType == HTYPE_EnOceanESP3))
	{
		BatteryLevel = 255;
		SignalLevel = 12;
		Unit = (pResponse->RFXSENSOR.rssi << 4) | pResponse->RFXSENSOR.filler;
	}

	float temp;
	int volt = 0;
	switch (pResponse->RFXSENSOR.subtype)
	{
	case sTypeRFXSensorTemp:
	{
		if ((pResponse->RFXSENSOR.msg1 & 0x80) == 0) //positive temperature?
			temp = float((pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2) / 100.0f;
		else
			temp = -(float(((pResponse->RFXSENSOR.msg1 & 0x7F) * 256) + pResponse->RFXSENSOR.msg2) / 100.0f);
		float AddjValue = 0.0f;
		float AddjMulti = 1.0f;
		m_sql.GetAddjustment(HwdID, ID.c_str(), Unit, devType, subType, AddjValue, AddjMulti);
		temp += AddjValue;
		sprintf(szTmp, "%.1f", temp);
	}
	break;
	case sTypeRFXSensorAD:
	case sTypeRFXSensorVolt:
	{
		volt = (pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2;
		if (
			(HwdType == HTYPE_RFXLAN) ||
			(HwdType == HTYPE_RFXtrx315) ||
			(HwdType == HTYPE_RFXtrx433) ||
			(HwdType == HTYPE_RFXtrx868)
			)
		{
			volt *= 10;
		}
		sprintf(szTmp, "%d", volt);
	}
	break;
	}
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	switch (pResponse->RFXSENSOR.subtype)
	{
	case sTypeRFXSensorTemp:
	{
		m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, temp);
	}
	break;
	case sTypeRFXSensorAD:
	case sTypeRFXSensorVolt:
	{
		m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, (float)volt);
	}
	break;
	}

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->RFXSENSOR.subtype)
		{
		case sTypeRFXSensorTemp:
			WriteMessage("subtype       = Temperature");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXSENSOR.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->RFXSENSOR.id);
			WriteMessage(szTmp);

			if ((pResponse->RFXSENSOR.msg1 & 0x80) == 0) //positive temperature?
			{
				sprintf(szTmp, "Temperature   = %.2f C", float((pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2) / 100.0f);
				WriteMessage(szTmp);
			}
			else
			{
				sprintf(szTmp, "Temperature   = -%.2f C", float(((pResponse->RFXSENSOR.msg1 & 0x7F) * 256) + pResponse->RFXSENSOR.msg2) / 100.0f);
				WriteMessage(szTmp);
			}
			break;
		case sTypeRFXSensorAD:
			WriteMessage("subtype       = A/D");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXSENSOR.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->RFXSENSOR.id);
			WriteMessage(szTmp);
			sprintf(szTmp, "volt          = %d mV", (pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXSensorVolt:
			WriteMessage("subtype       = Voltage");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXSENSOR.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->RFXSENSOR.id);
			WriteMessage(szTmp);
			sprintf(szTmp, "volt          = %d mV", (pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXSensorMessage:
			WriteMessage("subtype       = Message");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXSENSOR.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", pResponse->RFXSENSOR.id);
			WriteMessage(szTmp);
			switch (pResponse->RFXSENSOR.msg2)
			{
			case 0x1:
				WriteMessage("msg           = sensor addresses incremented");
				break;
			case 0x2:
				WriteMessage("msg           = battery low detected");
				break;
			case 0x81:
				WriteMessage("msg           = no 1-wire device connected");
				break;
			case 0x82:
				WriteMessage("msg           = 1-Wire ROM CRC error");
				break;
			case 0x83:
				WriteMessage("msg           = 1-Wire device connected is not a DS18B20 or DS2438");
				break;
			case 0x84:
				WriteMessage("msg           = no end of read signal received from 1-Wire device");
				break;
			case 0x85:
				WriteMessage("msg           = 1-Wire scratch pad CRC error");
				break;
			default:
				WriteMessage("ERROR: unknown message");
				break;
			}
			sprintf(szTmp, "msg           = %d", (pResponse->RFXSENSOR.msg1 * 256) + pResponse->RFXSENSOR.msg2);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->RFXSENSOR.packettype, pResponse->RFXSENSOR.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->RFXSENSOR.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_RFXMeter(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	uint64_t DevRowIdx = -1;
	char szTmp[100];
	unsigned char devType = pTypeRFXMeter;
	unsigned char subType = pResponse->RFXMETER.subtype;
	if (subType == sTypeRFXMeterCount)
	{
		std::string ID;
		sprintf(szTmp, "%d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
		ID = szTmp;
		unsigned char Unit = 0;
		unsigned char cmnd = 0;
		unsigned char SignalLevel = pResponse->RFXMETER.rssi;
		unsigned char BatteryLevel = 255;

		unsigned long counter = (pResponse->RFXMETER.count1 << 24) + (pResponse->RFXMETER.count2 << 16) + (pResponse->RFXMETER.count3 << 8) + pResponse->RFXMETER.count4;
		//float RFXPwr = float(counter) / 1000.0f;

		sprintf(szTmp, "%lu", counter);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		unsigned long counter;

		switch (pResponse->RFXMETER.subtype)
		{
		case sTypeRFXMeterCount:
			WriteMessage("subtype       = RFXMeter counter");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			counter = (pResponse->RFXMETER.count1 << 24) + (pResponse->RFXMETER.count2 << 16) + (pResponse->RFXMETER.count3 << 8) + pResponse->RFXMETER.count4;
			sprintf(szTmp, "Counter       = %lu", counter);
			WriteMessage(szTmp);
			sprintf(szTmp, "if RFXPwr     = %.3f kWh", float(counter) / 1000.0f);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterInterval:
			WriteMessage("subtype       = RFXMeter new interval time set");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			WriteMessage("Interval time = ", false);

			switch (pResponse->RFXMETER.count3)
			{
			case 0x1:
				WriteMessage("30 seconds");
				break;
			case 0x2:
				WriteMessage("1 minute");
				break;
			case 0x4:
				WriteMessage("6 minutes");
				break;
			case 0x8:
				WriteMessage("12 minutes");
				break;
			case 0x10:
				WriteMessage("15 minutes");
				break;
			case 0x20:
				WriteMessage("30 minutes");
				break;
			case 0x40:
				WriteMessage("45 minutes");
				break;
			case 0x80:
				WriteMessage("1 hour");
				break;
			}
			break;
		case sTypeRFXMeterCalib:
			switch (pResponse->RFXMETER.count2 & 0xC0)
			{
			case 0x0:
				WriteMessage("subtype       = Calibrate mode for channel 1");
				break;
			case 0x40:
				WriteMessage("subtype       = Calibrate mode for channel 2");
				break;
			case 0x80:
				WriteMessage("subtype       = Calibrate mode for channel 3");
				break;
			}
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			counter = (((pResponse->RFXMETER.count2 & 0x3F) << 16) + (pResponse->RFXMETER.count3 << 8) + pResponse->RFXMETER.count4) / 1000;
			sprintf(szTmp, "Calibrate cnt = %lu msec", counter);
			WriteMessage(szTmp);

			sprintf(szTmp, "RFXPwr        = %.3f kW", 1.0f / (float(16 * counter) / (3600000.0f / 62.5f)));
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterAddr:
			WriteMessage("subtype       = New address set, push button for next address");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterCounterReset:
			switch (pResponse->RFXMETER.count2 & 0xC0)
			{
			case 0x0:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else RESET COUNTER channel 1 will be executed");
				break;
			case 0x40:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else RESET COUNTER channel 2 will be executed");
				break;
			case 0x80:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else RESET COUNTER channel 3 will be executed");
				break;
			}
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterCounterSet:
			switch (pResponse->RFXMETER.count2 & 0xC0)
			{
			case 0x0:
				WriteMessage("subtype       = Counter channel 1 is reset to zero");
				break;
			case 0x40:
				WriteMessage("subtype       = Counter channel 2 is reset to zero");
				break;
			case 0x80:
				WriteMessage("subtype       = Counter channel 3 is reset to zero");
				break;
			}
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			counter = (pResponse->RFXMETER.count1 << 24) + (pResponse->RFXMETER.count2 << 16) + (pResponse->RFXMETER.count3 << 8) + pResponse->RFXMETER.count4;
			sprintf(szTmp, "Counter       = %lu", counter);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterSetInterval:
			WriteMessage("subtype       = Push the button for next mode within 5 seconds or else SET INTERVAL MODE will be entered");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterSetCalib:
			switch (pResponse->RFXMETER.count2 & 0xC0)
			{
			case 0x0:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else CALIBRATION mode for channel 1 will be executed");
				break;
			case 0x40:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else CALIBRATION mode for channel 2 will be executed");
				break;
			case 0x80:
				WriteMessage("subtype       = Push the button for next mode within 5 seconds or else CALIBRATION mode for channel 3 will be executed");
				break;
			}
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterSetAddr:
			WriteMessage("subtype       = Push the button for next mode within 5 seconds or else SET ADDRESS MODE will be entered");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			break;
		case sTypeRFXMeterIdent:
			WriteMessage("subtype       = RFXMeter identification");
			sprintf(szTmp, "Sequence nbr  = %d", pResponse->RFXMETER.seqnbr);
			WriteMessage(szTmp);
			sprintf(szTmp, "ID            = %d", (pResponse->RFXMETER.id1 * 256) + pResponse->RFXMETER.id2);
			WriteMessage(szTmp);
			sprintf(szTmp, "FW version    = %02X", pResponse->RFXMETER.count3);
			WriteMessage(szTmp);
			WriteMessage("Interval time = ", false);

			switch (pResponse->RFXMETER.count4)
			{
			case 0x1:
				WriteMessage("30 seconds");
				break;
			case 0x2:
				WriteMessage("1 minute");
				break;
			case 0x4:
				WriteMessage("6 minutes");
				break;
			case 0x8:
				WriteMessage("12 minutes");
				break;
			case 0x10:
				WriteMessage("15 minutes");
				break;
			case 0x20:
				WriteMessage("30 minutes");
				break;
			case 0x40:
				WriteMessage("45 minutes");
				break;
			case 0x80:
				WriteMessage("1 hour");
				break;
			}
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->RFXMETER.packettype, pResponse->RFXMETER.subtype);
			WriteMessage(szTmp);
			break;
		}
		sprintf(szTmp, "Signal level  = %d", pResponse->RFXMETER.rssi);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_P1MeterPower(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tP1Power *p1Power = reinterpret_cast<const _tP1Power*>(pResponse);

	if (p1Power->len != sizeof(_tP1Power) - 1)
		return;

	unsigned char devType = p1Power->type;
	unsigned char subType = p1Power->subtype;
	std::string ID;
	sprintf(szTmp, "%d", p1Power->ID);
	ID = szTmp;

	unsigned char Unit = subType;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%u;%u;%u;%u;%u;%u",
		p1Power->powerusage1,
		p1Power->powerusage2,
		p1Power->powerdeliv1,
		p1Power->powerdeliv2,
		p1Power->usagecurrent,
		p1Power->delivcurrent
	);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (p1Power->subtype)
		{
		case sTypeP1Power:
			WriteMessage("subtype       = P1 Smart Meter Power");

			sprintf(szTmp, "powerusage1 = %.3f kWh", float(p1Power->powerusage1) / 1000.0f);
			WriteMessage(szTmp);
			sprintf(szTmp, "powerusage2 = %.3f kWh", float(p1Power->powerusage2) / 1000.0f);
			WriteMessage(szTmp);

			sprintf(szTmp, "powerdeliv1 = %.3f kWh", float(p1Power->powerdeliv1) / 1000.0f);
			WriteMessage(szTmp);
			sprintf(szTmp, "powerdeliv2 = %.3f kWh", float(p1Power->powerdeliv2) / 1000.0f);
			WriteMessage(szTmp);

			sprintf(szTmp, "current usage = %03u Watt", p1Power->usagecurrent);
			WriteMessage(szTmp);
			sprintf(szTmp, "current deliv = %03u Watt", p1Power->delivcurrent);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", p1Power->type, p1Power->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_P1MeterGas(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tP1Gas *p1Gas = reinterpret_cast<const _tP1Gas*>(pResponse);
	if (p1Gas->len != sizeof(_tP1Gas) - 1)
		return;

	unsigned char devType = p1Gas->type;
	unsigned char subType = p1Gas->subtype;
	std::string ID;
	sprintf(szTmp, "%d", p1Gas->ID);
	ID = szTmp;
	unsigned char Unit = subType;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%u", p1Gas->gasusage);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (p1Gas->subtype)
		{
		case sTypeP1Gas:
			WriteMessage("subtype       = P1 Smart Meter Gas");

			sprintf(szTmp, "gasusage = %.3f m3", float(p1Gas->gasusage) / 1000.0f);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", p1Gas->type, p1Gas->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_YouLessMeter(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tYouLessMeter *pMeter = reinterpret_cast<const _tYouLessMeter*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;
	sprintf(szTmp, "%d", pMeter->ID1);
	std::string ID = szTmp;
	unsigned char Unit = subType;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%lu;%lu",
		pMeter->powerusage,
		pMeter->usagecurrent
	);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pMeter->subtype)
		{
		case sTypeYouLess:
			WriteMessage("subtype       = YouLess Meter");

			sprintf(szTmp, "powerusage = %.3f kWh", float(pMeter->powerusage) / 1000.0f);
			WriteMessage(szTmp);
			sprintf(szTmp, "current usage = %03lu Watt", pMeter->usagecurrent);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Rego6XXTemp(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tRego6XXTemp *pRego = reinterpret_cast<const _tRego6XXTemp*>(pResponse);
	unsigned char devType = pRego->type;
	unsigned char subType = pRego->subtype;
	std::string ID = pRego->ID;
	unsigned char Unit = subType;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%.1f",
		pRego->temperature
	);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, pRego->temperature);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		WriteMessage("subtype       = Rego6XX Temp");

		sprintf(szTmp, "Temp = %.1f", pRego->temperature);
		WriteMessage(szTmp);
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Rego6XXValue(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tRego6XXStatus *pRego = reinterpret_cast<const _tRego6XXStatus*>(pResponse);
	unsigned char devType = pRego->type;
	unsigned char subType = pRego->subtype;
	std::string ID = pRego->ID;
	unsigned char Unit = subType;
	int numValue = pRego->value;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%d",
		pRego->value
	);

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, numValue, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pRego->subtype)
		{
		case sTypeRego6XXStatus:
			WriteMessage("subtype       = Rego6XX Status");
			sprintf(szTmp, "Status = %d", pRego->value);
			WriteMessage(szTmp);
			break;
		case sTypeRego6XXCounter:
			WriteMessage("subtype       = Rego6XX Counter");
			sprintf(szTmp, "Counter = %d", pRego->value);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_AirQuality(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tAirQualityMeter *pMeter = reinterpret_cast<const _tAirQualityMeter*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;
	sprintf(szTmp, "%d", pMeter->id1);
	std::string ID = szTmp;
	unsigned char Unit = pMeter->id2;
	//unsigned char cmnd=0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, pMeter->airquality, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, (const int)pMeter->airquality);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pMeter->subtype)
		{
		case sTypeVoltcraft:
			WriteMessage("subtype       = Voltcraft CO-20");

			sprintf(szTmp, "CO2 = %d ppm", pMeter->airquality);
			WriteMessage(szTmp);
			if (pMeter->airquality < 700)
				strcpy(szTmp, "Quality = Excellent");
			else if (pMeter->airquality < 900)
				strcpy(szTmp, "Quality = Good");
			else if (pMeter->airquality < 1100)
				strcpy(szTmp, "Quality = Fair");
			else if (pMeter->airquality < 1600)
				strcpy(szTmp, "Quality = Mediocre");
			else
				strcpy(szTmp, "Quality = Bad");
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Usage(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tUsageMeter *pMeter = reinterpret_cast<const _tUsageMeter*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;
	sprintf(szTmp, "%X%02X%02X%02X", pMeter->id1, pMeter->id2, pMeter->id3, pMeter->id4);
	std::string ID = szTmp;
	unsigned char Unit = pMeter->dunit;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = 255;

	sprintf(szTmp, "%.1f", pMeter->fusage);

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, pMeter->fusage);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pMeter->subtype)
		{
		case sTypeElectric:
			WriteMessage("subtype       = Electric");

			sprintf(szTmp, "Usage = %.1f W", pMeter->fusage);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Lux(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tLightMeter *pMeter = reinterpret_cast<const _tLightMeter*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;
	sprintf(szTmp, "%X%02X%02X%02X", pMeter->id1, pMeter->id2, pMeter->id3, pMeter->id4);
	std::string ID = szTmp;
	unsigned char Unit = pMeter->dunit;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = pMeter->battery_level;

	sprintf(szTmp, "%.0f", pMeter->fLux);

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, pMeter->fLux);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pMeter->subtype)
		{
		case sTypeLux:
			WriteMessage("subtype       = Lux");

			sprintf(szTmp, "Lux = %.1f W", pMeter->fLux);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_Thermostat(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tThermostat *pMeter = reinterpret_cast<const _tThermostat*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;
	sprintf(szTmp, "%X%02X%02X%02X", pMeter->id1, pMeter->id2, pMeter->id3, pMeter->id4);
	std::string ID = szTmp;
	unsigned char Unit = pMeter->dunit;
	unsigned char cmnd = 0;
	unsigned char SignalLevel = 12;
	unsigned char BatteryLevel = pMeter->battery_level;

	switch (pMeter->subtype)
	{
	case sTypeThermSetpoint:
		sprintf(szTmp, "%.2f", pMeter->temp);
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
		WriteMessage(szTmp);
		return;
	}

	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;

	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, pMeter->temp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		double tvalue = ConvertTemperature(pMeter->temp, m_sql.m_tempsign[0]);
		switch (pMeter->subtype)
		{
		case sTypeThermSetpoint:
			WriteMessage("subtype       = SetPoint");
			sprintf(szTmp, "Temp = %.2f", tvalue);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_General(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult, const unsigned char SignalLevel, const unsigned char BatteryLevel)
{
	char szTmp[200];
	const _tGeneralDevice *pMeter = reinterpret_cast<const _tGeneralDevice*>(pResponse);
	unsigned char devType = pMeter->type;
	unsigned char subType = pMeter->subtype;

	if (
		(subType == sTypeVoltage) ||
		(subType == sTypeCurrent) ||
		(subType == sTypePercentage) ||
		(subType == sTypeWaterflow) ||
		(subType == sTypePressure) ||
		(subType == sTypeZWaveClock) ||
		(subType == sTypeZWaveThermostatMode) ||
		(subType == sTypeZWaveThermostatFanMode) ||
		(subType == sTypeFan) ||
		(subType == sTypeTextStatus) ||
		(subType == sTypeSoundLevel) ||
		(subType == sTypeBaro) ||
		(subType == sTypeDistance) ||
		(subType == sTypeSoilMoisture) ||
		(subType == sTypeCustom) ||
		(subType == sTypeKwh) ||
		(subType == sTypeZWaveAlarm)
		)
	{
		sprintf(szTmp, "%08X", (unsigned int)pMeter->intval1);
	}
	else
	{
		sprintf(szTmp, "%d", pMeter->id);

	}
	std::string ID = szTmp;
	unsigned char Unit = 1;
	unsigned char cmnd = 0;
	strcpy(szTmp, "");

	uint64_t DevRowIdx = -1;

	if (subType == sTypeVisibility)
	{
		sprintf(szTmp, "%.1f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeDistance)
	{
		sprintf(szTmp, "%.1f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeSolarRadiation)
	{
		sprintf(szTmp, "%.1f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeSoilMoisture)
	{
		cmnd = pMeter->intval2;
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeLeafWetness)
	{
		cmnd = pMeter->intval1;
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeVoltage)
	{
		sprintf(szTmp, "%.3f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeCurrent)
	{
		sprintf(szTmp, "%.3f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeBaro)
	{
		sprintf(szTmp, "%.02f;%d", pMeter->floatval1, pMeter->intval2);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypePressure)
	{
		sprintf(szTmp, "%.1f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypePercentage)
	{
		sprintf(szTmp, "%.2f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeWaterflow)
	{
		sprintf(szTmp, "%.2f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeFan)
	{
		sprintf(szTmp, "%d", pMeter->intval2);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeSoundLevel)
	{
		sprintf(szTmp, "%d", pMeter->intval2);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeZWaveClock)
	{
		int tintval = pMeter->intval2;
		int day = tintval / (24 * 60); tintval -= (day * 24 * 60);
		int hour = tintval / (60); tintval -= (hour * 60);
		int minute = tintval;
		sprintf(szTmp, "%d;%d;%d", day, hour, minute);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	}
	else if ((subType == sTypeZWaveThermostatMode) || (subType == sTypeZWaveThermostatFanMode))
	{
		cmnd = (unsigned char)pMeter->intval2;
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
	}
	else if (subType == sTypeKwh)
	{
		sprintf(szTmp, "%.3f;%.3f", pMeter->floatval1, pMeter->floatval2);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	}
	else if (subType == sTypeAlert)
	{
		if (strcmp(pMeter->text, ""))
			sprintf(szTmp, "(%d) %s", pMeter->intval1, pMeter->text);
		else
			sprintf(szTmp, "%d", pMeter->intval1);
		cmnd = pMeter->intval1;
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeCustom)
	{
		sprintf(szTmp, "%.4f", pMeter->floatval1);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeZWaveAlarm)
	{
		Unit = pMeter->id;
		cmnd = pMeter->intval2;
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, procResult.DeviceName);
		if (DevRowIdx == -1)
			return;
	}
	else if (subType == sTypeTextStatus)
	{
		strcpy(szTmp, pMeter->text);
		DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, szTmp, procResult.DeviceName);
	}
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, cmnd, szTmp);

	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pMeter->subtype)
		{
		case sTypeVisibility:
			WriteMessage("subtype       = Visibility");
			sprintf(szTmp, "Visibility = %.1f km", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeDistance:
			WriteMessage("subtype       = Distance");
			sprintf(szTmp, "Distance = %.1f cm", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeSolarRadiation:
			WriteMessage("subtype       = Solar Radiation");
			sprintf(szTmp, "Radiation = %.1f Watt/m2", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeSoilMoisture:
			WriteMessage("subtype       = Soil Moisture");
			sprintf(szTmp, "Moisture = %d cb", pMeter->intval2);
			WriteMessage(szTmp);
			break;
		case sTypeLeafWetness:
			WriteMessage("subtype       = Leaf Wetness");
			sprintf(szTmp, "Wetness = %d", pMeter->intval1);
			WriteMessage(szTmp);
			break;
		case sTypeVoltage:
			WriteMessage("subtype       = Voltage");
			sprintf(szTmp, "Voltage = %.3f V", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeCurrent:
			WriteMessage("subtype       = Current");
			sprintf(szTmp, "Current = %.3f V", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypePressure:
			WriteMessage("subtype       = Pressure");
			sprintf(szTmp, "Pressure = %.1f bar", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeBaro:
			WriteMessage("subtype       = Barometric Pressure");
			sprintf(szTmp, "Pressure = %.1f hPa", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeFan:
			WriteMessage("subtype       = Fan");
			sprintf(szTmp, "Speed = %d RPM", pMeter->intval2);
			WriteMessage(szTmp);
			break;
		case sTypeSoundLevel:
			WriteMessage("subtype       = Sound Level");
			sprintf(szTmp, "Sound Level = %d dB", pMeter->intval2);
			WriteMessage(szTmp);
			break;
		case sTypeZWaveClock:
		{
			int tintval = pMeter->intval2;
			int day = tintval / (24 * 60); tintval -= (day * 24 * 60);
			int hour = tintval / (60); tintval -= (hour * 60);
			int minute = tintval;
			WriteMessage("subtype       = Thermostat Clock");
			sprintf(szTmp, "Clock = %s %02d:%02d", ZWave_Clock_Days(day), hour, minute);
			WriteMessage(szTmp);
		}
		break;
		case sTypeZWaveThermostatMode:
			WriteMessage("subtype       = Thermostat Mode");
			//sprintf(szTmp, "Mode = %d (%s)", pMeter->intval2, ZWave_Thermostat_Modes[pMeter->intval2]);
			sprintf(szTmp, "Mode = %d", pMeter->intval2);
			WriteMessage(szTmp);
			break;
		case sTypeZWaveThermostatFanMode:
			WriteMessage("subtype       = Thermostat Fan Mode");
			sprintf(szTmp, "Mode = %d (%s)", pMeter->intval2, ZWave_Thermostat_Fan_Modes[pMeter->intval2]);
			WriteMessage(szTmp);
			break;
		case sTypePercentage:
			WriteMessage("subtype       = Percentage");
			sprintf(szTmp, "Percentage = %.2f", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeWaterflow:
			WriteMessage("subtype       = Waterflow");
			sprintf(szTmp, "l/min = %.2f", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeKwh:
			WriteMessage("subtype       = kWh");
			sprintf(szTmp, "Instant = %.3f", pMeter->floatval1);
			WriteMessage(szTmp);
			sprintf(szTmp, "Counter = %.3f", pMeter->floatval2 / 1000.0f);
			WriteMessage(szTmp);
			break;
		case sTypeAlert:
			WriteMessage("subtype       = Alert");
			sprintf(szTmp, "Alert = %d", pMeter->intval1);
			WriteMessage(szTmp);
			break;
		case sTypeCustom:
			WriteMessage("subtype       = Custom Sensor");
			sprintf(szTmp, "Value = %g", pMeter->floatval1);
			WriteMessage(szTmp);
			break;
		case sTypeZWaveAlarm:
			WriteMessage("subtype       = Alarm");
			sprintf(szTmp, "Level = %d (0x%02X)", pMeter->intval2, pMeter->intval2);
			WriteMessage(szTmp);
			break;
		case sTypeTextStatus:
			WriteMessage("subtype       = Text");
			sprintf(szTmp, "Text = %s", pMeter->text);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pMeter->type, pMeter->subtype);
			WriteMessage(szTmp);
			break;
		}
		WriteMessageEnd();
	}
	procResult.DeviceRowIdx = DevRowIdx;
}

void MainWorker::decode_GeneralSwitch(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[200];
	const _tGeneralSwitch *pSwitch = reinterpret_cast<const _tGeneralSwitch*>(pResponse);
	unsigned char devType = pSwitch->type;
	unsigned char subType = pSwitch->subtype;

	sprintf(szTmp, "%08X", pSwitch->id);
	std::string ID = szTmp;
	unsigned char Unit = pSwitch->unitcode;
	unsigned char cmnd = pSwitch->cmnd;
	unsigned char level = pSwitch->level;
	unsigned char SignalLevel = pSwitch->rssi;

	sprintf(szTmp, "%d", level);
	uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, -1, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	unsigned char check_cmnd = cmnd;
	if ((cmnd == gswitch_sGroupOff) || (cmnd == gswitch_sGroupOn))
		check_cmnd = (cmnd == gswitch_sGroupOff) ? gswitch_sOff : gswitch_sOn;
	CheckSceneCode(DevRowIdx, devType, subType, check_cmnd, szTmp);

	if ((cmnd == gswitch_sGroupOff) || (cmnd == gswitch_sGroupOn))
	{
		//set the status of all lights with the same code to on/off
		m_sql.GeneralSwitchGroupCmd(ID, subType, (cmnd == gswitch_sGroupOff) ? gswitch_sOff : gswitch_sOn);
	}

	procResult.DeviceRowIdx = DevRowIdx;
}

//BBQ sensor has two temperature sensors, add them as two temperature devices
void MainWorker::decode_BBQ(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	unsigned char devType = pTypeBBQ;
	unsigned char subType = pResponse->BBQ.subtype;

	sprintf(szTmp, "%d", 1);//(pResponse->BBQ.id1 * 256) + pResponse->BBQ.id2); //this because every time you turn the device on, you get a new ID
	std::string ID = szTmp;

	unsigned char Unit = pResponse->BBQ.id2;

	unsigned char cmnd = 0;
	unsigned char SignalLevel = pResponse->BBQ.rssi;
	unsigned char BatteryLevel = 0;
	if ((pResponse->BBQ.battery_level & 0x0F) == 0)
		BatteryLevel = 0;
	else
		BatteryLevel = 100;

	uint64_t DevRowIdx = 0;

	float temp1, temp2;
	temp1 = float((pResponse->BBQ.sensor1h * 256) + pResponse->BBQ.sensor1l);// / 10.0f;
	temp2 = float((pResponse->BBQ.sensor2h * 256) + pResponse->BBQ.sensor2l);// / 10.0f;

	sprintf(szTmp, "%.0f;%.0f", temp1, temp2);
	DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	if (m_verboselevel >= EVBL_ALL)
	{
		WriteMessageStart();
		switch (pResponse->BBQ.subtype)
		{
		case sTypeBBQ1:
			WriteMessage("subtype       = Maverick ET-732");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->BBQ.packettype, pResponse->BBQ.subtype);
			WriteMessage(szTmp);
			break;
		}

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->BBQ.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "ID            = %d", (pResponse->BBQ.id1 * 256) + pResponse->BBQ.id2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Sensor1 Temp  = %.1f C", temp1);
		WriteMessage(szTmp);
		sprintf(szTmp, "Sensor2 Temp  = %.1f C", temp2);
		WriteMessage(szTmp);

		sprintf(szTmp, "Signal level  = %d", pResponse->BBQ.rssi);
		WriteMessage(szTmp);

		if ((pResponse->BBQ.battery_level & 0x0F) == 0)
			WriteMessage("Battery       = Low");
		else
			WriteMessage("Battery       = OK");
		WriteMessageEnd();
	}

	//Send the two sensors

	//Temp
	RBUF tsen;
	memset(&tsen, 0, sizeof(RBUF));
	tsen.TEMP.packetlength = sizeof(tsen.TEMP) - 1;
	tsen.TEMP.packettype = pTypeTEMP;
	tsen.TEMP.subtype = sTypeTEMP6;
	tsen.TEMP.battery_level = 9;
	tsen.TEMP.rssi = SignalLevel;
	tsen.TEMP.id1 = 0;
	tsen.TEMP.id2 = 1;

	tsen.TEMP.tempsign = (temp1 >= 0) ? 0 : 1;
	int at10 = round(std::abs(temp1*10.0f));
	tsen.TEMP.temperatureh = (BYTE)(at10 / 256);
	at10 -= (tsen.TEMP.temperatureh * 256);
	tsen.TEMP.temperaturel = (BYTE)(at10);
	_tRxMessageProcessingResult tmpProcResult1;
	tmpProcResult1.DeviceName = "";
	tmpProcResult1.DeviceRowIdx = -1;
	decode_Temp(HwdID, HwdType, (const tRBUF*)&tsen.TEMP, tmpProcResult1);

	tsen.TEMP.id1 = 0;
	tsen.TEMP.id2 = 2;

	tsen.TEMP.tempsign = (temp2 >= 0) ? 0 : 1;
	at10 = round(std::abs(temp2*10.0f));
	tsen.TEMP.temperatureh = (BYTE)(at10 / 256);
	at10 -= (tsen.TEMP.temperatureh * 256);
	tsen.TEMP.temperaturel = (BYTE)(at10);
	_tRxMessageProcessingResult tmpProcResult2;
	tmpProcResult2.DeviceName = "";
	tmpProcResult2.DeviceRowIdx = -1;
	decode_Temp(HwdID, HwdType, (const tRBUF*)&tsen.TEMP, tmpProcResult2);

	procResult.DeviceRowIdx = DevRowIdx;
}

//not in dbase yet
void MainWorker::decode_FS20(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	//unsigned char devType=pTypeFS20;

	char szTmp[100];

	switch (pResponse->FS20.subtype)
	{
	case sTypeFS20:
		WriteMessage("subtype       = FS20");
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->FS20.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "House code    = %02X%02X", pResponse->FS20.hc1, pResponse->FS20.hc2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Address       = %02X", pResponse->FS20.addr);
		WriteMessage(szTmp);

		WriteMessage("Cmd1          = ", false);

		switch (pResponse->FS20.cmd1 & 0x1F)
		{
		case 0x0:
			WriteMessage("Off");
			break;
		case 0x1:
			WriteMessage("dim level 1 = 6.25%");
			break;
		case 0x2:
			WriteMessage("dim level 2 = 12.5%");
			break;
		case 0x3:
			WriteMessage("dim level 3 = 18.75%");
			break;
		case 0x4:
			WriteMessage("dim level 4 = 25%");
			break;
		case 0x5:
			WriteMessage("dim level 5 = 31.25%");
			break;
		case 0x6:
			WriteMessage("dim level 6 = 37.5%");
			break;
		case 0x7:
			WriteMessage("dim level 7 = 43.75%");
			break;
		case 0x8:
			WriteMessage("dim level 8 = 50%");
			break;
		case 0x9:
			WriteMessage("dim level 9 = 56.25%");
			break;
		case 0xA:
			WriteMessage("dim level 10 = 62.5%");
			break;
		case 0xB:
			WriteMessage("dim level 11 = 68.75%");
			break;
		case 0xC:
			WriteMessage("dim level 12 = 75%");
			break;
		case 0xD:
			WriteMessage("dim level 13 = 81.25%");
			break;
		case 0xE:
			WriteMessage("dim level 14 = 87.5%");
			break;
		case 0xF:
			WriteMessage("dim level 15 = 93.75%");
			break;
		case 0x10:
			WriteMessage("On (100%)");
			break;
		case 0x11:
			WriteMessage("On ( at last dim level set)");
			break;
		case 0x12:
			WriteMessage("Toggle between Off and On (last dim level set)");
			break;
		case 0x13:
			WriteMessage("Bright one step");
			break;
		case 0x14:
			WriteMessage("Dim one step");
			break;
		case 0x15:
			WriteMessage("Start dim cycle");
			break;
		case 0x16:
			WriteMessage("Program(Timer)");
			break;
		case 0x17:
			WriteMessage("Request status from a bidirectional device");
			break;
		case 0x18:
			WriteMessage("Off for timer period");
			break;
		case 0x19:
			WriteMessage("On (100%) for timer period");
			break;
		case 0x1A:
			WriteMessage("On ( at last dim level set) for timer period");
			break;
		case 0x1B:
			WriteMessage("Reset");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown command = %02X", pResponse->FS20.cmd1);
			WriteMessage(szTmp);
			break;
		}

		if ((pResponse->FS20.cmd1 & 0x80) == 0)
			WriteMessage("                command to receiver");
		else
			WriteMessage("                response from receiver");

		if ((pResponse->FS20.cmd1 & 0x40) == 0)
			WriteMessage("                unidirectional command");
		else
			WriteMessage("                bidirectional command");

		if ((pResponse->FS20.cmd1 & 0x20) == 0)
			WriteMessage("                additional cmd2 byte not present");
		else
			WriteMessage("                additional cmd2 byte present");

		if ((pResponse->FS20.cmd1 & 0x20) != 0)
		{
			sprintf(szTmp, "Cmd2          = %02X", pResponse->FS20.cmd2);
			WriteMessage(szTmp);
		}
		break;
	case sTypeFHT8V:
		WriteMessage("subtype       = FHT 8V valve");

		sprintf(szTmp, "Sequence nbr  = %d", pResponse->FS20.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "House code    = %02X%02X", pResponse->FS20.hc1, pResponse->FS20.hc2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Address       = %02X", pResponse->FS20.addr);
		WriteMessage(szTmp);

		WriteMessage("Cmd1          = ", false);

		if ((pResponse->FS20.cmd1 & 0x80) == 0)
			WriteMessage("new command");
		else
			WriteMessage("repeated command");

		if ((pResponse->FS20.cmd1 & 0x40) == 0)
			WriteMessage("                unidirectional command");
		else
			WriteMessage("                bidirectional command");

		if ((pResponse->FS20.cmd1 & 0x20) == 0)
			WriteMessage("                additional cmd2 byte not present");
		else
			WriteMessage("                additional cmd2 byte present");

		if ((pResponse->FS20.cmd1 & 0x10) == 0)
			WriteMessage("                battery empty beep not enabled");
		else
			WriteMessage("                enable battery empty beep");

		switch (pResponse->FS20.cmd1 & 0xF)
		{
		case 0x0:
			WriteMessage("                Synchronize now");
			sprintf(szTmp, "Cmd2          = valve position: %02X is %.2f %%", pResponse->FS20.cmd2, float(pResponse->FS20.cmd2) / 2.55f);
			WriteMessage(szTmp);
			break;
		case 0x1:
			WriteMessage("                open valve");
			break;
		case 0x2:
			WriteMessage("                close valve");
			break;
		case 0x6:
			WriteMessage("                open valve at percentage level");
			sprintf(szTmp, "Cmd2          = valve position: %02X is %.2f %%", pResponse->FS20.cmd2, float(pResponse->FS20.cmd2) / 2.55f);
			WriteMessage(szTmp);
			break;
		case 0x8:
			WriteMessage("                relative offset (cmd2 bit 7=direction, bit 5-0 offset value)");
			break;
		case 0xA:
			WriteMessage("                decalcification cycle");
			sprintf(szTmp, "Cmd2          = valve position: %02X is %.2f %%", pResponse->FS20.cmd2, float(pResponse->FS20.cmd2) / 2.55f);
			WriteMessage(szTmp);
			break;
		case 0xC:
			WriteMessage("                synchronization active");
			sprintf(szTmp, "Cmd2          = count down is %d seconds", pResponse->FS20.cmd2 >> 1);
			WriteMessage(szTmp);
			break;
		case 0xE:
			WriteMessage("                test, drive valve and produce an audible signal");
			break;
		case 0xF:
			WriteMessage("                pair valve (cmd2 bit 7-1 is count down in seconds, bit 0=1)");
			sprintf(szTmp, "Cmd2          = count down is %d seconds", pResponse->FS20.cmd2 >> 1);
			WriteMessage(szTmp);
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown command = %02X", pResponse->FS20.cmd1);
			WriteMessage(szTmp);
			break;
		}
		break;
	case sTypeFHT80:
		WriteMessage("subtype       = FHT80 door/window sensor");
		sprintf(szTmp, "Sequence nbr  = %d", pResponse->FS20.seqnbr);
		WriteMessage(szTmp);
		sprintf(szTmp, "House code    = %02X%02X", pResponse->FS20.hc1, pResponse->FS20.hc2);
		WriteMessage(szTmp);
		sprintf(szTmp, "Address       = %02X", pResponse->FS20.addr);
		WriteMessage(szTmp);

		WriteMessage("Cmd1          = ", false);

		switch (pResponse->FS20.cmd1 & 0xF)
		{
		case 0x1:
			WriteMessage("sensor opened");
			break;
		case 0x2:
			WriteMessage("sensor closed");
			break;
		case 0xC:
			WriteMessage("synchronization active");
			break;
		default:
			sprintf(szTmp, "ERROR: Unknown command = %02X", pResponse->FS20.cmd1);
			WriteMessage(szTmp);
			break;
		}

		if ((pResponse->FS20.cmd1 & 0x80) == 0)
			WriteMessage("                new command");
		else
			WriteMessage("                repeated command");
		break;
	default:
		sprintf(szTmp, "ERROR: Unknown Sub type for Packet type= %02X:%02X", pResponse->FS20.packettype, pResponse->FS20.subtype);
		WriteMessage(szTmp);
		break;
	}

	sprintf(szTmp, "Signal level  = %d", pResponse->FS20.rssi);
	WriteMessage(szTmp);
	procResult.DeviceRowIdx = -1;
}

void MainWorker::decode_Cartelectronic(const int HwdID, const _eHardwareTypes HwdType, const tRBUF *pResponse, _tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	std::string ID;

	sprintf(szTmp, "%llu", ((unsigned long long)(pResponse->TIC.id1) << 32) + (pResponse->TIC.id2 << 24) + (pResponse->TIC.id3 << 16) + (pResponse->TIC.id4 << 8) + (pResponse->TIC.id5));
	ID = szTmp;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;

	unsigned char subType = pResponse->TIC.subtype;

	switch (subType)
	{
	case sTypeTIC:
		decode_CartelectronicTIC(HwdID, pResponse, procResult);
		WriteMessage("Cartelectronic TIC received");
		break;
	case sTypeCEencoder:
		decode_CartelectronicEncoder(HwdID, pResponse, procResult);
		WriteMessage("Cartelectronic Encoder received");
		break;
	default:
		WriteMessage("Cartelectronic protocol not supported");
		break;
	}
}

void MainWorker::decode_CartelectronicTIC(const int HwdID,
	const tRBUF *pResponse,
	_tRxMessageProcessingResult & procResult)
{
	//Contract Options
	typedef enum {
		OP_NOT_DEFINED,
		OP_BASE,
		OP_CREUSE,
		OP_EJP,
		OP_TEMPO
	} Contract;

	//Running time
	typedef enum {
		PER_NOT_DEFINED,
		PER_ALL_HOURS,             //TH..
		PER_LOWCOST_HOURS,         //HC..
		PER_HIGHCOST_HOURS,        //HP..
		PER_NORMAL_HOURS,          //HN..
		PER_MOBILE_PEAK_HOURS,     //PM..
		PER_BLUE_LOWCOST_HOURS,    //HCJB
		PER_WHITE_LOWCOST_HOURS,   //HCJW
		PER_RED_LOWCOST_HOURS,     //HCJR
		PER_BLUE_HIGHCOST_HOURS,   //HPJB
		PER_WHITE_HIGHCOST_HOURS,  //HPJW
		PER_RED_HIGHCOST_HOURS     //HPJR
	} PeriodTime;

	char szTmp[100];
	uint32_t counter1 = 0;
	uint32_t counter2 = 0;
	unsigned int apparentPower = 0;
	unsigned int counter1ApparentPower = 0;
	unsigned int counter2ApparentPower = 0;
	unsigned char unitCounter1 = 0;
	unsigned char unitCounter2 = 1;
	unsigned char cmnd = 0;
	std::string ID;

	unsigned char devType = pTypeGeneral;
	unsigned char subType = sTypeKwh;
	unsigned char SignalLevel = pResponse->TIC.rssi;

	unsigned char BatteryLevel = (pResponse->TIC.battery_level + 1) * 10;

	// If no error
	if ((pResponse->TIC.state & 0x04) == 0)
	{
		// Id of the counter
		uint64_t uint64ID = ((uint64_t)pResponse->TIC.id1 << 32) +
			((uint64_t)pResponse->TIC.id2 << 24) +
			((uint64_t)pResponse->TIC.id3 << 16) +
			((uint64_t)pResponse->TIC.id4 << 8) +
			(uint64_t)(pResponse->TIC.id5);

		sprintf(szTmp, "%.12" PRIu64, uint64ID);
		ID = szTmp;

		// Contract Type
		Contract contractType = (Contract)(pResponse->TIC.contract_type >> 4);

		// Period
		PeriodTime period = (PeriodTime)(pResponse->TIC.contract_type & 0x0f);

		// Apparent Power
		if ((pResponse->TIC.state & 0x02) != 0) // Only if this one is present
		{
			apparentPower = (pResponse->TIC.power_H << 8) + pResponse->TIC.power_L;

			sprintf(szTmp, "%u", apparentPower);
			uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 1, pTypeUsage, sTypeElectric, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);

			if (DevRowIdx == -1)
				return;

			m_notifications.CheckAndHandleNotification(DevRowIdx, procResult.DeviceName, pTypeUsage, sTypeElectric, NTYPE_ENERGYINSTANT, (const float)apparentPower);
		}

		switch (contractType)
		{
			// BASE CONTRACT
		case OP_BASE:
		{
			unitCounter1 = 0;
			counter1ApparentPower = apparentPower;
		}
		break;

		// LOW/HIGH PERIOD
		case OP_CREUSE:
		{
			unitCounter1 = 0;

			if (period == PER_LOWCOST_HOURS)
			{
				counter1ApparentPower = apparentPower;
				counter2ApparentPower = 0;
			}
			if (period == PER_HIGHCOST_HOURS)
			{
				counter1ApparentPower = 0;
				counter2ApparentPower = apparentPower;
			}

			// Counter 2
			counter2 = (pResponse->TIC.counter2_0 << 24) + (pResponse->TIC.counter2_1 << 16) + (pResponse->TIC.counter2_2 << 8) + (pResponse->TIC.counter2_3);
			sprintf(szTmp, "%u;%d", counter2ApparentPower, counter2);

			uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 1, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);

			if (DevRowIdx == -1)
				return;

			m_notifications.CheckAndHandleNotification(DevRowIdx, procResult.DeviceName, devType, subType, NTYPE_TODAYENERGY, (float)counter2);
			//----------------------------
		}
		break;

		// EJP
		case OP_EJP:
		{
			unitCounter1 = 0;
			// Counter 2
			counter2 = (pResponse->TIC.counter2_0 << 24) + (pResponse->TIC.counter2_1 << 16) + (pResponse->TIC.counter2_2 << 8) + (pResponse->TIC.counter2_3);

			sprintf(szTmp, "%u;%d", counter2ApparentPower, counter2);
			uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 1, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);

			if (DevRowIdx == -1)
				return;

			m_notifications.CheckAndHandleNotification(DevRowIdx, procResult.DeviceName, devType, subType, NTYPE_TODAYENERGY, (float)counter2);
			//----------------------------
		}
		break;

		// TEMPO Contracts
		// Total of 6 counters. Theses counters depends of the time period received. For each period time, only 2 counters are sended.
		case OP_TEMPO:
		{
			switch (period)
			{
			case PER_BLUE_LOWCOST_HOURS:
				counter1ApparentPower = apparentPower;
				counter2ApparentPower = 0;
				unitCounter1 = 0;
				unitCounter2 = 1;
				break;
			case PER_BLUE_HIGHCOST_HOURS:
				counter1ApparentPower = 0;
				counter2ApparentPower = apparentPower;
				unitCounter1 = 0;
				unitCounter2 = 1;
				break;
			case PER_WHITE_LOWCOST_HOURS:
				counter1ApparentPower = apparentPower;
				counter2ApparentPower = 0;
				unitCounter1 = 2;
				unitCounter2 = 3;
				break;
			case PER_WHITE_HIGHCOST_HOURS:
				counter1ApparentPower = 0;
				counter2ApparentPower = apparentPower;
				unitCounter1 = 2;
				unitCounter2 = 3;
				break;
			case PER_RED_LOWCOST_HOURS:
				counter1ApparentPower = apparentPower;
				counter2ApparentPower = 0;
				unitCounter1 = 4;
				unitCounter2 = 5;
				break;
			case PER_RED_HIGHCOST_HOURS:
				counter1ApparentPower = 0;
				counter2ApparentPower = apparentPower;
				unitCounter1 = 4;
				unitCounter2 = 5;
				break;
			default:
				break;
			}

			// Counter 2
			counter2 = (pResponse->TIC.counter2_0 << 24) + (pResponse->TIC.counter2_1 << 16) + (pResponse->TIC.counter2_2 << 8) + (pResponse->TIC.counter2_3);

			sprintf(szTmp, "%u;%d", counter2ApparentPower, counter2);
			uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), unitCounter2, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);

			if (DevRowIdx == -1)
				return;

			m_notifications.CheckAndHandleNotification(DevRowIdx, procResult.DeviceName, devType, subType, NTYPE_TODAYENERGY, (float)counter2);
			//----------------------------
		}
		break;
		default:
			break;
		}

		// Counter 1
		counter1 = (pResponse->TIC.counter1_0 << 24) + (pResponse->TIC.counter1_1 << 16) + (pResponse->TIC.counter1_2 << 8) + (pResponse->TIC.counter1_3);

		sprintf(szTmp, "%u;%d", counter1ApparentPower, counter1);
		uint64_t DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), unitCounter1, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);

		if (DevRowIdx == -1)
			return;

		m_notifications.CheckAndHandleNotification(DevRowIdx, procResult.DeviceName, devType, subType, NTYPE_TODAYENERGY, (float)counter1);
		//----------------------------
	}
	else
	{
		WriteMessage("TeleInfo not connected");
	}
}

void MainWorker::decode_CartelectronicEncoder(const int HwdID,
	const tRBUF *pResponse,
	_tRxMessageProcessingResult & procResult)
{
	char szTmp[100];
	uint32_t counter1 = 0;
	uint32_t counter2 = 0;
	int apparentPower = 0;
	unsigned char Unit = 0;
	unsigned char cmnd = 0;
	uint64_t DevRowIdx = 0;
	std::string ID;

	unsigned char devType = pTypeRFXMeter;
	unsigned char subType = sTypeRFXMeterCount;
	unsigned char SignalLevel = pResponse->CEENCODER.rssi;

	unsigned char BatteryLevel = (pResponse->CEENCODER.battery_level + 1) * 10;

	// Id of the module
	sprintf(szTmp, "%d", ((uint32_t)(pResponse->CEENCODER.id1 << 24) + (pResponse->CEENCODER.id2 << 16) + (pResponse->CEENCODER.id3 << 8) + pResponse->CEENCODER.id4));
	ID = szTmp;

	// Counter 1
	counter1 = (pResponse->CEENCODER.counter1_0 << 24) + (pResponse->CEENCODER.counter1_1 << 16) + (pResponse->CEENCODER.counter1_2 << 8) + (pResponse->CEENCODER.counter1_3);

	sprintf(szTmp, "%d", counter1);
	DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), Unit, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, Unit, devType, subType, (float)counter1);

	// Counter 2
	counter2 = (pResponse->CEENCODER.counter2_0 << 24) + (pResponse->CEENCODER.counter2_1 << 16) + (pResponse->CEENCODER.counter2_2 << 8) + (pResponse->CEENCODER.counter2_3);

	sprintf(szTmp, "%d", counter2);
	DevRowIdx = m_sql.UpdateValue(HwdID, ID.c_str(), 1, devType, subType, SignalLevel, BatteryLevel, cmnd, szTmp, procResult.DeviceName);
	if (DevRowIdx == -1)
		return;
	m_notifications.CheckAndHandleNotification(DevRowIdx, HwdID, ID, procResult.DeviceName, 1, devType, subType, (float)counter2);
}