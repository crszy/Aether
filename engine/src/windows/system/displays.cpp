#include "displays.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qstring.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "../services/message_window.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logDisplays, "quickshell.windows.displays", QtWarningMsg);

constexpr int CONFIRM_SECONDS = 15;
constexpr int SDR_MIN_NITS = 80;
constexpr int SDR_MAX_NITS = 480;

constexpr auto GET_DPI_SCALE = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFDu);
constexpr auto SET_DPI_SCALE = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFCu);
constexpr auto SET_SDR_WHITE_LEVEL = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFEEu);
constexpr auto GET_ADVANCED_COLOR_INFO_2 = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);
constexpr auto SET_HDR_STATE = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(16);

constexpr UINT32 COLOR2_LIMITED_BY_POLICY = 1u << 3;
constexpr UINT32 COLOR2_HDR_SUPPORTED = 1u << 4;
constexpr UINT32 COLOR2_HDR_USER_ENABLED = 1u << 5;

constexpr int DPI_VALUES[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};
constexpr int DPI_COUNT = static_cast<int>(std::size(DPI_VALUES));

struct DpiScaleGet {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	INT32 minScaleRel;
	INT32 curScaleRel;
	INT32 maxScaleRel;
};
static_assert(sizeof(DpiScaleGet) == 0x20);

struct DpiScaleSet {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	INT32 scaleRel;
};
static_assert(sizeof(DpiScaleSet) == 0x18);

struct AdvancedColorInfo2 {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	UINT32 value;
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 bitsPerColorChannel;
	UINT32 activeColorMode;
};

struct HdrState {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	UINT32 value;
};

struct SdrWhiteLevelSet {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	ULONG level;
	BYTE finalValue;
};

template <typename T>
void initHeader(T& packet, DISPLAYCONFIG_DEVICE_INFO_TYPE type, const LUID& adapter, UINT32 id) {
	packet.header.type = type;
	packet.header.size = sizeof(T);
	packet.header.adapterId = adapter;
	packet.header.id = id;
}

QString adapterKey(const LUID& luid) {
	return QString::number(static_cast<qint64>(luid.HighPart), 16) + '-'
	     + QString::number(static_cast<quint64>(luid.LowPart), 16);
}

QString targetKey(const LUID& luid, UINT32 target) {
	return adapterKey(luid) + QStringLiteral(":t") + QString::number(target);
}

QString sourceKey(const LUID& luid, UINT32 source) {
	return adapterKey(luid) + QStringLiteral(":s") + QString::number(source);
}

bool sameLuid(const LUID& a, const LUID& b) {
	return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

bool queryTopology(UINT32 flags, DisplayTopology& out) {
	for (int attempt = 0; attempt < 4; ++attempt) {
		UINT32 pathCount = 0;
		UINT32 modeCount = 0;
		if (GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount) != ERROR_SUCCESS) return false;
		if (pathCount == 0) {
			out = {};
			return true;
		}

		out.paths.resize(pathCount);
		out.modes.resize(std::max<UINT32>(modeCount, 1));

		auto result = QueryDisplayConfig(
		    flags,
		    &pathCount,
		    out.paths.data(),
		    &modeCount,
		    out.modes.data(),
		    nullptr
		);

		if (result == ERROR_INSUFFICIENT_BUFFER) continue;
		if (result != ERROR_SUCCESS) return false;

		out.paths.resize(pathCount);
		out.modes.resize(modeCount);
		return true;
	}

	return false;
}

void compactModes(DisplayTopology& topology) {
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	std::vector<UINT32> remap(topology.modes.size(), DISPLAYCONFIG_PATH_MODE_IDX_INVALID);

	auto keep = [&](UINT32& index) {
		if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || index >= topology.modes.size()) {
			index = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
			return;
		}

		if (remap[index] == DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
			remap[index] = static_cast<UINT32>(modes.size());
			modes.push_back(topology.modes[index]);
		}

		index = remap[index];
	};

	for (auto& path: topology.paths) {
		keep(path.sourceInfo.modeInfoIdx);
		keep(path.targetInfo.modeInfoIdx);
	}

	topology.modes = std::move(modes);
}

LONG applyTopology(DisplayTopology& topology, bool allowChanges, bool save) {
	if (topology.paths.empty()) return ERROR_INVALID_PARAMETER;

	auto* modes = topology.modes.empty() ? nullptr : topology.modes.data();
	const auto pathCount = static_cast<UINT32>(topology.paths.size());
	const auto modeCount = static_cast<UINT32>(topology.modes.size());
	const UINT32 base = SDC_USE_SUPPLIED_DISPLAY_CONFIG | (allowChanges ? SDC_ALLOW_CHANGES : 0);

	auto result =
	    SetDisplayConfig(pathCount, topology.paths.data(), modeCount, modes, base | SDC_VALIDATE);
	if (result != ERROR_SUCCESS) return result;

	return SetDisplayConfig(
	    pathCount,
	    topology.paths.data(),
	    modeCount,
	    modes,
	    base | SDC_APPLY | (save ? SDC_SAVE_TO_DATABASE : 0)
	);
}

LONG applyBest(DisplayTopology& topology, bool save) {
	auto result = applyTopology(topology, false, save);
	if (result == ERROR_SUCCESS) return result;
	return applyTopology(topology, true, save);
}

QString errorText(const char* what, LONG code) {
	return QStringLiteral("%1 failed (%2)").arg(QString::fromLatin1(what)).arg(code);
}

QString revertTopology(DisplayTopology& previous) {
	LONG result = ERROR_INVALID_PARAMETER;
	if (!previous.paths.empty()) {
		result = applyTopology(previous, false, true);
		if (result == ERROR_SUCCESS) return {};
		result = applyTopology(previous, true, true);
		if (result == ERROR_SUCCESS) return {};
		qCWarning(logDisplays) << "Restoring the previous layout failed:" << result
		                       << "- going back to the saved one";
	}

	result = SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_USE_DATABASE_CURRENT);
	return result == ERROR_SUCCESS ? QString() : errorText("SetDisplayConfig", result);
}

QString saveCurrent() {
	DisplayTopology current;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, current) || current.paths.empty()) {
		return QStringLiteral("QueryDisplayConfig failed");
	}

	auto* modes = current.modes.empty() ? nullptr : current.modes.data();
	auto result = SetDisplayConfig(
	    static_cast<UINT32>(current.paths.size()),
	    current.paths.data(),
	    static_cast<UINT32>(current.modes.size()),
	    modes,
	    SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE
	);

	return result == ERROR_SUCCESS ? QString() : errorText("SetDisplayConfig", result);
}

