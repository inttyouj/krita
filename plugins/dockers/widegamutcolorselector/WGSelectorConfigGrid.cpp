/*
 * SPDX-FileCopyrightText: 2020 Mathias Wein <lynx.mw+kde@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WGSelectorConfigGrid.h"

#include "KisColorSelectorConfiguration.h"
#include "KisVisualColorSelector.h"

#include "kis_debug.h"

#include <QAction>
#include <QActionGroup>
#include <QGridLayout>
#include <QIcon>
#include <QString>
#include <QToolButton>


class SelectorConfigAction: public QAction
{
public:
    explicit SelectorConfigAction(const KisColorSelectorConfiguration &cfg, QObject *parent)
        : QAction(parent)
        , m_config(cfg)
    {
        setCheckable(true);
    }
    const KisColorSelectorConfiguration& configuration() { return m_config; }
private:
    KisColorSelectorConfiguration m_config;
};

WGSelectorConfigGrid::WGSelectorConfigGrid(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QGridLayout(this))
    , m_actionGroup(new QActionGroup(this))
    , m_selector(new KisVisualColorSelector(this))
{
    m_actionGroup->setExclusive(true);
    connect(m_actionGroup, SIGNAL(triggered(QAction*)), SLOT(slotActionTriggered(QAction*)));

    m_selector->setGeometry(0, 0, m_iconSize, m_iconSize);
    m_selector->setVisible(false);
    m_selector->setEnabled(false);
    m_selector->slotSetColorSpace(KoColorSpaceRegistry::instance()->rgb8());
    m_selector->slotSetColor(KoColor(QColor(255, 0, 0), KoColorSpaceRegistry::instance()->rgb8()));
}

void WGSelectorConfigGrid::clear()
{
    while (QLayoutItem *child = m_layout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    qDeleteAll(m_actionGroup->actions());
}

void WGSelectorConfigGrid::setColorModel(KisVisualColorModel::ColorModel model)
{
    if (model != m_selector->selectorModel()->colorModel()) {
        m_selector->selectorModel()->setColorModel(model);
        updateIcons();
    }
}

void WGSelectorConfigGrid::setConfigurations(const QVector<KisColorSelectorConfiguration> &configurations)
{
    clear();
    // exclusive action groups have no nice way to set none as checked, and
    // ExclusiveOptional is not what we want either, so use a dummy action
    m_dummyAction = new QAction("dummy", m_actionGroup);
    m_dummyAction->setCheckable(true);
    m_dummyAction->setChecked(true);
    m_currentAction = m_dummyAction;

    for (int i = 0; i < configurations.size(); i++) {
        const KisColorSelectorConfiguration &config = configurations.at(i);
        SelectorConfigAction *action = new SelectorConfigAction(config, m_actionGroup);
        //action->setText(QString("TBD %1").arg(i));
        action->setIcon(generateIcon(config, devicePixelRatioF()));

        QToolButton *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setDefaultAction(action);
        button->setIconSize(QSize(m_iconSize, m_iconSize));
        m_layout->addWidget(button, i/m_columns, i%m_columns);
    }
}

/*
void WGSelectorConfigGrid::setChecked(const KisColorSelectorConfiguration &configuration)
{
    const QList<QAction*> actions = m_actionGroup->actions();
    for (QAction *action: actions) {
        SelectorConfigAction *sa = dynamic_cast<SelectorConfigAction *>(action);
        if (sa && sa->configuration() == configuration) {
            sa->setChecked(true);
            return;
        }
    }
    m_dummyAction->setChecked(true);
}
*/

QIcon WGSelectorConfigGrid::generateIcon(const KisColorSelectorConfiguration &configuration, qreal pixelRatio) const
{
    QSize sz(m_selector->width() * pixelRatio, m_selector->height() * pixelRatio);
    QPixmap pixmap(sz);
    pixmap.setDevicePixelRatio(pixelRatio);
    pixmap.fill(Qt::transparent);
    m_selector->setConfiguration(&configuration);
    m_selector->render(&pixmap, QPoint(), QRegion(), RenderFlag::DrawChildren);
    return QIcon(pixmap);
}

void WGSelectorConfigGrid::slotActionTriggered(QAction *action)
{
    // Unfortunately, exclusive QActionGroups don't filter out attempts to toggle
    // an action that is already checked, so need to check ourselves if checked
    // action really changed. Checking the hidden dummy also is a silent operation.
    if (action == m_currentAction) {
        return;
    }
    m_currentAction = action;
    if (action != m_dummyAction) {
        SelectorConfigAction *sa = dynamic_cast<SelectorConfigAction *>(action);
        KIS_SAFE_ASSERT_RECOVER_RETURN(sa);

        emit sigConfigSelected(sa->configuration());
    }
}

void WGSelectorConfigGrid::updateIcons() {
    const QList<QAction*> actions = m_actionGroup->actions();
    for (QAction *action: actions) {
        SelectorConfigAction *sa = dynamic_cast<SelectorConfigAction *>(action);
        if (sa) {
            sa->setIcon(generateIcon(sa->configuration(), devicePixelRatioF()));
        }
    }
}
