#include "modulewidget.h"

#include <QEvent>
#include <QDebug>

ModuleWidget::ModuleWidget()
    : QWidget(nullptr)
{
    m_moduleIcon = new QLabel;
    m_moduleIcon->setFixedSize(32, 32);
    m_moduleIcon->setStyleSheet("background-color:pink;");
    m_moduleTitle = new LargeLabel;
    m_moduleTitle->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_moduleTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_moduleTitle->setStyleSheet("color: white;"
//                                 "font-size: 16px;"
                                 "margin: 0 0 0 5px;");

    QHBoxLayout *titleLayout = new QHBoxLayout;
    titleLayout->addWidget(m_moduleIcon);
    titleLayout->setAlignment(m_moduleIcon, Qt::AlignCenter);
    titleLayout->addWidget(m_moduleTitle);
    titleLayout->setSpacing(0);
    titleLayout->setMargin(0);

    m_centeralLayout = new QVBoxLayout;
    m_centeralLayout->addLayout(titleLayout);
    m_centeralLayout->setSpacing(8);
    m_centeralLayout->setMargin(0);

    setLayout(m_centeralLayout);
}

void ModuleWidget::setTitle(const QString &title)
{
    m_moduleTitle->setText(title);
}

void ModuleWidget::setIcon(const QPixmap &icon)
{
    m_moduleIcon->setPixmap(icon);
}

bool ModuleWidget::event(QEvent *event)
{
    if (event->type() == QEvent::LayoutRequest)
        setFixedHeight(m_centeralLayout->sizeHint().height());

    return QWidget::event(event);
}