QString connectorCode(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech) {
	switch (tech) {
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: return QStringLiteral("vga");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO: return QStringLiteral("svideo");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO: return QStringLiteral("composite");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO: return QStringLiteral("component");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return QStringLiteral("dvi");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return QStringLiteral("hdmi");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS: return QStringLiteral("internal");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN: return QStringLiteral("djpn");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI: return QStringLiteral("sdi");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return QStringLiteral("displayport");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: return QStringLiteral("internal");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL: return QStringLiteral("udi");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED: return QStringLiteral("internal");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE: return QStringLiteral("sdtv");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST: return QStringLiteral("miracast");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED: return QStringLiteral("indirect");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL: return QStringLiteral("virtual");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL: return QStringLiteral("usbc");
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL: return QStringLiteral("internal");
	default: return {};
	}
}

int gdiNumber(const QString& name) {
	auto start = name.size();
	while (start > 0 && name.at(start - 1).isDigit()) --start;
	return name.mid(start).toInt();
}

int rotationIndex(DISPLAYCONFIG_ROTATION rotation) {
	switch (rotation) {
	case DISPLAYCONFIG_ROTATION_ROTATE90: return 1;
	case DISPLAYCONFIG_ROTATION_ROTATE180: return 2;
	case DISPLAYCONFIG_ROTATION_ROTATE270: return 3;
	default: return 0;
	}
}

bool isPortrait(DWORD orientation) { return orientation == DMDO_90 || orientation == DMDO_270; }

QVariantList enumerateModes(const std::wstring& device) {
	std::map<std::pair<int, int>, std::set<int>> found;

	DEVMODEW mode {};
	mode.dmSize = sizeof(mode);
	for (DWORD index = 0; EnumDisplaySettingsExW(device.c_str(), index, &mode, 0) != FALSE; ++index) {
		const bool interlaced = (mode.dmDisplayFlags & DM_INTERLACED) != 0;
		const bool deepEnough = mode.dmBitsPerPel == 0 || mode.dmBitsPerPel >= 24;

		if (!interlaced && deepEnough) {
			auto width = static_cast<int>(mode.dmPelsWidth);
			auto height = static_cast<int>(mode.dmPelsHeight);
			if ((mode.dmFields & DM_DISPLAYORIENTATION) != 0 && isPortrait(mode.dmDisplayOrientation))
			{
				std::swap(width, height);
			}

			if (std::max(width, height) >= 800 && std::min(width, height) >= 600) {
				auto& rates = found[{width, height}];
				if (mode.dmDisplayFrequency > 1) rates.insert(static_cast<int>(mode.dmDisplayFrequency));
			}
		}

		mode = {};
		mode.dmSize = sizeof(mode);
	}

	std::vector<std::pair<std::pair<int, int>, std::set<int>>> sorted(found.begin(), found.end());
	std::ranges::sort(sorted, [](const auto& a, const auto& b) {
		const auto areaA = static_cast<qint64>(a.first.first) * a.first.second;
		const auto areaB = static_cast<qint64>(b.first.first) * b.first.second;
		if (areaA != areaB) return areaA > areaB;
		return a.first.first > b.first.first;
	});

	QVariantList list;
	for (const auto& [size, rates]: sorted) {
		QVariantList rateList;
		for (auto rate = rates.rbegin(); rate != rates.rend(); ++rate) rateList.append(*rate);

		list.append(QVariantMap {
		    {QStringLiteral("width"), size.first},
		    {QStringLiteral("height"), size.second},
		    {QStringLiteral("rates"), rateList},
		});
	}

	return list;
}

struct Tile {
	QString key;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

bool overlaps(const Tile& a, const Tile& b) {
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool touches(const Tile& a, const Tile& b) {
	const bool sideBySide = (a.x + a.w == b.x || b.x + b.w == a.x) && a.y < b.y + b.h && b.y < a.y + a.h;
	const bool stacked = (a.y + a.h == b.y || b.y + b.h == a.y) && a.x < b.x + b.w && b.x < a.x + a.w;
	return sideBySide || stacked;
}

bool fits(const Tile& tile, const std::vector<Tile>& placed) {
	bool touching = false;
	for (const auto& other: placed) {
		if (overlaps(tile, other)) return false;
		if (touches(tile, other)) touching = true;
	}
	return touching;
}

std::optional<QPoint>
placeAdjacent(const Tile& tile, int wantedX, int wantedY, const std::vector<Tile>& placed) {
	std::vector<int> ys {wantedY};
	std::vector<int> xs {wantedX};
	for (const auto& other: placed) {
		ys.insert(ys.end(), {other.y, other.y + other.h - tile.h, other.y + other.h, other.y - tile.h});
		xs.insert(xs.end(), {other.x, other.x + other.w - tile.w, other.x + other.w, other.x - tile.w});
	}

	std::optional<QPoint> best;
	auto bestDistance = std::numeric_limits<qint64>::max();

	auto consider = [&](int x, int y) {
		auto candidate = tile;
		candidate.x = x;
		candidate.y = y;
		if (!fits(candidate, placed)) return;

		const auto dx = static_cast<qint64>(x) - wantedX;
		const auto dy = static_cast<qint64>(y) - wantedY;
		const auto distance = dx * dx + dy * dy;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = QPoint(x, y);
		}
	};

	for (const auto& other: placed) {
		const auto yMin = other.y - tile.h + 1;
		const auto yMax = other.y + other.h - 1;
		const auto xMin = other.x - tile.w + 1;
		const auto xMax = other.x + other.w - 1;

		for (auto y: ys) {
			const auto clamped = std::clamp(y, yMin, yMax);
			consider(other.x - tile.w, clamped);
			consider(other.x + other.w, clamped);
		}

		for (auto x: xs) {
			const auto clamped = std::clamp(x, xMin, xMax);
			consider(clamped, other.y - tile.h);
			consider(clamped, other.y + other.h);
		}
	}

	return best;
}

void compactLayout(std::vector<Tile>& tiles, size_t anchor) {
	if (tiles.size() < 2 || anchor >= tiles.size()) return;

	const auto& origin = tiles[anchor];
	const auto originX = 2 * static_cast<qint64>(origin.x) + origin.w;
	const auto originY = 2 * static_cast<qint64>(origin.y) + origin.h;
	auto distance = [&](const Tile& tile) {
		const auto dx = 2 * static_cast<qint64>(tile.x) + tile.w - originX;
		const auto dy = 2 * static_cast<qint64>(tile.y) + tile.h - originY;
		return dx * dx + dy * dy;
	};

	std::vector<size_t> order;
	for (size_t i = 0; i < tiles.size(); ++i) {
		if (i != anchor) order.push_back(i);
	}
	std::ranges::sort(order, [&](size_t a, size_t b) { return distance(tiles[a]) < distance(tiles[b]); });

	std::vector<Tile> placed {tiles[anchor]};
	std::vector<bool> done(tiles.size(), false);
	done[anchor] = true;
	auto remaining = order.size();

	while (remaining > 0) {
		bool progressed = false;
		for (auto i: order) {
			if (done[i] || !fits(tiles[i], placed)) continue;
			done[i] = true;
			placed.push_back(tiles[i]);
			--remaining;
			progressed = true;
		}

		if (progressed) continue;

		for (auto i: order) {
			if (done[i]) continue;

			auto& tile = tiles[i];
			if (auto spot = placeAdjacent(tile, tile.x, tile.y, placed)) {
				tile.x = spot->x();
				tile.y = spot->y();
			} else {
				int right = std::numeric_limits<int>::min();
				for (const auto& other: placed) right = std::max(right, other.x + other.w);
				tile.x = right;
				tile.y = placed.front().y;
			}

			done[i] = true;
			placed.push_back(tile);
			--remaining;
			break;
		}
	}
}

std::vector<Tile> tilesOf(const DisplayTopology& topology) {
	std::vector<Tile> tiles;
	for (const auto& path: topology.paths) {
		const auto index = path.sourceInfo.modeInfoIdx;
		if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || index >= topology.modes.size()) continue;

		const auto& mode = topology.modes[index];
		if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) continue;

