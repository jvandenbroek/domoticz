#pragma once
#include "concurrent_queue.h"
#include "NotifyObserver.h"

class CNotifySystem
{
public:
	CNotifySystem(void);
	~CNotifySystem(void);
	void Notify(const std::string &type, const uint64_t id);
	void Notify(const NOTIFY::_eType type);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const std::string &message);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const char *message);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const void *genericPtr);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const uint64_t id, const std::string &message);
	void Notify(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const uint64_t id, const std::string &message, const void *genericPtr);
	bool NotifyWait(const NOTIFY::_eType type);
	bool NotifyWait(const NOTIFY::_eType type, const NOTIFY::_eStatus status);
	bool NotifyWait(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const std::string &message);
	bool NotifyWait(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const char *message);
	bool NotifyWait(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const void *genericPtr);
	bool NotifyWait(const NOTIFY::_eType type, const NOTIFY::_eStatus status, const uint64_t id, const std::string &message, const void *genericPtr);
	bool Register(CNotifyObserver* pHardware);
	bool Unregister(CNotifyObserver* pHardware);
	std::string const GetTypeString(const int type);
	std::string const GetStatusString(const int status);
	void Start();

private:
	struct _tNotifyQueue
	{
		uint64_t id;
		NOTIFY::_eType type;
		NOTIFY::_eStatus status;
		std::string message;
		const void *genericPtr;
		queue_element_trigger* trigger;
	};
	struct _tNotifyTypeTable
	{
		NOTIFY::_eType type;
		std::string name;
	};
	struct _tNotifyStatusTable
	{
		NOTIFY::_eStatus status;
		std::string name;
	};

	void QueueThread();

	volatile bool m_stoprequested;
	boost::mutex m_mutex;
	std::vector<CNotifyObserver*> m_notify;
	concurrent_queue<_tNotifyQueue> m_notifyqueue;
	boost::shared_ptr<boost::thread> m_pQueueThread;

	std::vector<std::string> m_customTypes;

	static const _tNotifyTypeTable typeTable[];
	static const _tNotifyStatusTable statusTable[];
};

extern CNotifySystem _notify;
