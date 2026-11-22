#include "qalculator.hpp"

#include <qstringlist.h>
#include <qtconcurrentrun.h>

#include <qjsengine.h>
#include <qregularexpression.h>

#include "util/i18n.hpp"

namespace caelestia::shell {

using Qt::StringLiterals::operator""_s;
using util::i18n::mark;

QMutex Qalculator::s_calculatorMutex;

Qalculator::Qalculator(QObject* parent)
    : QObject(parent) {}

QString Qalculator::eval(const QString& expr, bool printExpr) {
    if (expr.isEmpty()) {
        return {};
    }

    const auto [formatted, raw] = evaluate(expr);
    return printExpr ? formatted : raw;
}

void Qalculator::evalAsync(const QString& expr) {
    const auto gen = ++m_generation;

    if (expr.isEmpty()) {
        clearResult();
        setBusy(false);
        return;
    }

    setBusy(true);

    QtConcurrent::run([expr] {
        return evaluate(expr);
    }).then(this, [this, gen](const QPair<QString, QString>& result) {
        applyResult(gen, result);
    });
}

QString Qalculator::result() const {
    return m_result;
}

QString Qalculator::rawResult() const {
    return m_rawResult;
}

bool Qalculator::busy() const {
    return m_busy;
}

// Windows: libqalculate is not available, so plain maths goes through a sandboxed QJSEngine
// (only digits, operators, brackets and Math function names get through).
QPair<QString, QString> Qalculator::evaluate(const QString& expr) {
    const QMutexLocker locker(&s_calculatorMutex);
    static const QRegularExpression allowed(uR"(^[0-9\s+\-*/%^().,a-zA-Z_]*$)"_s);
    if (!allowed.match(expr).hasMatch())
        return { mark(u"error: %1"_s, { u"unsupported characters"_s }), {} };
    static const QRegularExpression word(uR"([A-Za-z_]+)"_s);
    static const QStringList fns = { u"abs"_s, u"sqrt"_s, u"cbrt"_s, u"sin"_s, u"cos"_s, u"tan"_s, u"asin"_s, u"acos"_s,
        u"atan"_s, u"log"_s, u"log2"_s, u"log10"_s, u"exp"_s, u"floor"_s, u"ceil"_s, u"round"_s, u"min"_s, u"max"_s,
        u"pow"_s, u"PI"_s, u"E"_s, u"pi"_s, u"e"_s };
    for (auto it = word.globalMatch(expr); it.hasNext();)
        if (!fns.contains(it.next().captured()))
            return { mark(u"error: %1"_s, { u"unknown name"_s }), {} };
    QString js = expr;
    js.replace(u'^', u"**"_s);
    js.replace(QRegularExpression(uR"(\bpi\b)"_s), u"PI"_s).replace(QRegularExpression(uR"(\be\b)"_s), u"E"_s);
    static QJSEngine engine;
    const QJSValue v = engine.evaluate(u"(function(){ with(Math){ return ("_s + js + u"); } })()"_s);
    if (v.isError() || !v.isNumber())
        return { mark(u"error: %1"_s, { v.toString() }), {} };
    const QString r = QString::number(v.toNumber(), 'g', 12);
    return { u"%1 = %2"_s.arg(expr.trimmed(), r), r };
}

void Qalculator::applyResult(quint64 gen, const QPair<QString, QString>& result) {
    if (gen != m_generation)
        return;

    const auto& [formatted, raw] = result;

    if (m_result != formatted) {
        m_result = formatted;
        emit resultChanged();
    }
    if (m_rawResult != raw) {
        m_rawResult = raw;
        emit rawResultChanged();
    }

    setBusy(false);
}

void Qalculator::clearResult() {
    if (!m_result.isEmpty()) {
        m_result.clear();
        emit resultChanged();
    }
    if (!m_rawResult.isEmpty()) {
        m_rawResult.clear();
        emit rawResultChanged();
    }
}

void Qalculator::setBusy(bool busy) {
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
}

} // namespace caelestia::shell
