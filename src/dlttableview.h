#ifndef DLTTABLEVIEW_H
#define DLTTABLEVIEW_H

#include <QTableView>
#include <QMutex>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QPointer>

/**
 * @brief QTableView to wrap the paintEvent
 * There was heap corruption from paintEvent, when
 * enabling filters. Root cause was not yet found.
 * This is a workaround to disable painting while updating filters.
 *
 * It also adds two gestures on top of the normal row selection, both active
 * only when exactly one row is selected and the press lands on that row:
 *
 *  - a plain click deselects it,
 *  - pressing and dragging horizontally selects the text inside the cell,
 *    via the read-only editor supplied by TextSelectDelegate.
 *
 * Everything else -- selecting an unselected row, rubber band and Ctrl/Shift
 * multi-selection -- behaves exactly as before.
 */
class DltTableView : public QTableView
{
    Q_OBJECT
public:
    explicit DltTableView(QWidget *parent = 0);
    void lock();
    void unlock();

signals:
    void changeFontSize(int delta);

private:
    QMutex paintMutex;

    //! State for the press-drag-to-select-text gesture.
    QPoint pressPos;
    QPersistentModelIndex pressIndex;
    bool pressOnLoneSelectedRow = false;
    QPointer<QWidget> textDragEditor;

    bool isLoneSelectedRow(const QModelIndex &index) const;
    void forwardToEditor(QMouseEvent *event);

protected:
    void paintEvent(QPaintEvent *e) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public slots:

};

#endif // DLTTABLEVIEW_H
