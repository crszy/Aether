#pragma once

#include <memory>
#include <optional>

#include <qhash.h>
#include <qlist.h>
#include <qpoint.h>
#include <qrect.h>
#include <qset.h>
#include <qtypes.h>

namespace qs::windows::tiling {

enum class Edge : quint8 { Left, Right, Top, Bottom };

enum class NewStatus : quint8 { Slave, Master, Inherit };

enum class Orientation : quint8 { Left, Right, Top, Bottom, Center };

enum class Kind : quint8 { Dwindle, Master, Scrolling };

enum class CenterMode : quint8 { Overflow, Always, Never };

enum class FitMode : quint8 { Active, Visible, All, ToEnd, ToBeginning };

struct Options {
	bool preserveSplit = true;
	bool smartSplit = false;
	qint32 forceSplit = 0;
	double defaultSplitRatio = 1.0;
	NewStatus newStatus = NewStatus::Slave;
	bool newOnTop = false;
	Orientation orientation = Orientation::Left;
	double mfact = 0.55;
	double columnWidth = 0.5;
	QList<double> widthPresets;
	CenterMode centerMode = CenterMode::Overflow;
};

class LayoutEngine {
public:
	using Id = quintptr;

	LayoutEngine() = default;
	virtual ~LayoutEngine() = default;
	LayoutEngine(const LayoutEngine&) = delete;
	LayoutEngine& operator=(const LayoutEngine&) = delete;
	LayoutEngine(LayoutEngine&&) = delete;
	LayoutEngine& operator=(LayoutEngine&&) = delete;

	[[nodiscard]] virtual Kind kind() const = 0;
	[[nodiscard]] virtual bool isEmpty() const = 0;
	[[nodiscard]] virtual bool contains(Id id) const = 0;
	[[nodiscard]] virtual QList<Id> ids() const = 0;

	virtual void insert(Id id, Id target, const QPoint* cursor) = 0;
	virtual void remove(Id id) = 0;
	virtual void swap(Id a, Id b) = 0;
	virtual bool replace(Id id, Id with) = 0;

	virtual bool toggleSplit(Id id) = 0;
	virtual bool swapSplit(Id id) = 0;
	virtual bool splitRatio(Id id, double value, bool exact) = 0;

	virtual bool moveEdge(Id id, Edge edge, int position) = 0;
	[[nodiscard]] virtual bool hasEdge(Id id, Edge edge) const = 0;
	virtual bool resize(Id id, int dx, int dy) = 0;

	virtual void configure(const Options& options) = 0;

	virtual void setHidden(Id id, bool hidden) = 0;
	[[nodiscard]] virtual bool isHidden(Id id) const = 0;

	virtual void compute(const QRect& area) = 0;
	[[nodiscard]] virtual QRect area() const = 0;
	[[nodiscard]] virtual QRect box(Id id) const = 0;
	[[nodiscard]] virtual bool isParked(Id /*id*/) const { return false; }

	[[nodiscard]] static std::unique_ptr<LayoutEngine> create(Kind kind);
};

class DwindleLayout: public LayoutEngine {
public:
	DwindleLayout() = default;
	~DwindleLayout() override = default;
	DwindleLayout(const DwindleLayout&) = delete;
	DwindleLayout& operator=(const DwindleLayout&) = delete;
	DwindleLayout(DwindleLayout&&) = delete;
	DwindleLayout& operator=(DwindleLayout&&) = delete;

	[[nodiscard]] Kind kind() const override { return Kind::Dwindle; }
	[[nodiscard]] bool isEmpty() const override { return this->root == nullptr; }
	[[nodiscard]] bool contains(Id id) const override { return this->find(id) != nullptr; }
	[[nodiscard]] qsizetype count() const;
	[[nodiscard]] QList<Id> ids() const override;

	void insert(Id id, Id target, const QPoint* cursor) override;
	void remove(Id id) override;
	void swap(Id a, Id b) override;
	bool replace(Id id, Id with) override;

	bool toggleSplit(Id id) override;
	bool swapSplit(Id id) override;
	bool splitRatio(Id id, double value, bool exact) override;

	bool moveEdge(Id id, Edge edge, int position) override;
	[[nodiscard]] bool hasEdge(Id id, Edge edge) const override;
	bool resize(Id id, int dx, int dy) override;

	void configure(const Options& options) override;

	void setHidden(Id id, bool hidden) override;
	[[nodiscard]] bool isHidden(Id id) const override { return this->hidden.contains(id); }

	void compute(const QRect& area) override;
	[[nodiscard]] QRect area() const override { return this->mArea; }
	[[nodiscard]] QRect box(Id id) const override;

private:
	struct Node {
		Node* parent = nullptr;
		std::unique_ptr<Node> first;
		std::unique_ptr<Node> second;
		Id id = 0;
		bool splitTop = false;
		bool splitPinned = false;
		double ratio = 0.5;
		QRect box;

