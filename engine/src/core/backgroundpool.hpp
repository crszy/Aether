#pragma once

#include <qglobal.h>
#include <qtclasshelpermacros.h>

class QThreadPool;

class BackgroundThreadPool {
public:
	static QThreadPool* instance();
};

class BackgroundWorkScope {
public:
	explicit BackgroundWorkScope(bool lowIoPriority = false);
	~BackgroundWorkScope();
	Q_DISABLE_COPY_MOVE(BackgroundWorkScope);

#ifdef Q_OS_WIN
private:
	bool mEfficiencyMode = false;
	bool mBackgroundMode = false;
#endif
};
