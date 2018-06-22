#include "stdafx.h"
#include "NotifySystem.h"
#include "Logger.h"

const CNotifySystem::_tNotifyTypeTable CNotifySystem::typeTable[] =
{ // don't change order
	{ Notify::LOG,             "log"             },
	{ Notify::DZ_START,        "domoticzStart"   },
	{ Notify::DZ_STOP,         "domoticzStop"    },
	{ Notify::BACKUP_BEGIN,    "backupBegin"	    },
	{ Notify::BACKUP_END,      "backupEnd"       },
	{ Notify::HW_TIMEOUT,      "hardwareTimeout" },
	{ Notify::HW_START,        "hardwareStart"   },
	{ Notify::HW_STOP,         "hardwareStop"    },
	{ Notify::HW_THREAD_ENDED, "threadEnded"     },
	{ Notify::NOTIFICATION,    "notification"    },
	{ Notify::SWITCHCMD,       "switchCmd"       }
};

const CNotifySystem::_tNotifyStatusTable CNotifySystem::statusTable[] =
{ // don't change order
	{ Notify::OK,              "ok"              },
	{ Notify::INFO,            "info"            },
	{ Notify::ERROR,           "error"           },
	{ Notify::WARNING,         "warning"         }
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
	if (type <= 255) // constants defined in Notify::_eType
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
			m_notify[i]->NotifyReceiver(item.type, item.status, item.id, item.message, item.genericPtr);
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

	Notify(static_cast<Notify::_eType>(++i << 8), Notify::INFO, id, "", NULL);  // first byte reserved for constant types
}

void CNotifySystem::Notify(const Notify::_eType type)
{
	Notify(type, Notify::INFO, 0, "", NULL);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status)
{
	Notify(type, status, 0, "", NULL);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status, const std::string &message)
{
	Notify(type, status, 0, message, NULL);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status, const char *message)
{
	Notify(type, status, 0, message, NULL);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status, const void *genericPtr)
{
	Notify(type, status, 0, "", genericPtr);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status, const uint64_t id, const std::string &message)
{
	Notify(type, status, id, message, NULL);
}
void CNotifySystem::Notify(const Notify::_eType type, const Notify::_eStatus status, const uint64_t id, const std::string &message, const void *genericPtr)
{
	_tNotifyQueue item;
	item.id = id;
	item.type = type;
	item.status = status;
	item.message = message;
	item.genericPtr = genericPtr;
	m_notifyqueue.push(item);
}

bool CNotifySystem::NotifyWait(const Notify::_eType type)
{
	return NotifyWait(type, Notify::INFO, 0, "", NULL);
}
bool CNotifySystem::NotifyWait(const Notify::_eType type, const Notify::_eStatus status)
{
	return NotifyWait(type, status, 0, "", NULL);
}
bool CNotifySystem::NotifyWait(const Notify::_eType type, const Notify::_eStatus status, const std::string &message)
{
	return NotifyWait(type, status, 0, message, NULL);
}
bool CNotifySystem::NotifyWait(const Notify::_eType type, const Notify::_eStatus status, const char *message)
{
	return NotifyWait(type, status, 0, message, NULL);
}
bool CNotifySystem::NotifyWait(const Notify::_eType type, const Notify::_eStatus status, const void *genericPtr)
{
	return NotifyWait(type, status, 0, "", genericPtr);
}
bool CNotifySystem::NotifyWait(const Notify::_eType type, const Notify::_eStatus status, const uint64_t id, const std::string &message, const void *genericPtr)
{
	bool response = false;
	boost::unique_lock<boost::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_notify.size(); i++)
		response |= m_notify[i]->NotifyReceiver(type, status, id, message, genericPtr);
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
