#include "stdafx.h"
#include "NotifySystem.h"
#include "Logger.h"

const CNotifySystem::_tNotifyTypeTable CNotifySystem::typeTable[] =
{
	{ NOTIFY, 				"notify"		},
	{ NOTIFY_STARTUP,		"startup"		},
	{ NOTIFY_SHUTDOWN, 		"shutdown"		},
	{ NOTIFY_NOTIFICATION,	"notification"	},
	{ NOTIFY_BACKUP_START,	"backupStart"	},
	{ NOTIFY_BACKUP_END,	"backupEnd"		},
	{ NOTIFY_TIMEOUT,		"timeout"		},
	{ NOTIFY_ENDED,			"ended"			},
	{ NOTIFY_DISKFULL,		"diskfull"		},
	{ NOTIFY_WHATEVER,		"whatever"		}
};

const CNotifySystem::_tNotifyStatusTable CNotifySystem::statusTable[] =
{
	{ NOTIFY_ERROR, 		"error"			},
	{ NOTIFY_INFO,			"info"			},
	{ NOTIFY_NORM, 			"normal"		},
	{ NOTIFY_TRACE,			"trace"			}
};

CNotifySystem::CNotifySystem(void)
{
}

CNotifySystem::~CNotifySystem(void)
{
}

void CNotifySystem::Start()
{
	m_stoprequested = false;
	if (!m_pQueueThread)
		m_pQueueThread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CNotifySystem::QueueThread, this)));
	_log.Log(LOG_STATUS, "NotifySystem: Started");
}

void CNotifySystem::Stop()
{
	if (m_pQueueThread)
	{
		m_stoprequested = true;
		_tNotifyQueue item;
		item.trigger = NULL;
		m_notifyqueue.push(item);
		m_pQueueThread->join();
	}
	_log.Log(LOG_STATUS, "NotifySystem: Stopped...");
}

void CNotifySystem::SetEnabled(const bool bEnabled)
{
	m_bEnabled = bEnabled;
	bEnabled ? Start() : Stop();
}

std::string CNotifySystem::GetTypeString(const int type)
{
	for (uint8_t i = 0; i < sizeof(typeTable) / sizeof(typeTable[0]); i++)
	{
		if (typeTable[i].type == static_cast<_eNotifyType>(type))
			return typeTable[i].name;
	}
	return "";
}

std::string CNotifySystem::GetStatusString(const int status)
{
	for (uint8_t i = 0; i < sizeof(statusTable) / sizeof(statusTable[0]); i++)
	{
		if (statusTable[i].status == static_cast<_eNotifyStatus>(status))
			return statusTable[i].name;
	}
	return "";
}

void CNotifySystem::QueueThread()
{
	_tNotifyQueue item;

	while (!m_stoprequested)
	{
		bool hasPopped = m_notifyqueue.timed_wait_and_pop<boost::posix_time::milliseconds>(item, boost::posix_time::milliseconds(5000));

		if (!hasPopped)
			continue;

		if (m_stoprequested)
			break;

		boost::unique_lock<boost::mutex> lock(m_mutex);
		for (size_t i = 0; i < m_notify.size(); i++)
			m_notify[i]->NotifyReceiver(item.type, item.status, item.message);
	}
}

void CNotifySystem::Notify(const _eNotifyType type)
{
	if (!m_bEnabled)
		return;
	Notify(type, NOTIFY_NORM, "");
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status)
{
	if (!m_bEnabled)
		return;
	Notify(type, status, "");
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	if (!m_bEnabled)
		return;

	_tNotifyQueue item;
	item.type = type;
	item.status = status;
	item.message = message;
	m_notifyqueue.push(item);
}

bool CNotifySystem::NotifyWait(const _eNotifyType type)
{
	if (!m_bEnabled)
		return false;

	return NotifyWait(type, NOTIFY_NORM, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status)
{
	if (!m_bEnabled)
		return false;

	return NotifyWait(type, status, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	if (!m_bEnabled)
		return false;
	bool response = false;
	boost::unique_lock<boost::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_notify.size(); i++)
		response |= m_notify[i]->NotifyReceiver(type, status, message);
	return response;
}

bool CNotifySystem::Register(CNotifyObserver* pHardware)
{
	if (!m_bEnabled || pHardware == NULL)
		return false;

	boost::unique_lock<boost::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_notify.size(); i++)
	{
		if (m_notify[i] == pHardware)
			return false;
	}
	m_notify.push_back(pHardware);
	return true;
}

bool CNotifySystem::Unregister(CNotifyObserver* pHardware)
{
	if (!m_bEnabled || pHardware == NULL)
		return false;

	if (m_notify.size() > 0)
	{
		boost::unique_lock<boost::mutex> lock(m_mutex);
		for (size_t i = 0; i < m_notify.size(); i++)
		{
			if (m_notify[i] == pHardware)
			{
				m_notify.erase(m_notify.begin() + i);
				return true;
			}
		}
	}
	return false;
}