		[[nodiscard]] bool isLeaf() const { return this->first == nullptr; }
	};

	[[nodiscard]] Node* find(Id id) const;
	[[nodiscard]] Node* lastLeaf() const;
	[[nodiscard]] Node* dividerAncestor(Node* leaf, Edge edge) const;
	static void setDivider(Node* node, int position);
	void computeNode(Node* node, const QRect& box) const;
	[[nodiscard]] bool hasVisible(const Node* node) const;
	static void collect(const Node* node, QList<Id>& out);

	std::unique_ptr<Node> root;
	QRect mArea;
	bool preserveSplit = true;
	bool smartSplit = false;
	qint32 forceSplit = 0;
	double defaultRatio = 0.5;
	QSet<Id> hidden;
};

class MasterLayout: public LayoutEngine {
public:
	MasterLayout() = default;
	~MasterLayout() override = default;
	MasterLayout(const MasterLayout&) = delete;
	MasterLayout& operator=(const MasterLayout&) = delete;
	MasterLayout(MasterLayout&&) = delete;
	MasterLayout& operator=(MasterLayout&&) = delete;

	[[nodiscard]] Kind kind() const override { return Kind::Master; }
	[[nodiscard]] bool isEmpty() const override { return this->entries.isEmpty(); }
	[[nodiscard]] bool contains(Id id) const override { return this->indexOf(id) != -1; }
	[[nodiscard]] QList<Id> ids() const override;

	void insert(Id id, Id target, const QPoint* cursor) override;
	void remove(Id id) override;
	void swap(Id a, Id b) override;
	bool replace(Id id, Id with) override;

	bool toggleSplit(Id /*id*/) override { return false; }
	bool swapSplit(Id /*id*/) override { return false; }
	bool splitRatio(Id id, double value, bool exact) override;

	bool moveEdge(Id id, Edge edge, int position) override;
	[[nodiscard]] bool hasEdge(Id id, Edge edge) const override;
	bool resize(Id id, int dx, int dy) override;

	void configure(const Options& options) override;

	void setHidden(Id id, bool hidden) override;
	[[nodiscard]] bool isHidden(Id id) const override { return this->hidden.contains(id); }

	void compute(const QRect& area) override;
	[[nodiscard]] QRect area() const override { return this->mArea; }
	[[nodiscard]] QRect box(Id id) const override;

	[[nodiscard]] Id master() const;
	[[nodiscard]] Id firstInStack() const;

private:
	struct Entry {
		Id id = 0;
		double weight = 1.0;
		QRect box;
	};

	struct Divider {
		bool master = false;
		Edge side = Edge::Right;
		qsizetype before = -1;
		qsizetype after = -1;
		bool vertical = true;
	};

	[[nodiscard]] qsizetype indexOf(Id id) const;
	[[nodiscard]] qsizetype firstVisible(qsizetype from) const;
	[[nodiscard]] std::optional<Divider> dividerAt(Id id, Edge edge) const;
	void moveDivider(const Divider& divider, int position);
	void layoutColumn(const QList<qsizetype>& column, const QRect& rect, bool vertical);

	QList<Entry> entries;
	QSet<Id> hidden;
	QRect mArea;
	double mfact = 0.55;
	double configuredMfact = -1;
	NewStatus newStatus = NewStatus::Slave;
	bool newOnTop = false;
	Orientation orientation = Orientation::Left;

	qsizetype masterIndex = -1;
	Orientation effective = Orientation::Left;
	QList<QList<qsizetype>> columns;
	QList<Edge> columnSides;
};

class ScrollingLayout: public LayoutEngine {
public:
	static constexpr double MIN_WIDTH = 0.1;
	static constexpr double MAX_WIDTH = 1.0;
	static constexpr int PARK_MARGIN = 256;

	ScrollingLayout() = default;
	~ScrollingLayout() override = default;
	ScrollingLayout(const ScrollingLayout&) = delete;
	ScrollingLayout& operator=(const ScrollingLayout&) = delete;
	ScrollingLayout(ScrollingLayout&&) = delete;
	ScrollingLayout& operator=(ScrollingLayout&&) = delete;

	[[nodiscard]] Kind kind() const override { return Kind::Scrolling; }
	[[nodiscard]] bool isEmpty() const override { return this->columns.isEmpty(); }
	[[nodiscard]] bool contains(Id id) const override { return this->find(id).column != -1; }
	[[nodiscard]] QList<Id> ids() const override;

	void insert(Id id, Id target, const QPoint* cursor) override;
	void remove(Id id) override;
	void swap(Id a, Id b) override;
	bool replace(Id id, Id with) override;

	bool toggleSplit(Id /*id*/) override { return false; }
	bool swapSplit(Id /*id*/) override { return false; }
	bool splitRatio(Id id, double value, bool exact) override;