		auto key = sourceKey(path.sourceInfo.adapterId, path.sourceInfo.id);
		if (std::ranges::find(tiles, key, &Tile::key) != tiles.end()) continue;

		tiles.push_back({
		    key,
		    mode.sourceMode.position.x,
		    mode.sourceMode.position.y,
		    static_cast<int>(mode.sourceMode.width),
		    static_cast<int>(mode.sourceMode.height),
		});
	}

	return tiles;
}

void writeTiles(DisplayTopology& topology, const std::vector<Tile>& tiles) {
	for (const auto& path: topology.paths) {
		const auto index = path.sourceInfo.modeInfoIdx;
		if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || index >= topology.modes.size()) continue;

		auto& mode = topology.modes[index];
		if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) continue;

		const auto key = sourceKey(path.sourceInfo.adapterId, path.sourceInfo.id);
		const auto tile = std::ranges::find(tiles, key, &Tile::key);
		if (tile == tiles.end()) continue;

		mode.sourceMode.position.x = tile->x;
		mode.sourceMode.position.y = tile->y;
		mode.sourceMode.width = static_cast<UINT32>(tile->w);
		mode.sourceMode.height = static_cast<UINT32>(tile->h);
	}
}

size_t indexOfKey(const std::vector<Tile>& tiles, const QString& key) {
	const auto tile = std::ranges::find(tiles, key, &Tile::key);
	return tile == tiles.end() ? tiles.size() : static_cast<size_t>(tile - tiles.begin());
}

QString primaryKeyOf(const std::vector<Tile>& tiles) {
	for (const auto& tile: tiles) {
		if (tile.x == 0 && tile.y == 0) return tile.key;
	}

	if (tiles.empty()) return {};

	const auto closest = std::ranges::min_element(tiles, {}, [](const Tile& tile) {
		return static_cast<qint64>(std::abs(tile.x)) + std::abs(tile.y);
	});
	return closest->key;
}

void translateTo(std::vector<Tile>& tiles, const QString& key) {
	const auto index = indexOfKey(tiles, key);
	if (index >= tiles.size()) return;

	const auto dx = -tiles[index].x;
	const auto dy = -tiles[index].y;
	for (auto& tile: tiles) {
		tile.x += dx;
		tile.y += dy;
	}
}

void resizeTile(std::vector<Tile>& tiles, const QString& key, int width, int height) {
	const auto index = indexOfKey(tiles, key);
	if (index >= tiles.size()) return;

	auto& tile = tiles[index];
	const auto right = tile.x + tile.w;
	const auto bottom = tile.y + tile.h;
	const auto dw = width - tile.w;
	const auto dh = height - tile.h;

	for (auto& other: tiles) {
		if (other.key == key) continue;
		if (other.x >= right) other.x += dw;
		if (other.y >= bottom) other.y += dh;
	}

	tile.w = width;
	tile.h = height;
}

bool sameLayout(const std::vector<Tile>& a, const std::vector<Tile>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].key != b[i].key || a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w
		    || a[i].h != b[i].h)
		{
			return false;
		}
	}
	return true;
}

void finishLayout(std::vector<Tile>& tiles, const QString& anchorKey, const QString& primaryKey) {
	compactLayout(tiles, indexOfKey(tiles, anchorKey));
	translateTo(tiles, primaryKey);
}

std::optional<size_t> pathOfTarget(const DisplayTopology& topology, const LUID& adapter, UINT32 target) {
	for (size_t i = 0; i < topology.paths.size(); ++i) {
		const auto& info = topology.paths[i].targetInfo;
		if (sameLuid(info.adapterId, adapter) && info.id == target) return i;
	}
	return std::nullopt;
}

struct HdrInfo {
	bool supported = false;
	bool enabled = false;
	bool limited = false;
	bool modern = false;
};

HdrInfo readHdr(const LUID& adapter, UINT32 target) {
	AdvancedColorInfo2 modern {};
	initHeader(modern, GET_ADVANCED_COLOR_INFO_2, adapter, target);
	if (DisplayConfigGetDeviceInfo(&modern.header) == ERROR_SUCCESS) {
		return {
		    .supported = (modern.value & COLOR2_HDR_SUPPORTED) != 0,
		    .enabled = (modern.value & COLOR2_HDR_USER_ENABLED) != 0,
		    .limited = (modern.value & COLOR2_LIMITED_BY_POLICY) != 0,
		    .modern = true,
		};
	}

	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy {};
	initHeader(legacy, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, adapter, target);
	if (DisplayConfigGetDeviceInfo(&legacy.header) != ERROR_SUCCESS) return {};

	const bool wideOnly = legacy.wideColorEnforced != 0;
	return {
	    .supported = legacy.advancedColorSupported != 0 && !wideOnly,
	    .enabled = legacy.advancedColorEnabled != 0 && !wideOnly,
	    .limited = legacy.advancedColorForceDisabled != 0,
	    .modern = false,
	};
}

