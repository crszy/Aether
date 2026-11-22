#include "tiling_layout.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include <qhash.h>
#include <qlist.h>
#include <qpoint.h>
#include <qrect.h>
#include <qset.h>
#include <qtypes.h>

namespace qs::windows::tiling {

namespace {

constexpr double MIN_RATIO = 0.05;
constexpr double MAX_RATIO = 0.95;

double clampRatio(double ratio) { return qBound(MIN_RATIO, ratio, MAX_RATIO); }

bool isVerticalEdge(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }

int rightOf(const QRect& rect) { return rect.left() + rect.width(); }
int bottomOf(const QRect& rect) { return rect.top() + rect.height(); }

Edge opposite(Edge edge) {
	switch (edge) {
	case Edge::Left: return Edge::Right;
	case Edge::Right: return Edge::Left;
	case Edge::Top: return Edge::Bottom;
	case Edge::Bottom: return Edge::Top;
	}
	return edge;
}

int edgeOf(const QRect& box, Edge edge) {
	switch (edge) {
	case Edge::Left: return box.left();
	case Edge::Right: return rightOf(box);
	case Edge::Top: return box.top();
	case Edge::Bottom: return bottomOf(box);
	}
	return 0;
}

} // namespace

std::unique_ptr<LayoutEngine> LayoutEngine::create(Kind kind) {
	if (kind == Kind::Master) return std::make_unique<MasterLayout>();
	if (kind == Kind::Scrolling) return std::make_unique<ScrollingLayout>();
	return std::make_unique<DwindleLayout>();
}

qsizetype DwindleLayout::count() const { return this->ids().length(); }

QList<DwindleLayout::Id> DwindleLayout::ids() const {
	QList<Id> out;
	if (this->root != nullptr) DwindleLayout::collect(this->root.get(), out);
	return out;
}

void DwindleLayout::collect(const Node* node, QList<Id>& out) {
	if (node->isLeaf()) {
		out.append(node->id);
		return;
	}

	DwindleLayout::collect(node->first.get(), out);
	DwindleLayout::collect(node->second.get(), out);
}

DwindleLayout::Node* DwindleLayout::find(Id id) const {
	if (this->root == nullptr) return nullptr;

	QList<Node*> stack {this->root.get()};
	while (!stack.isEmpty()) {
		auto* node = stack.takeLast();
		if (node->isLeaf()) {
			if (node->id == id) return node;
			continue;
		}

		stack.append(node->second.get());
		stack.append(node->first.get());
	}

	return nullptr;
}

DwindleLayout::Node* DwindleLayout::lastLeaf() const {
	auto* node = this->root.get();
	while (node != nullptr && !node->isLeaf()) node = node->second.get();
	return node;
}

void DwindleLayout::insert(Id id, Id target, const QPoint* cursor) {
	if (this->contains(id)) return;

	auto leaf = std::make_unique<Node>();
	leaf->id = id;

	if (this->root == nullptr) {
		this->root = std::move(leaf);
		return;
	}

	if (this->mArea.isValid()) this->compute(this->mArea);

	auto* split = this->find(target);
	if (split == nullptr) split = this->lastLeaf();

	auto old = std::make_unique<Node>();
	old->id = split->id;
	old->box = split->box;
	old->parent = split;
	leaf->parent = split;

	const auto box = split->box;
	split->id = 0;
	split->splitTop = box.height() > box.width();
	split->splitPinned = false;
	split->ratio = this->defaultRatio;

	auto newFirst = false;
	if (this->smartSplit && cursor != nullptr) {
		auto delta = *cursor - box.center();
		auto across = qAbs(qint64(delta.y())) * box.width();
		auto along = qAbs(qint64(delta.x())) * box.height();
		split->splitTop = across >= along;
		newFirst = split->splitTop ? delta.y() <= 0 : delta.x() <= 0;
	} else if (this->forceSplit == 1) {
		newFirst = true;
	} else if (this->forceSplit == 2) {
		newFirst = false;
	} else if (cursor != nullptr && box.contains(*cursor)) {
		newFirst = split->splitTop ? cursor->y() < box.center().y() : cursor->x() < box.center().x();
	}

	if (newFirst) {
		split->first = std::move(leaf);
		split->second = std::move(old);
	} else {
		split->first = std::move(old);
		split->second = std::move(leaf);
	}
}

void DwindleLayout::remove(Id id) {
	auto* node = this->find(id);
	if (node == nullptr) return;

	this->hidden.remove(id);

	if (node == this->root.get()) {
		this->root.reset();
		return;
	}

	auto* parent = node->parent;
	auto sibling = parent->first.get() == node ? std::move(parent->second) : std::move(parent->first);
	sibling->parent = parent->parent;

	if (parent->parent == nullptr) {
		this->root = std::move(sibling);
	} else {
		auto* grand = parent->parent;
		auto& slot = grand->first.get() == parent ? grand->first : grand->second;
		slot = std::move(sibling);
	}
}

void DwindleLayout::swap(Id a, Id b) {
	auto* nodeA = this->find(a);
	auto* nodeB = this->find(b);
	if (nodeA == nullptr || nodeB == nullptr || nodeA == nodeB) return;
	std::swap(nodeA->id, nodeB->id);
}

bool DwindleLayout::replace(Id id, Id with) {
	auto* node = this->find(id);
	if (node == nullptr) return false;
	node->id = with;
	this->hidden.remove(id);
	return true;
}

void DwindleLayout::setHidden(Id id, bool hidden) {
	if (hidden && this->contains(id)) this->hidden.insert(id);
	else this->hidden.remove(id);
}

bool DwindleLayout::hasVisible(const Node* node) const {
	if (node->isLeaf()) return !this->hidden.contains(node->id);
	return this->hasVisible(node->first.get()) || this->hasVisible(node->second.get());
}

bool DwindleLayout::toggleSplit(Id id) {
	auto* node = this->find(id);
	if (node == nullptr || node->parent == nullptr) return false;
	node->parent->splitTop = !node->parent->splitTop;
	node->parent->splitPinned = true;
	return true;
}

bool DwindleLayout::swapSplit(Id id) {
	auto* node = this->find(id);
	if (node == nullptr || node->parent == nullptr) return false;
	std::swap(node->parent->first, node->parent->second);
	return true;
}

bool DwindleLayout::splitRatio(Id id, double value, bool exact) {
	auto* node = this->find(id);
	if (node == nullptr || node->parent == nullptr) return false;

	auto* parent = node->parent;
	auto hyprland = exact ? value : parent->ratio * 2.0 + value;
	parent->ratio = clampRatio(hyprland / 2.0);
	return true;
}

DwindleLayout::Node* DwindleLayout::dividerAncestor(Node* leaf, Edge edge) const {
	auto vertical = isVerticalEdge(edge);
	auto trailing = edge == Edge::Right || edge == Edge::Bottom;

	for (Node *child = leaf, *node = leaf->parent; node != nullptr;
	     child = node, node = node->parent)
	{
		if (node->splitTop == vertical) continue;

		auto childFirst = node->first.get() == child;
		if (childFirst != trailing) continue;

		if (this->hasVisible(childFirst ? node->second.get() : node->first.get())) return node;
	}

	return nullptr;
}

void DwindleLayout::setDivider(Node* node, int position) {
	const auto& box = node->box;
	auto length = node->splitTop ? box.height() : box.width();
	if (length <= 0) return;

	auto offset = position - (node->splitTop ? box.top() : box.left());
	node->ratio = clampRatio(static_cast<double>(offset) / length);
}

bool DwindleLayout::moveEdge(Id id, Edge edge, int position) {
	auto* leaf = this->find(id);
	if (leaf == nullptr) return false;

	auto* node = this->dividerAncestor(leaf, edge);
	if (node == nullptr) return false;

	DwindleLayout::setDivider(node, position);
	return true;
}

bool DwindleLayout::hasEdge(Id id, Edge edge) const {
	auto* leaf = this->find(id);
	return leaf != nullptr && this->dividerAncestor(leaf, edge) != nullptr;
}

bool DwindleLayout::resize(Id id, int dx, int dy) {
	auto* leaf = this->find(id);
	if (leaf == nullptr) return false;

	auto changed = false;

	auto grow = [&](int delta, Edge trailing, Edge leading) {
		if (delta == 0) return;

		auto divider = [](const Node* node) {
			const auto& box = node->box;
			return node->splitTop ? box.top() + static_cast<int>(std::lround(box.height() * node->ratio))
			                      : box.left() + static_cast<int>(std::lround(box.width() * node->ratio));
		};

		if (auto* node = this->dividerAncestor(leaf, trailing)) {
			DwindleLayout::setDivider(node, divider(node) + delta);
			changed = true;
		} else if (auto* node = this->dividerAncestor(leaf, leading)) {
			DwindleLayout::setDivider(node, divider(node) - delta);
			changed = true;
		}
	};

	grow(dx, Edge::Right, Edge::Left);
	grow(dy, Edge::Bottom, Edge::Top);
	return changed;
}

void DwindleLayout::configure(const Options& options) {
	this->preserveSplit = options.preserveSplit;
	this->smartSplit = options.smartSplit;
	this->forceSplit = options.forceSplit;
	this->defaultRatio = clampRatio(options.defaultSplitRatio / 2.0);
}

void DwindleLayout::compute(const QRect& area) {
	this->mArea = area;
	if (this->root != nullptr) this->computeNode(this->root.get(), area);
}

void DwindleLayout::computeNode(Node* node, const QRect& box) const {
	if (node->isLeaf()) {
		node->box = this->hidden.contains(node->id) ? QRect() : box;
		return;
	}

	node->box = box;

	auto firstVisible = this->hasVisible(node->first.get());
	auto secondVisible = this->hasVisible(node->second.get());
	if (!firstVisible || !secondVisible) {
		this->computeNode(node->first.get(), firstVisible ? box : QRect());
		this->computeNode(node->second.get(), secondVisible ? box : QRect());
		return;
	}

	if (!this->preserveSplit && !this->smartSplit && !node->splitPinned) {
		node->splitTop = box.height() > box.width();
	}

	if (node->splitTop) {
		auto firstHeight = static_cast<int>(std::lround(box.height() * node->ratio));
		this->computeNode(node->first.get(), QRect(box.left(), box.top(), box.width(), firstHeight));
		this->computeNode(
		    node->second.get(),
		    QRect(box.left(), box.top() + firstHeight, box.width(), box.height() - firstHeight)
		);
	} else {
		auto firstWidth = static_cast<int>(std::lround(box.width() * node->ratio));
		this->computeNode(node->first.get(), QRect(box.left(), box.top(), firstWidth, box.height()));
		this->computeNode(
		    node->second.get(),
		    QRect(box.left() + firstWidth, box.top(), box.width() - firstWidth, box.height())
		);
	}
}

QRect DwindleLayout::box(Id id) const {
	auto* node = this->find(id);
	return node == nullptr ? QRect() : node->box;
}

QList<LayoutEngine::Id> MasterLayout::ids() const {
	QList<Id> out;
	out.reserve(this->entries.length());
	for (const auto& entry: this->entries) out.append(entry.id);
	return out;
}

qsizetype MasterLayout::indexOf(Id id) const {
	for (qsizetype i = 0; i < this->entries.length(); i++) {
		if (this->entries[i].id == id) return i;
	}

	return -1;
}

qsizetype MasterLayout::firstVisible(qsizetype from) const {
	for (auto i = from; i < this->entries.length(); i++) {
		if (!this->hidden.contains(this->entries[i].id)) return i;
	}

	return -1;
}

LayoutEngine::Id MasterLayout::master() const {
	auto index = this->firstVisible(0);
	return index == -1 ? 0 : this->entries[index].id;
}

LayoutEngine::Id MasterLayout::firstInStack() const {
	auto index = this->firstVisible(0);
	if (index == -1) return 0;
	index = this->firstVisible(index + 1);
	return index == -1 ? 0 : this->entries[index].id;
}

void MasterLayout::insert(Id id, Id target, const QPoint* /*cursor*/) {
	if (this->contains(id)) return;

	auto entry = Entry {.id = id, .weight = 1.0, .box = QRect()};

	if (this->entries.isEmpty()) {
		this->entries.append(entry);
		return;
	}

	auto masterIndex = this->firstVisible(0);
	auto targetIsMaster = target != 0 && masterIndex != -1 && this->entries[masterIndex].id == target;

	if (this->newStatus == NewStatus::Master
	    || (this->newStatus == NewStatus::Inherit && targetIsMaster))
	{
		this->entries.prepend(entry);
	} else if (this->newOnTop) {
		this->entries.insert(masterIndex == -1 ? 1 : masterIndex + 1, entry);
	} else {
		this->entries.append(entry);
	}
}

void MasterLayout::remove(Id id) {
	auto index = this->indexOf(id);
	if (index == -1) return;

	this->entries.removeAt(index);
	this->hidden.remove(id);
}

void MasterLayout::swap(Id a, Id b) {
	auto indexA = this->indexOf(a);
	auto indexB = this->indexOf(b);
	if (indexA == -1 || indexB == -1 || indexA == indexB) return;
	std::swap(this->entries[indexA].id, this->entries[indexB].id);
}

bool MasterLayout::replace(Id id, Id with) {
	auto index = this->indexOf(id);
	if (index == -1) return false;
	this->entries[index].id = with;
	this->hidden.remove(id);
	return true;
}

void MasterLayout::setHidden(Id id, bool hidden) {
	if (hidden && this->contains(id)) this->hidden.insert(id);
	else this->hidden.remove(id);
}

bool MasterLayout::splitRatio(Id id, double value, bool exact) {
	if (!this->contains(id)) return false;
	this->mfact = clampRatio(exact ? value / 2.0 : this->mfact + value / 2.0);
	return true;
}

void MasterLayout::configure(const Options& options) {
	this->newStatus = options.newStatus;
	this->newOnTop = options.newOnTop;
	this->orientation = options.orientation;

	if (options.mfact != this->configuredMfact) {
		this->configuredMfact = options.mfact;
		this->mfact = clampRatio(options.mfact);
	}
}

void MasterLayout::layoutColumn(const QList<qsizetype>& column, const QRect& rect, bool vertical) {
	auto total = 0.0;
	for (auto index: column) total += qMax(this->entries[index].weight, 0.01);

	auto length = vertical ? rect.height() : rect.width();
	auto start = vertical ? rect.top() : rect.left();
	auto previous = start;
	auto sum = 0.0;

	for (qsizetype i = 0; i < column.length(); i++) {
		auto& entry = this->entries[column[i]];
		sum += qMax(entry.weight, 0.01);

		auto end = i == column.length() - 1
		             ? start + length
		             : start + static_cast<int>(std::lround(length * sum / total));

		entry.box = vertical ? QRect(rect.left(), previous, rect.width(), end - previous)
		                     : QRect(previous, rect.top(), end - previous, rect.height());
		previous = end;
	}
}

void MasterLayout::compute(const QRect& area) {
	this->mArea = area;
	this->masterIndex = -1;
	this->columns.clear();
	this->columnSides.clear();

	for (auto& entry: this->entries) entry.box = QRect();

	QList<qsizetype> visible;
	for (qsizetype i = 0; i < this->entries.length(); i++) {
		if (!this->hidden.contains(this->entries[i].id)) visible.append(i);
	}

	if (visible.isEmpty()) return;

	this->masterIndex = visible.takeFirst();
	auto& master = this->entries[this->masterIndex];

	if (visible.isEmpty()) {
		master.box = area;
		return;
	}

	this->effective = this->orientation;
	if (this->effective == Orientation::Center && visible.length() < 2) {
		this->effective = Orientation::Left;
	}

	auto width = area.width();
	auto height = area.height();
	auto masterWidth = static_cast<int>(std::lround(width * this->mfact));
	auto masterHeight = static_cast<int>(std::lround(height * this->mfact));

	switch (this->effective) {
	case Orientation::Left:
		master.box = QRect(area.left(), area.top(), masterWidth, height);
		this->columns.append(visible);
		this->columnSides.append(Edge::Right);
		this->layoutColumn(
		    visible,
		    QRect(area.left() + masterWidth, area.top(), width - masterWidth, height),
		    true
		);
		break;
	case Orientation::Right:
		master.box = QRect(area.left() + width - masterWidth, area.top(), masterWidth, height);
		this->columns.append(visible);
		this->columnSides.append(Edge::Left);
		this->layoutColumn(visible, QRect(area.left(), area.top(), width - masterWidth, height), true);
		break;
	case Orientation::Top:
		master.box = QRect(area.left(), area.top(), width, masterHeight);
		this->columns.append(visible);
		this->columnSides.append(Edge::Bottom);
		this->layoutColumn(
		    visible,
		    QRect(area.left(), area.top() + masterHeight, width, height - masterHeight),
		    false
		);
		break;
	case Orientation::Bottom:
		master.box = QRect(area.left(), area.top() + height - masterHeight, width, masterHeight);
		this->columns.append(visible);
		this->columnSides.append(Edge::Top);
		this->layoutColumn(visible, QRect(area.left(), area.top(), width, height - masterHeight), false);
		break;
	case Orientation::Center: {
		auto sides = width - masterWidth;
		auto leftWidth = sides / 2;
		auto rightWidth = sides - leftWidth;
		master.box = QRect(area.left() + leftWidth, area.top(), masterWidth, height);

		QList<qsizetype> right;
		QList<qsizetype> left;
		for (qsizetype i = 0; i < visible.length(); i++) {
			if (i % 2 == 0) right.append(visible[i]);
			else left.append(visible[i]);
		}

		this->columns.append(right);
		this->columnSides.append(Edge::Right);
		this->columns.append(left);
		this->columnSides.append(Edge::Left);
		this->layoutColumn(
		    right,
		    QRect(area.left() + leftWidth + masterWidth, area.top(), rightWidth, height),
		    true
		);
		this->layoutColumn(left, QRect(area.left(), area.top(), leftWidth, height), true);
		break;
	}
	}
}

QRect MasterLayout::box(Id id) const {
	auto index = this->indexOf(id);
	return index == -1 ? QRect() : this->entries[index].box;
}

std::optional<MasterLayout::Divider> MasterLayout::dividerAt(Id id, Edge edge) const {
	auto index = this->indexOf(id);
	if (index == -1 || this->masterIndex == -1 || this->hidden.contains(id)) return std::nullopt;

	if (index == this->masterIndex) {
		for (qsizetype c = 0; c < this->columns.length(); c++) {
			if (this->columnSides[c] == edge && !this->columns[c].isEmpty()) {
				return Divider {.master = true, .side = edge};
			}
		}

		return std::nullopt;
	}

	for (qsizetype c = 0; c < this->columns.length(); c++) {
		const auto& column = this->columns[c];
		auto position = column.indexOf(index);
		if (position == -1) continue;

		auto side = this->columnSides[c];
		if (edge == opposite(side)) return Divider {.master = true, .side = side};

		auto vertical = isVerticalEdge(side);
		auto leading = vertical ? Edge::Top : Edge::Left;
		auto trailing = vertical ? Edge::Bottom : Edge::Right;

		if (edge == leading && position > 0) {
			return Divider {.before = column[position - 1], .after = index, .vertical = vertical};
		}

		if (edge == trailing && position < column.length() - 1) {
			return Divider {.before = index, .after = column[position + 1], .vertical = vertical};
		}

		return std::nullopt;
	}

	return std::nullopt;
}

void MasterLayout::moveDivider(const Divider& divider, int position) {
	const auto& area = this->mArea;

	if (divider.master) {
		if (area.width() <= 0 || area.height() <= 0) return;

		auto size = 0.0;
		auto whole = 1.0;

		if (this->effective == Orientation::Center) {
			auto center = area.left() + area.width() / 2.0;
			size = 2.0 * (divider.side == Edge::Right ? position - center : center - position);
			whole = area.width();
		} else {
			switch (divider.side) {
			case Edge::Right:
				size = position - area.left();
				whole = area.width();
				break;
			case Edge::Left:
				size = rightOf(area) - position;
				whole = area.width();
				break;
			case Edge::Bottom:
				size = position - area.top();
				whole = area.height();
				break;
			case Edge::Top:
				size = bottomOf(area) - position;
				whole = area.height();
				break;
			}
		}

		this->mfact = clampRatio(size / whole);
		return;
	}

	auto& before = this->entries[divider.before];
	auto& after = this->entries[divider.after];

	auto start = divider.vertical ? before.box.top() : before.box.left();
	auto combined = divider.vertical ? before.box.height() + after.box.height()
	                                 : before.box.width() + after.box.width();
	if (combined <= 0) return;

	auto share = clampRatio(static_cast<double>(position - start) / combined);
	auto total = qMax(before.weight, 0.01) + qMax(after.weight, 0.01);
	before.weight = total * share;
	after.weight = total - before.weight;
}

bool MasterLayout::moveEdge(Id id, Edge edge, int position) {
	auto divider = this->dividerAt(id, edge);
	if (!divider) return false;

	this->moveDivider(*divider, position);
	return true;
}

bool MasterLayout::hasEdge(Id id, Edge edge) const { return this->dividerAt(id, edge).has_value(); }

bool MasterLayout::resize(Id id, int dx, int dy) {
	auto index = this->indexOf(id);
	if (index == -1) return false;

	const auto box = this->entries[index].box;
	auto changed = false;

	auto grow = [&](int delta, Edge trailing, Edge leading) {
		if (delta == 0) return;

		if (auto divider = this->dividerAt(id, trailing)) {
			this->moveDivider(*divider, edgeOf(box, trailing) + delta);
			changed = true;
		} else if (auto divider = this->dividerAt(id, leading)) {
			this->moveDivider(*divider, edgeOf(box, leading) - delta);
			changed = true;
		}
	};

	grow(dx, Edge::Right, Edge::Left);
	grow(dy, Edge::Bottom, Edge::Top);
	return changed;
}


namespace {

double clampWidth(double width) {
	return qBound(ScrollingLayout::MIN_WIDTH, width, ScrollingLayout::MAX_WIDTH);
}

qint64 gapDistance(const QRect& a, const QRect& b) {
	auto dx = qMax(0, qMax(a.left() - rightOf(b), b.left() - rightOf(a)));
	auto dy = qMax(0, qMax(a.top() - bottomOf(b), b.top() - bottomOf(a)));
	return qint64(dx) * dx + qint64(dy) * dy;
}

bool overlaps(const QRect& a, const QRect& b) {
	return a.left() < rightOf(b) && b.left() < rightOf(a) && a.top() < bottomOf(b)
	    && b.top() < bottomOf(a);
}

} // namespace

const QList<double>& ScrollingLayout::presets() {
	static const QList<double> list {1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0};
	return list;
}

QList<LayoutEngine::Id> ScrollingLayout::ids() const {
	QList<Id> out;
	for (const auto& column: this->columns) {
		for (const auto& window: column.windows) out.append(window.id);
	}
	return out;
}

QList<LayoutEngine::Id> ScrollingLayout::column(qsizetype index) const {
	QList<Id> out;
	if (index < 0 || index >= this->columns.length()) return out;
	for (const auto& window: this->columns[index].windows) out.append(window.id);
	return out;
}

double ScrollingLayout::columnWidth(qsizetype index) const {
	if (index < 0 || index >= this->columns.length()) return 0.0;
	return this->columns[index].width;
}

ScrollingLayout::Position ScrollingLayout::find(Id id) const {
	if (id == 0) return {};

	for (qsizetype c = 0; c < this->columns.length(); c++) {
		const auto& windows = this->columns[c].windows;
		for (qsizetype r = 0; r < windows.length(); r++) {
			if (windows[r].id == id) return {.column = c, .row = r};
		}
	}

	return {};
}

bool ScrollingLayout::columnVisible(qsizetype index) const {
	for (const auto& window: this->columns[index].windows) {
		if (!this->hidden.contains(window.id)) return true;
	}
	return false;
}

qsizetype ScrollingLayout::nextVisible(qsizetype from, int step, bool wrap) const {
	auto count = this->columns.length();
	auto index = from;

	for (qsizetype n = 0; n < count; n++) {
		index += step;
		if (index < 0 || index >= count) {
			if (!wrap) return -1;
			index = (index + count) % count;
		}
		if (index == from) return -1;
		if (this->columnVisible(index)) return index;
	}

	return -1;
}

qsizetype ScrollingLayout::nextRow(const Position& position, int step, bool wrap) const {
	const auto& windows = this->columns[position.column].windows;
	auto count = windows.length();
	auto index = position.row;

	for (qsizetype n = 0; n < count; n++) {
		index += step;
		if (index < 0 || index >= count) {
			if (!wrap) return -1;
			index = (index + count) % count;
		}
		if (index == position.row) return -1;
		if (!this->hidden.contains(windows[index].id)) return index;
	}

	return -1;
}

LayoutEngine::Id ScrollingLayout::focusedIn(qsizetype index) const {
	if (index < 0 || index >= this->columns.length()) return 0;

	const auto& column = this->columns[index];
	for (const auto& window: column.windows) {
		if (window.id == column.focused && !this->hidden.contains(window.id)) return window.id;
	}

	for (const auto& window: column.windows) {
		if (!this->hidden.contains(window.id)) return window.id;
	}

	return column.windows.isEmpty() ? 0 : column.windows.first().id;
}

qsizetype ScrollingLayout::activeColumn() const { return this->find(this->activeId).column; }

ScrollingLayout::Metrics ScrollingLayout::metrics() const {
	Metrics m;
	auto width = this->mArea.isValid() ? this->mArea.width() : 0;
	m.view = qMax(1, width - 2 * this->gapsOut);
	m.base = qMax(1, m.view + 2 * this->gapsIn);

	auto cumulative = 0.0;
	auto previous = 0;
	auto any = false;

	for (qsizetype i = 0; i < this->columns.length(); i++) {
		m.x.append(previous);

		if (!this->columnVisible(i)) {
			m.width.append(0);
			continue;
		}

		cumulative += this->columns[i].width;
		auto next = static_cast<int>(std::lround(cumulative * m.base));
		auto pitch = qMax(2 * this->gapsIn + 1, next - previous);
		m.width.append(pitch - 2 * this->gapsIn);
		m.strip = previous + pitch - 2 * this->gapsIn;
		previous += pitch;
		any = true;
	}

	if (!any) m.strip = 0;
	return m;
}

int ScrollingLayout::viewX(const Metrics& m) const {
	auto active = this->activeColumn();
	return (active == -1 ? 0 : m.x[active]) + this->offset;
}

void ScrollingLayout::setViewX(const Metrics& m, int view) {
	auto active = this->activeColumn();
	this->offset = view - (active == -1 ? 0 : m.x[active]);
}

int ScrollingLayout::view() const { return this->viewX(this->metrics()); }

bool ScrollingLayout::fullyVisible(const Metrics& m, qsizetype index, int view) const {
	return m.x[index] >= view && m.x[index] + m.width[index] <= view + m.view;
}

int ScrollingLayout::fitView(const Metrics& m, qsizetype index, int view) {
	auto x = m.x[index];
	auto width = m.width[index];
	if (width >= m.view) return x;
	if (x >= view && x + width <= view + m.view) return view;

	auto leftAligned = x;
	auto rightAligned = x + width - m.view;
	return qAbs(view - leftAligned) <= qAbs(view - rightAligned) ? leftAligned : rightAligned;
}

int ScrollingLayout::centerView(const Metrics& m, qsizetype index) {
	auto width = m.width[index];
	if (width >= m.view) return m.x[index];
	return m.x[index] - (m.view - width) / 2;
}

int ScrollingLayout::clampView(const Metrics& m, int view) const {
	if (this->mCenterMode == CenterMode::Always) return view;
	return qBound(0, view, qMax(0, m.strip - m.view));
}

int ScrollingLayout::targetView(const Metrics& m, qsizetype from, qsizetype to, int view) const {
	if (!this->columnVisible(to)) return view;
	if (this->mCenterMode == CenterMode::Always) return ScrollingLayout::centerView(m, to);
	if (this->fullyVisible(m, to, view)) return view;

	auto fitted = this->clampView(m, ScrollingLayout::fitView(m, to, view));
	if (this->mCenterMode == CenterMode::Never || from < 0 || from == to) return fitted;

	auto source = this->nextVisible(to, from > to ? 1 : -1, false);
	if (source == -1) return fitted;

	auto left = qMin(m.x[source], m.x[to]);
	auto right = qMax(m.x[source] + m.width[source], m.x[to] + m.width[to]);
	if (right - left <= m.view) return fitted;

	return this->clampView(m, ScrollingLayout::centerView(m, to));
}

int ScrollingLayout::screenLeft(const Metrics& m, qsizetype index, int view) const {
	return this->mArea.left() + this->gapsOut + m.x[index] - view;
}

QList<QRect> ScrollingLayout::frames(qsizetype index, int left, int width) const {
	const auto& windows = this->columns[index].windows;
	QList<QRect> out(windows.length());

	auto total = 0.0;
	qsizetype count = 0;
	for (const auto& window: windows) {
		if (this->hidden.contains(window.id)) continue;
		total += qMax(window.weight, 0.01);
		count++;
	}

	if (count == 0) return out;

	auto top = this->mArea.top() + this->gapsOut;
	auto gaps = static_cast<int>(count - 1) * 2 * this->gapsIn;
	auto available = qMax(static_cast<int>(count), this->mArea.height() - 2 * this->gapsOut - gaps);
	auto sum = 0.0;
	auto previous = 0;
	qsizetype seen = 0;

	for (qsizetype i = 0; i < windows.length(); i++) {
		if (this->hidden.contains(windows[i].id)) continue;

		sum += qMax(windows[i].weight, 0.01);
		seen++;

		auto end = seen == count ? available : static_cast<int>(std::lround(available * sum / total));
		auto y = top + previous + static_cast<int>(seen - 1) * 2 * this->gapsIn;
		out[i] = QRect(left, y, width, qMax(1, end - previous));
		previous = end;
	}

	return out;
}

bool ScrollingLayout::nearestIsOwn(const QRect& rect) const {
	auto own = this->monitor.isValid() ? this->monitor : this->mArea;
	auto distance = gapDistance(rect, own);

	for (const auto& other: this->others) {
		if (gapDistance(rect, other) < distance) return false;
	}

	return true;
}

bool ScrollingLayout::crossesOther(const QRect& rect) const {
	for (const auto& other: this->others) {
		if (overlaps(rect, other)) return true;
	}
	return false;
}

QRect ScrollingLayout::parkedFrame(const QRect& frame, bool left) const {
	auto own = this->monitor.isValid() ? this->monitor : this->mArea;
	auto bounds = own;
	for (const auto& other: this->others) bounds = bounds.united(other);

	auto width = frame.width();
	auto height = frame.height();
	auto side = [&](bool toLeft) {
		auto x = toLeft ? bounds.left() - PARK_MARGIN - width : rightOf(bounds) + PARK_MARGIN;
		return QRect(x, frame.top(), width, height);
	};

	auto x = own.left() + (own.width() - width) / 2;
	const auto candidates = std::array {
	    side(left),
	    QRect(x, bottomOf(bounds) + PARK_MARGIN, width, height),
	    QRect(x, bounds.top() - PARK_MARGIN - height, width, height),
	    side(!left),
	};

	for (const auto& candidate: candidates) {
		if (this->nearestIsOwn(candidate)) return candidate;
	}

	return candidates[0];
}

void ScrollingLayout::setWidth(qsizetype index, double width) {
	auto& column = this->columns[index];
	column.width = clampWidth(width);
	column.restoreWidth = 0.0;
}

void ScrollingLayout::setWeights(qsizetype index, qsizetype upper, qsizetype lower, double share) {
	auto& windows = this->columns[index].windows;
	auto total = qMax(windows[upper].weight, 0.01) + qMax(windows[lower].weight, 0.01);
	windows[upper].weight = total * clampRatio(share);
	windows[lower].weight = total - windows[upper].weight;
}

ScrollingLayout::Window ScrollingLayout::takeWindow(const Position& position) {
	auto& column = this->columns[position.column];
	auto window = column.windows.takeAt(position.row);

	if (column.windows.isEmpty()) {
		this->columns.removeAt(position.column);
	} else if (column.focused == window.id) {
		column.focused = column.windows[qMin(position.row, column.windows.length() - 1)].id;
	}

	return window;
}

void ScrollingLayout::insertColumn(qsizetype index, const Window& window, double width) {
	auto column = Column();
	column.windows.append(Window {.id = window.id, .weight = 1.0});
	column.width = clampWidth(width);
	column.focused = window.id;
	this->columns.insert(qBound(qsizetype(0), index, this->columns.length()), column);
}

void ScrollingLayout::insert(Id id, Id target, const QPoint* /*cursor*/) {
	if (this->contains(id)) return;

	auto anchor = this->find(target).column;
	if (anchor == -1) anchor = this->activeColumn();
	auto index = anchor == -1 ? this->columns.length() : anchor + 1;

	this->insertColumn(index, Window {.id = id, .weight = 1.0}, this->defaultWidth);

	if (this->activeColumn() == -1) {
		this->activeId = id;
		this->offset = 0;

		if (this->mCenterMode == CenterMode::Always) {
			auto m = this->metrics();
			this->setViewX(m, ScrollingLayout::centerView(m, this->activeColumn()));
		}
	}

	this->openedChild = id;
	this->openedParent = this->activeId == id ? 0 : this->activeId;
}

void ScrollingLayout::remove(Id id) {
	auto position = this->find(id);
	if (position.column == -1) return;

	auto before = this->metrics();
	auto view = this->viewX(before);
	auto wasActive = id == this->activeId;

	this->hidden.remove(id);
	this->parked.remove(id);
	this->boxes.remove(id);
	this->visible.remove(id);

	auto c = position.column;
	auto width = before.width[c];
	auto alone = this->columns[c].windows.length() == 1;

	auto returnTo = Id(0);
	if (wasActive && this->openedChild == id && this->openedParent != id
	    && this->contains(this->openedParent))
	{
		returnTo = this->openedParent;
	}

	if (wasActive || this->openedChild == id || this->openedParent == id) {
		this->openedChild = 0;
		this->openedParent = 0;
	}

	this->takeWindow(position);

	if (alone && width > 0 && before.x[c] + width <= view) view -= width + 2 * this->gapsIn;

	if (this->columns.isEmpty()) {
		this->activeId = 0;
		this->offset = 0;
		return;
	}

	if (wasActive) {
		if (returnTo != 0) {
			this->activeId = returnTo;
		} else {
			auto next = qMin(c, this->columns.length() - 1);
			if (!alone) next = c;
			if (!this->columnVisible(next)) {
				auto other = this->nextVisible(next, -1, false);
				if (other == -1) other = this->nextVisible(next, 1, false);
				if (other != -1) next = other;
			}
			this->activeId = this->focusedIn(next);
		}
	}

	auto after = this->metrics();
	auto active = this->activeColumn();
	if (wasActive && this->mCenterMode == CenterMode::Always && active != -1) {
		this->setViewX(after, ScrollingLayout::centerView(after, active));
	} else {
		this->setViewX(after, this->clampView(after, view));
	}
}

void ScrollingLayout::swap(Id a, Id b) {
	auto positionA = this->find(a);
	auto positionB = this->find(b);
	if (positionA.column == -1 || positionB.column == -1 || a == b) return;

	auto view = this->viewX(this->metrics());

	std::swap(
	    this->columns[positionA.column].windows[positionA.row].id,
	    this->columns[positionB.column].windows[positionB.row].id
	);

	if (positionA.column != positionB.column) {
		auto& columnA = this->columns[positionA.column];
		auto& columnB = this->columns[positionB.column];
		if (columnA.focused == a) columnA.focused = b;
		if (columnB.focused == b) columnB.focused = a;
	}

	this->setViewX(this->metrics(), view);
}

bool ScrollingLayout::replace(Id id, Id with) {
	auto position = this->find(id);
	if (position.column == -1) return false;

	auto& column = this->columns[position.column];
	column.windows[position.row].id = with;
	if (column.focused == id) column.focused = with;
	if (this->activeId == id) this->activeId = with;
	if (this->openedChild == id) this->openedChild = with;
	if (this->openedParent == id) this->openedParent = with;

	this->hidden.remove(id);
	this->parked.remove(id);
	this->boxes.remove(id);
	this->visible.remove(id);
	return true;
}

void ScrollingLayout::setHidden(Id id, bool hidden) {
	if (hidden && this->contains(id)) this->hidden.insert(id);
	else this->hidden.remove(id);
}

void ScrollingLayout::configure(const Options& options) {
	this->defaultWidth = clampWidth(options.columnWidth);
	this->mCenterMode = options.centerMode;

	QList<double> widths;
	for (auto width: options.widthPresets) widths.append(clampWidth(width));
	std::sort(widths.begin(), widths.end());
	auto close = [](double a, double b) { return b - a < 0.005; };
	widths.erase(std::unique(widths.begin(), widths.end(), close), widths.end());
	this->widthPresets = widths.isEmpty() ? ScrollingLayout::presets() : widths;
}

void ScrollingLayout::setGaps(int in, int out) {
	this->gapsIn = qMax(0, in);
	this->gapsOut = qMax(0, out);
}

void ScrollingLayout::setMonitors(const QRect& monitor, const QList<QRect>& others) {
	this->monitor = monitor;
	this->others = others;
}

void ScrollingLayout::compute(const QRect& area) {
	this->mArea = area;
	this->boxes.clear();
	this->visible.clear();
	this->parked.clear();

	if (this->columns.isEmpty()) return;

	if (this->activeColumn() == -1) {
		auto first = this->columnVisible(0) ? 0 : this->nextVisible(0, 1, false);
		this->activeId = this->focusedIn(first == -1 ? 0 : first);
		this->offset = 0;
	}

	auto m = this->metrics();
	auto view = this->viewX(m);
	auto active = this->activeColumn();

	if (active != -1 && this->columnVisible(active) && !this->fullyVisible(m, active, view)) {
		view = ScrollingLayout::fitView(m, active, view);
		this->setViewX(m, view);
	}

	for (qsizetype c = 0; c < this->columns.length(); c++) {
		if (!this->columnVisible(c)) continue;

		auto left = this->screenLeft(m, c, view);
		auto width = m.width[c];
		auto rows = this->frames(c, left, width);

		auto inside = left >= area.left() - 1 && left + width <= rightOf(area) + 1;
		auto partial = !inside && left < rightOf(area) && left + width > area.left();

		auto span = QRect();
		for (const auto& row: rows) {
			if (!row.isNull()) span = span.united(row);
		}

		auto park = c != active && !inside && (!partial || this->crossesOther(span));
		auto toLeft = left + width / 2 < area.left() + area.width() / 2;

		for (qsizetype i = 0; i < rows.length(); i++) {
			const auto& row = rows[i];
			if (row.isNull()) continue;

			auto id = this->columns[c].windows[i].id;
			auto frame = park ? this->parkedFrame(row, toLeft) : row;
			auto gap = this->gapsIn;
			this->visible.insert(id, row.adjusted(-gap, -gap, gap, gap));
			this->boxes.insert(id, frame.adjusted(-gap, -gap, gap, gap));
			if (park) this->parked.insert(id);
		}
	}
}

bool ScrollingLayout::splitRatio(Id id, double value, bool exact) {
	return this->setColumnWidth(id, value, !exact);
}

bool ScrollingLayout::moveEdge(Id id, Edge edge, int position) {
	auto at = this->find(id);
	if (at.column == -1 || this->hidden.contains(id)) return false;

	auto c = at.column;
	auto m = this->metrics();
	auto view = this->viewX(m);
	auto left = this->screenLeft(m, c, view);
	auto width = m.width[c];

	switch (edge) {
	case Edge::Right: {
		auto newWidth = position - this->gapsIn - left;
		this->setWidth(c, static_cast<double>(newWidth + 2 * this->gapsIn) / m.base);
		this->setViewX(this->metrics(), view);
		return true;
	}
	case Edge::Left: {
		auto newWidth = left + width - (position + this->gapsIn);
		this->setWidth(c, static_cast<double>(newWidth + 2 * this->gapsIn) / m.base);
		auto after = this->metrics();
		this->setViewX(after, view + after.width[c] - width);
		return true;
	}
	case Edge::Top:
	case Edge::Bottom: {
		auto other = this->nextRow(at, edge == Edge::Top ? -1 : 1, false);
		if (other == -1) return false;

		auto rows = this->frames(c, left, width);
		auto upper = edge == Edge::Top ? other : at.row;
		auto lower = edge == Edge::Top ? at.row : other;
		auto combined = rows[upper].height() + rows[lower].height();
		if (combined <= 0) return false;

		auto share = static_cast<double>(position - this->gapsIn - rows[upper].top()) / combined;
		this->setWeights(c, upper, lower, share);
		return true;
	}
	}

	return false;
}

bool ScrollingLayout::hasEdge(Id id, Edge edge) const {
	auto at = this->find(id);
	if (at.column == -1 || this->hidden.contains(id)) return false;
	if (isVerticalEdge(edge)) return true;
	return this->nextRow(at, edge == Edge::Top ? -1 : 1, false) != -1;
}

bool ScrollingLayout::resize(Id id, int dx, int dy) {
	auto at = this->find(id);
	if (at.column == -1 || this->hidden.contains(id)) return false;

	auto c = at.column;
	auto changed = false;

	if (dx != 0) {
		auto m = this->metrics();
		auto view = this->viewX(m);
		this->setWidth(c, static_cast<double>(m.width[c] + dx + 2 * this->gapsIn) / m.base);
		this->setViewX(this->metrics(), view);
		changed = true;
	}

	if (dy != 0) {
		auto m = this->metrics();
		auto view = this->viewX(m);
		auto rows = this->frames(c, this->screenLeft(m, c, view), m.width[c]);
		auto below = this->nextRow(at, 1, false);
		auto above = this->nextRow(at, -1, false);

		if (below != -1) {
			auto combined = rows[at.row].height() + rows[below].height();
			if (combined > 0) {
				auto share = static_cast<double>(rows[at.row].height() + dy) / combined;
				this->setWeights(c, at.row, below, share);
				changed = true;
			}
		} else if (above != -1) {
			auto combined = rows[above].height() + rows[at.row].height();
			if (combined > 0) {
				auto share = static_cast<double>(rows[above].height() - dy) / combined;
				this->setWeights(c, above, at.row, share);
				changed = true;
			}
		}
	}

	return changed;
}

bool ScrollingLayout::activate(Id id) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	this->columns[at.column].focused = id;
	if (this->openedChild != id) {
		this->openedChild = 0;
		this->openedParent = 0;
	}

