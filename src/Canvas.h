#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QStack>
#include <QRect>
#include <QPainterPath>

// O Canvas é a área de pintura. Mantém a imagem em um QImage e
// desenha sobre ele conforme o usuário arrasta o mouse.
//
// O tamanho do canvas é independente do tamanho do widget: o widget é um
// pouco maior para acomodar as alças de redimensionamento nas bordas.
class Canvas : public QWidget
{
    Q_OBJECT

public:
    // Ferramentas disponíveis, no espírito do Paint clássico.
    enum class Tool {
        Pencil,
        Eraser,
        Line,
        Rectangle,
        Ellipse,
        Fill,
        Select
    };

    explicit Canvas(QWidget *parent = nullptr);

    bool openImage(const QString &fileName);
    void openImageFromData(const QImage &image);  // carrega como conteúdo do canvas
    bool saveImage(const QString &fileName, const char *fileFormat);
    void newImage(const QSize &size = QSize(800, 600));

    void setPenColor(const QColor &color) { m_penColor = color; }
    QColor penColor() const { return m_penColor; }

    void setPenWidth(int width) { m_penWidth = width; }
    int penWidth() const { return m_penWidth; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    bool isModified() const { return m_modified; }

    QSize canvasSize() const { return m_image.size(); }
    QSize sizeHint() const override;

    // Redimensiona a tela de pintura para um novo tamanho (preserva o conteúdo).
    void resizeCanvas(const QSize &newSize);

    // Insere uma imagem como objeto flutuante para o usuário posicionar.
    void insertFloatingImage(const QImage &image);

    // Cola a imagem do clipboard (se houver) como objeto flutuante.
    void pasteFromClipboard();

    // Zoom: 1.0 = 100%. Limites em kMinZoom / kMaxZoom.
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal factor);
    void zoomIn();
    void zoomOut();
    void resetZoom();

    // "Colidir com a borda": limita a imagem flutuante/seleção à área do canvas
    // e, ao inserir imagem maior que o canvas, expande-o para caber.
    bool boundToEdge() const { return m_boundToEdge; }
    void setBoundToEdge(bool on) { m_boundToEdge = on; }

    // Ações da seleção / flutuante.
    void copySelection();   // Ctrl+C: copia seleção (ou flutuante) ao clipboard
    void deleteSelection(); // Del: apaga a seleção (ou descarta a flutuante)
    bool hasSelectionOrFloating() const { return m_hasFloating || m_hasSelection; }

    // Arredondamento dos cantos da seleção, em porcentagem (0 = quadrado,
    // 100 = totalmente arredondado/elipse). Afeta o recorte e a cópia.
    int selectionCornerPercent() const { return m_selectionCornerPercent; }
    void setSelectionCornerPercent(int pct);

public slots:
    void undo();
    void redo();
    void clearImage();

signals:
    void modifiedChanged(bool modified);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void canvasSizeChanged(const QSize &size);
    void zoomChanged(qreal factor);
    void insertImageRequested();  // clique direito → "Inserir imagem"

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // Margem extra ao redor da imagem para desenhar as alças.
    static constexpr int kHandleMargin = 8;
    static constexpr int kHandleSize = 10;

    // Alças de redimensionamento (cantos e meios das bordas).
    enum class Handle { None, Right, Bottom, Corner };

    void drawLineTo(const QPoint &endPoint);
    void drawShapeTo(const QPoint &endPoint, QImage &target);
    void floodFill(const QPoint &startPoint);
    void resizeImage(QImage *image, const QSize &newSize);
    void pushUndoState();
    void setModified(bool modified);
    void emitHistorySignals();
    void updateWidgetSize();

    // Geometria das alças, em coordenadas do widget (já escaladas pelo zoom).
    QRect handleRect(Handle h) const;
    Handle handleAt(const QPoint &pos) const;

    // Converte coordenadas do widget para coordenadas da imagem (desfaz o zoom).
    QPoint toImage(const QPoint &widgetPos) const {
        return QPoint(qRound(widgetPos.x() / m_zoom),
                      qRound(widgetPos.y() / m_zoom));
    }

    // Aplica o zoom mantendo o ponto sob o cursor (em coord. da viewport) fixo.
    void applyZoom(qreal factor, const QPoint *anchorWidgetPos);

    // Fixa a imagem flutuante no canvas (se houver).
    void commitFloatingImage();

    // Transforma a seleção atual em imagem flutuante (recorta do canvas).
    void liftSelectionToFloating();

    // Limita a posição da flutuante à área do canvas (se boundToEdge).
    QPoint clampFloatingPos(const QPoint &pos) const;

    // Cancela a seleção em andamento/confirmada.
    void clearSelection();

    QImage m_image;          // imagem "confirmada"
    QImage m_previewBase;    // estado antes de desenhar a forma atual (para preview)
    bool m_drawing = false;
    bool m_modified = false;

    Tool m_tool = Tool::Pencil;
    QColor m_penColor = QColor(0, 0, 0);
    int m_penWidth = 3;

    QPoint m_lastPoint;
    QPoint m_startPoint;

    // Estado de redimensionamento por alça.
    Handle m_activeHandle = Handle::None;
    bool m_resizing = false;
    QPoint m_resizeStartPos;
    QSize m_resizeStartSize;

    // Imagem flutuante (inserida/colada) aguardando posicionamento.
    bool m_hasFloating = false;
    bool m_movingFloating = false;
    QImage m_floatingImage;
    QPoint m_floatingPos;       // canto sup-esq da imagem flutuante (coord. imagem)
    QPoint m_floatingGrab;      // deslocamento do clique dentro da imagem flutuante

    // Seleção retangular (ferramenta Select).
    bool m_hasSelection = false;   // existe um retângulo de seleção confirmado
    bool m_selecting = false;      // está arrastando para criar a seleção
    QRect m_selectionRect;         // em coordenadas da imagem
    QPoint m_selectStart;
    int m_selectionCornerPercent = 0;  // 0 = cantos retos; 100 = totalmente redondo

    // Raio em pixels dos cantos para um dado retângulo (a partir da %).
    qreal cornerRadiusFor(const QRect &r) const;
    // Caminho da seleção (retângulo arredondado) em coordenadas da imagem.
    QPainterPath selectionPath(const QRect &r) const;
    // Recorta uma região aplicando a máscara de cantos arredondados.
    QImage maskedCopy(const QRect &r) const;

    // Colidir com a borda.
    bool m_boundToEdge = false;

    // Zoom.
    qreal m_zoom = 1.0;
    static constexpr qreal kMinZoom = 0.1;
    static constexpr qreal kMaxZoom = 8.0;

    QStack<QImage> m_undoStack;
    QStack<QImage> m_redoStack;
    static constexpr int kMaxHistory = 30;
};

#endif // CANVAS_H