int readSdrWhiteNits(const LUID& adapter, UINT32 target) {
	DISPLAYCONFIG_SDR_WHITE_LEVEL level {};
	initHeader(level, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, adapter, target);
	if (DisplayConfigGetDeviceInfo(&level.header) != ERROR_SUCCESS) return 0;
	return static_cast<int>(std::lround(static_cast<double>(level.SDRWhiteLevel) * 80.0 / 1000.0));
}

struct ScaleInfo {
	bool supported = false;
	int current = 0;
	int recommended = 0;
	QVariantList values;
};

std::optional<DpiScaleGet> readDpiPacket(const LUID& adapter, UINT32 source) {
	DpiScaleGet packet {};
	initHeader(packet, GET_DPI_SCALE, adapter, source);
	if (DisplayConfigGetDeviceInfo(&packet.header) != ERROR_SUCCESS) return std::nullopt;

	const auto recommended = std::abs(packet.minScaleRel);
	if (packet.minScaleRel > 0 || recommended >= DPI_COUNT || packet.maxScaleRel < packet.minScaleRel) {
		return std::nullopt;
	}

	return packet;
}

ScaleInfo readScale(const LUID& adapter, UINT32 source) {
	auto packet = readDpiPacket(adapter, source);
	if (!packet) return {};

	const auto recommended = std::abs(packet->minScaleRel);
	const auto first = std::clamp(recommended + packet->minScaleRel, 0, DPI_COUNT - 1);
	const auto last = std::clamp(recommended + packet->maxScaleRel, 0, DPI_COUNT - 1);
	const auto current = std::clamp(
	    recommended + std::clamp(packet->curScaleRel, packet->minScaleRel, packet->maxScaleRel),
	    0,
	    DPI_COUNT - 1
	);

	ScaleInfo info;
	info.supported = last > first;
	info.current = DPI_VALUES[current];
	info.recommended = DPI_VALUES[recommended];
	for (auto i = first; i <= last; ++i) info.values.append(DPI_VALUES[i]);
	return info;
}

} // namespace

Displays::Displays(QObject* parent): QObject(parent) {
	this->refreshTimer.setSingleShot(true);
	QObject::connect(&this->refreshTimer, &QTimer::timeout, this, [this] { this->load(true); });

	this->countdownTimer.setInterval(1000);
	QObject::connect(&this->countdownTimer, &QTimer::timeout, this, [this] {
		if (!this->mPending) {
			this->countdownTimer.stop();
			return;
		}

		this->mSecondsLeft = std::max(0, this->mSecondsLeft - 1);
		emit this->confirmSecondsLeftChanged();
		if (this->mSecondsLeft == 0) this->revertChanges();
	});

	auto* window = qs::windows::services::ServiceMessageWindow::instance();
	window->addHandler(WM_DISPLAYCHANGE, [self = QPointer(this)](WPARAM /*w*/, LPARAM /*l*/) {
		if (self && self->mLoaded) self->scheduleRefresh(250);
	});
}

Displays::~Displays() {
	if (this->mPending && !this->mBusy && !this->mPrevious.paths.empty()) {
		auto error = revertTopology(this->mPrevious);
		if (!error.isEmpty()) qCWarning(logDisplays) << "Reverting on exit failed:" << error;
	}
}

int Displays::confirmTimeout() { return CONFIRM_SECONDS; }

QVariantList Displays::displays() {
	if (!this->mLoaded) this->load(false);
	return this->mDisplays;
}

QString Displays::primaryName() {
	if (!this->mLoaded) this->load(false);
	return this->mPrimaryName;
}

void Displays::refresh() { this->load(true); }

void Displays::scheduleRefresh(int delay) { this->refreshTimer.start(delay); }

const Displays::Entry* Displays::find(const QString& id) const {
	const auto entry = std::ranges::find(this->mEntries, id, &Entry::id);
	return entry == this->mEntries.end() ? nullptr : &*entry;
}

