#pragma once
#include "concurrent_queue.h"
#include "NotifyObserver.h"

class CNotifySystem
{
public:
	CNotifySystem(void);
	~CNotifySystem(void);
	void Notify(const std::string &type);
	void Notify(const _eNotifyType type);
	void Notify(const _eNotifyType type, const _eNotifyStatus status);
	void Notify(const _eNotifyType type, const _eNotifyStatus status, const std::string &message);
	void Notify(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message);
	bool NotifyWait(const _eNotifyType type);
	bool NotifyWait(const _eNotifyType type, const _eNotifyStatus status);
	bool NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const std::string &message);
	bool NotifyWait(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message);
	bool Register(CNotifyObserver* pHardware);
	bool Unregister(CNotifyObserver* pHardware);
	void SetEnabled(const bool bEnabled);
	std::string const GetTypeString(const int type);
	std::string const GetStatusString(const int status);

private:
	struct _tNotifyQueue
	{
		uint64_t id;
		_eNotifyType type;
		_eNotifyStatus status;
		std::string message;
		queue_element_trigger* trigger;
	};
	struct _tNotifyTypeTable
	{
		_eNotifyType type;
		std::string name;
	};
	struct _tNotifyStatusTable
	{
		_eNotifyStatus status;
		std::string name;
	};

	void Start();
	void Stop();
	void QueueThread();

	volatile bool m_stoprequested;
	boost::mutex m_mutex;
	std::vector<CNotifyObserver*> m_notify;
	concurrent_queue<_tNotifyQueue> m_notifyqueue;
	boost::shared_ptr<boost::thread> m_pQueueThread;
	bool m_bEnabled;

	std::vector<std::string> m_customTypes;

	static const _tNotifyTypeTable typeTable[];
	static const _tNotifyStatusTable statusTable[];
};

extern CNotifySystem _notify;
