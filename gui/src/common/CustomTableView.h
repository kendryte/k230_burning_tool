#ifndef CCHECKBOXMODEL_H
#define CCHECKBOXMODEL_H

#include <QEvent>
#include <QHeaderView>
#include <QPainter>
#include <QItemDelegate>
#include <QStyledItemDelegate>
#include <QAbstractTableModel>

class TableHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    TableHeaderView(Qt::Orientation orientation, QWidget *parent);
    ~TableHeaderView();

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const;
    bool event(QEvent *e) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *e) Q_DECL_OVERRIDE;

public slots:
    void onStateChanged(int state);

signals:
    void stateChanged(int state);

private:
    bool m_bPressed;
    bool m_bChecked;
    bool m_bTristate;
    bool m_bNoChange;
    bool m_bMoving;
};

/*
 * CheckBox
 */
class CheckBoxDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    CheckBoxDelegate(QObject *parent);
    ~CheckBoxDelegate();

    // painting
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return NULL;
    }

protected:
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index);
};

/*
 * ComboBox
 */
class ComboDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    ComboDelegate(QObject *parent = 0);

    void setItems(QStringList items);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    void setEditorData(QWidget *editor, const QModelIndex &index) const;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;

private:
    QStringList m_sItemList;
};

class PushButtonDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PushButtonDelegate(QString btnName, QWidget *parent = 0);
    ~PushButtonDelegate();

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return NULL;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index);

signals:
    void btnClicked(const QModelIndex &index);

private:
    QString m_btnName;
};

#endif // TABLE_MODEL_H
