#include "stdafx.h"
#include "NotifyObserver.h"
#include "NotifySystem.h"
#include <iostream>

const CNotifyObserver::_tNotifyTypeTable CNotifyObserver::typeTable[] =
{
	{ NOTIFY_LOG,             "log"             },
	{ NOTIFY_DZ_START,        "domoticzStart"   },
	{ NOTIFY_DZ_STOP,         "domoticzStop"    },
	{ NOTIFY_BACKUP_BEGIN,    "backupBegin"	    },
	{ NOTIFY_BACKUP_END,      "backupEnd"       },
	{ NOTIFY_HW_TIMEOUT,      "hardwareTimeout" },
	{ NOTIFY_HW_START,        "hardwareStart"   },
	{ NOTIFY_HW_STOP,         "hardwareStop"    },
	{ NOTIFY_NOTIFICATION,    "notification"    },
	{ NOTIFY_THREAD_ENDED,    "threadEnded"     },
};

const CNotifyObserver::_tNotifyStatusTable CNotifyObserver::statusTable[] =
{
	{ NOTIFY_OK,              "ok"              },
	{ NOTIFY_INFO,            "info"            },
	{ NOTIFY_ERROR,           "error"           },
	{ NOTIFY_WARNING,         "warning"         }
};

CNotifyObserver::CNotifyObserver(void)
{
	_notify.Register(this);
}

CNotifyObserver::~CNotifyObserver(void)
{
     _notify.Unregister(this);
}

std::string CNotifyObserver::NotifyGetTypeString(const int type)
{
	for (uint8_t i = 0; i < sizeof(typeTable) / sizeof(typeTable[0]); i++)
	{
		if (typeTable[i].type == static_cast<_eNotifyType>(type))
			return typeTable[i].name;
	}
	return "";
}

std::string CNotifyObserver::NotifyGetStatusString(const int status)
{
	for (uint8_t i = 0; i < sizeof(statusTable) / sizeof(statusTable[0]); i++)
	{
		if (statusTable[i].status == static_cast<_eNotifyStatus>(status))
			return statusTable[i].name;
	}
	return "";
}