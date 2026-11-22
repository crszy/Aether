#pragma once

#include <qquickitem.h>
#undef CAE_MACRO_API
#define CAE_MACRO_API CAE_COMPONENTS_API


namespace caelestia::shell::components {

class CAE_COMPONENTS_API ButtonRow : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)

public:
    explicit ButtonRow(QQuickItem* parent = nullptr);

    [[nodiscard]] qreal spacing() const;
    void setSpacing(qreal spacing);

signals:
    void spacingChanged();

protected:
    void itemChange(QQuickItem::ItemChange change, const QQuickItem::ItemChangeData& data) override;
    void updatePolish() override;

private slots:
    void invalidate();

private:
    void relayout();
    static qreal getMorphExpansion(const QQuickItem* item);

    bool m_dirty;
    qreal m_spacing;
};

} // namespace caelestia::shell::components
