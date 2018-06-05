#include "stdafx.h"
#include "NotifyObserver.h"
#include "NotifySystem.h"
//#include <iostream>

CNotifyObserver::CNotifyObserver(void)
{
	_notify.Register(this);
}

CNotifyObserver::~CNotifyObserver(void)
{
     _notify.Unregister(this);
}
