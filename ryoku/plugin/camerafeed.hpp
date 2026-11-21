#pragma once

#include <QImage>
#include <QMediaCaptureSession>
#include <QQuickItem>
#include <QVideoSink>
#include <qqmlregistration.h>

class QCamera;
class QVideoFrame;

// Live webcam feed rendered as a scene-graph texture, so it can be masked
// (rounded / circle) and positioned inside a Quickshell layer surface -- unlike
// QtMultimedia's VideoOutput, whose Wayland hardware plane bypasses QML masking
// and mispositions in a sub-surface. Captures the default camera while `active`;
// mirrors horizontally for the selfie feel when `mirror` is set. The frame is
// preserve-aspect-cropped to fill the item.
class CameraFeed : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY mirrorChanged)

public:
    explicit CameraFeed(QQuickItem* parent = nullptr);
    ~CameraFeed() override;

    bool active() const { return m_active; }
    void setActive(bool active);

    bool mirror() const { return m_mirror; }
    void setMirror(bool mirror);

signals:
    void activeChanged();
    void mirrorChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;

private:
    void startCapture();
    void stopCapture();
    void onFrame(const QVideoFrame& frame);

    QMediaCaptureSession m_session;
    QVideoSink m_sink;
    QCamera* m_camera = nullptr;
    QImage m_image;
    bool m_dirty = false;
    bool m_active = false;
    bool m_mirror = true;
};
