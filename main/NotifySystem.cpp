#include "stdafx.h"
#include "NotifySystem.h"
#include "Logger.h"

const CNotifySystem::_tNotifyTypeTable CNotifySystem::typeTable[] =
{ // don't change order
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
	{ NOTIFY_SWITCHCMD,       "switchCmd"       }
};

const CNotifySystem::_tNotifyStatusTable CNotifySystem::statusTable[] =
{ // don't change order
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
	if (m_pQueueThread)
	{
		m_stoprequested = true;
		_tNotifyQueue item;
		item.trigger = NULL;
		m_notifyqueue.push(item);
		m_pQueueThread->join();
	}
}

void CNotifySystem::Start()
{
	m_stoprequested = false;
	if (!m_pQueueThread)
		m_pQueueThread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CNotifySystem::QueueThread, this)));
}

std::string const CNotifySystem::GetTypeString(const int type)
{
	if (type <= 255) // constants defined in _eNotifyType
	{
		if (type < sizeof(typeTable) / sizeof(typeTable[0]))
			return typeTable[type].name;
	}
	else
	{
		uint8_t shiftType = (type >> 8) - 1; // shift back to get correct value from custom type vector
		if (shiftType < m_customTypes.size())
			return m_customTypes[shiftType];
	}
	return "unknown";
}

std::string const CNotifySystem::GetStatusString(const int status)
{
	if (status < sizeof(statusTable) / sizeof(statusTable[0]))
			return statusTable[status].name;
	return "unknown";
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

void CNotifySystem::Notify(const std::string &type, const uint64_t id)
{
	bool found = false;
	uint16_t i = 0;
	for (; i < m_customTypes.size(); i++)
	{
		if (m_customTypes[i] == type)
		{
			found = true;
			break;
		}
	}
	if (!found && m_customTypes.size() < 255)
		m_customTypes.push_back(type);

	Notify(static_cast<_eNotifyType>(++i << 8), NOTIFY_INFO, id, "");  // first byte reserved for constant types
}

void CNotifySystem::Notify(const _eNotifyType type)
{
	Notify(type, NOTIFY_INFO, 0, "");
}
void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status)
{
	Notify(type, status, 0, "");
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	Notify(type, status, 0, message);
}

void CNotifySystem::Notify(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message)
{
	_tNotifyQueue item;
	item.id = id;
	item.type = type;
	item.status = status;
	item.message = message;
	m_notifyqueue.push(item);
}

bool CNotifySystem::NotifyWait(const _eNotifyType type)
{
	return NotifyWait(type, NOTIFY_INFO, 0, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status)
{
	return NotifyWait(type, status, 0, "");
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const std::string &message)
{
	return NotifyWait(type, status, 0, message);
}

bool CNotifySystem::NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message)
{
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