	if (id == this->activeId) return false;

	auto m = this->metrics();
	auto view = this->viewX(m);
	auto from = this->activeColumn();
	this->activeId = id;

	if (from == at.column) {
		this->setViewX(m, view);
		return true;
	}

	this->setViewX(m, this->targetView(m, from, at.column, view));
	return true;
}

LayoutEngine::Id ScrollingLayout::neighbor(Id id, Edge direction, bool wrap) const {
	auto at = this->find(id);
	if (at.column == -1) return 0;

	if (isVerticalEdge(direction)) {
		auto c = this->nextVisible(at.column, direction == Edge::Left ? -1 : 1, wrap);
		return c == -1 ? 0 : this->focusedIn(c);
	}

	auto row = this->nextRow(at, direction == Edge::Top ? -1 : 1, wrap);
	return row == -1 ? 0 : this->columns[at.column].windows[row].id;
}

bool ScrollingLayout::moveWindow(Id id, Edge direction) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	if (!isVerticalEdge(direction)) {
		auto row = this->nextRow(at, direction == Edge::Top ? -1 : 1, false);
		if (row == -1) return false;
		this->columns[at.column].windows.swapItemsAt(at.row, row);
		return true;
	}

	auto view = this->viewX(this->metrics());
	auto step = direction == Edge::Left ? -1 : 1;

	if (this->columns[at.column].windows.length() > 1) {
		auto width = this->columns[at.column].width;
		auto window = this->takeWindow(at);
		this->insertColumn(step < 0 ? at.column : at.column + 1, window, width);
		this->setViewX(this->metrics(), view);
		return true;
	}

	auto target = this->nextVisible(at.column, step, false);
	if (target == -1) return false;

	this->columns.swapItemsAt(at.column, target);
	this->setViewX(this->metrics(), view);
	return true;
}