	bool moveEdge(Id id, Edge edge, int position) override;
	[[nodiscard]] bool hasEdge(Id id, Edge edge) const override;
	bool resize(Id id, int dx, int dy) override;

	void configure(const Options& options) override;

	void setHidden(Id id, bool hidden) override;
	[[nodiscard]] bool isHidden(Id id) const override { return this->hidden.contains(id); }

	void compute(const QRect& area) override;
	[[nodiscard]] QRect area() const override { return this->mArea; }
	[[nodiscard]] QRect box(Id id) const override { return this->boxes.value(id); }
	[[nodiscard]] bool isParked(Id id) const override { return this->parked.contains(id); }

	void setGaps(int in, int out);
	void setMonitors(const QRect& monitor, const QList<QRect>& others);

	[[nodiscard]] QRect visibleBox(Id id) const { return this->visible.value(id); }
	[[nodiscard]] Id active() const { return this->activeId; }
	[[nodiscard]] qsizetype columnCount() const { return this->columns.length(); }
	[[nodiscard]] qsizetype columnOf(Id id) const { return this->find(id).column; }
	[[nodiscard]] QList<Id> column(qsizetype index) const;
	[[nodiscard]] double columnWidth(qsizetype index) const;
	[[nodiscard]] int view() const;
	[[nodiscard]] CenterMode centerMode() const { return this->mCenterMode; }

	bool activate(Id id);
	[[nodiscard]] Id neighbor(Id id, Edge direction, bool wrap) const;
	bool moveWindow(Id id, Edge direction);
	bool moveInto(Id id, Edge direction);
	bool swapColumn(Id id, Edge direction);
	bool promote(Id id);
	bool center(Id id);
	bool scrollColumns(int delta);
	bool scrollBy(int pixels);
	bool fit(Id id, FitMode mode);
	bool setColumnWidth(Id id, double value, bool relative);
	bool setAllWidths(double value);
	bool cycleWidth(Id id, int direction);
	bool toggleFullWidth(Id id);

	[[nodiscard]] static const QList<double>& presets();

private:
	struct Window {
		Id id = 0;
		double weight = 1.0;
	};

	struct Column {
		QList<Window> windows;
		double width = 0.5;
		double restoreWidth = 0.0;
		Id focused = 0;
	};

	struct Position {
		qsizetype column = -1;
		qsizetype row = -1;
	};

	struct Metrics {
		int base = 1;
		int view = 1;
		int strip = 0;
		QList<int> x;
		QList<int> width;
	};

	[[nodiscard]] Position find(Id id) const;
	[[nodiscard]] bool columnVisible(qsizetype index) const;
	[[nodiscard]] qsizetype nextVisible(qsizetype from, int step, bool wrap) const;
	[[nodiscard]] qsizetype nextRow(const Position& position, int step, bool wrap) const;
	[[nodiscard]] Id focusedIn(qsizetype index) const;
	[[nodiscard]] qsizetype activeColumn() const;
	[[nodiscard]] Metrics metrics() const;
	[[nodiscard]] int viewX(const Metrics& m) const;
	void setViewX(const Metrics& m, int view);
	[[nodiscard]] bool fullyVisible(const Metrics& m, qsizetype index, int view) const;
	[[nodiscard]] static int fitView(const Metrics& m, qsizetype index, int view);
	[[nodiscard]] static int centerView(const Metrics& m, qsizetype index);
	[[nodiscard]] int clampView(const Metrics& m, int view) const;
	[[nodiscard]] int targetView(const Metrics& m, qsizetype from, qsizetype to, int view) const;
	[[nodiscard]] QList<QRect> frames(qsizetype index, int left, int width) const;
	[[nodiscard]] QRect parkedFrame(const QRect& frame, bool left) const;
	[[nodiscard]] bool nearestIsOwn(const QRect& rect) const;
	[[nodiscard]] bool crossesOther(const QRect& rect) const;
	[[nodiscard]] int screenLeft(const Metrics& m, qsizetype index, int view) const;
	void setWidth(qsizetype index, double width);
	void setWeights(qsizetype index, qsizetype upper, qsizetype lower, double share);
	Window takeWindow(const Position& position);
	void insertColumn(qsizetype index, const Window& window, double width);

	QList<Column> columns;
	QSet<Id> hidden;
	QSet<Id> parked;
	QHash<Id, QRect> boxes;
	QHash<Id, QRect> visible;
	QRect mArea;
	QRect monitor;
	QList<QRect> others;
	int gapsIn = 0;
	int gapsOut = 0;
	double defaultWidth = 0.5;
	QList<double> widthPresets = ScrollingLayout::presets();
	CenterMode mCenterMode = CenterMode::Overflow;
	Id activeId = 0;
	Id openedChild = 0;
	Id openedParent = 0;
	int offset = 0;
};

} // namespace qs::windows::tiling
