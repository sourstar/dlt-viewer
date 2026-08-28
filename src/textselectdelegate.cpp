#include "textselectdelegate.h"

#include <QLineEdit>

TextSelectDelegate::TextSelectDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *TextSelectDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                          const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)

    QLineEdit *editor = new QLineEdit(parent);
    m_lastEditor = editor;
    editor->setReadOnly(true);
    editor->setFrame(false);
    /* keep the cell looking like a cell rather than an input field */
    /* Plain background while selecting, so the blue is the selection itself and
       not the row highlight. The row keeps its normal colours underneath. */
    editor->setStyleSheet("QLineEdit { background: palette(base); color: palette(text); }");
    editor->setContextMenuPolicy(Qt::DefaultContextMenu); // gives Copy / Select All
    return editor;
}

void TextSelectDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if(QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor))
    {
        lineEdit->setText(index.data(Qt::DisplayRole).toString());
        lineEdit->deselect();
        lineEdit->setCursorPosition(0);
    }
}

void TextSelectDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                      const QModelIndex &index) const
{
    /* read-only on purpose: never write back into the log */
    Q_UNUSED(editor)
    Q_UNUSED(model)
    Q_UNUSED(index)
}

void TextSelectDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                              const QModelIndex &index) const
{
    Q_UNUSED(index)
    editor->setGeometry(option.rect);
}
