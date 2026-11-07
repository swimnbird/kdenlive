/*
    SPDX-FileCopyrightText: 2025 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kddocksetup.h"
#include "core.h"
#include "kdenlivesettings.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QObject>
#include <QTabBar>

#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QRegion>
#include <QStyleFactory>
#include <QWindow>

#include <functional>

class DockTabDragHandler : public QObject
{
public:
    explicit DockTabDragHandler(KDDockWidgets::Core::DockWidget *coreDock, KDDockWidgets::Core::TabBar *coreTabBar, KDDockWidgets::QtWidgets::TabBar *qtTabBar,
                                std::function<void()> onDragEnteredTab = {}, std::function<bool(const QMimeData *)> mimeHandler = {}, QObject *parent = nullptr)
        : QObject(parent)
        , m_coreDock(coreDock)
        , m_coreTabBar(coreTabBar)
        , m_qtTabBar(qtTabBar)
        , m_onDragEnteredTab(std::move(onDragEnteredTab))
        , m_mimeHandler(std::move(mimeHandler))
    {
        qtTabBar->setAcceptDrops(true);
        qtTabBar->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj != m_qtTabBar || !m_coreTabBar) {
            return QObject::eventFilter(obj, event);
        }

        switch (event->type()) {
        case QEvent::DragEnter: {
            auto *e = static_cast<QDragEnterEvent *>(event);
            e->accept();
            return true;
        }
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            const int idx = m_qtTabBar->tabAt(e->position().toPoint());
            if (idx >= 0) {
                if (m_mimeHandler && m_mimeHandler(e->mimeData()) && m_coreTabBar->dockWidgetAt(idx) == m_coreDock && m_onDragEnteredTab) {
                    m_onDragEnteredTab();
                }
                e->ignore();
            }
            return true;
            break;
        }
        default:
            break;
        }

        return QObject::eventFilter(obj, event);
    }

private:
    KDDockWidgets::Core::DockWidget *m_coreDock = nullptr;
    KDDockWidgets::Core::TabBar *m_coreTabBar = nullptr;
    KDDockWidgets::QtWidgets::TabBar *m_qtTabBar = nullptr;
    std::function<void()> m_onDragEnteredTab;
    std::function<bool(const QMimeData *)> m_mimeHandler;
};

class KdenliveDockTabBar : public KDDockWidgets::QtWidgets::TabBar
{
public:
    explicit KdenliveDockTabBar(KDDockWidgets::Core::TabBar *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::TabBar(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
        auto parentWidget = KDDockWidgets::QtCommon::View_qt::asQWidget(parent);
        setProperty("_breeze_force_frame", false);
        setDocumentMode(true);
        parentWidget->setProperty("_breeze_force_frame", false);
        setContextMenuPolicy(Qt::CustomContextMenu);
        // The constructor of KDDockWidgets::QtWidgets::TabBar makes a QProxyStyle
        // that ends up taking ownership of the style for the entire application!
        if (QProxyStyle *proxy_style = qobject_cast<QProxyStyle *>(style())) {
            proxy_style->baseStyle()->setParent(qApp);
            proxy_style->setBaseStyle(QStyleFactory::create(qApp->style()->name()));
        }
        setPalette(qApp->palette());

        connect(this, &QWidget::customContextMenuRequested, []() { Q_EMIT pCore.get()->switchTitleBars(); });
        connect(this, &KDDockWidgets::QtWidgets::TabBar::countChanged, [&]() {
            if (!KdenliveSettings::showtitlebars()) {
                pCore->startHideBarsTimer();
            }
        });
        connect(this, &KDDockWidgets::QtWidgets::TabBar::dockWidgetInserted, [&, this, controller](int index) {
            auto dock = controller->dockWidgetAt(index);
            if (dock && dock->view() && dock->uniqueName() == QStringLiteral("project_bin")) {
                KDDockWidgets::QtWidgets::DockWidget *qtDock = dynamic_cast<KDDockWidgets::QtWidgets::DockWidget *>(dock->view());
                new DockTabDragHandler(
                    dock, controller, this,
                    [qtDock]() {
                        qtDock->open();
                        qtDock->setAsCurrentTab();
                        qtDock->setFocus(Qt::OtherFocusReason);
                    },
                    [](const QMimeData *mime) { return mime && (mime->hasFormat("application/x-kdenlive-clip") || mime->hasUrls() || mime->hasText()); }, this);
            }
        });
    }
};

class KdenliveDockGroup : public KDDockWidgets::QtWidgets::Group
{
public:
    explicit KdenliveDockGroup(KDDockWidgets::Core::Group *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::Group(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
        // The group itself paints the gutter and the rounded fill. Children must not fill square corners over it.
        setAttribute(Qt::WA_StyledBackground, false);
    }

    static constexpr int kRadius = 8;
    static constexpr int kGutter = 4;

    void syncStoryClip()
    {
        if (m_syncing) {
            return;
        }
        m_syncing = true;
        const bool story = KdenliveSettings::storyAppearance() == QLatin1String("story");
        const auto descendants = findChildren<QWidget *>();
        if (!story || width() < 32 || height() < 32) {
            clearStoryMasks(descendants);
            m_syncing = false;
            update();
            return;
        }
        const QRect panelRect = rect().adjusted(kGutter, kGutter, -kGutter, -kGutter);
        QPainterPath path;
        path.addRoundedRect(QRectF(panelRect), kRadius, kRadius);
        const QRegion panelRegion(path.toFillPolygon().toPolygon());
        for (QWidget *child : descendants) {
            const bool frame = child->parentWidget() == this;
            const bool native = child->testAttribute(Qt::WA_NativeWindow) || child->inherits("QQuickWidget");
            if (!frame && !native) {
                continue;
            }
            if (!child->isVisible() || child->size().isEmpty()) {
                continue;
            }
            const QPoint origin = child->mapTo(this, QPoint(0, 0));
            const QRect childRect(origin, child->size());
            if (!childRect.intersects(rect())) {
                continue;
            }
            const QRegion local = panelRegion.translated(-origin);
            const QRect bounds = child->rect();
            const QPoint corners[] = {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()};
            bool cutsCorner = false;
            for (const QPoint &corner : corners) {
                // QRegion::contains(QRect) is overlap, not containment. Test the corner pixels themselves.
                if (!local.contains(corner)) {
                    cutsCorner = true;
                    break;
                }
            }
            if (!cutsCorner) {
                clearOneMask(child);
                continue;
            }
            const QRegion mask = local.intersected(QRegion(child->rect()));
            if (mask.isEmpty() || child->mask() == mask) {
                child->setProperty("storyClip", true);
                continue;
            }
            child->setMask(mask);
            if (QWindow *window = child->windowHandle()) {
                window->setMask(mask);
            }
            child->setProperty("storyClip", true);
        }
        m_syncing = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (KdenliveSettings::storyAppearance() != QLatin1String("story") || width() < 32 || height() < 32) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(12, 12, 14));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(38, 38, 42));
        painter.drawRoundedRect(QRectF(rect()).adjusted(kGutter, kGutter, -kGutter, -kGutter), kRadius, kRadius);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        KDDockWidgets::QtWidgets::Group::resizeEvent(event);
        syncStoryClip();
    }

    void showEvent(QShowEvent *event) override
    {
        KDDockWidgets::QtWidgets::Group::showEvent(event);
        syncStoryClip();
    }

    bool event(QEvent *event) override
    {
        const bool handled = KDDockWidgets::QtWidgets::Group::event(event);
        if (event->type() == QEvent::ChildAdded || event->type() == QEvent::LayoutRequest) {
            syncStoryClip();
        }
        return handled;
    }

private:
    void clearOneMask(QWidget *child)
    {
        if (!child->property("storyClip").toBool()) {
            return;
        }
        child->clearMask();
        if (QWindow *window = child->windowHandle()) {
            window->setMask(QRegion());
        }
        child->setProperty("storyClip", false);
    }

    void clearStoryMasks(const QList<QWidget *> &descendants)
    {
        for (QWidget *child : descendants) {
            clearOneMask(child);
        }
    }

    bool m_syncing{false};
};

void refreshStoryDockFrames(QWidget *root)
{
    if (root == nullptr) {
        return;
    }
    const auto groups = root->findChildren<KDDockWidgets::QtWidgets::Group *>();
    for (KDDockWidgets::QtWidgets::Group *group : groups) {
        static_cast<KdenliveDockGroup *>(group)->syncStoryClip();
    }
}

class KdenliveDockStack : public KDDockWidgets::QtWidgets::Stack
{
public:
    explicit KdenliveDockStack(KDDockWidgets::Core::Stack *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::Stack(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent))
    {
    }
    void paintEvent(QPaintEvent *) override {}
};

class KdenliveDockTitleBar : public KDDockWidgets::QtWidgets::TitleBar
{
public:
    explicit KdenliveDockTitleBar(KDDockWidgets::Core::TitleBar *controller, KDDockWidgets::Core::View *parent = nullptr)
        : KDDockWidgets::QtWidgets::TitleBar(controller, parent)
        , m_controller(controller)
    {
        connect(pCore.get(), &Core::hideBars, this, [this](bool hide) {
            if (hide) {
#if defined(Q_OS_WIN)
                auto parentWidget = m_controller->view()->parentView();
                if (parentWidget && parentWidget->asFloatingWindowController() != nullptr) {
                    // Floating window, don't show as we already have the widget titlebar
                    return;
                }
#endif
            } else {
                if (m_controller->dockWidgets().size() > 1) {
                    // Don't show title bar when there are tabbed widgets
                    return;
                }
                auto parentWidget = m_controller->view()->parentView();
                if (parentWidget && parentWidget->asFloatingWindowController() != nullptr) {
                    // Floating window, don't show as we already have the widget titlebar
                    return;
                }
            }
            setVisible(!hide);
        });
    }

private:
    KDDockWidgets::Core::TitleBar *const m_controller;
};

class KdenliveDockSeparator : public KDDockWidgets::QtWidgets::Separator
{
public:
    explicit KdenliveDockSeparator(KDDockWidgets::Core::Separator *controller, KDDockWidgets::Core::View *parent)
        : KDDockWidgets::QtWidgets::Separator(controller, parent)
        , m_controller(controller)
    {
    }

    ~KdenliveDockSeparator() override;

    void enterEvent(KDDockWidgets::Qt5Qt6Compat::QEnterEvent *event) override
    {
        hovered = true;
        KDDockWidgets::QtWidgets::Separator::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        hovered = false;
        KDDockWidgets::QtWidgets::Separator::leaveEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (KdenliveSettings::storyAppearance() == QLatin1String("story")) {
            // The groups paint their own gutter. A center hairline would read as a second seam.
            p.fillRect(QWidget::rect(), QColor(12, 12, 14));
            return;
        }
        QColor separatorColor = hovered ? palette().highlight().color() : palette().midlight().color();
        if (hovered) {
            separatorColor.setAlpha(128);
        }
        QPen pen(separatorColor);
        pen.setWidth(1);
        if (m_controller->isVertical()) {
            // Vertical rect
            p.fillRect(QWidget::rect(), palette().window());
            p.setPen(pen);
            p.drawLine(QWidget::rect().x(), QWidget::rect().y() + QWidget::rect().height() / 2, QWidget::rect().right(),
                       QWidget::rect().y() + QWidget::rect().height() / 2);
        } else {
            // Horizontal rect
            p.fillRect(QWidget::rect(), palette().window());
            p.setPen(pen);
            p.drawLine(QWidget::rect().x() + QWidget::rect().width() / 2, QWidget::rect().top(), QWidget::rect().x() + QWidget::rect().width() / 2,
                       QWidget::rect().bottom());
        }
    }

private:
    KDDockWidgets::Core::Separator *const m_controller;
    bool hovered{false};
};

KdenliveDockSeparator::~KdenliveDockSeparator() = default;

KDDockWidgets::Core::View *CustomWidgetFactory::createTitleBar(KDDockWidgets::Core::TitleBar *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockTitleBar(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createGroup(KDDockWidgets::Core::Group *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockGroup(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createStack(KDDockWidgets::Core::Stack *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockStack(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createSeparator(KDDockWidgets::Core::Separator *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockSeparator(controller, parent);
}

KDDockWidgets::Core::View *CustomWidgetFactory::createTabBar(KDDockWidgets::Core::TabBar *controller, KDDockWidgets::Core::View *parent) const
{
    return new KdenliveDockTabBar(controller, parent);
}
