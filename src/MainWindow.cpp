#include "MainWindow.h"
#include "IconLoader.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QLabel>
#include <QSpinBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSlider>
#include <QLineEdit>
#include <QResizeEvent>
#include <cmath>
#include <QCloseEvent>
#include <QFrame>
#include <QFileInfo>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QAction>
#include <QToolBar>
#include <QKeySequence>
#include <QIcon>
#include <QTabWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QPushButton>
#include <QPainterPath>

namespace {

// Widget de pré-visualização: desenha um retângulo com cantos arredondados
// conforme o percentual, para o diálogo "Alterar cantos".
class CornerPreview : public QWidget
{
public:
    explicit CornerPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(280, 180);
    }
    void setPercent(int p) { m_percent = p; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));

        const QRectF box = QRectF(rect()).adjusted(20, 16, -20, -16);
        const qreal maxR = qMin(box.width(), box.height()) / 2.0;
        const qreal rad = maxR * (m_percent / 100.0);

        QPainterPath path;
        if (rad <= 0.0)
            path.addRect(box);
        else
            path.addRoundedRect(box, rad, rad);

        painter.fillPath(path, QColor(0x35, 0x84, 0xe4, 60));
        painter.setPen(QPen(QColor(0x35, 0x84, 0xe4), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

private:
    int m_percent = 0;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Taiul Paint"));
    resize(1080, 720);
    setAcceptDrops(true);

    // Cada aba é uma página independente (um QScrollArea + Canvas).
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("pageTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

    // Botão "+" (nova página) no canto da barra de abas.
    QToolButton *addTabBtn = new QToolButton(m_tabs);
    addTabBtn->setObjectName(QStringLiteral("addTabButton"));
    addTabBtn->setText(QStringLiteral("+"));
    addTabBtn->setAutoRaise(true);
    addTabBtn->setToolTip(QStringLiteral("Nova página"));
    connect(addTabBtn, &QToolButton::clicked, this, &MainWindow::newPage);
    m_tabs->setCornerWidget(addTabBtn, Qt::TopRightCorner);

    setCentralWidget(m_tabs);

    buildToolBar();
    buildZoomOverlay();

    // Página inicial.
    addPage();

    updateColorButton();
}

// ---- Páginas (abas) ----

Canvas *MainWindow::currentCanvas() const
{
    QWidget *page = m_tabs ? m_tabs->currentWidget() : nullptr;
    if (!page)
        return nullptr;
    QScrollArea *area = qobject_cast<QScrollArea *>(page);
    return area ? qobject_cast<Canvas *>(area->widget()) : nullptr;
}

Canvas *MainWindow::addPage(const QString &title, const QImage &initial)
{
    Canvas *canvas = new Canvas;
    if (!initial.isNull())
        canvas->openImageFromData(initial);

    // Aplica os ajustes globais atuais à nova página.
    canvas->setBoundToEdge(m_boundToEdge);
    if (m_widthSpin)
        canvas->setPenWidth(m_widthSpin->value());

    QScrollArea *area = new QScrollArea;
    area->setWidget(canvas);
    area->setAlignment(Qt::AlignCenter);
    area->setObjectName(QStringLiteral("canvasArea"));

    const QString tabName = title.isEmpty() ? QStringLiteral("Sem título") : title;
    const int idx = m_tabs->addTab(area, tabName);
    connectCanvas(canvas);
    m_tabs->setCurrentIndex(idx);
    return canvas;
}

void MainWindow::connectCanvas(Canvas *canvas)
{
    connect(canvas, &Canvas::zoomChanged, this, [this, canvas](qreal f) {
        if (canvas == currentCanvas()) onZoomChanged(f);
    });
    connect(canvas, &Canvas::undoAvailable, this, [this, canvas](bool a) {
        if (canvas == currentCanvas()) m_undoButton->setEnabled(a);
    });
    connect(canvas, &Canvas::redoAvailable, this, [this, canvas](bool a) {
        if (canvas == currentCanvas()) m_redoButton->setEnabled(a);
    });
    connect(canvas, &Canvas::modifiedChanged, this, [this, canvas](bool) {
        updateTabTitle(canvas);
        if (canvas == currentCanvas()) {
            setWindowModified(canvas->isModified());
            if (m_titleLabel) {
                const QString file = canvas->property("filePath").toString();
                const QString base = file.isEmpty() ? QStringLiteral("Sem título")
                                                    : QFileInfo(file).fileName();
                m_titleLabel->setText(base + (canvas->isModified()
                                                  ? QStringLiteral(" •") : QString()));
            }
        }
    });
    connect(canvas, &Canvas::canvasSizeChanged, this, [this, canvas](const QSize &s) {
        if (canvas == currentCanvas()) onCanvasSizeChanged(s);
    });
    connect(canvas, &Canvas::insertImageRequested,
            this, &MainWindow::insertImageFromFile);
}

void MainWindow::updateTabTitle(Canvas *canvas)
{
    const int idx = [&]() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            QScrollArea *a = qobject_cast<QScrollArea *>(m_tabs->widget(i));
            if (a && a->widget() == canvas)
                return i;
        }
        return -1;
    }();
    if (idx < 0)
        return;

    const QString file = canvas->property("filePath").toString();
    const QString base = file.isEmpty() ? QStringLiteral("Sem título")
                                        : QFileInfo(file).fileName();
    m_tabs->setTabText(idx, base + (canvas->isModified() ? QStringLiteral(" •")
                                                         : QString()));
}

