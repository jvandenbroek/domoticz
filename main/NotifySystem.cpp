#include "stdafx.h"
#include "NotifySystem.h"
#include "Logger.h"

const CNotifySystem::_tNotifyTypeTable CNotifySystem::typeTable[] =
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

const CNotifySystem::_tNotifyStatusTable CNotifySystem::statusTable[] =
{
	{ NOTIFY_OK,              "ok"              },
	{ NOTIFY_INFO,            "info"            },
	{ NOTIFY_ERROR,           "error"           },
	{ NOTIFY_WARNING,         "warning"         }
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


std::string const CNotifySystem::GetTypeString(const int type)
{
	for (uint8_t i = 0; i < sizeof(typeTable) / sizeof(typeTable[0]); i++)
	{
		if (typeTable[i].type == static_cast<_eNotifyType>(type))
			return typeTable[i].name;
	}
	return "";
}

std::string const CNotifySystem::GetStatusString(const int status)
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
			m_notify[i]->NotifyReceiver(item.type, item.status, item.id, item.message);
	}
}

void CNotifySystem::Notify(const _eNotifyType type)
{
	if (!m_bEnabled)
		return;
	Notify(type, NOTIFY_INFO, 0, "");
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status)
{
	if (!m_bEnabled)
		return;
	Notify(type, status, 0, "");
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	if (!m_bEnabled)
		return;
	Notify(type, status, 0, message);
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message)
{
	if (!m_bEnabled)
		return;

	_tNotifyQueue item;
	item.id = id;
	item.type = type;
	item.status = status;
	item.message = message;
	m_notifyqueue.push(item);
}

bool CNotifySystem::NotifyWait(const _eNotifyType type)
{
	if (!m_bEnabled)
		return false;

	return NotifyWait(type, NOTIFY_INFO, 0, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status)
{
	if (!m_bEnabled)
		return false;

	return NotifyWait(type, status, 0, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	if (!m_bEnabled)
		return false;

	return NotifyWait(type, status, 0, message);
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message)
{
	if (!m_bEnabled)
		return false;
	bool response = false;
	boost::unique_lock<boost::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_notify.size(); i++)
		response |= m_notify[i]->NotifyReceiver(type, status, id, message);
	return response;
}

bool CNotifySystem::Register(CNotifyObserver* pHardware)
{
	if (pHardware == NULL)
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
	if (pHardware == NULL)
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
