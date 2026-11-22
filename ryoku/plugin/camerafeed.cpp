#include "camerafeed.hpp"

#include <QCamera>
#include <QMediaDevices>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QVideoFrame>

CameraFeed::CameraFeed(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    m_session.setVideoSink(&m_sink);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) { onFrame(frame); });
}

CameraFeed::~CameraFeed() { stopCapture(); }

void CameraFeed::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    if (active)
        startCapture();
    else
        stopCapture();
    emit activeChanged();
}

void CameraFeed::setMirror(bool mirror) {
    if (m_mirror == mirror)
        return;
    m_mirror = mirror;
    emit mirrorChanged();
    update();
}

void CameraFeed::startCapture() {
    if (m_camera)
        return;
    const QCameraDevice dev = QMediaDevices::defaultVideoInput();
    if (dev.isNull())
        return;
    m_camera = new QCamera(dev, this);
    m_session.setCamera(m_camera);
    m_camera->start();
}

void CameraFeed::stopCapture() {
    if (m_camera) {
        m_camera->stop();
        m_session.setCamera(nullptr);
        delete m_camera;
        m_camera = nullptr;
    }
    m_image = QImage();
    m_dirty = true;
    update();
}

void CameraFeed::onFrame(const QVideoFrame& frame) {
    if (!m_active)
        return;
    QImage img = frame.toImage();
    if (img.isNull())
        return;
    m_image = m_mirror ? img.flipped(Qt::Horizontal) : img;
    m_dirty = true;
    update();
}

QSGNode* CameraFeed::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGImageNode*>(old);
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }
    if (!node)
        node = window()->createImageNode();
    if (m_dirty || !node->texture()) {
        QSGTexture* tex =
            window()->createTextureFromImage(m_image, QQuickWindow::TextureHasAlphaChannel);
        node->setTexture(tex);
        node->setOwnsTexture(true);
        m_dirty = false;
    }

    // preserve-aspect-crop: fill the item rect, sample a centred sub-rect of the frame.
    const QRectF dst(0, 0, width(), height());
    const qreal imgAspect = qreal(m_image.width()) / m_image.height();
    const qreal itemAspect = dst.width() / dst.height();
    QRectF src(0, 0, m_image.width(), m_image.height());
    if (imgAspect > itemAspect) {
        const qreal w = m_image.height() * itemAspect;
        src = QRectF((m_image.width() - w) / 2.0, 0, w, m_image.height());
    } else {
        const qreal h = m_image.width() / itemAspect;
        src = QRectF(0, (m_image.height() - h) / 2.0, m_image.width(), h);
    }
    node->setRect(dst);
    node->setSourceRect(src);
    node->setFiltering(QSGTexture::Linear);
    return node;
}