bool ScrollingLayout::moveInto(Id id, Edge direction) {
	if (!isVerticalEdge(direction)) return this->moveWindow(id, direction);

	auto at = this->find(id);
	if (at.column == -1) return false;

	auto view = this->viewX(this->metrics());
	auto step = direction == Edge::Left ? -1 : 1;
	auto target = this->nextVisible(at.column, step, false);
	auto alone = this->columns[at.column].windows.length() == 1;

	if (target == -1) {
		if (alone) return true;
		auto width = this->columns[at.column].width;
		auto window = this->takeWindow(at);
		this->insertColumn(step < 0 ? 0 : this->columns.length(), window, width);
		this->setViewX(this->metrics(), view);
		return true;
	}

	auto window = this->takeWindow(at);
	if (alone && target > at.column) target--;

	auto& column = this->columns[target];
	column.windows.append(Window {.id = window.id, .weight = 1.0});
	column.focused = window.id;
	this->setViewX(this->metrics(), view);
	return true;
}

bool ScrollingLayout::swapColumn(Id id, Edge direction) {
	auto at = this->find(id);
	if (at.column == -1 || !isVerticalEdge(direction) || this->columns.length() < 2) return false;

	auto view = this->viewX(this->metrics());
	auto step = direction == Edge::Left ? -1 : 1;
	auto target = this->nextVisible(at.column, step, false);

	if (target == -1) this->columns.move(at.column, step < 0 ? this->columns.length() - 1 : 0);
	else this->columns.swapItemsAt(at.column, target);

	this->setViewX(this->metrics(), view);
	return true;
}

