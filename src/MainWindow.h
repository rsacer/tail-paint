#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "Canvas.h"

class QToolButton;
class QButtonGroup;
class QScrollArea;
class QLabel;
class QSpinBox;
class QFrame;
class QSlider;
class QWidget;
class QLineEdit;
class QTabWidget;

// Janela principal: posição dos botões/layout no estilo Paint do Windows 11
// (barra de ferramentas horizontal no topo, com grupos), porém com a
// estética GNOME/Adwaita escura.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void chooseColor();
    void about();
    void onToolSelected(int id);
    void onPenWidthChanged(int width);
    void updateColorButton();
    void resizeCanvasDialog();
    void insertImageFromFile();
    void onCanvasSizeChanged(const QSize &size);
    void onZoomChanged(qreal factor);
    void onZoomSliderMoved(int value);
    void onZoomEdited();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void newPage();
    void copySelectionAction();
    void deleteSelectionAction();
    void editSelectionCornersDialog();

private:
    void buildToolBar();
    void buildZoomOverlay();
    void repositionZoomOverlay();
    QToolButton *makeActionButton(const QString &iconName, const QString &tooltip);
    QToolButton *makeToolButton(const QString &iconName, const QString &tooltip);
    QLabel *makeGroupLabel(const QString &text);
    QFrame *makeVSeparator();
    bool maybeSave();
    bool maybeSaveCanvas(Canvas *canvas);

    // Cria uma nova página (aba) com um canvas; opcionalmente já com uma imagem
    // e um título. Retorna o canvas criado e o seleciona.
    Canvas *addPage(const QString &title = QString(),
                    const QImage &initial = QImage());
    // Canvas da aba ativa (ou nullptr se não houver).
    Canvas *currentCanvas() const;
    // Conecta os sinais de um canvas recém-criado à UI.
    void connectCanvas(Canvas *canvas);
    // Atualiza a UI (botões, tamanho, zoom, título) para o canvas atual.
    void syncUiToCanvas();
    // Texto da aba a partir do arquivo/modificado.
    void updateTabTitle(Canvas *canvas);

    // Cor usada para pintar os ícones (clara, por estarmos no tema escuro).
    QColor iconColor() const { return QColor(0xE3, 0xE3, 0xE3); }

    QTabWidget *m_tabs = nullptr;
    bool m_boundToEdge = false;  // estado global "colidir com a borda"
    QColor m_lastColor = QColor(0, 0, 0);  // última cor escolhida (para novas páginas)

    QButtonGroup *m_toolGroup = nullptr;
    QToolButton *m_colorButton = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    int m_selectionCorner = 0;  // % de arredondamento da seleção (estado global)
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;

    // Controle de zoom flutuante (canto inferior direito).
    QWidget *m_zoomOverlay = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLineEdit *m_zoomEdit = nullptr;
};

#endif // MAINWINDOW_H