void MainWindow::syncUiToCanvas()
{
    Canvas *c = currentCanvas();
    if (!c)
        return;
    onZoomChanged(c->zoom());
    onCanvasSizeChanged(c->canvasSize());
    setWindowModified(c->isModified());

    // Título central reflete a página atual.
    if (m_titleLabel) {
        const QString file = c->property("filePath").toString();
        const QString base = file.isEmpty() ? QStringLiteral("Sem título")
                                            : QFileInfo(file).fileName();
        m_titleLabel->setText(base + (c->isModified() ? QStringLiteral(" •")
                                                      : QString()));
    }

    // Reaplica a ferramenta/cor/espessura selecionadas na UI à página atual.
    if (m_toolGroup && m_toolGroup->checkedId() >= 0)
        c->setTool(static_cast<Canvas::Tool>(m_toolGroup->checkedId()));
    if (m_widthSpin)
        c->setPenWidth(m_widthSpin->value());
    c->setSelectionCornerPercent(m_selectionCorner);
    c->setPenColor(m_lastColor);
}

void MainWindow::onTabChanged(int)
{
    syncUiToCanvas();
}

void MainWindow::newPage()
{
    addPage();
}

void MainWindow::onTabCloseRequested(int index)
{
    QScrollArea *area = qobject_cast<QScrollArea *>(m_tabs->widget(index));
    Canvas *canvas = area ? qobject_cast<Canvas *>(area->widget()) : nullptr;
    if (canvas && !maybeSaveCanvas(canvas))
        return;

    m_tabs->removeTab(index);
    if (area)
        area->deleteLater();

    // Sempre mantém ao menos uma página aberta.
    if (m_tabs->count() == 0)
        addPage();
}