bool ScrollingLayout::promote(Id id) {
	auto at = this->find(id);
	if (at.column == -1 || this->columns[at.column].windows.length() < 2) return false;

	auto view = this->viewX(this->metrics());
	auto window = this->takeWindow(at);
	this->insertColumn(at.column + 1, window, this->defaultWidth);
	this->setViewX(this->metrics(), view);
	return true;
}

bool ScrollingLayout::center(Id id) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	this->activate(id);
	auto m = this->metrics();
	this->setViewX(m, ScrollingLayout::centerView(m, at.column));
	return true;
}

bool ScrollingLayout::scrollColumns(int delta) {
	auto active = this->activeColumn();
	if (active == -1 || delta == 0) return false;

	auto target = active;
	for (auto n = 0; n < qAbs(delta); n++) {
		auto next = this->nextVisible(target, delta > 0 ? 1 : -1, false);
		if (next == -1) break;
		target = next;
	}

	if (target == active) return false;
	return this->activate(this->focusedIn(target));
}

bool ScrollingLayout::scrollBy(int pixels) {
	auto active = this->activeColumn();
	if (active == -1 || pixels == 0) return false;

	auto m = this->metrics();
	auto view = this->viewX(m) + pixels;

	if (!this->columnVisible(active) || !this->fullyVisible(m, active, view)) {
		qsizetype best = -1;
		for (qsizetype c = 0; c < this->columns.length(); c++) {
			if (!this->columnVisible(c) || !this->fullyVisible(m, c, view)) continue;

			auto distance = qAbs(c - active);
			auto bestDistance = qAbs(best - active);
			auto ahead = pixels > 0 ? c > active : c < active;
			if (best == -1 || distance < bestDistance || (distance == bestDistance && ahead)) best = c;
		}

		if (best != -1) {
			this->activeId = this->focusedIn(best);
			this->openedChild = 0;
			this->openedParent = 0;
		} else if (this->columnVisible(active)) {
			view = ScrollingLayout::fitView(m, active, view);
		}
	}

	this->setViewX(m, view);
	return true;
}

