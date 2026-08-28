#ifndef TEXTSELECTDELEGATE_H
#define TEXTSELECTDELEGATE_H

#include <QPointer>
#include <QStyledItemDelegate>

/**
 * @brief Lets the text inside a cell be selected with the mouse and copied.
 *
 * A QTableView normally selects whole rows, so there is no way to drag over
 * part of a payload and copy just that. This delegate opens a read-only
 * QLineEdit over the cell, which gives character level selection, the usual
 * Ctrl+C, and a right click menu with Copy -- all handled by the line edit.
 *
 * It is read only in both directions: the editor cannot be typed into, and
 * setModelData() deliberately does nothing, so the log can never be altered
 * by clicking on it.
 */
class TextSelectDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit TextSelectDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;

    //! The editor most recently created by createEditor().
    /*! createEditor() runs synchronously inside QAbstractItemView::edit(),
        so the caller can pick the editor up straight afterwards. Looking it
        up by position instead is unreliable: the widget may not be shown
        yet, and QWidget::childAt() skips hidden children. */
    QWidget *lastEditor() const { return m_lastEditor; }

private:
    mutable QPointer<QWidget> m_lastEditor;
};

#endif // TEXTSELECTDELEGATE_H
