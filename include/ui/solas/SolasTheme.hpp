#pragma once

// Solas Edition: the warm, Claude-style colours behind the "Claude Dark" and "Claude Light" themes.
// ThemeManager::ApplyTheme() applies them like any other theme of the settings list (see ThemeManager.cpp);
// this header only supplies the palette, the style sheet and the tokens.
// Header-only and Q_OBJECT-free on purpose, so the fork needs no CMake/moc changes.
// Literals go through QString::fromUtf8 on plain narrow strings: that survives MSVC with or without /utf-8.

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
#include <QMenu>
#include <QPair>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QtGlobal>

#include <array>

#include "include/ui/setting/ThemeManager.hpp"

#ifndef SLS
#define SLS(x) QString::fromUtf8(x)
#endif

namespace Solas {

    // Theme names as stored in the settings and shown in the theme list. Never translated: the stored text is the key.
    inline QString claudeDarkTheme() { return SLS("Claude Dark"); }
    inline QString claudeLightTheme() { return SLS("Claude Light"); }

    inline bool isClaudeTheme(const QString &theme) {
        const QString lower = theme.trimmed().toLower();
        return lower == SLS("claude dark") || lower == SLS("claude light");
    }

    inline bool claudeThemeIsDark(const QString &theme) {
        return theme.trimmed().toLower() == SLS("claude dark");
    }

    // config/solas.ini next to the executable (portable), else the per-user config dir.
    inline QSettings &settings() {
        static QSettings *s = [] {
            const QString appDir = QCoreApplication::applicationDirPath();
            QString path;
            if (QFileInfo(appDir).isWritable()) {
                QDir(appDir).mkpath(SLS("config"));
                path = QDir(appDir).filePath(SLS("config/solas.ini"));
            } else {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
                QDir().mkpath(dir);
                path = QDir(dir).filePath(SLS("solas.ini"));
            }
            return new QSettings(path, QSettings::IniFormat);
        }();
        return *s;
    }

    struct Tokens {
        bool dark = true;
        QColor window, base, alt, raised, button, hover;
        QColor text, muted, faint, disabled;
        QColor accent, accentHover, accentPress, onAccent;
        QColor border, borderSoft, accentSoft, accentStrong;
        QColor danger, warning, success, info;
        std::array<QColor, 5> series;
    };