// ---- Barra única: ações + ferramentas + formas + tamanho + cores ----
void MainWindow::buildToolBar()
{
    QWidget *ribbon = new QWidget(this);
    ribbon->setObjectName(QStringLiteral("ribbon"));
    ribbon->setFixedHeight(78);

    QHBoxLayout *layout = new QHBoxLayout(ribbon);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // Cria uma coluna "grupo" (linha de widgets + rótulo embaixo, como no W11)
    // e devolve o layout-coluna pronto para receber widgets na sua linha.
    auto makeGroup = [&](const QString &title, QHBoxLayout *&rowOut) -> QVBoxLayout* {
        QVBoxLayout *col = new QVBoxLayout();
        col->setSpacing(4);
        col->setContentsMargins(0, 0, 0, 0);
        rowOut = new QHBoxLayout();
        rowOut->setSpacing(4);
        col->addLayout(rowOut, 1);
        col->addWidget(makeGroupLabel(title), 0, Qt::AlignHCenter);
        return col;
    };

    // Grupo Arquivo (novo, abrir, salvar)
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Arquivo"), row);

        QToolButton *newBtn = makeActionButton(QStringLiteral("new"),
                                               QStringLiteral("Novo (Ctrl+N)"));
        QToolButton *openBtn = makeActionButton(QStringLiteral("open"),
                                                QStringLiteral("Abrir (Ctrl+O)"));
        QToolButton *saveBtn = makeActionButton(QStringLiteral("save"),
                                                QStringLiteral("Salvar (Ctrl+S)"));
        connect(newBtn, &QToolButton::clicked, this, &MainWindow::newFile);
        connect(openBtn, &QToolButton::clicked, this, &MainWindow::openFile);
        connect(saveBtn, &QToolButton::clicked, this, &MainWindow::saveFile);
        row->addWidget(newBtn);
        row->addWidget(openBtn);
        row->addWidget(saveBtn);
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    // Grupo Histórico (desfazer / refazer)
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Histórico"), row);

        m_undoButton = makeActionButton(QStringLiteral("undo"),
                                        QStringLiteral("Desfazer (Ctrl+Z)"));
        m_redoButton = makeActionButton(QStringLiteral("redo"),
                                        QStringLiteral("Refazer (Ctrl+Y)"));
        connect(m_undoButton, &QToolButton::clicked, this,
                [this]{ if (auto *c = currentCanvas()) c->undo(); });
        connect(m_redoButton, &QToolButton::clicked, this,
                [this]{ if (auto *c = currentCanvas()) c->redo(); });
        row->addWidget(m_undoButton);
        row->addWidget(m_redoButton);
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    // Grupo Ferramentas
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Ferramentas"), row);

        struct ToolDef { const char *icon; const char *tip; Canvas::Tool tool; };
        const ToolDef tools[] = {
            { "select", "Selecionar", Canvas::Tool::Select },
            { "pencil", "Lápis",    Canvas::Tool::Pencil },
            { "eraser", "Borracha", Canvas::Tool::Eraser },
            { "fill",   "Balde",    Canvas::Tool::Fill },
        };
        for (const auto &t : tools) {
            QToolButton *btn = makeToolButton(QString::fromUtf8(t.icon),
                                              QString::fromUtf8(t.tip));
            m_toolGroup->addButton(btn, static_cast<int>(t.tool));
            row->addWidget(btn);

            // Clique direito no botão "Selecionar" abre o ajuste de cantos.
            if (t.tool == Canvas::Tool::Select) {
                btn->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(btn, &QToolButton::customContextMenuRequested, this,
                        [this, btn](const QPoint &p) {
                            QMenu m(btn);
                            QAction *act = m.addAction(
                                QStringLiteral("Alterar cantos…"));
                            connect(act, &QAction::triggered, this,
                                    &MainWindow::editSelectionCornersDialog);
                            m.exec(btn->mapToGlobal(p));
                        });
            }
        }
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    // Grupo Formas
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Formas"), row);

        struct ToolDef { const char *icon; const char *tip; Canvas::Tool tool; };
        const ToolDef shapes[] = {
            { "line",      "Linha",     Canvas::Tool::Line },
            { "rectangle", "Retângulo", Canvas::Tool::Rectangle },
            { "ellipse",   "Elipse",    Canvas::Tool::Ellipse },
        };
        for (const auto &t : shapes) {
            QToolButton *btn = makeToolButton(QString::fromUtf8(t.icon),
                                              QString::fromUtf8(t.tip));
            m_toolGroup->addButton(btn, static_cast<int>(t.tool));
            row->addWidget(btn);
        }
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    // Grupo Espessura
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Tamanho"), row);

        m_widthSpin = new QSpinBox(ribbon);
        m_widthSpin->setObjectName(QStringLiteral("widthSpin"));
        m_widthSpin->setRange(1, 64);
        m_widthSpin->setValue(3);
        m_widthSpin->setSuffix(QStringLiteral(" px"));
        m_widthSpin->setFixedWidth(78);
        connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &MainWindow::onPenWidthChanged);
        row->addWidget(m_widthSpin);
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    // Grupo Cores
    {
        QHBoxLayout *row = nullptr;
        QVBoxLayout *group = makeGroup(QStringLiteral("Cores"), row);

        row->setSpacing(10);

        m_colorButton = new QToolButton(ribbon);
        m_colorButton->setObjectName(QStringLiteral("colorButton"));
        m_colorButton->setFixedSize(44, 44);
        m_colorButton->setToolTip(QStringLiteral("Escolher cor"));
        connect(m_colorButton, &QToolButton::clicked, this, &MainWindow::chooseColor);
        row->addWidget(m_colorButton);

        // Paleta rápida de cores comuns (como o W11), em duas linhas de 4.
        const QColor palette[] = {
            QColor("#000000"), QColor("#ffffff"), QColor("#e01b24"),
            QColor("#ff7800"), QColor("#f6d32d"), QColor("#33d17a"),
            QColor("#3584e4"), QColor("#9141ac"),
        };
        QWidget *swatchHost = new QWidget(ribbon);
        swatchHost->setObjectName(QStringLiteral("swatchHost"));
        QGridLayout *swatchLayout = new QGridLayout(swatchHost);
        swatchLayout->setContentsMargins(0, 0, 0, 0);
        swatchLayout->setHorizontalSpacing(5);
        swatchLayout->setVerticalSpacing(5);
        int idx = 0;
        for (const QColor &c : palette) {
            QToolButton *sw = new QToolButton(swatchHost);
            sw->setObjectName(QStringLiteral("swatch"));
            sw->setFixedSize(22, 22);

            // Pinta a amostra com cantos arredondados (em alta resolução),
            // para não ficar um quadrado de cor sólida atrás da borda do QSS.
            const qreal dpr = 2.0;
            QPixmap pm(static_cast<int>(18 * dpr), static_cast<int>(18 * dpr));
            pm.fill(Qt::transparent);
            QPainter pp(&pm);
            pp.setRenderHint(QPainter::Antialiasing, true);
            pp.setBrush(c);
            pp.setPen(QPen(QColor(255, 255, 255, 40), 1 * dpr));
            pp.drawRoundedRect(QRectF(0.5 * dpr, 0.5 * dpr,
                                      17 * dpr, 17 * dpr), 5 * dpr, 5 * dpr);
            pp.end();
            pm.setDevicePixelRatio(dpr);
            sw->setIcon(QIcon(pm));
            sw->setIconSize(QSize(18, 18));
            sw->setToolTip(c.name());
            connect(sw, &QToolButton::clicked, this, [this, c]() {
                if (auto *cv = currentCanvas()) cv->setPenColor(c);
                m_lastColor = c;
                updateColorButton();
            });
            swatchLayout->addWidget(sw, idx / 4, idx % 4);
            ++idx;
        }
        row->addWidget(swatchHost);
        layout->addLayout(group);
    }

    layout->addWidget(makeVSeparator());

    layout->addStretch(1);

    // Título centralizado (estilo GNOME) à direita do flexível.
    m_titleLabel = new QLabel(QStringLiteral("Sem título"), ribbon);
    m_titleLabel->setObjectName(QStringLiteral("titleLabel"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_titleLabel);

    layout->addStretch(1);

    // Indicador de tamanho da tela (ex.: "800 × 600").
    m_sizeLabel = new QLabel(ribbon);
    m_sizeLabel->setObjectName(QStringLiteral("sizeLabel"));
    m_sizeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_sizeLabel);

    // Menu hambúrguer à direita.
    QToolButton *menuBtn = makeActionButton(QStringLiteral("menu"),
                                            QStringLiteral("Menu principal"));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu *menu = new QMenu(menuBtn);

    QAction *newPageAct = menu->addAction(QStringLiteral("Nova página (Ctrl+T)"));
    connect(newPageAct, &QAction::triggered, this, &MainWindow::newPage);

    QAction *boundAct = menu->addAction(QStringLiteral("Colidir com a borda"));
    boundAct->setCheckable(true);
    boundAct->setChecked(m_boundToEdge);
    boundAct->setToolTip(QStringLiteral("Mantém imagens dentro da área e "
                                        "expande a tela ao colar imagem maior"));
    connect(boundAct, &QAction::toggled, this, [this](bool on) {
        m_boundToEdge = on;
        // Aplica a todas as páginas abertas.
        for (int i = 0; i < m_tabs->count(); ++i) {
            QScrollArea *a = qobject_cast<QScrollArea *>(m_tabs->widget(i));
            if (a)
                if (auto *cv = qobject_cast<Canvas *>(a->widget()))
                    cv->setBoundToEdge(on);
        }
    });

    menu->addSeparator();
    QAction *aboutAct = menu->addAction(QStringLiteral("Sobre o Taiul Paint"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);
    menuBtn->setMenu(menu);
    layout->addWidget(menuBtn);

    connect(m_toolGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onToolSelected);
    if (auto *first = m_toolGroup->button(static_cast<int>(Canvas::Tool::Pencil)))
        first->setChecked(true);

    QToolBar *tb = new QToolBar(this);
    tb->setObjectName(QStringLiteral("ribbonToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    // Impede o menu de contexto (clique direito) que permitiria ocultar a barra.
    tb->setContextMenuPolicy(Qt::PreventContextMenu);
    tb->toggleViewAction()->setVisible(false);
    tb->addWidget(ribbon);
    addToolBar(Qt::TopToolBarArea, tb);
    // Também desliga o menu de contexto da área de toolbars do QMainWindow.
    setContextMenuPolicy(Qt::NoContextMenu);

    // Atalhos de teclado. Atalhos de janela (New/Open/Save) vão ao MainWindow;
    // os que operam no canvas usam lambdas que pegam a página atual.
    auto addWinShortcut = [this](QKeySequence key, auto slot) {
        QAction *act = new QAction(this);
        act->setShortcut(key);
        connect(act, &QAction::triggered, this, slot);
        addAction(act);
    };
    auto addCanvasShortcut = [this](QKeySequence key, auto fn) {
        QAction *act = new QAction(this);
        act->setShortcut(key);
        connect(act, &QAction::triggered, this, [this, fn]{
            if (auto *c = currentCanvas()) fn(c);
        });
        addAction(act);
    };

    addWinShortcut(QKeySequence::New, &MainWindow::newFile);
    addWinShortcut(QKeySequence::Open, &MainWindow::openFile);
    addWinShortcut(QKeySequence::Save, &MainWindow::saveFile);
    addWinShortcut(QKeySequence(QStringLiteral("Ctrl+T")), &MainWindow::newPage);
    addWinShortcut(QKeySequence(QStringLiteral("Ctrl+R")),
                   &MainWindow::resizeCanvasDialog);

    addCanvasShortcut(QKeySequence::Undo, [](Canvas *c){ c->undo(); });
    addCanvasShortcut(QKeySequence::Redo, [](Canvas *c){ c->redo(); });
    addCanvasShortcut(QKeySequence::Paste, [](Canvas *c){ c->pasteFromClipboard(); });
    addCanvasShortcut(QKeySequence::Copy, [](Canvas *c){ c->copySelection(); });
    addCanvasShortcut(QKeySequence::Delete, [](Canvas *c){ c->deleteSelection(); });
    addCanvasShortcut(QKeySequence(QKeySequence::ZoomIn), [](Canvas *c){ c->zoomIn(); });
    addCanvasShortcut(QKeySequence(QStringLiteral("Ctrl+=")),
                      [](Canvas *c){ c->zoomIn(); });
    addCanvasShortcut(QKeySequence(QKeySequence::ZoomOut),
                      [](Canvas *c){ c->zoomOut(); });
    addCanvasShortcut(QKeySequence(QStringLiteral("Ctrl+0")),
                      [](Canvas *c){ c->resetZoom(); });
}

QLabel *MainWindow::makeGroupLabel(const QString &text)
{
    QLabel *lbl = new QLabel(text, this);
    lbl->setObjectName(QStringLiteral("groupLabel"));
    return lbl;
}

QFrame *MainWindow::makeVSeparator()
{
    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName(QStringLiteral("ribbonSep"));
    return sep;
}

QToolButton *MainWindow::makeActionButton(const QString &iconName, const QString &tooltip)
{
    QToolButton *btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("actionButton"));
    btn->setIcon(IconLoader::load(iconName, iconColor()));
    btn->setToolTip(tooltip);
    btn->setAutoRaise(true);
    btn->setIconSize(QSize(20, 20));
    btn->setFixedSize(34, 34);
    return btn;
}

QToolButton *MainWindow::makeToolButton(const QString &iconName, const QString &tooltip)
{
    QToolButton *btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("toolButton"));
    btn->setIcon(IconLoader::load(iconName, iconColor()));
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setIconSize(QSize(24, 24));
    btn->setFixedSize(40, 40);
    return btn;
}

