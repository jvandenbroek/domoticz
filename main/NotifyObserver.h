#pragma once

enum _eNotifyType
{
	NOTIFY_LOG,             // 0
	NOTIFY_DZ_START,        // 1
	NOTIFY_DZ_STOP,         // 2
	NOTIFY_BACKUP_BEGIN,    // 3
	NOTIFY_BACKUP_END,      // 4
	NOTIFY_HW_TIMEOUT,      // 5
	NOTIFY_HW_START,        // 6
	NOTIFY_HW_STOP,         // 7
	NOTIFY_NOTIFICATION,    // 8
	NOTIFY_THREAD_ENDED     // 9
};

enum _eNotifyStatus
{
	NOTIFY_OK,
	NOTIFY_INFO,
	NOTIFY_ERROR,
	NOTIFY_WARNING
};

class CNotifyObserver
{
public:
	CNotifyObserver(void);
	~CNotifyObserver(void);
	virtual bool NotifyReceiver(const _eNotifyType type, const _eNotifyStatus status, const uint64_t id, const std::string &message) = 0;
};
