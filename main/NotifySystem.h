#pragma once
#include "concurrent_queue.h"
#include "NotifyObserver.h"

class CNotifySystem
{
public:
	CNotifySystem(void);
	~CNotifySystem(void);
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

private:
	struct _tNotifyQueue
	{
		uint64_t id;
		_eNotifyType type;
		_eNotifyStatus status;
		std::string message;
		queue_element_trigger* trigger;
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
};

extern CNotifySystem _notify;