void MainWindow::onToolSelected(int id)
{
    if (auto *c = currentCanvas())
        c->setTool(static_cast<Canvas::Tool>(id));
}

void MainWindow::onPenWidthChanged(int width)
{
    if (auto *c = currentCanvas())
        c->setPenWidth(width);
}

void MainWindow::chooseColor()
{
    const QColor newColor = QColorDialog::getColor(
        m_lastColor, this, QStringLiteral("Escolher cor"));
    if (newColor.isValid()) {
        m_lastColor = newColor;
        if (auto *c = currentCanvas())
            c->setPenColor(newColor);
        updateColorButton();
    }
}

void MainWindow::updateColorButton()
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(m_lastColor);
    p.setPen(QPen(QColor(255, 255, 255, 50), 1));
    p.drawRoundedRect(QRect(2, 2, 24, 24), 6, 6);
    p.end();
    m_colorButton->setIcon(QIcon(pm));
    m_colorButton->setIconSize(QSize(28, 28));
}

void MainWindow::copySelectionAction()
{
    if (auto *c = currentCanvas())
        c->copySelection();
}

void MainWindow::deleteSelectionAction()
{
    if (auto *c = currentCanvas())
        c->deleteSelection();
}

void MainWindow::editSelectionCornersDialog()
{
    // Diálogo subordinado à janela principal (acompanha o Paint).
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Alterar cantos da seleção"));

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(16);

    // Pré-visualização (ocupa o espaço extra quando a janela é redimensionada).
    CornerPreview *preview = new CornerPreview(&dialog);
    preview->setPercent(m_selectionCorner);
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(preview, 1);

    // Linha de controles: [−] slider [+] [campo editável %] (estilo do zoom).
    QHBoxLayout *ctrl = new QHBoxLayout();
    ctrl->setSpacing(8);

    // objectName "zoomBtn": estilo já definido (sem padding, fonte grande), o
    // que garante que o "+"/"−" apareçam corretamente.
    QToolButton *minus = new QToolButton(&dialog);
    minus->setObjectName(QStringLiteral("zoomBtn"));
    minus->setText(QStringLiteral("−"));
    minus->setAutoRaise(true);
    minus->setFixedSize(32, 32);

    QSlider *slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setObjectName(QStringLiteral("zoomSlider"));  // mesmo estilo do zoom
    slider->setRange(0, 100);
    slider->setValue(m_selectionCorner);

    QToolButton *plus = new QToolButton(&dialog);
    plus->setObjectName(QStringLiteral("zoomBtn"));
    plus->setText(QStringLiteral("+"));
    plus->setAutoRaise(true);
    plus->setFixedSize(32, 32);

    // Campo editável da porcentagem (digite e tecle Enter).
    QLineEdit *pctEdit = new QLineEdit(QStringLiteral("%1%").arg(m_selectionCorner),
                                       &dialog);
    pctEdit->setObjectName(QStringLiteral("zoomEdit"));
    pctEdit->setAlignment(Qt::AlignCenter);
    pctEdit->setFixedWidth(60);

    ctrl->addWidget(minus);
    ctrl->addWidget(slider, 1);
    ctrl->addWidget(plus);
    ctrl->addWidget(pctEdit);
    root->addLayout(ctrl);

    // Mantém slider, campo e preview sincronizados (sem reentrar nos slots).
    auto setValue = [slider, pctEdit, preview](int v) {
        v = qBound(0, v, 100);
        QSignalBlocker b1(slider), b2(pctEdit);
        slider->setValue(v);
        pctEdit->setText(QStringLiteral("%1%").arg(v));
        preview->setPercent(v);
    };
    connect(slider, &QSlider::valueChanged, this, [setValue](int v){ setValue(v); });
    connect(minus, &QToolButton::clicked, this,
            [setValue, slider]{ setValue(slider->value() - 5); });
    connect(plus, &QToolButton::clicked, this,
            [setValue, slider]{ setValue(slider->value() + 5); });
    connect(pctEdit, &QLineEdit::editingFinished, this, [setValue, pctEdit]{
        QString digits;
        for (const QChar &ch : pctEdit->text())
            if (ch.isDigit())
                digits.append(ch);
        bool ok = false;
        const int v = digits.toInt(&ok);
        setValue(ok ? v : 0);
    });

    // Botões Aplicar / Cancelar.
    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(QStringLiteral("Aplicar"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("Cancelar"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    // Janela redimensionável: largura mínima maior + tamanho inicial.
    dialog.setMinimumWidth(460);
    dialog.resize(560, 440);

    if (dialog.exec() == QDialog::Accepted) {
        m_selectionCorner = slider->value();
        if (auto *c = currentCanvas())
            c->setSelectionCornerPercent(m_selectionCorner);
    }
}

void MainWindow::newFile()
{
    // "Novo" cria uma nova página em branco (mantém as demais abertas).
    addPage();
}

void MainWindow::openFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("Abrir imagem"), QString(),
        QStringLiteral("Imagens (*.png *.jpg *.jpeg *.bmp *.gif);;Todos os arquivos (*)"));

    if (fileName.isEmpty())
        return;

    QImage img;
    if (!img.load(fileName)) {
        QMessageBox::warning(this, QStringLiteral("Taiul Paint"),
                             QStringLiteral("Não foi possível abrir o arquivo."));
        return;
    }

    // Abre numa nova página, registrando o caminho do arquivo.
    Canvas *c = addPage(QFileInfo(fileName).fileName(), img);
    c->setProperty("filePath", fileName);
    updateTabTitle(c);
}

bool MainWindow::saveFile()
{
    Canvas *c = currentCanvas();
    if (!c)
        return false;

    const QString cur = c->property("filePath").toString();
    if (cur.isEmpty())
        return saveFileAs();

    QByteArray fmt = QFileInfo(cur).suffix().toLower().toUtf8();
    if (fmt.isEmpty())
        fmt = "png";

    if (c->saveImage(cur, fmt.constData())) {
        updateTabTitle(c);
        return true;
    }
    QMessageBox::warning(this, QStringLiteral("Taiul Paint"),
                         QStringLiteral("Não foi possível salvar o arquivo."));
    return false;
}

bool MainWindow::saveFileAs()
{
    Canvas *c = currentCanvas();
    if (!c)
        return false;

    const QString cur = c->property("filePath").toString();
    QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salvar como"),
        cur.isEmpty() ? QStringLiteral("sem-titulo.png") : cur,
        QStringLiteral("PNG (*.png);;JPEG (*.jpg);;Bitmap (*.bmp)"));

    if (fileName.isEmpty())
        return false;

    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".png");

    QByteArray fmt = QFileInfo(fileName).suffix().toLower().toUtf8();
    if (c->saveImage(fileName, fmt.constData())) {
        c->setProperty("filePath", fileName);
        updateTabTitle(c);
        return true;
    }
    QMessageBox::warning(this, QStringLiteral("Taiul Paint"),
                         QStringLiteral("Não foi possível salvar o arquivo."));
    return false;
}