void Displays::load(bool notify) {
	DisplayTopology active;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, active)) {
		qCWarning(logDisplays) << "QueryDisplayConfig failed:" << GetLastError();
		this->mLoaded = true;
		return;
	}

	DisplayTopology all;
	const bool haveAll = queryTopology(QDC_ALL_PATHS, all);

	std::vector<Entry> entries;

	auto fillTarget = [](Entry& entry) {
		DISPLAYCONFIG_TARGET_DEVICE_NAME target {};
		initHeader(target, DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, entry.adapterId, entry.targetId);
		auto tech = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
		if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
			entry.name = QString::fromWCharArray(target.monitorFriendlyDeviceName);
			tech = target.outputTechnology;
		}

		entry.connector = connectorCode(tech);
		entry.builtIn = entry.connector == QStringLiteral("internal");

		DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred {};
		initHeader(
		    preferred,
		    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,
		    entry.adapterId,
		    entry.targetId
		);
		if (DisplayConfigGetDeviceInfo(&preferred.header) == ERROR_SUCCESS) {
			entry.preferredWidth = static_cast<int>(preferred.width);
			entry.preferredHeight = static_cast<int>(preferred.height);
		}
	};

	for (const auto& path: active.paths) {
		if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0) continue;

		auto id = targetKey(path.targetInfo.adapterId, path.targetInfo.id);
		if (std::ranges::find(entries, id, &Entry::id) != entries.end()) continue;

		Entry entry;
		entry.id = id;
		entry.sourceKey = sourceKey(path.sourceInfo.adapterId, path.sourceInfo.id);
		entry.adapterId = path.targetInfo.adapterId;
		entry.targetId = path.targetInfo.id;
		entry.sourceId = path.sourceInfo.id;
		entry.active = true;
		fillTarget(entry);

		DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
		initHeader(
		    source,
		    DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
		    path.sourceInfo.adapterId,
		    path.sourceInfo.id
		);
		if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) {
			entry.gdiName = QString::fromWCharArray(source.viewGdiDeviceName);
		}

		const auto sourceIndex = path.sourceInfo.modeInfoIdx;
		if (sourceIndex < active.modes.size()
		    && active.modes[sourceIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
		{
			const auto& mode = active.modes[sourceIndex].sourceMode;
			entry.rect = QRect(
			    mode.position.x,
			    mode.position.y,
			    static_cast<int>(mode.width),
			    static_cast<int>(mode.height)
			);
		}

		entry.rotation = rotationIndex(path.targetInfo.rotation);

		auto refresh = path.targetInfo.refreshRate;
		const auto targetIndex = path.targetInfo.modeInfoIdx;
		if (targetIndex < active.modes.size()
		    && active.modes[targetIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
		{
			refresh = active.modes[targetIndex].targetMode.targetVideoSignalInfo.vSyncFreq;
		}
		if (refresh.Denominator != 0) {
			entry.refreshExact =
			    static_cast<double>(refresh.Numerator) / static_cast<double>(refresh.Denominator);
		}

		const auto device = entry.gdiName.toStdWString();
		DEVMODEW current {};
		current.dmSize = sizeof(current);
		if (!device.empty()
		    && EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &current, 0) != FALSE)
		{
			entry.modeWidth = static_cast<int>(current.dmPelsWidth);
			entry.modeHeight = static_cast<int>(current.dmPelsHeight);
			if ((current.dmFields & DM_DISPLAYORIENTATION) != 0
			    && isPortrait(current.dmDisplayOrientation))
			{
				std::swap(entry.modeWidth, entry.modeHeight);
			}
			entry.refreshRate = static_cast<int>(current.dmDisplayFrequency);
		} else {
			const bool portrait = entry.rotation % 2 == 1;
			entry.modeWidth = portrait ? entry.rect.height() : entry.rect.width();
			entry.modeHeight = portrait ? entry.rect.width() : entry.rect.height();
		}

		if (entry.refreshRate <= 1) entry.refreshRate = static_cast<int>(std::floor(entry.refreshExact));
		if (!device.empty()) entry.modes = enumerateModes(device);

		const auto hdr = readHdr(entry.adapterId, entry.targetId);
		entry.hdrSupported = hdr.supported;
		entry.hdrEnabled = hdr.enabled;
		entry.hdrLimited = hdr.limited;
		entry.hdrModern = hdr.modern;
		if (hdr.enabled) entry.sdrWhiteNits = readSdrWhiteNits(entry.adapterId, entry.targetId);

		auto scale = readScale(path.sourceInfo.adapterId, path.sourceInfo.id);
		entry.scaleSupported = scale.supported;
		entry.scale = scale.current;
		entry.scaleRecommended = scale.recommended;
		entry.scales = std::move(scale.values);

		entry.primary = entry.rect.x() == 0 && entry.rect.y() == 0;
		entries.push_back(std::move(entry));
	}

	std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) {
		return gdiNumber(a.gdiName) < gdiNumber(b.gdiName);
	});

	if (haveAll) {
		for (const auto& path: all.paths) {
			if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0) continue;
			if (path.targetInfo.targetAvailable == FALSE) continue;
			if ((path.targetInfo.statusFlags & DISPLAYCONFIG_TARGET_IS_HMD) != 0) continue;

			auto id = targetKey(path.targetInfo.adapterId, path.targetInfo.id);
			if (std::ranges::find(entries, id, &Entry::id) != entries.end()) continue;

			Entry entry;
			entry.id = id;
			entry.adapterId = path.targetInfo.adapterId;
			entry.targetId = path.targetInfo.id;
			fillTarget(entry);
			entries.push_back(std::move(entry));
		}
	}

	QVariantList list;
	QString primary;
	int number = 0;
	for (auto& entry: entries) {
		entry.number = ++number;
		if (entry.primary) primary = entry.gdiName;

		list.append(QVariantMap {
		    {QStringLiteral("id"), entry.id},
		    {QStringLiteral("name"), entry.name},
		    {QStringLiteral("connector"), entry.connector},
		    {QStringLiteral("gdiName"), entry.gdiName},
		    {QStringLiteral("number"), entry.number},
		    {QStringLiteral("active"), entry.active},
		    {QStringLiteral("primary"), entry.primary},
		    {QStringLiteral("builtIn"), entry.builtIn},
		    {QStringLiteral("x"), entry.rect.x()},
		    {QStringLiteral("y"), entry.rect.y()},
		    {QStringLiteral("width"), entry.rect.width()},
		    {QStringLiteral("height"), entry.rect.height()},
		    {QStringLiteral("resolutionWidth"), entry.modeWidth},
		    {QStringLiteral("resolutionHeight"), entry.modeHeight},
		    {QStringLiteral("refreshRate"), entry.refreshRate},
		    {QStringLiteral("refreshRateExact"), std::round(entry.refreshExact * 100.0) / 100.0},
		    {QStringLiteral("rotation"), entry.rotation},
		    {QStringLiteral("preferredWidth"), entry.preferredWidth},
		    {QStringLiteral("preferredHeight"), entry.preferredHeight},
		    {QStringLiteral("modes"), entry.modes},
		    {QStringLiteral("hdrSupported"), entry.hdrSupported},
		    {QStringLiteral("hdrEnabled"), entry.hdrEnabled},
		    {QStringLiteral("hdrLimited"), entry.hdrLimited},
		    {QStringLiteral("sdrBrightness"), entry.sdrWhiteNits},
		    {QStringLiteral("sdrBrightnessSupported"),
		     entry.hdrEnabled && entry.sdrWhiteNits > 0 && !this->mSdrSetFailed},
		    {QStringLiteral("scaleSupported"), entry.scaleSupported},
		    {QStringLiteral("scale"), entry.scale},
		    {QStringLiteral("scaleRecommended"), entry.scaleRecommended},
		    {QStringLiteral("scales"), entry.scales},
		});
	}

	const bool changed = list != this->mDisplays || primary != this->mPrimaryName;
	this->mEntries = std::move(entries);
	this->mDisplays = std::move(list);
	this->mPrimaryName = primary;
	this->mLoaded = true;

	if (notify && changed) emit this->displaysChanged();
}

void Displays::setBusy(bool busy) {
	if (busy == this->mBusy) return;
	this->mBusy = busy;
	emit this->busyChanged();
}

void Displays::fail(const QString& message) {
	qCWarning(logDisplays) << message;
	this->mLastError = message;
	emit this->lastErrorChanged();
	emit this->changeFailed(message);
}

