#include "IconLoader.h"

#include <QFile>
#include <QByteArray>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>

namespace IconLoader {

QIcon load(const QString &name, const QColor &color)
{
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QIcon();

    QByteArray data = file.readAll();
    file.close();

    // Substitui currentColor pela cor pedida (em hex) diretamente no SVG.
    const QString hex = color.name(QColor::HexRgb);
    data.replace("currentColor", hex.toUtf8());

    QSvgRenderer renderer(data);

    // Renderiza em alta resolução (para telas HiDPI) e marca o devicePixelRatio.
    const int logical = 24;
    const qreal dpr = 2.0;
    QPixmap pixmap(static_cast<int>(logical * dpr), static_cast<int>(logical * dpr));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);
    painter.end();

    pixmap.setDevicePixelRatio(dpr);
    return QIcon(pixmap);
}

} // namespace IconLoader
