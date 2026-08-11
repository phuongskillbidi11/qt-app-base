#include "table_style.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace {

// Keeps an empty-table hint the same size as the viewport it floats over, and shows it
// only while there are no rows. An event filter is used rather than subclassing so the
// tabs can keep using plain QTableWidget.
class EmptyHintRelay : public QObject {
public:
    EmptyHintRelay(QLabel *hint, QTableWidget *table)
        : QObject(table), m_hint(hint), m_table(table) {
        m_table->viewport()->installEventFilter(this);
        connect(m_table->model(), &QAbstractItemModel::rowsInserted, this, [this] { sync(); });
        connect(m_table->model(), &QAbstractItemModel::rowsRemoved, this, [this] { sync(); });
        connect(m_table->model(), &QAbstractItemModel::modelReset, this, [this] { sync(); });
        sync();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == m_table->viewport() && event->type() == QEvent::Resize) {
            sync();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void sync() {
        m_hint->setGeometry(m_table->viewport()->rect());
        m_hint->setVisible(m_table->rowCount() == 0);
    }

    QLabel *m_hint;
    QTableWidget *m_table;
};

}  // namespace

namespace UiStyle {

void tuneTable(QTableWidget *table) {
    if (table == nullptr) {
        return;
    }
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(26);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setFixedHeight(30);
    table->setFont(QFont(QStringLiteral("Consolas"), 9));
}

void alignNumericColumn(QTableWidget *table, int column) {
    if (table == nullptr || column < 0 || column >= table->columnCount()) {
        return;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        if (QTableWidgetItem *item = table->item(row, column)) {
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

QLabel *attachEmptyHint(QTableWidget *table, const QString &text) {
    if (table == nullptr) {
        return nullptr;
    }
    auto *hint = new QLabel(text, table->viewport());
    hint->setAlignment(Qt::AlignCenter);
    hint->setProperty("hint", true);
    hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    new EmptyHintRelay(hint, table);
    return hint;
}

void makeBarButton(QPushButton *button, bool accent) {
    if (button == nullptr) {
        return;
    }
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setMinimumWidth(96);
    button->setFixedHeight(28);
    button->setProperty("fixed", true);
    if (accent) {
        button->setProperty("accent", true);
    }
    repolish(button);
}

void repolish(QWidget *widget) {
    if (widget == nullptr) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

}  // namespace UiStyle