bool Displays::runJob(Job job, bool confirm) {
	if (this->mBusy) {
		this->fail(QStringLiteral("Another display change is still running"));
		return false;
	}

	bool tookSnapshot = false;
	if (confirm && !this->mPending) {
		if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, this->mPrevious) || this->mPrevious.paths.empty()) {
			this->mPrevious = {};
			this->fail(QStringLiteral("Couldn't read the current display layout"));
			return false;
		}
		tookSnapshot = true;
	}

	if (!this->mLastError.isEmpty()) {
		this->mLastError.clear();
		emit this->lastErrorChanged();
	}

	this->countdownTimer.stop();
	this->setBusy(true);

	std::thread([self = QPointer(this), job = std::move(job), confirm, tookSnapshot]() {
		SetThreadDescription(GetCurrentThread(), L"qs display config");
		auto error = job();

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [self, error = std::move(error), confirm, tookSnapshot]() {
			    if (self) self->onJobFinished(error, confirm, tookSnapshot);
		    },
		    Qt::QueuedConnection
		);
	}).detach();

	return true;
}

void Displays::onJobFinished(const QString& error, bool confirm, bool tookSnapshot) {
	this->setBusy(false);

	if (!error.isEmpty()) {
		if (tookSnapshot) this->mPrevious = {};
		this->fail(error);
		if (this->mPending) this->countdownTimer.start();
	} else if (confirm) {
		this->armConfirmation();
	} else if (this->mPending) {
		this->countdownTimer.start();
	}

	this->scheduleRefresh(300);
	QTimer::singleShot(1500, this, [this] { this->load(true); });
}

void Displays::armConfirmation() {
	this->mSecondsLeft = CONFIRM_SECONDS;
	emit this->confirmSecondsLeftChanged();

	if (!this->mPending) {
		this->mPending = true;
		emit this->pendingConfirmationChanged();
	}

	this->countdownTimer.start();
}

void Displays::clearConfirmation() {
	this->countdownTimer.stop();

	if (this->mSecondsLeft != 0) {
		this->mSecondsLeft = 0;
		emit this->confirmSecondsLeftChanged();
	}

	if (this->mPending) {
		this->mPending = false;
		emit this->pendingConfirmationChanged();
	}
}

void Displays::keepChanges() {
	if (!this->mPending || this->mBusy) return;

	this->mPrevious = {};
	this->clearConfirmation();
	this->runJob([] { return saveCurrent(); }, false);
}

void Displays::revertChanges() {
	if (!this->mPending || this->mBusy) return;

	auto previous = std::move(this->mPrevious);
	this->mPrevious = {};
	this->clearConfirmation();
	this->runJob([previous = std::move(previous)]() mutable { return revertTopology(previous); }, false);
}

bool Displays::applyLayoutChange(DisplayTopology topology) {
	return this->runJob(
	    [topology = std::move(topology)]() mutable -> QString {
		    auto result = applyBest(topology, false);
		    return result == ERROR_SUCCESS ? QString() : errorText("SetDisplayConfig", result);
	    },
	    true
	);
}

bool Displays::setMode(const QString& id, int width, int height, int refreshRate) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || width <= 0 || height <= 0) return false;

	const bool sameSize = width == entry->modeWidth && height == entry->modeHeight;
	const bool sameRate = refreshRate == entry->refreshRate;
	if (sameSize && sameRate) return true;

	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) {
		this->fail(QStringLiteral("Couldn't read the current display layout"));
		return false;
	}

	const auto pathIndex = pathOfTarget(topology, entry->adapterId, entry->targetId);
	if (!pathIndex) return false;

	const bool portrait = entry->rotation % 2 == 1;
	const auto desktopWidth = portrait ? height : width;
	const auto desktopHeight = portrait ? width : height;

	auto tiles = tilesOf(topology);
	const auto primaryKey = primaryKeyOf(tiles);
	resizeTile(tiles, entry->sourceKey, desktopWidth, desktopHeight);
	finishLayout(tiles, primaryKey, primaryKey);
	writeTiles(topology, tiles);

	const auto device = entry->gdiName.toStdWString();
	DEVMODEW devMode {};
	devMode.dmSize = sizeof(devMode);
	if (!device.empty()) EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &devMode, 0);
	devMode.dmPelsWidth = static_cast<DWORD>(desktopWidth);
	devMode.dmPelsHeight = static_cast<DWORD>(desktopHeight);
	devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
	if (refreshRate > 1) {
		devMode.dmDisplayFrequency = static_cast<DWORD>(refreshRate);
		devMode.dmFields |= DM_DISPLAYFREQUENCY;
	}

	auto job = [topology = std::move(topology),
	            index = *pathIndex,
	            refreshRate,
	            keepTarget = sameRate,
	            device,
	            devMode]() mutable -> QString {
		auto attempt = [&](bool keepTargetMode,
		                   DISPLAYCONFIG_RATIONAL rate,
		                   bool preferredScaling,
		                   bool allowChanges) {
			auto candidate = topology;
			auto& target = candidate.paths[index].targetInfo;
			if (!keepTargetMode) {
				target.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
				target.refreshRate = rate;
				if (rate.Numerator == 0) {
					target.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
				}
			}
			if (preferredScaling) target.scaling = DISPLAYCONFIG_SCALING_PREFERRED;
			compactModes(candidate);
			return applyTopology(candidate, allowChanges, false);
		};

		std::vector<DISPLAYCONFIG_RATIONAL> rates;
		if (keepTarget) {
			const auto& current = topology.paths[index].targetInfo.refreshRate;
			if (current.Numerator != 0 && current.Denominator != 0) rates.push_back(current);
		}
		if (refreshRate > 1) {
			const auto rate = static_cast<UINT32>(refreshRate);
			rates.push_back({rate, 1});
			rates.push_back({(rate + 1) * 1000, 1001});
			rates.push_back({rate * 1000, 1001});
		}

		LONG result = ERROR_INVALID_PARAMETER;
		for (auto allowChanges: {false, true}) {
			for (auto preferred: {false, true}) {
				for (const auto& rate: rates) {
					result = attempt(false, rate, preferred, allowChanges);
					if (result == ERROR_SUCCESS) return {};
				}
			}

			if (keepTarget) {
				result = attempt(true, {}, true, allowChanges);
				if (result == ERROR_SUCCESS) return {};
			}
		}

		if (rates.empty()) {
			result = attempt(false, {0, 0}, true, true);
			if (result == ERROR_SUCCESS) return {};
		}

		if (device.empty()) return errorText("SetDisplayConfig", result);

		auto legacy = ChangeDisplaySettingsExW(device.c_str(), &devMode, nullptr, CDS_TEST, nullptr);
		if (legacy == DISP_CHANGE_SUCCESSFUL) {
			legacy = ChangeDisplaySettingsExW(device.c_str(), &devMode, nullptr, 0, nullptr);
		}
		if (legacy == DISP_CHANGE_SUCCESSFUL) return {};

		return QStringLiteral("SetDisplayConfig failed (%1), ChangeDisplaySettingsEx failed (%2)")
		    .arg(result)
		    .arg(legacy);
	};

	return this->runJob(std::move(job), true);
}