bool ScrollingLayout::fit(Id id, FitMode mode) {
	auto at = this->find(id).column;
	if (at == -1) at = this->activeColumn();
	if (at == -1) return false;

	auto m = this->metrics();
	auto view = this->viewX(m);
	QList<qsizetype> chosen;

	for (qsizetype c = 0; c < this->columns.length(); c++) {
		if (!this->columnVisible(c)) continue;

		auto take = false;
		switch (mode) {
		case FitMode::Active: take = c == at; break;
		case FitMode::All: take = true; break;
		case FitMode::Visible:
			take = m.x[c] < view + m.view && m.x[c] + m.width[c] > view;
			break;
		case FitMode::ToEnd: take = c >= at; break;
		case FitMode::ToBeginning: take = c <= at; break;
		}

		if (take) chosen.append(c);
	}

	if (chosen.isEmpty()) return false;

	for (auto c: chosen) this->setWidth(c, 1.0 / static_cast<double>(chosen.length()));

	auto after = this->metrics();
	this->setViewX(after, after.x[chosen.first()]);
	return true;
}

bool ScrollingLayout::setColumnWidth(Id id, double value, bool relative) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	auto view = this->viewX(this->metrics());
	auto current = this->columns[at.column].width;
	this->setWidth(at.column, relative ? current + value : value);
	this->setViewX(this->metrics(), view);
	return true;
}