    inline QColor mix(const QColor &a, const QColor &b, qreal f) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f,
                                a.greenF() + (b.greenF() - a.greenF()) * f,
                                a.blueF() + (b.blueF() - a.blueF()) * f);
    }

    inline Tokens makeTokens(bool dark) {
        Tokens t;
        t.dark = dark;
        if (dark) {
            t.window = QColor(0x21, 0x20, 0x1E);
            t.base = QColor(0x19, 0x18, 0x16);
            t.alt = QColor(0x1E, 0x1D, 0x1A);
            t.raised = QColor(0x2A, 0x29, 0x26);
            t.button = QColor(0x30, 0x2E, 0x2A);
            t.hover = QColor(0x39, 0x37, 0x32);
            t.text = QColor(0xEC, 0xE9, 0xE0);
            t.muted = QColor(0xB0, 0xAE, 0xA5);
            t.faint = QColor(0x87, 0x86, 0x7F);
            t.disabled = QColor(0x5E, 0x5D, 0x59);
            t.accent = QColor(0xD9, 0x77, 0x57);
            t.accentHover = QColor(0xE2, 0x88, 0x6B);
            t.accentPress = QColor(0xC9, 0x64, 0x42);
            t.border = QColor(236, 233, 224, 38);
            t.borderSoft = QColor(236, 233, 224, 22);
            t.accentSoft = QColor(217, 119, 87, 52);
            t.accentStrong = QColor(217, 119, 87, 110);
            t.danger = QColor(0xE5, 0x6B, 0x6B);
            t.warning = QColor(0xD9, 0xA4, 0x41);
            t.success = QColor(0x8F, 0xB3, 0x6B);
            t.info = QColor(0x6A, 0x9B, 0xCC);
            t.series = {QColor(0xD9, 0x77, 0x57), QColor(0x6A, 0x9B, 0xCC), QColor(0x8F, 0xB3, 0x6B),
                        QColor(0xD9, 0xA4, 0x41), QColor(0xB0, 0x8C, 0xD9)};
        } else {
            t.window = QColor(0xFA, 0xF9, 0xF5);
            t.base = QColor(0xFF, 0xFF, 0xFF);
            t.alt = QColor(0xF5, 0xF4, 0xEE);
            t.raised = QColor(0xFF, 0xFF, 0xFF);
            t.button = QColor(0xFF, 0xFF, 0xFF);
            t.hover = QColor(0xF0, 0xEE, 0xE6);
            t.text = QColor(0x14, 0x14, 0x13);
            t.muted = QColor(0x5E, 0x5D, 0x59);
            t.faint = QColor(0x87, 0x86, 0x7F);
            t.disabled = QColor(0xB0, 0xAE, 0xA5);
            t.accent = QColor(0xC9, 0x64, 0x42);
            t.accentHover = QColor(0xD9, 0x77, 0x57);
            t.accentPress = QColor(0xB0, 0x52, 0x33);
            t.border = QColor(20, 20, 19, 40);
            t.borderSoft = QColor(20, 20, 19, 20);
            t.accentSoft = QColor(201, 100, 66, 36);
            t.accentStrong = QColor(201, 100, 66, 90);
            t.danger = QColor(0xC2, 0x41, 0x41);
            t.warning = QColor(0xA8, 0x6E, 0x12);
            t.success = QColor(0x4F, 0x7A, 0x34);
            t.info = QColor(0x3B, 0x6E, 0xA5);
            t.series = {QColor(0xC9, 0x64, 0x42), QColor(0x3B, 0x6E, 0xA5), QColor(0x5E, 0x8A, 0x3E),
                        QColor(0xB8, 0x82, 0x1E), QColor(0x86, 0x5F, 0xB8)};
        }
        t.onAccent = QColor(Qt::white);
        return t;
    }

    // Whether a Claude theme is the one on screen right now.
    inline bool active() {
        return isClaudeTheme(themeManager()->current_theme);
    }

    inline Tokens current() {
        return makeTokens(claudeThemeIsDark(themeManager()->current_theme));
    }

    inline QString css(const QColor &c) {
        if (c.alpha() == 255) return c.name(QColor::HexRgb);
        return SLS("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }

    inline QPalette makePalette(const Tokens &t) {
        QPalette p;
        p.setColor(QPalette::Window, t.window);
        p.setColor(QPalette::WindowText, t.text);
        p.setColor(QPalette::Base, t.base);
        p.setColor(QPalette::AlternateBase, t.alt);
        p.setColor(QPalette::ToolTipBase, t.raised);
        p.setColor(QPalette::ToolTipText, t.text);
        p.setColor(QPalette::PlaceholderText, t.faint);
        p.setColor(QPalette::Text, t.text);
        p.setColor(QPalette::Button, t.button);
        p.setColor(QPalette::ButtonText, t.text);
        p.setColor(QPalette::BrightText, QColor(Qt::white));
        p.setColor(QPalette::Link, t.dark ? t.accentHover : t.accent);
        p.setColor(QPalette::LinkVisited, t.accentPress);
        p.setColor(QPalette::Highlight, t.accent);
        p.setColor(QPalette::HighlightedText, t.onAccent);
        p.setColor(QPalette::Light, t.hover);
        p.setColor(QPalette::Midlight, t.hover);
        p.setColor(QPalette::Mid, t.faint);
        p.setColor(QPalette::Dark, t.dark ? QColor(0x14, 0x13, 0x11) : t.faint);
        p.setColor(QPalette::Shadow, QColor(0, 0, 0, t.dark ? 160 : 60));
        p.setColor(QPalette::Disabled, QPalette::WindowText, t.disabled);
        p.setColor(QPalette::Disabled, QPalette::Text, t.disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, t.disabled);
        p.setColor(QPalette::Disabled, QPalette::Highlight, t.hover);
        return p;
    }

    inline QString styleSheet(const Tokens &t) {
        QString qss = SLS(R"QSS(
QMainWindow, QDialog { background: {window}; color: {text}; }
QWidget { color: {text}; selection-background-color: {accent}; selection-color: {onAccent}; }
QWidget:disabled { color: {disabled}; }
QToolTip { background: {raised}; color: {text}; border: 1px solid {accentStrong}; border-radius: 8px; padding: 5px 9px; }

QMenuBar { background: {window}; border-bottom: 1px solid {borderSoft}; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: {accentSoft}; }
QMenu { background: {raised}; border: 1px solid {border}; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 26px 6px 26px; border-radius: 6px; }
QMenu::item:selected { background: {accentSoft}; color: {text}; }
QMenu::item:disabled { color: {disabled}; }
QMenu::separator { height: 1px; background: {borderSoft}; margin: 5px 8px; }

QTabWidget::pane { background: {raised}; border: 1px solid {borderSoft}; border-radius: 10px; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: {muted}; border: 1px solid transparent; border-radius: 8px; padding: 5px 14px; margin: 2px 3px 4px 0px; }
QTabBar::tab:hover { background: {hover}; color: {text}; }
QTabBar::tab:selected { background: {accentSoft}; color: {text}; border-color: {accentStrong}; }

QTableView, QTreeView, QListView, QTableWidget, QTreeWidget, QListWidget {
  background: {base}; alternate-background-color: {alt}; border: 1px solid {borderSoft}; border-radius: 10px;
  gridline-color: {borderSoft}; outline: none; }
QTableView::item:hover, QTreeView::item:hover, QListView::item:hover { background: {hover}; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: {accentSoft}; color: {text}; }
QHeaderView { background: transparent; border: none; }
QHeaderView::section { background: {window}; color: {muted}; font-weight: 600; padding: 6px 10px; border: none; border-bottom: 1px solid {borderSoft}; }
QHeaderView::section:hover { color: {text}; }
QTableCornerButton::section { background: {window}; border: none; }

QPushButton { background: {button}; color: {text}; border: 1px solid {border}; border-radius: 8px; padding: 5px 14px; min-height: 18px; }
QPushButton:hover { background: {hover}; border-color: {accentStrong}; }
QPushButton:pressed { background: {accentSoft}; border-color: {accent}; }
QPushButton:checked { background: {accentSoft}; border-color: {accent}; }
QPushButton:default { background: {accent}; border-color: {accent}; color: {onAccent}; font-weight: 600; }
QPushButton:default:hover { background: {accentHover}; border-color: {accentHover}; }
QPushButton:default:pressed { background: {accentPress}; }
QPushButton:disabled { background: {alt}; color: {disabled}; border-color: {borderSoft}; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 4px 6px; }
QToolButton:hover { background: {hover}; border-color: {border}; }
QToolButton:pressed, QToolButton:checked { background: {accentSoft}; border-color: {accentStrong}; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
  background: {base}; color: {text}; border: 1px solid {border}; border-radius: 8px; padding: 4px 8px;
  selection-background-color: {accent}; selection-color: {onAccent}; }
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color: {accentStrong}; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: {accent}; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: {raised}; border: 1px solid {border}; border-radius: 8px; padding: 4px;
  selection-background-color: {accentSoft}; selection-color: {text}; outline: none; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: {border}; border-radius: 3px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: {faint}; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: {border}; border-radius: 3px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: {faint}; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: {accentSoft}; }
QGroupBox { border: 1px solid {borderSoft}; border-radius: 10px; margin-top: 14px; padding: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: {muted}; }
QProgressBar { background: {alt}; border: none; border-radius: 4px; text-align: center; }
QProgressBar::chunk { background: {accent}; border-radius: 4px; }
QStatusBar { background: {window}; color: {muted}; }
QFrame#solasCard { background: {raised}; border: 1px solid {borderSoft}; border-radius: 12px; }
)QSS");
        const QList<QPair<QString, QColor>> vars = {
            {SLS("window"), t.window}, {SLS("base"), t.base}, {SLS("alt"), t.alt}, {SLS("raised"), t.raised},
            {SLS("button"), t.button}, {SLS("hover"), t.hover}, {SLS("text"), t.text}, {SLS("muted"), t.muted},
            {SLS("faint"), t.faint}, {SLS("disabled"), t.disabled}, {SLS("accentHover"), t.accentHover},
            {SLS("accentPress"), t.accentPress}, {SLS("accentSoft"), t.accentSoft}, {SLS("accentStrong"), t.accentStrong},
            {SLS("accent"), t.accent}, {SLS("onAccent"), t.onAccent}, {SLS("borderSoft"), t.borderSoft}, {SLS("border"), t.border},
        };
        for (const auto &v : vars) qss.replace(SLS("{") + v.first + SLS("}"), css(v.second));
        return qss;
    }

    inline QColor dangerColor() {
        if (active()) return current().danger;
        const QColor c = themeManager()->tokens.danger;
        return c.isValid() ? c : QColor(0xD0, 0x45, 0x45);
    }

    inline QColor warningColor() {
        if (active()) return current().warning;
        const QColor c = themeManager()->tokens.warning;
        return c.isValid() ? c : QColor(0xC0, 0x80, 0x20);
    }

    inline QColor successColor() {
        if (active()) return current().success;
        const QColor c = themeManager()->tokens.success;
        return c.isValid() ? c : QColor(0x4F, 0x8A, 0x3E);
    }

    inline QColor seriesColor(int i) {
        if (active()) return current().series[size_t(i) % 5];
        const QColor c = themeManager()->tokens.chartSeries[size_t(i) % 5];
        if (c.isValid()) return c;
        static const QColor fallback[5] = {QColor(0xD9, 0x77, 0x57), QColor(0x6A, 0x9B, 0xCC), QColor(0x8F, 0xB3, 0x6B),
                                           QColor(0xD9, 0xA4, 0x41), QColor(0xB0, 0x8C, 0xD9)};
        return fallback[i % 5];
    }

    // Throne's own widgets (proxy list, charts, log) read these instead of the palette.
    // Called by ThemeManager::ApplyTheme() after resolveTokens(), so a Claude theme keeps its own colours.
    inline void fillThemeTokens(ThemeTokens &tk, const Tokens &t) {
        tk.surface = t.base;
        tk.onSurface = t.text;
        tk.accent = t.accent;
        tk.muted = t.faint;
        tk.tag = t.info;
        tk.danger = t.danger;
        tk.warning = t.warning;
        tk.success = t.success;
        tk.info = t.info;
        tk.chartSeries = t.series;
        tk.chartContext = t.faint;
        tk.runningRow = mix(t.base, t.accent, t.dark ? 0.22 : 0.14);
        tk.onRunningRow = t.text;
        tk.runningMark = t.accent;
        tk.onRunningMark = t.onAccent;
    }

} // namespace Solas
