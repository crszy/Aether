// Renders an SVG to a multi-size .ico (Qt's ico image plugin writes every image in the list).
//   mkicon.exe logo.svg out.ico [#rrggbb background, optional]
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QList>
#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc < 3) { std::puts("usage: mkicon in.svg out.ico [#bg]"); return 2; }
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) { std::puts("bad svg"); return 1; }
    const QColor bg = argc > 3 ? QColor(QString::fromLatin1(argv[3])) : QColor(Qt::transparent);
    QList<QImage> images;
    for (int s : { 256, 64, 48, 32, 24, 16 }) {
        QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        if (bg.alpha()) { QPainterPath r; r.addRoundedRect(0, 0, s, s, s * 0.22, s * 0.22); p.fillPath(r, bg); }
        const double m = s * 0.14;
        svg.render(&p, QRectF(m, m, s - 2 * m, s - 2 * m));
        p.end();
        images << img;
    }
    QImageWriter w(QString::fromLocal8Bit(argv[2]), "ico");
    for (const auto& img : images) if (!w.write(img)) { std::printf("write failed: %s\n", qPrintable(w.errorString())); return 1; }
    std::puts("ok");
    return 0;
}