bool Displays::setRotation(const QString& id, int rotation) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || rotation < 0 || rotation > 3) return false;
	if (rotation == entry->rotation) return true;

	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) {
		this->fail(QStringLiteral("Couldn't read the current display layout"));
		return false;
	}

	const auto pathIndex = pathOfTarget(topology, entry->adapterId, entry->targetId);
	if (!pathIndex) return false;

	static constexpr DISPLAYCONFIG_ROTATION ROTATIONS[] = {
	    DISPLAYCONFIG_ROTATION_IDENTITY,
	    DISPLAYCONFIG_ROTATION_ROTATE90,
	    DISPLAYCONFIG_ROTATION_ROTATE180,
	    DISPLAYCONFIG_ROTATION_ROTATE270,
	};

	const auto sourceKeyOfPath = entry->sourceKey;
	for (auto& path: topology.paths) {
		if (sourceKey(path.sourceInfo.adapterId, path.sourceInfo.id) == sourceKeyOfPath) {
			path.targetInfo.rotation = ROTATIONS[rotation];
		}
	}

	auto tiles = tilesOf(topology);
	const auto primaryKey = primaryKeyOf(tiles);
	const auto index = indexOfKey(tiles, entry->sourceKey);
	if (index < tiles.size() && (entry->rotation % 2) != (rotation % 2)) {
		const auto width = tiles[index].h;
		const auto height = tiles[index].w;
		resizeTile(tiles, entry->sourceKey, width, height);
	}
	finishLayout(tiles, primaryKey, primaryKey);
	writeTiles(topology, tiles);

	const auto device = entry->gdiName.toStdWString();
	DEVMODEW devMode {};
	devMode.dmSize = sizeof(devMode);
	bool haveDevMode = !device.empty()
	                && EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &devMode, 0) != FALSE;
	if (haveDevMode) {
		const bool wasPortrait = (devMode.dmFields & DM_DISPLAYORIENTATION) != 0
		                      && isPortrait(devMode.dmDisplayOrientation);
		if (wasPortrait != (rotation % 2 == 1)) std::swap(devMode.dmPelsWidth, devMode.dmPelsHeight);
		devMode.dmDisplayOrientation = static_cast<DWORD>(rotation);
		devMode.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;
	}

	auto job = [topology = std::move(topology), device, devMode, haveDevMode]() mutable -> QString {
		auto result = applyBest(topology, false);
		if (result == ERROR_SUCCESS) return {};
		if (!haveDevMode) return errorText("SetDisplayConfig", result);

		auto legacy = ChangeDisplaySettingsExW(device.c_str(), &devMode, nullptr, CDS_TEST, nullptr);
		if (legacy == DISP_CHANGE_SUCCESSFUL) {
			legacy = ChangeDisplaySettingsExW(device.c_str(), &devMode, nullptr, 0, nullptr);
		}
		if (legacy == DISP_CHANGE_SUCCESSFUL) return {};

		return QStringLiteral("SetDisplayConfig failed (%1), ChangeDisplaySettingsEx failed (%2)")
		    .arg(result)
		    .arg(legacy);
	};

	return this->runJob(std::move(job), true);
}

QVariantMap Displays::previewPosition(const QString& id, int x, int y) {
	if (!this->mLoaded) this->load(false);

	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active) return {};

	std::vector<Tile> tiles;
	for (const auto& other: this->mEntries) {
		if (!other.active) continue;
		if (std::ranges::find(tiles, other.sourceKey, &Tile::key) != tiles.end()) continue;
		tiles.push_back(
		    {other.sourceKey, other.rect.x(), other.rect.y(), other.rect.width(), other.rect.height()}
		);
	}

	const auto index = indexOfKey(tiles, entry->sourceKey);
	if (index >= tiles.size() || tiles.size() < 2) return {};

	std::vector<Tile> others;
	for (const auto& tile: tiles) {
		if (tile.key != entry->sourceKey) others.push_back(tile);
	}

	if (auto spot = placeAdjacent(tiles[index], x, y, others)) {
		tiles[index].x = spot->x();
		tiles[index].y = spot->y();
		compactLayout(tiles, index);
	}

	QVariantMap result;
	for (const auto& other: this->mEntries) {
		if (!other.active) continue;
		const auto tile = std::ranges::find(tiles, other.sourceKey, &Tile::key);
		if (tile == tiles.end()) continue;
		result.insert(
		    other.id,
		    QVariantMap {{QStringLiteral("x"), tile->x}, {QStringLiteral("y"), tile->y}}
		);
	}

	return result;
}

bool Displays::setPosition(const QString& id, int x, int y) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active) return false;

	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) {
		this->fail(QStringLiteral("Couldn't read the current display layout"));
		return false;
	}

	const auto before = tilesOf(topology);
	auto tiles = before;
	const auto index = indexOfKey(tiles, entry->sourceKey);
	if (index >= tiles.size() || tiles.size() < 2) return false;

	const auto primaryKey = primaryKeyOf(tiles);

	std::vector<Tile> others;
	for (const auto& tile: tiles) {
		if (tile.key != entry->sourceKey) others.push_back(tile);
	}

	auto spot = placeAdjacent(tiles[index], x, y, others);
	if (!spot) return false;

	tiles[index].x = spot->x();
	tiles[index].y = spot->y();
	finishLayout(tiles, entry->sourceKey, primaryKey);

	if (sameLayout(before, tiles)) return true;

	writeTiles(topology, tiles);
	return this->applyLayoutChange(std::move(topology));
}

bool Displays::setPrimary(const QString& id) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || entry->primary) return entry != nullptr && entry->primary;

	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) {
		this->fail(QStringLiteral("Couldn't read the current display layout"));
		return false;
	}

	auto tiles = tilesOf(topology);
	if (indexOfKey(tiles, entry->sourceKey) >= tiles.size()) return false;

	translateTo(tiles, entry->sourceKey);
	writeTiles(topology, tiles);
	return this->applyLayoutChange(std::move(topology));
}

