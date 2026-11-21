#pragma once

#include <qbytearray.h>
#include <qstring.h>

#include <winrt/Windows.Storage.Streams.h>

namespace qs::windows::services::mpris {

struct CachedArt {
	QString url;
	QByteArray hash;
};

CachedArt
cacheThumbnail(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& thumbnail);

} // namespace qs::windows::services::mpris
