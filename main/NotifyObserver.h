#pragma once
#include <string>

enum _eNotifyType
{
	NOTIFY,					// 0
	NOTIFY_STARTUP,			// 1
	NOTIFY_SHUTDOWN,		// 2
	NOTIFY_NOTIFICATION,	// 4
	NOTIFY_BACKUP_START,	// 5
	NOTIFY_BACKUP_END,		// 6
	NOTIFY_TIMEOUT,			// 7
	NOTIFY_ENDED,			// 8
	NOTIFY_DISKFULL,		// 9
	NOTIFY_WHATEVER			// 10
};

enum _eNotifyStatus
{
	NOTIFY_ERROR, 	// LOG_ERROR
	NOTIFY_INFO,	// LOG_STATUS
	NOTIFY_NORM,	// LOG_NORM
	NOTIFY_TRACE,	// LOG_TRACE
};

class CNotifyObserver
{
public:
	virtual bool NotifyReceiver(const _eNotifyType type, const _eNotifyStatus status, const std::string &message) = 0;
};
