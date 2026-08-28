#include "dlttableview.h"

#include <QApplication>
#include <QDebug>
#include <QItemSelectionModel>
#include <QLineEdit>

#include "textselectdelegate.h"
#include <QMouseEvent>
#include <QWheelEvent>

DltTableView::DltTableView(QWidget *parent) :
    QTableView(parent)
{
}

/*!
    Paints the table on receipt of the given paint event \a event.
*/
void DltTableView::paintEvent(QPaintEvent *event)
{
    if(paintMutex.tryLock())
    {
        QTableView::paintEvent(event);
        paintMutex.unlock();
    }
}

void DltTableView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        auto val = event->angleDelta().y();
        emit changeFontSize((0 < val) - (val < 0));
        event->accept();
    } else {
        QTableView::wheelEvent(event);
    }
}

bool DltTableView::isLoneSelectedRow(const QModelIndex &index) const
{
    if(!index.isValid() || !selectionModel())
        return false;

    const QModelIndexList rows = selectionModel()->selectedRows();
    return (rows.size() == 1) && (rows.first().row() == index.row());
}

void DltTableView::forwardToEditor(QMouseEvent *event)
{
    if(!textDragEditor)
        return;

    const QPoint local = textDragEditor->mapFrom(viewport(), event->position().toPoint());
    QMouseEvent forwarded(event->type(), local, event->scenePosition(), event->globalPosition(),
                          event->button(), event->buttons(), event->modifiers());
    QApplication::sendEvent(textDragEditor, &forwarded);
}

void DltTableView::mousePressEvent(QMouseEvent *event)
{
    textDragEditor = nullptr;
    pressOnLoneSelectedRow = false;

    if(event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier)
    {
        pressPos = event->position().toPoint();
        const QModelIndex index = indexAt(pressPos);
        pressIndex = index;

        if(isLoneSelectedRow(index))
        {
            /* Hold the decision: a plain click deselects the row, a horizontal
               drag selects text. Either way the base class must not act yet. */
            pressOnLoneSelectedRow = true;
            event->accept();
            return;
        }
    }
    else
    {
        pressIndex = QPersistentModelIndex();
    }

    QTableView::mousePressEvent(event);
}

void DltTableView::mouseMoveEvent(QMouseEvent *event)
{
    if(textDragEditor)
    {
        forwardToEditor(event);
        event->accept();
        return;
    }

    if(pressOnLoneSelectedRow && (event->buttons() & Qt::LeftButton) && pressIndex.isValid())
    {
        const QPoint now = event->position().toPoint();
        const int dx = qAbs(now.x() - pressPos.x());
        const int dy = qAbs(now.y() - pressPos.y());

        /* horizontal movement inside the row means "select the text" */
        if(dx >= QApplication::startDragDistance() && dx > dy)
        {
            const QModelIndex index = pressIndex;
            if(edit(index, QAbstractItemView::AllEditTriggers, event))
            {
                /* Ask the delegate which editor it just made. childAt() cannot
                   be used here: the widget may not be shown yet, and it skips
                   hidden children. */
                if(TextSelectDelegate *d = qobject_cast<TextSelectDelegate*>(itemDelegateForIndex(index)))
                    textDragEditor = d->lastEditor();
                else
                    textDragEditor = viewport()->childAt(pressPos);

                if(textDragEditor && !textDragEditor->isVisible())
                    textDragEditor->show();
                if(textDragEditor)
                {
                    /* replay the original press so the editor starts a selection
                       there, then hand it the movement so far */
                    QMouseEvent press(QEvent::MouseButtonPress,
                                      textDragEditor->mapFrom(viewport(), pressPos),
                                      event->scenePosition(), event->globalPosition(),
                                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(textDragEditor, &press);
                    forwardToEditor(event);
                }
            }
        }
        event->accept();
        return;
    }

    QTableView::mouseMoveEvent(event);
}

void DltTableView::mouseReleaseEvent(QMouseEvent *event)
{
    if(textDragEditor)
    {
        forwardToEditor(event);
        textDragEditor = nullptr;
        pressOnLoneSelectedRow = false;
        event->accept();
        return;
    }

    if(pressOnLoneSelectedRow && event->button() == Qt::LeftButton)
    {
        /* click, no drag, on the one selected row: deselect it */
        pressOnLoneSelectedRow = false;
        if(selectionModel())
            selectionModel()->clearSelection();
        event->accept();
        return;
    }

    QTableView::mouseReleaseEvent(event);
}

void DltTableView::lock()
{
    paintMutex.lock();
}

void DltTableView::unlock()
{
    paintMutex.unlock();
}