bool MainWindow::maybeSave()
{
    return maybeSaveCanvas(currentCanvas());
}

bool MainWindow::maybeSaveCanvas(Canvas *canvas)
{
    if (!canvas || !canvas->isModified())
        return true;

    // Garante que a aba do canvas em questão esteja visível ao perguntar.
    for (int i = 0; i < m_tabs->count(); ++i) {
        QScrollArea *a = qobject_cast<QScrollArea *>(m_tabs->widget(i));
        if (a && a->widget() == canvas) {
            m_tabs->setCurrentIndex(i);
            break;
        }
    }

    const QMessageBox::StandardButton ret = QMessageBox::warning(
        this, QStringLiteral("Taiul Paint"),
        QStringLiteral("O desenho foi modificado.\nDeseja salvar as alterações?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save)
        return saveFile();
    if (ret == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::about()
{
    QMessageBox::about(this, QStringLiteral("Sobre o Taiul Paint"),
        QStringLiteral("<h3>Taiul Paint</h3>"
                       "<p>Editor de pintura leve, rápido e intuitivo,"
                       " desenvolvido em Qt 6 com C++. Focado em performance"
                       " e experiência de usuário descomplicada.</p>"
                       "<p>Desenvolvido com a assistência do Claude Code.</p>"));
}

void MainWindow::onCanvasSizeChanged(const QSize &size)
{
    m_sizeLabel->setText(QStringLiteral("%1 × %2")
                             .arg(size.width()).arg(size.height()));
}

// ---- Controle de zoom flutuante ----

void MainWindow::buildZoomOverlay()
{
    m_zoomOverlay = new QWidget(this);
    m_zoomOverlay->setObjectName(QStringLiteral("zoomOverlay"));

    QHBoxLayout *lay = new QHBoxLayout(m_zoomOverlay);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(6);

    QToolButton *minus = makeActionButton(QStringLiteral("undo"),
                                          QStringLiteral("Diminuir zoom (Ctrl-)"));
    // Reaproveitamos texto em vez de ícone para os sinais de zoom.
    minus->setIcon(QIcon());
    minus->setText(QStringLiteral("−"));
    minus->setObjectName(QStringLiteral("zoomBtn"));
    connect(minus, &QToolButton::clicked, this,
            [this]{ if (auto *c = currentCanvas()) c->zoomOut(); });

    m_zoomSlider = new QSlider(Qt::Horizontal, m_zoomOverlay);
    m_zoomSlider->setObjectName(QStringLiteral("zoomSlider"));
    m_zoomSlider->setRange(10, 800);   // 10% .. 800%
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(120);
    connect(m_zoomSlider, &QSlider::valueChanged,
            this, &MainWindow::onZoomSliderMoved);

    QToolButton *plus = new QToolButton(m_zoomOverlay);
    plus->setObjectName(QStringLiteral("zoomBtn"));
    plus->setText(QStringLiteral("+"));
    plus->setAutoRaise(true);
    plus->setFixedSize(28, 28);
    plus->setToolTip(QStringLiteral("Aumentar zoom (Ctrl+)"));
    connect(plus, &QToolButton::clicked, this,
            [this]{ if (auto *c = currentCanvas()) c->zoomIn(); });

    // Ajusta o "−" para o mesmo estilo do "+".
    minus->setAutoRaise(true);
    minus->setFixedSize(28, 28);

    // Campo editável: digite o percentual e tecle Enter.
    m_zoomEdit = new QLineEdit(QStringLiteral("100%"), m_zoomOverlay);
    m_zoomEdit->setObjectName(QStringLiteral("zoomEdit"));
    m_zoomEdit->setAlignment(Qt::AlignCenter);
    m_zoomEdit->setFixedWidth(56);
    m_zoomEdit->setToolTip(QStringLiteral("Digite o zoom e tecle Enter "
                                          "(Ctrl+0 restaura 100%)"));
    connect(m_zoomEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onZoomEdited);

    lay->addWidget(minus);
    lay->addWidget(m_zoomSlider);
    lay->addWidget(plus);
    lay->addWidget(m_zoomEdit);

    // Separador + botão de redimensionar a área de pintura.
    QFrame *sep = new QFrame(m_zoomOverlay);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName(QStringLiteral("ribbonSep"));
    lay->addWidget(sep);

    QToolButton *resizeBtn = new QToolButton(m_zoomOverlay);
    resizeBtn->setObjectName(QStringLiteral("zoomBtn"));
    resizeBtn->setIcon(IconLoader::load(QStringLiteral("resize"), iconColor()));
    resizeBtn->setIconSize(QSize(18, 18));
    resizeBtn->setAutoRaise(true);
    resizeBtn->setFixedSize(28, 28);
    resizeBtn->setToolTip(QStringLiteral("Redimensionar área (Ctrl+R)"));
    connect(resizeBtn, &QToolButton::clicked, this,
            &MainWindow::resizeCanvasDialog);
    lay->addWidget(resizeBtn);

    m_zoomOverlay->adjustSize();
    m_zoomOverlay->raise();
    repositionZoomOverlay();
}

void MainWindow::repositionZoomOverlay()
{
    if (!m_zoomOverlay)
        return;
    const int margin = 16;
    const QSize sz = m_zoomOverlay->size();
    m_zoomOverlay->move(width() - sz.width() - margin,
                        height() - sz.height() - margin);
    m_zoomOverlay->raise();
}

void MainWindow::onZoomChanged(qreal factor)
{
    const int pct = qRound(factor * 100.0);
    // Atualiza o campo e o slider sem reentrar nos respectivos slots.
    {
        QSignalBlocker blocker(m_zoomEdit);
        m_zoomEdit->setText(QStringLiteral("%1%").arg(pct));
    }
    {
        QSignalBlocker blocker(m_zoomSlider);
        m_zoomSlider->setValue(qBound(m_zoomSlider->minimum(), pct,
                                      m_zoomSlider->maximum()));
    }
}

void MainWindow::onZoomSliderMoved(int value)
{
    if (auto *c = currentCanvas())
        c->setZoom(value / 100.0);
}

void MainWindow::onZoomEdited()
{
    Canvas *c = currentCanvas();
    if (!c)
        return;

    // Extrai apenas os dígitos do texto (ignora "%", espaços etc.).
    QString digits;
    for (const QChar &ch : m_zoomEdit->text())
        if (ch.isDigit())
            digits.append(ch);

    bool ok = false;
    int pct = digits.toInt(&ok);
    if (!ok) {
        // Texto inválido: restaura o valor atual.
        onZoomChanged(c->zoom());
        return;
    }

    pct = qBound(10, pct, 800);
    c->setZoom(pct / 100.0);
    // Garante que o campo reflita o valor efetivamente aplicado (após o clamp).
    onZoomChanged(c->zoom());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    repositionZoomOverlay();
}

void MainWindow::resizeCanvasDialog()
{
    Canvas *canvas = currentCanvas();
    if (!canvas)
        return;
    const QSize cur = canvas->canvasSize();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Redimensionar área"));

    QFormLayout *form = new QFormLayout(&dialog);

    QSpinBox *wSpin = new QSpinBox(&dialog);
    wSpin->setRange(1, 10000);
    wSpin->setValue(cur.width());
    wSpin->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Largura:"), wSpin);

    QSpinBox *hSpin = new QSpinBox(&dialog);
    hSpin->setRange(1, 10000);
    hSpin->setValue(cur.height());
    hSpin->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Altura:"), hSpin);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted)
        canvas->resizeCanvas(QSize(wSpin->value(), hSpin->value()));
}

void MainWindow::insertImageFromFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("Inserir imagem"), QString(),
        QStringLiteral("Imagens (*.png *.jpg *.jpeg *.bmp *.gif);;Todos os arquivos (*)"));

    if (fileName.isEmpty())
        return;

    QImage img;
    if (img.load(fileName)) {
        if (auto *c = currentCanvas())
            c->insertFloatingImage(img);
    } else {
        QMessageBox::warning(this, QStringLiteral("Taiul Paint"),
                             QStringLiteral("Não foi possível abrir a imagem."));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Pergunta sobre cada página modificada antes de fechar.
    for (int i = 0; i < m_tabs->count(); ++i) {
        QScrollArea *a = qobject_cast<QScrollArea *>(m_tabs->widget(i));
        Canvas *c = a ? qobject_cast<Canvas *>(a->widget()) : nullptr;
        if (c && !maybeSaveCanvas(c)) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

// ---- Arrastar e soltar (Nautilus) ----

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();

    // Resolve a imagem a partir do drop (arquivo local ou imagem direta).
    QImage img;
    QString sourcePath;
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const QString path = url.toLocalFile();
            QImage tmp;
            if (tmp.load(path)) {
                img = tmp;
                sourcePath = path;
                break;
            }
        }
    }
    if (img.isNull() && mime->hasImage())
        img = qvariant_cast<QImage>(mime->imageData());

    if (img.isNull())
        return;

    event->acceptProposedAction();

    // Pergunta: inserir na página atual ou abrir numa nova página para editar?
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Imagem arrastada"));
    box.setText(QStringLiteral("O que deseja fazer com a imagem?"));
    QPushButton *insertBtn = box.addButton(QStringLiteral("Inserir aqui"),
                                           QMessageBox::AcceptRole);
    QPushButton *editBtn = box.addButton(QStringLiteral("Editar em nova página"),
                                         QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == insertBtn) {
        if (auto *c = currentCanvas())
            c->insertFloatingImage(img);
    } else if (box.clickedButton() == editBtn) {
        const QString title = sourcePath.isEmpty()
            ? QString() : QFileInfo(sourcePath).fileName();
        Canvas *c = addPage(title, img);
        if (!sourcePath.isEmpty()) {
            c->setProperty("filePath", sourcePath);
            updateTabTitle(c);
        }
    }
}
