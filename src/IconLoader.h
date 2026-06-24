#ifndef ICONLOADER_H
#define ICONLOADER_H

#include <QIcon>
#include <QString>
#include <QColor>

// Carrega ícones SVG embutidos e os recolore para a cor desejada.
// O Qt renderiza "currentColor" como preto, então fazemos a substituição
// manualmente no texto do SVG antes de rasterizar. Assim os mesmos ícones
// funcionam tanto no tema claro quanto no escuro.
namespace IconLoader {

// Retorna um QIcon do recurso :/icons/<name>.svg pintado com `color`.
QIcon load(const QString &name, const QColor &color);

}

#endif // ICONLOADER_H