bool ScrollingLayout::setAllWidths(double value) {
	if (this->columns.isEmpty()) return false;
	for (qsizetype c = 0; c < this->columns.length(); c++) this->setWidth(c, value);
	return true;
}

bool ScrollingLayout::cycleWidth(Id id, int direction) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	const auto& list = this->widthPresets;
	auto current = this->columns[at.column].width;
	auto next = direction > 0 ? list.first() : list.last();

	if (direction > 0) {
		for (auto preset: list) {
			if (preset > current + 0.01) {
				next = preset;
				break;
			}
		}
	} else {
		for (auto i = list.length() - 1; i >= 0; i--) {
			if (list[i] < current - 0.01) {
				next = list[i];
				break;
			}
		}
	}

	return this->setColumnWidth(id, next, false);
}

bool ScrollingLayout::toggleFullWidth(Id id) {
	auto at = this->find(id);
	if (at.column == -1) return false;

	auto c = at.column;
	auto view = this->viewX(this->metrics());
	auto restore = this->columns[c].restoreWidth;
	auto current = this->columns[c].width;

	if (restore > 0.0) {
		this->setWidth(c, restore);
	} else if (current >= MAX_WIDTH - 0.001) {
		this->setWidth(c, this->defaultWidth);
	} else {
		this->setWidth(c, MAX_WIDTH);
		this->columns[c].restoreWidth = current;
	}

	this->setViewX(this->metrics(), view);
	return true;
}

} // namespace qs::windows::tiling
