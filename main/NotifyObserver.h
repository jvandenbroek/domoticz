#pragma once

class CNotifyObserver
{
public:
	enum _eType
	{
		LOG,             // 0
		DZ_START,        // 1
		DZ_STOP,         // 2
		BACKUP_BEGIN,    // 3
		BACKUP_END,      // 4
		HW_TIMEOUT,      // 5
		HW_START,        // 6
		HW_STOP,         // 7
		HW_THREAD_ENDED, // 8
		NOTIFICATION,    // 9
		SWITCHCMD        // 10
	};

	enum _eStatus
	{
		OK,
		INFO,
		ERROR,
		WARNING
	};

	CNotifyObserver(void);
	~CNotifyObserver(void);
	virtual bool NotifyReceiver(const _eType type, const _eStatus status, const uint64_t id, const std::string &message, const void *genericPtr) = 0;
};

typedef CNotifyObserver NOTIFY;
