#include "include/ui/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QKeySequence>
#include <QMenu>
#include <QSignalBlocker>
#include <QTabWidget>

#include <cstring>

#include "include/ui/solas/SolasTheme.hpp"
#include "include/ui/solas/QuickRules.hpp"
#include "include/ui/solas/RuleEditor.hpp"
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

    // Appends "Claude Dark" / "Claude Light" to the theme combo of the basic settings dialog the first time it is
    // shown, then reselects the stored theme: the dialog picks it by text before these items exist. Done here, so the
    // fork does not touch dialog_basic_settings.cpp. The combo's own currentIndexChanged handler applies and stores
    // the choice as for any other theme (ThemeManager::ApplyTheme knows both names).
    class ClaudeThemeListFiller : public QObject {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::Show) fill(qobject_cast<QComboBox *>(watched));
            return QObject::eventFilter(watched, event);
        }

    private:
        static void fill(QComboBox *box) {
            if (box == nullptr || box->objectName() != QStringLiteral("theme")) return;
            QWidget *dialog = box->window();
            if (dialog == nullptr || std::strcmp(dialog->metaObject()->className(), "DialogBasicSettings") != 0) return;
            if (box->findText(Solas::claudeDarkTheme()) >= 0) return;

            box->addItem(Solas::claudeDarkTheme());
            box->addItem(Solas::claudeLightTheme());
            const QString current = themeManager()->current_theme;
            if (Solas::isClaudeTheme(current)) {
                const int index = box->findText(current, Qt::MatchFixedString);
                // Same theme as on screen: ApplyTheme returns early and the dialog caches the same name.
                if (index >= 0) box->setCurrentIndex(index);
            }
        }
    };

    // Solas Edition: quick rules (Ctrl+R), the Rules tab and the Claude entries of the theme list.
    // Everything is owned by the window, so a menu clear() never deletes it.
    void installSolasEdition(QMainWindow *window, QTabWidget *stats, QMenu *routingMenu) {
        qApp->installEventFilter(new ClaudeThemeListFiller(window));

        auto *quick = new QAction(SLS("Быстрые правила…"), window);
        quick->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
        quick->setShortcutContext(Qt::WindowShortcut);
        window->addAction(quick);
        QObject::connect(quick, &QAction::triggered, window, [window] { Solas::openQuickRules(window); });
        keepInMenu(routingMenu, quick);

        // The Rules tab is the rule editor itself: no sub-views, it re-reads the active profile whenever it is shown.
        if (stats == nullptr) return;
        stats->addTab(new Solas::RuleEditorPanel, SLS("Правила"));
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
        installSolasEdition(this, ui->stats_widget, ui->menuRouting_Menu);
    }
}
