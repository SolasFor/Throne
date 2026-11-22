#include "include/ui/mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QKeySequence>
#include <QMenu>
#include <QTabWidget>

#include "include/engine/EngineHost.h"
#include "include/engine/services/StatsService.h"
#include "include/ui/solas/SolasTheme.hpp"
#include "include/ui/solas/QuickRules.hpp"
#include "include/ui/solas/RuleStatsTab.hpp"
#include "include/ui/utils/ThemedIcon.hpp"

namespace {
    // Adds action to menu once, behind a separator; re-checked on every opening in case the menu was rebuilt.
    void keepInMenu(QMenu *menu, QAction *action) {
        if (menu == nullptr || action == nullptr) return;
        const auto add = [menu, action] {
            if (menu->actions().contains(action)) return;
            if (!menu->actions().isEmpty() && !menu->actions().last()->isSeparator()) menu->addSeparator();
            menu->addAction(action);
        };
        add();
        QObject::connect(menu, &QMenu::aboutToShow, menu, add);
    }

    // Solas Edition: theme, quick rules (Ctrl+R) and the Rules tab. Everything is owned by the window,
    // so a menu clear() never deletes it.
    void installSolasEdition(QMainWindow *window, QTabWidget *stats, QMenu *routingMenu, QMenu *programMenu) {
        Solas::installTheme(window);

        auto *quick = new QAction(SLS("Быстрые правила…"), window);
        quick->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
        quick->setShortcutContext(Qt::WindowShortcut);
        window->addAction(quick);
        QObject::connect(quick, &QAction::triggered, window, [window] { Solas::openQuickRules(window); });
        keepInMenu(routingMenu, quick);
        keepInMenu(programMenu, Solas::themeMenuAction(window));

        if (stats == nullptr) return;
        auto *tab = new Solas::RuleStatsTab;
        stats->addTab(tab, SLS("Правила"));
        auto *keeper = new Solas::FeedKeeper(window, stats, tab);
        window->installEventFilter(keeper);
        tab->feedChanged = [keeper] { keeper->schedule(); };
        QObject::connect(stats, &QTabWidget::currentChanged, keeper, [keeper](int) { keeper->schedule(); });
        if (auto *host = Engine::host(); host != nullptr && host->stats() != nullptr) {
            QObject::connect(host->stats(), &Engine::StatsService::connectionsUpdated, tab,
                             [tab](const QList<Stats::ConnectionMetadata> &rows) { tab->ingest(rows); });
        }
        keeper->schedule();
    }
} // namespace

void MainWindow::setupThemedIcons() {
    ui->toolButton_program->setIcon(ThemedIcon(QStringLiteral("menu"), 215));
    ui->toolButton_preferences->setIcon(ThemedIcon(QStringLiteral("sliders-horizontal"), 270));
    ui->toolButton_testing->setIcon(ThemedIcon(QStringLiteral("folders"), 35));
    ui->toolButton_routing->setIcon(ThemedIcon(QStringLiteral("route"), 140));
    ui->toolButton_tools->setIcon(ThemedIcon(QStringLiteral("wrench"), 350));

    static bool solasInstalled = false;
    if (!solasInstalled) {
        solasInstalled = true;
        installSolasEdition(this, ui->stats_widget, ui->menuRouting_Menu, ui->menu_program);
    }
}
