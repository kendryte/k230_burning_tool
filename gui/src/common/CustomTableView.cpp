#include "common/CustomTableView.h"

#include <QComboBox>
#include <QCheckBox>
#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

/*code is from https://blog.csdn.net/mj348940862/article/details/124475266 */

#define CHECK_BOX_COLUMN 0

TableHeaderView::TableHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent),
      m_bPressed(false),
      m_bChecked(false),
      m_bTristate(false),
      m_bNoChange(false),
      m_bMoving(false)
{
    setSectionsClickable(true);
}

TableHeaderView::~TableHeaderView()
{
}

void TableHeaderView::onStateChanged(int state)
{
    if (state == Qt::PartiallyChecked)
    {
        m_bTristate = true;
        m_bNoChange = true;
    }
    else
    {
        m_bNoChange = false;
    }

    m_bChecked = (state != Qt::Unchecked);
    update();
}

void TableHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    painter->save();
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();

    if (logicalIndex == CHECK_BOX_COLUMN)
    {
        QStyleOptionButton option;
        option.initFrom(this);

        if (m_bChecked)
            option.state |= QStyle::State_Sunken;

        if (m_bTristate && m_bNoChange)
            option.state |= QStyle::State_NoChange;
        else
            option.state |= m_bChecked ? QStyle::State_On : QStyle::State_Off;

        int x, y, w, h, ow;
        ow = rect.width();
        rect.getRect(&x, &y, &w, &h);
        option.rect = QRect(x + ow / 2, y, w / 2, h);

        option.iconSize = QSize(20, 20);

        QCheckBox checkBox;
        checkBox.style()->drawControl(QStyle::CE_CheckBox, &option, painter, &checkBox);
        // checkBox.style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, painter, &checkBox);
    }
}

void TableHeaderView::mousePressEvent(QMouseEvent *event)
{
    int nColumn = logicalIndexAt(event->pos());

    if ((event->buttons() & Qt::LeftButton) && (nColumn == CHECK_BOX_COLUMN))
    {
        m_bPressed = true;
    }
    else
    {
        QHeaderView::mousePressEvent(event);
    }
}

void TableHeaderView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_bPressed)
    {
        if (m_bTristate && m_bNoChange)
        {
            m_bChecked = true;
            m_bNoChange = false;
        }
        else
        {
            m_bChecked = !m_bChecked;
        }

        update();

        Qt::CheckState state = m_bChecked ? Qt::Checked : Qt::Unchecked;

        emit stateChanged(state);

        updateSection(0);
    }
    else
    {
        QHeaderView::mouseReleaseEvent(event);
    }

    m_bPressed = false;
}

bool TableHeaderView::event(QEvent *event)
{
    updateSection(0);

    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        update();
        return true;
    }

    return QHeaderView::event(event);
}

/****************************************************
 * CheckBoxDelegate
 ****************************************************/
CheckBoxDelegate::CheckBoxDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
}

CheckBoxDelegate::~CheckBoxDelegate()
{
}

void CheckBoxDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem viewOption(option);

    initStyleOption(&viewOption, index);

    if (option.state.testFlag(QStyle::State_HasFocus))
        viewOption.state = viewOption.state ^ QStyle::State_HasFocus;

    QStyledItemDelegate::paint(painter, viewOption, index);

    bool data = index.model()->data(index, Qt::UserRole).toBool();

    QStyleOptionButton checkBoxStyle;
    checkBoxStyle.state = data ? QStyle::State_On : QStyle::State_Off;
    checkBoxStyle.state |= QStyle::State_Enabled;
    checkBoxStyle.iconSize = QSize(20, 20);

    int x, y, w, h, ow;
    ow = option.rect.width();
    option.rect.getRect(&x, &y, &w, &h);
    checkBoxStyle.rect = QRect(x + ow / 2, y, w / 2, h);

    QCheckBox checkBox;
    checkBox.style()->drawControl(QStyle::CE_CheckBox, &checkBoxStyle, painter, &checkBox);

    checkBox.setEnabled(false);
}

bool CheckBoxDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    QRect decorationRect = option.rect;

    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseButtonPress && decorationRect.contains(mouseEvent->pos()))
    {
        if (index.column() == CHECK_BOX_COLUMN)
        {
            bool data = model->data(index, Qt::UserRole).toBool();
            model->setData(index, !data, Qt::UserRole);
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

/****************************************************
 * PushButtonDelegate
 ****************************************************/
PushButtonDelegate::PushButtonDelegate(QString btnName, QWidget *parent) : QStyledItemDelegate(parent), m_btnName(btnName)
{
}

PushButtonDelegate::~PushButtonDelegate()
{
}

void PushButtonDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem viewOption(option);

    initStyleOption(&viewOption, index);

    if (option.state.testFlag(QStyle::State_HasFocus))
        viewOption.state = viewOption.state ^ QStyle::State_HasFocus;

    QStyledItemDelegate::paint(painter, viewOption, index);

    QStyleOptionButton button;
    button.rect = option.rect;
    button.text = m_btnName;
    button.state |= QStyle::State_Enabled;

    QPushButton pushBtn;
    pushBtn.style()->drawControl(QStyle::CE_PushButton, &button, painter, &pushBtn);
}

bool PushButtonDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    bool bRepaint = false;
    QMouseEvent *pEvent = static_cast<QMouseEvent *>(event);

    switch (event->type())
    {
    case QEvent::MouseButtonRelease:
    {
        emit btnClicked(index);
    }
    default:
        break;
    }

    return bRepaint;
}

/****************************************************
 * ComboDelegate
 ****************************************************/
ComboDelegate::ComboDelegate(QObject *parent) : QItemDelegate(parent)
{
}

void ComboDelegate::setItems(QStringList items)
{
    m_sItemList = items;
}

QWidget *ComboDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem & /*option*/, const QModelIndex & /*index*/) const
{
    QComboBox *editor = new QComboBox(parent);
    editor->setEditable(true);
    editor->addItems(m_sItemList);
    editor->installEventFilter(const_cast<ComboDelegate *>(this));
    return editor;
}

void ComboDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QString str = index.model()->data(index).toString();

    QComboBox *box = static_cast<QComboBox *>(editor);
    int i = box->findText(str);
    box->setCurrentIndex(i);
}

void ComboDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    QComboBox *box = static_cast<QComboBox *>(editor);
    QString str = box->currentText();
    model->setData(index, str);
}

void ComboDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex & /*index*/) const
{
    editor->setGeometry(option.rect);
}