bool Displays::setEnabled(const QString& id, bool enabled) {
	const auto* entry = this->find(id);
	if (entry == nullptr) return false;
	if (entry->active == enabled) return true;

	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) {
		this->fail(QStringLiteral("Couldn't read the current display layout"));
		return false;
	}

	if (!enabled) {
		const auto activeCount =
		    std::ranges::count_if(this->mEntries, [](const Entry& other) { return other.active; });
		if (activeCount <= 1) {
			this->fail(QStringLiteral("The last active display can't be turned off"));
			return false;
		}

		const auto before = tilesOf(topology);
		const auto oldPrimary = primaryKeyOf(before);

		std::erase_if(topology.paths, [&](const DISPLAYCONFIG_PATH_INFO& path) {
			return sameLuid(path.targetInfo.adapterId, entry->adapterId)
			    && path.targetInfo.id == entry->targetId;
		});
		if (topology.paths.empty()) return false;

		compactModes(topology);
		auto tiles = tilesOf(topology);
		if (tiles.empty()) return false;

		auto primaryKey = indexOfKey(tiles, oldPrimary) < tiles.size() ? oldPrimary : primaryKeyOf(tiles);
		finishLayout(tiles, primaryKey, primaryKey);
		writeTiles(topology, tiles);
		return this->applyLayoutChange(std::move(topology));
	}

	DisplayTopology all;
	if (!queryTopology(QDC_ALL_PATHS, all)) {
		this->fail(QStringLiteral("Couldn't read the available displays"));
		return false;
	}

	std::optional<DISPLAYCONFIG_PATH_INFO> candidate;
	int availableInactive = 0;
	std::set<QString> countedTargets;

	for (const auto& path: all.paths) {
		if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0) continue;
		if (path.targetInfo.targetAvailable == FALSE) continue;

		const auto key = targetKey(path.targetInfo.adapterId, path.targetInfo.id);
		const bool isActive = std::ranges::any_of(topology.paths, [&](const auto& active) {
			return targetKey(active.targetInfo.adapterId, active.targetInfo.id) == key;
		});
		if (isActive) continue;
		if (countedTargets.insert(key).second) ++availableInactive;

		if (candidate || key != entry->id) continue;

		const bool sourceTaken = std::ranges::any_of(topology.paths, [&](const auto& active) {
			return sameLuid(active.sourceInfo.adapterId, path.sourceInfo.adapterId)
			    && active.sourceInfo.id == path.sourceInfo.id;
		});
		if (sourceTaken) continue;

		candidate = path;
	}

	if (!candidate) {
		this->fail(QStringLiteral("This display can't be turned on from here"));
		return false;
	}

	candidate->flags = DISPLAYCONFIG_PATH_ACTIVE;
	candidate->sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
	candidate->targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
	candidate->targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
	candidate->targetInfo.scaling = DISPLAYCONFIG_SCALING_PREFERRED;
	candidate->targetInfo.refreshRate = {0, 0};
	candidate->targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
	topology.paths.push_back(*candidate);
	compactModes(topology);

	const bool canExtend = availableInactive == 1;
	return this->runJob(
	    [topology = std::move(topology), canExtend]() mutable -> QString {
		    auto result = applyTopology(topology, true, false);
		    if (result == ERROR_SUCCESS) return {};

		    if (canExtend) {
			    result = SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_TOPOLOGY_EXTEND);
			    if (result == ERROR_SUCCESS) return {};
		    }

		    return errorText("SetDisplayConfig", result);
	    },
	    true
	);
}

bool Displays::setHdrEnabled(const QString& id, bool enabled) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || !entry->hdrSupported) return false;
	if (entry->hdrEnabled == enabled) return true;

	return this->runJob(
	    [adapter = entry->adapterId, target = entry->targetId, modern = entry->hdrModern, enabled]()
	        -> QString {
		    LONG result = ERROR_SUCCESS;
		    if (modern) {
			    HdrState state {};
			    initHeader(state, SET_HDR_STATE, adapter, target);
			    state.value = enabled ? 1 : 0;
			    result = DisplayConfigSetDeviceInfo(&state.header);
		    } else {
			    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state {};
			    initHeader(state, DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE, adapter, target);
			    state.enableAdvancedColor = enabled ? 1 : 0;
			    result = DisplayConfigSetDeviceInfo(&state.header);
		    }

		    return result == ERROR_SUCCESS ? QString() : errorText("DisplayConfigSetDeviceInfo", result);
	    },
	    false
	);
}

bool Displays::setSdrBrightness(const QString& id, int nits) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || !entry->hdrEnabled || this->mSdrSetFailed) return false;

	SdrWhiteLevelSet packet {};
	initHeader(packet, SET_SDR_WHITE_LEVEL, entry->adapterId, entry->targetId);
	packet.level = static_cast<ULONG>(std::clamp(nits, SDR_MIN_NITS, SDR_MAX_NITS) * 1000 / 80);
	packet.finalValue = 1;

	const auto result = DisplayConfigSetDeviceInfo(&packet.header);
	if (result != ERROR_SUCCESS) {
		this->mSdrSetFailed = true;
		this->fail(errorText("DisplayConfigSetDeviceInfo", result));
		this->load(true);
		return false;
	}

	this->scheduleRefresh(400);
	return true;
}

bool Displays::setScale(const QString& id, int percent) {
	const auto* entry = this->find(id);
	if (entry == nullptr || !entry->active || !entry->scaleSupported) return false;
	if (percent == entry->scale) return true;
	if (!entry->scales.contains(percent)) return false;

	const auto* value = std::ranges::find(DPI_VALUES, percent);
	if (value == std::end(DPI_VALUES)) return false;
	const auto wanted = static_cast<int>(value - std::begin(DPI_VALUES));

	DISPLAYCONFIG_PATH_INFO path {};
	DisplayTopology topology;
	if (!queryTopology(QDC_ONLY_ACTIVE_PATHS, topology)) return false;
	const auto pathIndex = pathOfTarget(topology, entry->adapterId, entry->targetId);
	if (!pathIndex) return false;
	path = topology.paths[*pathIndex];

	return this->runJob(
	    [adapter = path.sourceInfo.adapterId, source = path.sourceInfo.id, wanted]() -> QString {
		    auto packet = readDpiPacket(adapter, source);
		    if (!packet) return QStringLiteral("Reading the display scale failed");

		    DpiScaleSet request {};
		    initHeader(request, SET_DPI_SCALE, adapter, source);
		    request.scaleRel = wanted - std::abs(packet->minScaleRel);

		    const auto result = DisplayConfigSetDeviceInfo(&request.header);
		    return result == ERROR_SUCCESS ? QString() : errorText("DisplayConfigSetDeviceInfo", result);
	    },
	    false
	);
}

} // namespace qs::windows::sys
