#pragma once

// Solas Edition: the "Rules" tab of the bottom panel ("Управление маршрутами").
// Organizes simple rules of the active routing profile into two tabs ("Процессы" and "Домены") inside a compact
// table-style list with inline editing, process icons, quick running-process picker, search, and bulk direction changes.
// Only manages the 3 supported simple rule types: domain: (По домену), suffix: (По домену и поддомену), and
// processName: (По имени процесса). Any other simple rules in the profile are preserved untouched.
// Header-only and Q_OBJECT-free, like the rest of the fork.

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

#include "include/database/TrafficStatsRepo.h"
#include "include/engine/EngineEvents.h"
#include "include/engine/EngineHost.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/ui/setting/ProcessPicker.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/solas/QuickRules.hpp"
#include "include/ui/solas/SolasTheme.hpp"
#include "include/ui/utils/NativeFileDialog.h"
#ifdef Q_OS_WIN
#include "include/sys/windows/guihelper.h"
#endif

namespace Solas {

    enum class ManagedRuleKind { ProcessName, DomainSuffix, DomainExact };

    inline QString domainPrefix() { return SLS("domain:"); }
    inline QString suffixPrefix() { return SLS("suffix:"); }
    inline QString processNamePrefix() { return SLS("processName:"); }

    inline QString managedPrefix(ManagedRuleKind kind) {
        switch (kind) {
            case ManagedRuleKind::ProcessName: return processNamePrefix();
            case ManagedRuleKind::DomainSuffix: return suffixPrefix();
            case ManagedRuleKind::DomainExact: return domainPrefix();
        }
        return suffixPrefix();
    }

    inline QString managedKindLabel(ManagedRuleKind kind) {
        switch (kind) {
            case ManagedRuleKind::ProcessName: return SLS("По имени процесса");
            case ManagedRuleKind::DomainSuffix: return SLS("По домену и поддомену");
            case ManagedRuleKind::DomainExact: return SLS("По домену");
        }
        return {};
    }

    inline QString managedPlaceholder(ManagedRuleKind kind) {
        switch (kind) {
            case ManagedRuleKind::ProcessName: return SLS("chrome.exe");
            case ManagedRuleKind::DomainSuffix: return SLS("example.com");
            case ManagedRuleKind::DomainExact: return SLS("www.example.com");
        }
        return {};
    }

    inline std::optional<ManagedRuleKind> parseManagedRule(const QString &line, QString *value) {
        struct Mapping {
            QString prefix;
            ManagedRuleKind kind;
        };
        const Mapping mappings[] = {
            {processNamePrefix(), ManagedRuleKind::ProcessName},
            {suffixPrefix(), ManagedRuleKind::DomainSuffix},
            {domainPrefix(), ManagedRuleKind::DomainExact},
        };
        for (const auto &m : mappings) {
            if (line.startsWith(m.prefix, Qt::CaseInsensitive)) {
                if (value != nullptr) *value = line.mid(m.prefix.size()).trimmed();
                return m.kind;
            }
        }
        if (value != nullptr) *value = line;
        return std::nullopt;
    }

    inline int actionIndexOf(Configs::simpleAction action) {
        for (int i = 0; i < actionKinds().size(); ++i) {
            if (actionKinds()[i].action == action) return i;
        }
        return 0;
    }

    inline QString canonicalProcessNameFromFile(const QString &chosen, QString *resolvedPath = nullptr) {
        QString finalPath;
#ifdef Q_OS_MACOS
        QString candidate = chosen;
        if (candidate.endsWith(SLS(".app"), Qt::CaseInsensitive) && QFileInfo(candidate).isDir()) {
            const QSettings info(candidate + SLS("/Contents/Info.plist"), QSettings::NativeFormat);
            const QString executable = info.value(SLS("CFBundleExecutable")).toString();
            if (!executable.isEmpty()) candidate += SLS("/Contents/MacOS/") + executable;
        }
        const QFileInfo macInfo(candidate);
        finalPath = macInfo.canonicalFilePath();
        if (finalPath.isEmpty()) finalPath = macInfo.absoluteFilePath();
#elif defined(Q_OS_WIN)
        finalPath = Windows_FinalPath(chosen);
        if (finalPath.isEmpty()) finalPath = QDir::toNativeSeparators(QFileInfo(chosen).absoluteFilePath());
#else
        const QFileInfo info(chosen);
        finalPath = info.canonicalFilePath();
        if (finalPath.isEmpty()) finalPath = info.absoluteFilePath();
#endif
        if (resolvedPath != nullptr) *resolvedPath = finalPath;
        return QFileInfo(finalPath).fileName();
    }

    // Heap-allocated so Qt never destroys QIcon/QFileIconProvider statics after QApplication teardown.
    inline QIcon fallbackAppIcon() {
        static auto *icon = [] {
            QPixmap pix(32, 32);
            pix.setDevicePixelRatio(2.0);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);
            QLinearGradient grad(0, 0, 16, 16);
            grad.setColorAt(0.0, QColor(0xE2, 0x88, 0x6B));
            grad.setColorAt(1.0, QColor(0xB8, 0x4E, 0x35));
            p.setPen(Qt::NoPen);
            p.setBrush(grad);
            p.drawRoundedRect(QRectF(0.5, 0.5, 15.0, 15.0), 4.0, 4.0);
            p.setBrush(QColor(255, 255, 255, 210));
            p.drawRoundedRect(QRectF(4.5, 4.5, 7.0, 7.0), 1.8, 1.8);
            p.end();
            auto *res = new QIcon;
            res->addPixmap(pix);
            return res;
        }();
        return *icon;
    }

    inline QIcon iconForFilePath(const QString &path) {
        static auto *provider = new QFileIconProvider;
        static auto *cache = new QHash<QString, QIcon>;
        if (path.isEmpty() || !QFileInfo::exists(path)) return fallbackAppIcon();
        const auto it = cache->constFind(path);
        if (it != cache->cend()) return *it;
        QIcon found = provider->icon(QFileInfo(path));
        if (found.isNull()) found = fallbackAppIcon();
        cache->insert(path, found);
        return found;
    }

    inline Tokens resolvePanelTokens() {
        if (Solas::active()) return Solas::current();
        const QPalette pal = qApp->palette();
        const QColor win = pal.color(QPalette::Window);
        const bool dark = win.lightness() < 128;
        Tokens t = makeTokens(dark);
        const ThemeTokens &tk = themeManager()->tokens;
        if (tk.surface.isValid()) t.window = tk.surface;
        if (pal.color(QPalette::Base).isValid()) t.base = pal.color(QPalette::Base);
        if (pal.color(QPalette::AlternateBase).isValid()) t.alt = pal.color(QPalette::AlternateBase);
        if (pal.color(QPalette::Button).isValid()) t.button = pal.color(QPalette::Button);
        if (tk.onSurface.isValid()) t.text = tk.onSurface;
        if (tk.muted.isValid()) t.muted = tk.muted;
        if (tk.accent.isValid()) {
            t.accent = tk.accent;
            t.accentHover = tk.accent.lighter(112);
            t.accentPress = tk.accent.darker(115);
            t.accentSoft = QColor(tk.accent.red(), tk.accent.green(), tk.accent.blue(), dark ? 52 : 36);
            t.accentStrong = QColor(tk.accent.red(), tk.accent.green(), tk.accent.blue(), dark ? 115 : 92);
        }
        if (tk.danger.isValid()) t.danger = tk.danger;
        if (tk.warning.isValid()) t.warning = tk.warning;
        if (tk.success.isValid()) t.success = tk.success;
        if (tk.info.isValid()) t.info = tk.info;
        t.raised = dark ? mix(t.window, QColor(Qt::white), 0.05) : t.base;
        t.hover = dark ? mix(t.button, QColor(Qt::white), 0.07) : mix(t.button, QColor(Qt::black), 0.04);
        return t;
    }

    inline QString rulesPanelStyleSheet(const Tokens &t) {
        QString qss = SLS(R"QSS(
QWidget#solas_rules_tab { background: {window}; color: {text}; }
QLabel#solasRulesTitle { color: {text}; font-size: 13px; font-weight: 600; }
QLabel#solasRouteLabel, QLabel#solasOtherRulesLabel, QLabel#solasStatusLabel { color: {muted}; font-size: 11px; }
QLabel#solasSectionLabel { color: {text}; font-size: 12px; font-weight: 600; }
QLabel#solasEmptyLabel { color: {faint}; padding: 14px; font-size: 12px; }
QLabel#solasCountBadge {
  background: {accentSoft};
  color: {accentHover};
  border: 1px solid {accentStrong};
  border-radius: 9px;
  padding: 0px 7px;
  font-size: 11px;
  font-weight: 600;
}

QPushButton#solasTabBtn {
  background: {raised};
  color: {muted};
  border: 1px solid {border};
  border-radius: 7px;
  padding: 3px 12px;
  min-height: 16px;
  min-width: 78px;
  font-weight: 500;
}
QPushButton#solasTabBtn:hover {
  background: {hover};
  color: {text};
  border-color: {accentStrong};
}
QPushButton#solasTabBtn:checked {
  background: {accentSoft};
  color: {text};
  border: 1px solid {accent};
  font-weight: 600;
}

QPushButton#solasActionBtn, QPushButton#solasBulkBtn, QToolButton#solasReloadBtn {
  background: {button};
  color: {text};
  border: 1px solid {border};
  border-radius: 7px;
  padding: 2px 10px;
  min-height: 18px;
}
QPushButton#solasActionBtn:hover, QPushButton#solasBulkBtn:hover, QToolButton#solasReloadBtn:hover {
  background: {hover};
  border-color: {accentStrong};
}
QPushButton#solasActionBtn:pressed, QPushButton#solasBulkBtn:pressed, QToolButton#solasReloadBtn:pressed {
  background: {accentSoft};
  border-color: {accent};
}
QPushButton#solasActionBtn:disabled, QPushButton#solasBulkBtn:disabled {
  background: {alt};
  color: {disabled};
  border-color: {borderSoft};
}

QLineEdit#solasSearchEdit {
  background: {base};
  color: {text};
  border: 1px solid {border};
  border-radius: 7px;
  padding: 2px 8px;
  min-height: 18px;
}
QLineEdit#solasSearchEdit:hover { border-color: {accentStrong}; }
QLineEdit#solasSearchEdit:focus { border-color: {accent}; }

QFrame#solasRulesCard {
  background: {base};
  border: 1px solid {borderSoft};
  border-radius: 8px;
}
QFrame#solasTableHeader {
  background: {raised};
  border: none;
  border-bottom: 1px solid {borderSoft};
  border-top-left-radius: 7px;
  border-top-right-radius: 7px;
}
QLabel#solasColHeader {
  color: {muted};
  font-size: 11px;
  font-weight: 600;
}
QFrame#solasColDivider {
  background: {borderSoft};
  border: none;
  min-width: 1px;
  max-width: 1px;
}
QScrollArea#solasRulesScroll {
  background: transparent;
  border: none;
}
QWidget#solasRowsContainer {
  background: transparent;
}

QFrame#solasRulesRow {
  background: {base};
  border: none;
  border-bottom: 1px solid {borderSoft};
}
QFrame#solasRulesRow[alt="true"] {
  background: {alt};
}
QFrame#solasRulesRow:hover {
  background: {hover};
}

QCheckBox#solasRowCheck, QCheckBox#solasSelectAllCheck {
  background: transparent;
  spacing: 0px;
  padding: 0px;
}
QCheckBox#solasRowCheck::indicator, QCheckBox#solasSelectAllCheck::indicator {
  width: 11px;
  height: 11px;
  border-radius: 5px;
  border: 1px solid {faint};
  background: transparent;
}
QCheckBox#solasRowCheck::indicator:hover, QCheckBox#solasSelectAllCheck::indicator:hover {
  border-color: {accentHover};
}
QCheckBox#solasRowCheck::indicator:checked, QCheckBox#solasSelectAllCheck::indicator:checked {
  border-color: {accent};
  background: {accent};
}
QCheckBox#solasSelectAllCheck::indicator:indeterminate {
  border-color: {accent};
  background: {accentStrong};
}

QLineEdit#solasRowInput {
  background: transparent;
  color: {text};
  border: 1px solid transparent;
  border-radius: 5px;
  padding: 1px 6px;
  min-height: 18px;
}
QLineEdit#solasRowInput[draft="true"] {
  background: {base};
  border-color: {border};
}
QLineEdit#solasRowInput:hover {
  background: {base};
  border-color: {border};
}
QLineEdit#solasRowInput:focus {
  background: {base};
  border-color: {accent};
}
QLineEdit#solasRowInput[invalid="true"],
QLineEdit#solasRowInput[invalid="true"]:hover,
QLineEdit#solasRowInput[invalid="true"]:focus {
  background: {base};
  border-color: {danger};
  color: {danger};
}
QLineEdit#solasRowInput:disabled {
  background: transparent;
  color: {disabled};
  border-color: transparent;
}

QToolButton#solasBrowseBtn {
  background: transparent;
  color: {muted};
  border: 1px solid {borderSoft};
  border-radius: 5px;
  padding: 0px;
  min-height: 0px;
}
QToolButton#solasBrowseBtn:hover {
  background: {button};
  color: {text};
  border-color: {accentStrong};
}
QToolButton#solasBrowseBtn:pressed {
  background: {accentSoft};
  border-color: {accent};
}
QToolButton#solasBrowseBtn:disabled {
  background: transparent;
  color: {disabled};
  border-color: transparent;
}

QToolButton#solasDeleteBtn {
  background: transparent;
  color: {muted};
  border: none;
  border-radius: 5px;
  padding: 0px;
  min-height: 0px;
  font-size: 13px;
  font-weight: 600;
}
QToolButton#solasDeleteBtn:hover {
  background: {button};
  color: {danger};
}
QToolButton#solasDeleteBtn:disabled {
  color: {disabled};
}

QComboBox#solasDirCombo, QComboBox#solasTypeCombo {
  background: transparent;
  color: {text};
  border: 1px solid transparent;
  border-radius: 5px;
  padding: 1px 6px;
  min-height: 18px;
}
QComboBox#solasDirCombo:hover, QComboBox#solasTypeCombo:hover {
  background: {raised};
  border-color: {border};
}
QComboBox#solasDirCombo:focus, QComboBox#solasTypeCombo:focus {
  background: {raised};
  border-color: {accent};
}
QComboBox#solasDirCombo:disabled, QComboBox#solasTypeCombo:disabled {
  background: transparent;
  color: {disabled};
  border-color: transparent;
}
QComboBox#solasDirCombo::drop-down, QComboBox#solasTypeCombo::drop-down {
  border: none;
  width: 18px;
}

QFrame#solasSaveBar {
  background: {raised};
  border: 1px solid {borderSoft};
  border-radius: 7px;
}
QPushButton#solasSaveBtn {
  background: {accent};
  color: {onAccent};
  border: 1px solid {accent};
  border-radius: 7px;
  padding: 3px 16px;
  min-height: 20px;
  min-width: 96px;
  font-weight: 600;
}
QPushButton#solasSaveBtn:hover {
  background: {accentHover};
  border-color: {accentHover};
}
QPushButton#solasSaveBtn:pressed {
  background: {accentPress};
  border-color: {accentPress};
}
QPushButton#solasSaveBtn:disabled {
  background: {alt};
  color: {disabled};
  border-color: {borderSoft};
  font-weight: 500;
}
)QSS");
        const QList<QPair<QString, QColor>> vars = {
            {SLS("window"), t.window}, {SLS("base"), t.base}, {SLS("alt"), t.alt}, {SLS("raised"), t.raised},
            {SLS("button"), t.button}, {SLS("hover"), t.hover}, {SLS("text"), t.text}, {SLS("muted"), t.muted},
            {SLS("faint"), t.faint}, {SLS("disabled"), t.disabled}, {SLS("accentHover"), t.accentHover},
            {SLS("accentPress"), t.accentPress}, {SLS("accentSoft"), t.accentSoft}, {SLS("accentStrong"), t.accentStrong},
            {SLS("accent"), t.accent}, {SLS("onAccent"), t.onAccent}, {SLS("borderSoft"), t.borderSoft}, {SLS("border"), t.border},
            {SLS("danger"), t.danger},
        };
        for (const auto &v : vars) qss.replace(SLS("{") + v.first + SLS("}"), css(v.second));
        return qss;
    }

    // Writes line into target and pulls it out of every other list and server, as QuickRulesDialog::addRule does:
    // one line in two lists means the earlier rule silently wins. Empty on success, else the reason.
    inline QString placeRule(const std::shared_ptr<Configs::RouteProfile> &route, const QString &line,
                             const Configs::SimpleTarget &target, QStringList *movedFrom) {
        QString why;
        if (!route->AppendSimpleRule(line, target, &why))
            return why.isEmpty() ? SLS("Не удалось добавить %1").arg(line) : why;
        for (const auto &other : actionKinds()) {
            const auto otherTarget = targetOf(other.action);
            if (otherTarget != target && route->RemoveSimpleRule(line, otherTarget) && movedFrom != nullptr)
                movedFrom->append(other.label);
        }
        for (const int server : route->ServersHoldingSimpleRule(line)) {
            const Configs::SimpleTarget serverTarget{Configs::proxy, server};
            if (serverTarget != target && route->RemoveSimpleRule(line, serverTarget) && movedFrom != nullptr)
                movedFrom->append(SLS("сервер #%1").arg(server));
        }
        return {};
    }

    // Saves through the engine; empty on success, else the reason.
    inline QString saveRoute(const std::shared_ptr<Configs::RouteProfile> &route) {
        const auto result = Engine::RouteService::Save(route);
        return refused(result) ? errorText(result) : QString();
    }

    class TriStateHeaderCheck : public QCheckBox {
    public:
        explicit TriStateHeaderCheck(QWidget *parent = nullptr) : QCheckBox(parent) { setTristate(true); }

    protected:
        void nextCheckState() override { setCheckState(checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked); }
    };

    // Prevents mouse wheel scrolling over the rule list from changing row combo values by mistake.
    class ComboWheelGuard : public QObject {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::Wheel) {
                if (auto *combo = qobject_cast<QComboBox *>(watched); combo != nullptr && !combo->hasFocus()) {
                    event->ignore();
                    return true;
                }
            }
            return QObject::eventFilter(watched, event);
        }
    };

    class LineEditEscapeFilter : public QObject {
    public:
        LineEditEscapeFilter(std::function<void()> onEscape, QObject *parent = nullptr)
            : QObject(parent), callback(std::move(onEscape)) {}

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::KeyPress) {
                auto *keyEvent = static_cast<QKeyEvent *>(event);
                if (keyEvent->key() == Qt::Key_Escape) {
                    if (callback) callback();
                    return true;
                }
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        std::function<void()> callback;
    };

    struct ManagedEntry {
        ManagedRuleKind kind = ManagedRuleKind::ProcessName;
        QString value;
        int action = 0;

        bool operator==(const ManagedEntry &other) const {
            return kind == other.kind && action == other.action && value == other.value;
        }
    };

    class RuleRowWidget : public QFrame {
    public:
        bool isProcess = false;
        bool deleting = false;
        bool existedOnLoad = false;
        QString initialValue;

        QCheckBox *checkBox = nullptr;
        QLabel *iconLabel = nullptr;
        QComboBox *typeBox = nullptr;
        QLineEdit *valueEdit = nullptr;
        QToolButton *browseBtn = nullptr;
        QToolButton *deleteBtn = nullptr;
        QComboBox *dirBox = nullptr;

        RuleRowWidget(bool processRow, QAbstractItemModel *processNamesModel, QWidget *parent = nullptr)
            : QFrame(parent), isProcess(processRow) {
            setObjectName(SLS("solasRulesRow"));
            setFixedHeight(26);
            setContextMenuPolicy(Qt::CustomContextMenu);

            auto *layout = new QHBoxLayout(this);
            layout->setContentsMargins(10, 1, 8, 1);
            layout->setSpacing(6);

            checkBox = new QCheckBox(this);
            checkBox->setObjectName(SLS("solasRowCheck"));
            checkBox->setFocusPolicy(Qt::NoFocus);
            checkBox->setFixedSize(14, 14);
            layout->addWidget(checkBox);

            if (isProcess) {
                iconLabel = new QLabel(this);
                iconLabel->setFixedSize(14, 14);
                iconLabel->setScaledContents(true);
                layout->addWidget(iconLabel);
            } else {
                typeBox = new QComboBox(this);
                typeBox->setObjectName(SLS("solasTypeCombo"));
                typeBox->setFocusPolicy(Qt::StrongFocus);
                typeBox->setFixedWidth(172);
                typeBox->setFixedHeight(22);
                typeBox->addItem(managedKindLabel(ManagedRuleKind::DomainSuffix), int(ManagedRuleKind::DomainSuffix));
                typeBox->addItem(managedKindLabel(ManagedRuleKind::DomainExact), int(ManagedRuleKind::DomainExact));
                typeBox->installEventFilter(new ComboWheelGuard(typeBox));
                layout->addWidget(typeBox);

                auto *typeDivider = new QFrame(this);
                typeDivider->setObjectName(SLS("solasColDivider"));
                typeDivider->setFixedWidth(1);
                typeDivider->setFixedHeight(16);
                layout->addWidget(typeDivider);
            }

            valueEdit = new QLineEdit(this);
            valueEdit->setObjectName(SLS("solasRowInput"));
            valueEdit->setFixedHeight(22);
            valueEdit->setClearButtonEnabled(false);
            if (isProcess && processNamesModel != nullptr) {
                auto *completer = new QCompleter(processNamesModel, this);
                completer->setCaseSensitivity(Qt::CaseInsensitive);
                completer->setFilterMode(Qt::MatchContains);
                completer->setCompletionMode(QCompleter::PopupCompletion);
                completer->setMaxVisibleItems(12);
                valueEdit->setCompleter(completer);
            }
            layout->addWidget(valueEdit, 1);

            if (isProcess) {
                browseBtn = new QToolButton(this);
                browseBtn->setObjectName(SLS("solasBrowseBtn"));
                browseBtn->setText(SLS("..."));
                browseBtn->setToolTip(SLS("Выбрать исполняемый файл"));
                browseBtn->setFocusPolicy(Qt::NoFocus);
                browseBtn->setFixedSize(24, 22);
                layout->addWidget(browseBtn);
            }

            deleteBtn = new QToolButton(this);
            deleteBtn->setObjectName(SLS("solasDeleteBtn"));
            deleteBtn->setText(SLS("×"));
            deleteBtn->setToolTip(SLS("Удалить правило"));
            deleteBtn->setFocusPolicy(Qt::NoFocus);
            deleteBtn->setFixedSize(20, 22);
            layout->addWidget(deleteBtn);

            auto *divider = new QFrame(this);
            divider->setObjectName(SLS("solasColDivider"));
            divider->setFixedWidth(1);
            divider->setFixedHeight(16);
            layout->addWidget(divider);

            dirBox = new QComboBox(this);
            dirBox->setObjectName(SLS("solasDirCombo"));
            dirBox->setFocusPolicy(Qt::StrongFocus);
            dirBox->setFixedWidth(138);
            dirBox->setFixedHeight(22);
            for (const auto &action : actionKinds()) dirBox->addItem(action.label);
            dirBox->installEventFilter(new ComboWheelGuard(dirBox));
            layout->addWidget(dirBox);
        }

        ManagedRuleKind selectedKind() const {
            if (isProcess) return ManagedRuleKind::ProcessName;
            return static_cast<ManagedRuleKind>(typeBox->currentData().toInt());
        }

        int selectedAction() const { return qBound(0, dirBox->currentIndex(), int(actionKinds().size()) - 1); }

        QString normalizedInput() const {
            const QString raw = valueEdit->text().trimmed();
            return isProcess ? raw : raw.toLower();
        }

        QString currentLine() const { return managedPrefix(selectedKind()) + normalizedInput(); }

        void setProcessAppearance(const QString &name, const QIcon &icon, const QString &knownPath) {
            if (!isProcess) return;
            const QString display = name.isEmpty() ? SLS("Новый процесс") : name;
            const QIcon effective = icon.isNull() ? fallbackAppIcon() : icon;
            iconLabel->setPixmap(effective.pixmap(14, 14));
            const QString tip = knownPath.isEmpty() ? display : display + QLatin1Char('\n') + knownPath;
            iconLabel->setToolTip(tip);
            valueEdit->setToolTip(knownPath.isEmpty() ? QString() : SLS("Известный путь: %1").arg(knownPath));
        }

        void updatePlaceholder() {
            valueEdit->setPlaceholderText(managedPlaceholder(selectedKind()));
        }

        void setAlt(bool on) {
            if (property("alt").toBool() == on) return;
            setProperty("alt", on);
            style()->unpolish(this);
            style()->polish(this);
        }

        void setDraft(bool on) {
            if (valueEdit->property("draft").toBool() == on) return;
            valueEdit->setProperty("draft", on);
            valueEdit->style()->unpolish(valueEdit);
            valueEdit->style()->polish(valueEdit);
            valueEdit->update();
        }

        void setInvalid(bool on) {
            if (valueEdit->property("invalid").toBool() == on) return;
            valueEdit->setProperty("invalid", on);
            valueEdit->style()->unpolish(valueEdit);
            valueEdit->style()->polish(valueEdit);
            valueEdit->update();
        }

        void setWritable(bool writable) {
            if (typeBox != nullptr) typeBox->setEnabled(writable);
            valueEdit->setEnabled(writable);
            if (browseBtn != nullptr) browseBtn->setEnabled(writable);
            deleteBtn->setEnabled(writable);
            dirBox->setEnabled(writable);
        }

        bool matchesFilter(const QString &needle) const {
            const QString val = normalizedInput();
            if (needle.isEmpty() || val.isEmpty()) return true;
            const ManagedRuleKind kind = selectedKind();
            if (val.contains(needle, Qt::CaseInsensitive)) return true;
            if ((managedPrefix(kind) + val).contains(needle, Qt::CaseInsensitive)) return true;
            if (managedKindLabel(kind).contains(needle, Qt::CaseInsensitive)) return true;
            if (actionKinds()[selectedAction()].label.contains(needle, Qt::CaseInsensitive)) return true;
            if (iconLabel != nullptr && iconLabel->toolTip().contains(needle, Qt::CaseInsensitive)) return true;
            return false;
        }
    };

    class RuleEditorPanel : public QWidget {
    public:
        explicit RuleEditorPanel(QWidget *parent = nullptr) : QWidget(parent) {
            setObjectName(SLS("solas_rules_tab"));

            ProcessCompletionModel::Layout layoutCfg;
            layoutCfg.names = true;
            nameModel = new ProcessCompletionModel(layoutCfg, this);

            auto *root = new QVBoxLayout(this);
            root->setContentsMargins(8, 6, 8, 6);
            root->setSpacing(4);

            // Row 1: Title + segmented tabs + profile + reload
            auto *topBar = new QHBoxLayout;
            topBar->setSpacing(6);
            auto *titleLabel = new QLabel(SLS("Управление маршрутами"), this);
            titleLabel->setObjectName(SLS("solasRulesTitle"));

            procTabBtn = new QPushButton(SLS("Процессы"), this);
            procTabBtn->setObjectName(SLS("solasTabBtn"));
            procTabBtn->setCheckable(true);
            procTabBtn->setAutoDefault(false);
            domTabBtn = new QPushButton(SLS("Домены"), this);
            domTabBtn->setObjectName(SLS("solasTabBtn"));
            domTabBtn->setCheckable(true);
            domTabBtn->setAutoDefault(false);
            tabGroup = new QButtonGroup(this);
            tabGroup->setExclusive(true);
            tabGroup->addButton(procTabBtn, 0);
            tabGroup->addButton(domTabBtn, 1);
            procTabBtn->setChecked(true);

            routeLabel = new QLabel(this);
            routeLabel->setObjectName(SLS("solasRouteLabel"));
            routeLabel->setTextFormat(Qt::PlainText);
            auto *reloadButton = new QToolButton(this);
            reloadButton->setObjectName(SLS("solasReloadBtn"));
            reloadButton->setText(SLS("Обновить"));
            reloadButton->setToolTip(SLS("Перечитать активный профиль маршрутизации"));

            topBar->addWidget(titleLabel);
            topBar->addSpacing(6);
            topBar->addWidget(procTabBtn);
            topBar->addWidget(domTabBtn);
            topBar->addStretch(1);
            topBar->addWidget(routeLabel);
            topBar->addWidget(reloadButton);
            root->addLayout(topBar);

            blockerLabel = new QLabel(this);
            blockerLabel->setTextFormat(Qt::PlainText);
            blockerLabel->setWordWrap(true);
            blockerLabel->hide();
            root->addWidget(blockerLabel);

            // Row 2: Section label + badge + Add + Running processes
            auto *toolbar = new QHBoxLayout;
            toolbar->setSpacing(6);
            sectionLabel = new QLabel(SLS("Приложения"), this);
            sectionLabel->setObjectName(SLS("solasSectionLabel"));
            countBadge = new QLabel(SLS("0"), this);
            countBadge->setObjectName(SLS("solasCountBadge"));
            countBadge->setAlignment(Qt::AlignCenter);

            addRuleBtn = new QPushButton(SLS("Добавить"), this);
            addRuleBtn->setObjectName(SLS("solasActionBtn"));
            addRuleBtn->setAutoDefault(false);

            runningProcBtn = new QPushButton(SLS("Запущенные процессы…"), this);
            runningProcBtn->setObjectName(SLS("solasActionBtn"));
            runningProcBtn->setAutoDefault(false);
            runningProcBtn->setToolTip(SLS("Выбрать из запущенных или недавно подключавшихся программ"));

            toolbar->addWidget(sectionLabel);
            toolbar->addWidget(countBadge);
            toolbar->addStretch(1);
            toolbar->addWidget(addRuleBtn);
            toolbar->addWidget(runningProcBtn);
            root->addLayout(toolbar);

            // Row 3: Search + Bulk routing button
            auto *searchRow = new QHBoxLayout;
            searchRow->setSpacing(6);
            filterEdit = new QLineEdit(this);
            filterEdit->setObjectName(SLS("solasSearchEdit"));
            filterEdit->setPlaceholderText(SLS("Поиск правил…"));
            filterEdit->setClearButtonEnabled(true);
            bulkRouteBtn = new QPushButton(SLS("Направить выбранные…"), this);
            bulkRouteBtn->setObjectName(SLS("solasBulkBtn"));
            bulkRouteBtn->setAutoDefault(false);
            bulkRouteBtn->setEnabled(false);
            searchRow->addWidget(filterEdit, 1);
            searchRow->addWidget(bulkRouteBtn);
            root->addLayout(searchRow);

            // Table card with compact header + scrollable rows stack
            auto *card = new QFrame(this);
            card->setObjectName(SLS("solasRulesCard"));
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(0, 0, 0, 0);
            cardLayout->setSpacing(0);

            auto *headerFrame = new QFrame(card);
            headerFrame->setObjectName(SLS("solasTableHeader"));
            headerFrame->setFixedHeight(24);
            auto *headerLayout = new QHBoxLayout(headerFrame);
            headerLayout->setContentsMargins(10, 0, 8, 0);
            headerLayout->setSpacing(6);

            selectAllCheck = new TriStateHeaderCheck(headerFrame);
            selectAllCheck->setObjectName(SLS("solasSelectAllCheck"));
            selectAllCheck->setFocusPolicy(Qt::NoFocus);
            selectAllCheck->setFixedSize(14, 14);
            selectAllCheck->setToolTip(SLS("Выбрать или снять все видимые правила"));

            colTypeHeader = new QLabel(SLS("Тип"), headerFrame);
            colTypeHeader->setObjectName(SLS("solasColHeader"));
            colTypeHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            colTypeHeader->setFixedWidth(172);
            colTypeHeader->hide();

            headerTypeDivider = new QFrame(headerFrame);
            headerTypeDivider->setObjectName(SLS("solasColDivider"));
            headerTypeDivider->setFixedWidth(1);
            headerTypeDivider->setFixedHeight(16);
            headerTypeDivider->hide();

            colMainHeader = new QLabel(SLS("Приложение"), headerFrame);
            colMainHeader->setObjectName(SLS("solasColHeader"));
            colMainHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

            auto *headerDivider = new QFrame(headerFrame);
            headerDivider->setObjectName(SLS("solasColDivider"));
            headerDivider->setFixedWidth(1);
            headerDivider->setFixedHeight(16);

            auto *colDirHeader = new QLabel(SLS("Направление"), headerFrame);
            colDirHeader->setObjectName(SLS("solasColHeader"));
            colDirHeader->setAlignment(Qt::AlignCenter);
            colDirHeader->setFixedWidth(138);

            headerLayout->addWidget(selectAllCheck);
            headerLayout->addWidget(colTypeHeader);
            headerLayout->addWidget(headerTypeDivider);
            headerLayout->addWidget(colMainHeader, 1);
            headerLayout->addWidget(headerDivider);
            headerLayout->addWidget(colDirHeader);
            cardLayout->addWidget(headerFrame);

            pages = new QStackedWidget(card);
            buildScrollPage(pages, true, &processScroll, &processRowsContainer, &processRowsLayout, &processEmptyLabel);
            buildScrollPage(pages, false, &domainScroll, &domainRowsContainer, &domainRowsLayout, &domainEmptyLabel);
            cardLayout->addWidget(pages, 1);
            root->addWidget(card, 1);

            // Compact save bar + status
            auto *saveBar = new QFrame(this);
            saveBar->setObjectName(SLS("solasSaveBar"));
            auto *foot = new QHBoxLayout(saveBar);
            foot->setContentsMargins(8, 5, 8, 5);
            foot->setSpacing(8);
            statusLabel = new QLabel(this);
            statusLabel->setObjectName(SLS("solasStatusLabel"));
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            otherRulesLabel = new QLabel(this);
            otherRulesLabel->setObjectName(SLS("solasOtherRulesLabel"));
            otherRulesLabel->setTextFormat(Qt::PlainText);
            otherRulesLabel->hide();
            saveBtn = new QPushButton(SLS("Сохранить"), saveBar);
            saveBtn->setObjectName(SLS("solasSaveBtn"));
            saveBtn->setAutoDefault(false);
            saveBtn->setEnabled(false);
            saveBtn->setToolTip(SLS("Сохранить изменения правил и перезагрузить прокси (Ctrl+S)"));
            foot->addWidget(statusLabel, 1);
            foot->addWidget(otherRulesLabel);
            foot->addWidget(saveBtn);
            root->addWidget(saveBar);

            auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
            saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(saveShortcut, &QShortcut::activated, this, [this] {
                if (saveBtn != nullptr && saveBtn->isEnabled()) saveAllChanges();
            });

            connect(tabGroup, &QButtonGroup::idClicked, this, [this](int id) { switchSubTab(id); });
            connect(reloadButton, &QToolButton::clicked, this, [this] {
                if (hasUnsavedChanges() &&
                    QMessageBox::question(window(), SLS("Обновить правила"),
                                          SLS("Есть несохранённые изменения. Сбросить их и перечитать профиль?")) != QMessageBox::Yes) {
                    return;
                }
                setStatus(QString(), QColor(), StatusKind::None);
                reload();
            });
            connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString &) { applyFilter(); });
            connect(addRuleBtn, &QPushButton::clicked, this, [this] { addDraftRow(activeTab == 0); });
            connect(runningProcBtn, &QPushButton::clicked, this, [this] { addFromRunningProcesses(); });
            connect(selectAllCheck, &QAbstractButton::clicked, this, [this](bool) { toggleSelectAll(); });
            connect(bulkRouteBtn, &QPushButton::clicked, this, [this] { showBulkMenu(); });
            connect(saveBtn, &QPushButton::clicked, this, [this] { saveAllChanges(); });

            connect(nameModel, &QAbstractItemModel::modelReset, this, [this] {
                ingestCompletionModel();
                refreshProcessRowIcons();
            });
            connect(Engine::EngineEvents::instance(), &Engine::EngineEvents::routesChanged, this, [this] {
                if (selfSaving || !isVisible()) return;
                if (hasUnsavedChanges()) {
                    setStatus(SLS("Активный профиль изменился извне. Нажмите «Сохранить» или «Обновить»."),
                              warningColor(), StatusKind::Warning);
                    return;
                }
                reload();
            });
            connect(themeManager(), &ThemeManager::themeChanged, this, [this](const QString &) { applyPanelStyle(); });

            applyPanelStyle();
            ingestCompletionModel();
            switchSubTab(0);
            reload();
        }

        void reload() {
            const auto stored = Configs::dataManager->routesRepo->GetRouteProfile(Configs::ActiveRouteId());
            route = stored ? std::make_shared<Configs::RouteProfile>(*stored) : nullptr;
            loadedRouteId = route ? route->id : -1;
            blocker = routeBlocker(stored);
            routeLabel->setText(route ? SLS("Профиль: %1").arg(route->name) : SLS("Нет активного профиля"));
            blockerLabel->setVisible(!blocker.isEmpty());
            paint(blockerLabel, blocker, dangerColor());

            ingestAppMeta();
            ingestCompletionModel();

            const QSet<QString> checkedProc = captureCheckedKeys(processRows);
            const QSet<QString> checkedDom = captureCheckedKeys(domainRows);

            clearRows(processRows, processRowsLayout);
            clearRows(domainRows, domainRowsLayout);

            int otherCount = 0;
            if (route) {
                for (int a = 0; a < actionKinds().size(); ++a) {
                    const QString text = route->GetSimpleRules(actionKinds()[a].action);
                    for (const auto &raw : text.split(QLatin1Char('\n'))) {
                        const QString line = raw.trimmed();
                        if (line.isEmpty()) continue;
                        QString value;
                        const auto kind = parseManagedRule(line, &value);
                        if (!kind.has_value()) {
                            ++otherCount;
                            continue;
                        }
                        if (*kind == ManagedRuleKind::ProcessName) {
                            RuleRowWidget *row = createRow(true, true, *kind, value, a);
                            if (checkedProc.contains(rowKey(*kind, value, a))) {
                                const QSignalBlocker b(row->checkBox);
                                row->checkBox->setChecked(true);
                            }
                        } else {
                            RuleRowWidget *row = createRow(false, true, *kind, value, a);
                            if (checkedDom.contains(rowKey(*kind, value, a))) {
                                const QSignalBlocker b(row->checkBox);
                                row->checkBox->setChecked(true);
                            }
                        }
                    }
                }
            }

            if (otherCount > 0) {
                otherRulesLabel->setText(SLS("Других правил: %1 (сохранены)").arg(otherCount));
                otherRulesLabel->setToolTip(
                    SLS("В профиле есть правила других типов (IP, rule-set, keyword, regex или processPath) — они не показаны и не затрагиваются."));
                otherRulesLabel->show();
            } else {
                otherRulesLabel->hide();
            }

            const bool writable = isWritable();
            addRuleBtn->setEnabled(writable);
            runningProcBtn->setEnabled(writable);

            savedEntries = collectEntries();
            updateCounts();
            applyFilter();
            updateDirtyState();
        }

    protected:
        void showEvent(QShowEvent *event) override {
            QWidget::showEvent(event);
            if (!hasUnsavedChanges() || Configs::ActiveRouteId() != loadedRouteId) {
                reload();
            } else {
                const auto stored = Configs::dataManager->routesRepo->GetRouteProfile(loadedRouteId);
                blocker = routeBlocker(stored);
                blockerLabel->setVisible(!blocker.isEmpty());
                paint(blockerLabel, blocker, dangerColor());
                updateDirtyState();
            }
        }

    private:
        enum class StatusKind { None, Info, Dirty, Success, Warning, Error };

        ProcessCompletionModel *nameModel = nullptr;
        QLabel *routeLabel = nullptr;
        QLabel *blockerLabel = nullptr;
        QButtonGroup *tabGroup = nullptr;
        QPushButton *procTabBtn = nullptr;
        QPushButton *domTabBtn = nullptr;
        QLabel *sectionLabel = nullptr;
        QLabel *countBadge = nullptr;
        QPushButton *addRuleBtn = nullptr;
        QPushButton *runningProcBtn = nullptr;
        QLineEdit *filterEdit = nullptr;
        QPushButton *bulkRouteBtn = nullptr;
        TriStateHeaderCheck *selectAllCheck = nullptr;
        QLabel *colTypeHeader = nullptr;
        QFrame *headerTypeDivider = nullptr;
        QLabel *colMainHeader = nullptr;
        QStackedWidget *pages = nullptr;

        QScrollArea *processScroll = nullptr;
        QWidget *processRowsContainer = nullptr;
        QVBoxLayout *processRowsLayout = nullptr;
        QLabel *processEmptyLabel = nullptr;

        QScrollArea *domainScroll = nullptr;
        QWidget *domainRowsContainer = nullptr;
        QVBoxLayout *domainRowsLayout = nullptr;
        QLabel *domainEmptyLabel = nullptr;

        QLabel *statusLabel = nullptr;
        QLabel *otherRulesLabel = nullptr;
        QPushButton *saveBtn = nullptr;

        QList<RuleRowWidget *> processRows;
        QList<RuleRowWidget *> domainRows;
        QList<ManagedEntry> savedEntries;
        QHash<QString, QString> knownPaths;
        QHash<QString, QIcon> knownIcons;

        std::shared_ptr<Configs::RouteProfile> route;
        QString blocker;
        StatusKind statusKind = StatusKind::None;
        bool selfSaving = false;
        int activeTab = 0;
        int loadedRouteId = -1;

        static void paint(QLabel *label, const QString &text, const QColor &color) {
            label->setText(text);
            label->setStyleSheet(color.isValid() ? SLS("color: %1;").arg(color.name()) : QString());
        }

        void setStatus(const QString &text, const QColor &color, StatusKind kind = StatusKind::Info) {
            statusKind = text.isEmpty() ? StatusKind::None : kind;
            paint(statusLabel, text, color);
        }

        bool isWritable() const { return route != nullptr && blocker.isEmpty(); }

        static QString rowKey(ManagedRuleKind kind, const QString &value, int action) {
            return QString::number(int(kind)) + QLatin1Char('|') + QString::number(action) + QLatin1Char('|') + value;
        }

        static QSet<QString> captureCheckedKeys(const QList<RuleRowWidget *> &rows) {
            QSet<QString> out;
            for (const RuleRowWidget *row : rows) {
                if (!row->deleting && row->checkBox->isChecked()) {
                    const QString val = row->normalizedInput();
                    if (!val.isEmpty()) out.insert(rowKey(row->selectedKind(), val, row->selectedAction()));
                }
            }
            return out;
        }

        static void buildScrollPage(QStackedWidget *stack, bool isProcess, QScrollArea **outScroll, QWidget **outContainer,
                                    QVBoxLayout **outLayout, QLabel **outEmptyLabel) {
            auto *scroll = new QScrollArea(stack);
            scroll->setObjectName(SLS("solasRulesScroll"));
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

            auto *container = new QWidget(scroll);
            container->setObjectName(SLS("solasRowsContainer"));
            auto *vLayout = new QVBoxLayout(container);
            vLayout->setContentsMargins(0, 0, 0, 0);
            vLayout->setSpacing(0);

            auto *emptyLabel = new QLabel(container);
            emptyLabel->setObjectName(SLS("solasEmptyLabel"));
            emptyLabel->setAlignment(Qt::AlignCenter);
            emptyLabel->setWordWrap(true);
            emptyLabel->setText(isProcess ? SLS("Нет правил по имени процесса") : SLS("Нет правил по домену"));
            emptyLabel->hide();
            vLayout->addWidget(emptyLabel);
            vLayout->addStretch(1);

            scroll->setWidget(container);
            stack->addWidget(scroll);

            *outScroll = scroll;
            *outContainer = container;
            *outLayout = vLayout;
            *outEmptyLabel = emptyLabel;
        }

        void applyPanelStyle() {
            setStyleSheet(rulesPanelStyleSheet(resolvePanelTokens()));
        }

        void switchSubTab(int index) {
            activeTab = index == 1 ? 1 : 0;
            pages->setCurrentIndex(activeTab);
            const bool isProc = activeTab == 0;
            sectionLabel->setText(isProc ? SLS("Приложения") : SLS("Домены"));
            colTypeHeader->setVisible(!isProc);
            headerTypeDivider->setVisible(!isProc);
            colMainHeader->setText(isProc ? SLS("Приложение") : SLS("Домен"));
            addRuleBtn->setToolTip(isProc ? SLS("Добавить новое правило по имени процесса")
                                          : SLS("Добавить новое правило по домену"));
            runningProcBtn->setVisible(isProc);
            updateCounts();
            applyFilter();
        }

        QList<RuleRowWidget *> &activeRows() { return activeTab == 0 ? processRows : domainRows; }

        void ingestAppMeta() {
            if (Configs::dataManager == nullptr || Configs::dataManager->trafficStatsRepo == nullptr) return;
            for (const auto &meta : Configs::dataManager->trafficStatsRepo->GetAllAppMeta()) {
                if (meta.process_name.isEmpty() || meta.last_path.isEmpty()) continue;
                const QString key = meta.process_name.toLower();
                if (!knownPaths.contains(key) || QFileInfo::exists(meta.last_path)) {
                    knownPaths.insert(key, meta.last_path);
                    knownIcons.remove(key);
                }
            }
        }

        void ingestCompletionModel() {
            if (nameModel == nullptr) return;
            const int rows = nameModel->rowCount({});
            for (int i = 0; i < rows; ++i) {
                const QModelIndex idx = nameModel->index(i, 0);
                const QString name = idx.data(Qt::DisplayRole).toString();
                if (name.isEmpty()) continue;
                const QString key = name.toLower();
                const QString path = idx.data(Qt::ToolTipRole).toString();
                if (!path.isEmpty() && (!knownPaths.contains(key) || QFileInfo::exists(path))) knownPaths.insert(key, path);
                const QVariant dec = idx.data(Qt::DecorationRole);
                if (dec.canConvert<QIcon>()) {
                    const QIcon icon = dec.value<QIcon>();
                    if (!icon.isNull()) knownIcons.insert(key, icon);
                }
            }
        }

        QIcon resolveProcessIcon(const QString &name, QString *outPath = nullptr) {
            const QString key = name.trimmed().toLower();
            const QString path = knownPaths.value(key);
            if (outPath != nullptr) *outPath = path;
            if (key.isEmpty()) return fallbackAppIcon();
            const auto iconIt = knownIcons.constFind(key);
            if (iconIt != knownIcons.cend() && !iconIt->isNull()) return *iconIt;
            const QIcon loaded = iconForFilePath(path);
            if (!path.isEmpty() && QFileInfo::exists(path)) knownIcons.insert(key, loaded);
            return loaded;
        }

        void refreshProcessRowIcons() {
            for (RuleRowWidget *row : std::as_const(processRows)) {
                if (row->deleting) continue;
                const QString name = row->valueEdit->text().trimmed();
                QString path;
                const QIcon icon = resolveProcessIcon(name, &path);
                row->setProcessAppearance(name, icon, path);
            }
        }

        void clearRows(QList<RuleRowWidget *> &list, QVBoxLayout *layout) {
            for (RuleRowWidget *row : std::as_const(list)) {
                layout->removeWidget(row);
                row->deleting = true;
                row->hide();
                row->deleteLater();
            }
            list.clear();
        }

        void removeRowFromUi(RuleRowWidget *row) {
            if (row == nullptr || row->deleting) return;
            row->deleting = true;
            QList<RuleRowWidget *> &list = row->isProcess ? processRows : domainRows;
            QVBoxLayout *layout = row->isProcess ? processRowsLayout : domainRowsLayout;
            list.removeOne(row);
            layout->removeWidget(row);
            row->hide();
            row->deleteLater();
        }

        RuleRowWidget *createRow(bool isProcess, bool existedOnLoad, ManagedRuleKind kind, const QString &value, int action) {
            QWidget *container = isProcess ? processRowsContainer : domainRowsContainer;
            QVBoxLayout *layout = isProcess ? processRowsLayout : domainRowsLayout;
            QList<RuleRowWidget *> &list = isProcess ? processRows : domainRows;

            const int boundedAction = qBound(0, action, int(actionKinds().size()) - 1);
            auto *row = new RuleRowWidget(isProcess, isProcess ? nameModel : nullptr, container);
            row->existedOnLoad = existedOnLoad;
            row->initialValue = value;

            {
                const QSignalBlocker b1(row->dirBox);
                row->dirBox->setCurrentIndex(boundedAction);
            }
            if (row->typeBox != nullptr) {
                const QSignalBlocker b2(row->typeBox);
                const int idx = row->typeBox->findData(int(kind));
                row->typeBox->setCurrentIndex(idx >= 0 ? idx : 0);
            }
            {
                const QSignalBlocker b3(row->valueEdit);
                row->valueEdit->setText(value);
            }
            row->setDraft(value.isEmpty());
            row->updatePlaceholder();
            row->setWritable(isWritable());

            if (isProcess) {
                QString path;
                const QIcon icon = resolveProcessIcon(value, &path);
                row->setProcessAppearance(value, icon, path);
            }

            // Insert just before the trailing stretch item.
            const int insertPos = qMax(0, layout->count() - 1);
            layout->insertWidget(insertPos, row);
            list.append(row);

            connectRowSignals(row);
            return row;
        }

        void connectRowSignals(RuleRowWidget *row) {
            connect(row->checkBox, &QAbstractButton::clicked, this, [this](bool) { updateSelectionUi(); });

            row->valueEdit->installEventFilter(new LineEditEscapeFilter([this, row] {
                if (!row->existedOnLoad && row->initialValue.isEmpty()) {
                    removeRowFromUi(row);
                    updateCounts();
                    applyFilter();
                    setStatus(QString(), QColor(), StatusKind::None);
                    updateDirtyState();
                } else {
                    {
                        const QSignalBlocker b(row->valueEdit);
                        row->valueEdit->setText(row->initialValue);
                    }
                    row->setInvalid(false);
                    row->setDraft(row->initialValue.isEmpty());
                    if (row->isProcess) {
                        QString path;
                        const QIcon icon = resolveProcessIcon(row->initialValue, &path);
                        row->setProcessAppearance(row->initialValue, icon, path);
                    }
                    row->valueEdit->clearFocus();
                    setStatus(QString(), QColor(), StatusKind::None);
                    updateCounts();
                    updateDirtyState();
                }
            }, row->valueEdit));

            connect(row->valueEdit, &QLineEdit::textChanged, this, [this, row](const QString &text) {
                row->setInvalid(false);
                row->setDraft(text.trimmed().isEmpty());
                if (row->isProcess) {
                    QString path;
                    const QIcon icon = resolveProcessIcon(text.trimmed(), &path);
                    row->setProcessAppearance(text.trimmed(), icon, path);
                }
                if (statusKind == StatusKind::Error || statusKind == StatusKind::Warning) {
                    setStatus(QString(), QColor(), StatusKind::None);
                }
                updateCounts();
                updateDirtyState();
            });

            connect(row->valueEdit, &QLineEdit::returnPressed, this, [this, row] {
                if (row->deleting) return;
                if (row->valueEdit->hasFocus()) row->valueEdit->clearFocus();
                finishRowEdit(row);
            });

            connect(row->valueEdit, &QLineEdit::editingFinished, this, [this, row] {
                if (row->deleting) return;
                finishRowEdit(row);
            });

            connect(row->dirBox, &QComboBox::currentIndexChanged, this, [this, row](int) {
                if (row->deleting) return;
                if (statusKind == StatusKind::Error || statusKind == StatusKind::Warning) {
                    setStatus(QString(), QColor(), StatusKind::None);
                }
                updateCounts();
                applyFilter();
                updateDirtyState();
            });

            if (row->typeBox != nullptr) {
                connect(row->typeBox, &QComboBox::currentIndexChanged, this, [this, row](int) {
                    if (row->deleting) return;
                    row->updatePlaceholder();
                    row->setInvalid(false);
                    if (statusKind == StatusKind::Error || statusKind == StatusKind::Warning) {
                        setStatus(QString(), QColor(), StatusKind::None);
                    }
                    finishRowEdit(row);
                });
            }

            if (row->browseBtn != nullptr) {
                connect(row->browseBtn, &QToolButton::clicked, this, [this, row] { browseForRow(row); });
            }

            connect(row->deleteBtn, &QToolButton::clicked, this, [this, row] { deleteRow(row); });

            connect(row, &QWidget::customContextMenuRequested, this, [this, row](const QPoint &pos) {
                showRowContextMenu(row, row->mapToGlobal(pos));
            });
        }

        void updateCounts() {
            int activeCount = 0;
            for (const RuleRowWidget *r : std::as_const(activeRows())) {
                if (!r->deleting && !r->normalizedInput().isEmpty()) ++activeCount;
            }
            countBadge->setText(QString::number(activeCount));
            updateSelectionUi();
        }

        QList<ManagedEntry> collectEntries() const {
            QList<ManagedEntry> out;
            const auto appendFrom = [&out](const QList<RuleRowWidget *> &rows) {
                for (const RuleRowWidget *row : rows) {
                    if (row->deleting) continue;
                    const QString val = row->normalizedInput();
                    if (val.isEmpty()) continue;
                    out.append({row->selectedKind(), val, row->selectedAction()});
                }
            };
            appendFrom(processRows);
            appendFrom(domainRows);
            return out;
        }

        bool hasUnsavedChanges() const { return collectEntries() != savedEntries; }

        void updateDirtyState() {
            const bool dirty = hasUnsavedChanges();
            if (saveBtn != nullptr) saveBtn->setEnabled(isWritable() && dirty);
            if (dirty) {
                if (statusKind == StatusKind::None || statusKind == StatusKind::Info || statusKind == StatusKind::Success) {
                    setStatus(SLS("Есть несохранённые изменения — нажмите «Сохранить» для применения и перезагрузки прокси."),
                              warningColor(), StatusKind::Dirty);
                }
            } else if (statusKind == StatusKind::Dirty) {
                setStatus(QString(), QColor(), StatusKind::None);
            }
        }

        void applyFilter() {
            const QString needle = filterEdit->text().trimmed();
            const auto filterList = [&needle](const QList<RuleRowWidget *> &rows, QLabel *emptyLabel, const QString &emptyMsg) {
                int visibleCount = 0;
                int totalRows = 0;
                for (RuleRowWidget *row : rows) {
                    if (row->deleting) continue;
                    ++totalRows;
                    const bool show = row->matchesFilter(needle);
                    row->setVisible(show);
                    if (show) {
                        row->setAlt((visibleCount % 2) == 1);
                        ++visibleCount;
                    }
                }
                if (visibleCount == 0) {
                    emptyLabel->setText(totalRows == 0 ? emptyMsg : SLS("По запросу «%1» ничего не найдено").arg(needle));
                    emptyLabel->show();
                } else {
                    emptyLabel->hide();
                }
            };
            filterList(processRows, processEmptyLabel, SLS("Нет правил по имени процесса"));
            filterList(domainRows, domainEmptyLabel, SLS("Нет правил по домену"));
            updateSelectionUi();
        }

        void updateSelectionUi() {
            int visibleRows = 0;
            int visibleChecked = 0;
            int totalChecked = 0;
            for (RuleRowWidget *row : std::as_const(activeRows())) {
                if (row->deleting) {
                    if (row->checkBox->isChecked()) {
                        const QSignalBlocker b(row->checkBox);
                        row->checkBox->setChecked(false);
                    }
                    row->checkBox->setEnabled(false);
                    continue;
                }
                row->checkBox->setEnabled(isWritable());
                const bool checked = row->checkBox->isChecked();
                if (checked) ++totalChecked;
                if (!row->isHidden()) {
                    ++visibleRows;
                    if (checked) ++visibleChecked;
                }
            }
            {
                const QSignalBlocker b(selectAllCheck);
                selectAllCheck->setEnabled(isWritable() && visibleRows > 0);
                if (visibleRows == 0 || visibleChecked == 0) selectAllCheck->setCheckState(Qt::Unchecked);
                else if (visibleChecked == visibleRows) selectAllCheck->setCheckState(Qt::Checked);
                else selectAllCheck->setCheckState(Qt::PartiallyChecked);
            }
            bulkRouteBtn->setEnabled(isWritable() && totalChecked > 0);
            bulkRouteBtn->setText(totalChecked > 0 ? SLS("Направить выбранные (%1)…").arg(totalChecked)
                                                   : SLS("Направить выбранные…"));
        }

        void toggleSelectAll() {
            if (!isWritable()) return;
            int visibleRows = 0;
            int visibleChecked = 0;
            for (const RuleRowWidget *row : std::as_const(activeRows())) {
                if (row->deleting || row->isHidden()) continue;
                ++visibleRows;
                if (row->checkBox->isChecked()) ++visibleChecked;
            }
            const bool checkAll = visibleRows > 0 && visibleChecked < visibleRows;
            for (RuleRowWidget *row : std::as_const(activeRows())) {
                if (row->deleting || row->isHidden()) continue;
                const QSignalBlocker b(row->checkBox);
                row->checkBox->setChecked(checkAll);
            }
            updateSelectionUi();
        }

        QString saveWithGuard(const std::shared_ptr<Configs::RouteProfile> &fresh) {
            const QScopedValueRollback<bool> guard(selfSaving, true);
            return saveRoute(fresh);
        }

        void finishRowEdit(RuleRowWidget *row) {
            if (row == nullptr || row->deleting) return;

            const QString val = row->normalizedInput();
            row->setDraft(val.isEmpty());
            if (val.isEmpty()) {
                row->setInvalid(false);
                if (row->isProcess) row->setProcessAppearance(QString(), fallbackAppIcon(), QString());
                updateCounts();
                updateDirtyState();
                return;
            }

            if (row->valueEdit->text() != val) {
                const QSignalBlocker b(row->valueEdit);
                row->valueEdit->setText(val);
            }
            if (row->isProcess) {
                QString path;
                const QIcon icon = resolveProcessIcon(val, &path);
                row->setProcessAppearance(val, icon, path);
            }

            if (row->isProcess && (val.contains(QLatin1Char('/')) || val.contains(QLatin1Char('\\')))) {
                row->setInvalid(true);
                setStatus(SLS("Укажите только имя файла процесса (например, chrome.exe), без пути."),
                          warningColor(), StatusKind::Warning);
            } else {
                const QString line = managedPrefix(row->selectedKind()) + val;
                const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                if (!issue.message.isEmpty() && issue.severity == Validation::Severity::Error) {
                    row->setInvalid(true);
                    setStatus(issue.message, dangerColor(), StatusKind::Error);
                } else {
                    row->setInvalid(false);
                    if (!issue.message.isEmpty()) {
                        setStatus(issue.message, warningColor(), StatusKind::Warning);
                    } else if (statusKind == StatusKind::Error || statusKind == StatusKind::Warning) {
                        setStatus(QString(), QColor(), StatusKind::None);
                    }
                }
            }

            updateCounts();
            applyFilter();
            updateDirtyState();
        }

        void addDraftRow(bool isProcess) {
            if (!isWritable()) return;
            const ManagedRuleKind defaultKind = isProcess ? ManagedRuleKind::ProcessName : ManagedRuleKind::DomainSuffix;
            const int defaultAction = actionIndexOf(Configs::proxy);
            RuleRowWidget *row = createRow(isProcess, false, defaultKind, QString(), defaultAction);
            updateCounts();
            applyFilter();
            QScrollArea *scroll = isProcess ? processScroll : domainScroll;
            QTimer::singleShot(0, this, [scroll, row = QPointer<RuleRowWidget>(row)] {
                if (row.isNull() || row->deleting) return;
                scroll->ensureWidgetVisible(row);
                row->valueEdit->setFocus();
            });
            if (!hasUnsavedChanges()) {
                setStatus(isProcess ? SLS("Введите имя процесса (например, chrome.exe) или выберите через «...», затем нажмите «Сохранить».")
                                    : SLS("Выберите тип, введите домен и нажмите «Сохранить»."),
                          palette().color(QPalette::PlaceholderText), StatusKind::Info);
            }
            updateDirtyState();
        }

        void browseForRow(RuleRowWidget *row) {
            if (row == nullptr || row->deleting || !row->isProcess || !isWritable()) return;
#if defined(Q_OS_WIN)
            const QString filters = SLS("Программы (*.exe);;Все файлы (*)");
            const QString start = qEnvironmentVariable("ProgramFiles");
#elif defined(Q_OS_MACOS)
            const QString filters = SLS("Приложения (*.app);;Все файлы (*)");
            const QString start = SLS("/Applications");
#else
            const QString filters;
            const QString start = SLS("/usr/bin");
#endif
            const QString chosen = NativeFileDialog::getOpenFileName(window(), SLS("Выбрать программу"), start, filters);
            if (chosen.isEmpty() || row->deleting) return;

            QString resolvedPath;
            const QString name = canonicalProcessNameFromFile(chosen, &resolvedPath);
            if (name.isEmpty()) return;

            const QString key = name.toLower();
            knownPaths.insert(key, resolvedPath);
            knownIcons.insert(key, iconForFilePath(resolvedPath));

            {
                const QSignalBlocker b(row->valueEdit);
                row->valueEdit->setText(name);
            }
            row->setInvalid(false);
            row->setDraft(false);
            row->setProcessAppearance(name, knownIcons.value(key), resolvedPath);
            finishRowEdit(row);
        }

        void addFromRunningProcesses() {
            if (!isWritable()) return;
            ProcessPicker::Match match = ProcessPicker::Match::Name;
            const QStringList values = ProcessPicker::Pick(window(), {ProcessPicker::Match::Name}, &match);
            if (values.isEmpty()) return;
            addProcessNamesBatch(values);
        }

        void addProcessNamesBatch(const QStringList &values) {
            if (!isWritable()) return;
            const int proxyIdx = actionIndexOf(Configs::proxy);
            QStringList added;
            QStringList skipped;
            int moved = 0;
            RuleRowWidget *lastRow = nullptr;

            for (const QString &raw : values) {
                const QString value = raw.trimmed();
                if (value.isEmpty()) continue;
                const QString line = processNamePrefix() + value;
                const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                if (!issue.message.isEmpty() && issue.severity == Validation::Severity::Error) {
                    skipped << SLS("%1 (%2)").arg(value, issue.message);
                    continue;
                }

                RuleRowWidget *existing = nullptr;
                for (RuleRowWidget *row : std::as_const(processRows)) {
                    if (!row->deleting && row->normalizedInput() == value) {
                        existing = row;
                        break;
                    }
                }
                if (existing != nullptr) {
                    if (existing->selectedAction() == proxyIdx) {
                        skipped << SLS("%1 (уже есть)").arg(value);
                        continue;
                    }
                    const QSignalBlocker b(existing->dirBox);
                    existing->dirBox->setCurrentIndex(proxyIdx);
                    lastRow = existing;
                    added << line;
                    ++moved;
                    continue;
                }

                lastRow = createRow(true, false, ManagedRuleKind::ProcessName, value, proxyIdx);
                added << line;
            }

            if (added.isEmpty()) {
                setStatus(SLS("Ничего не добавлено: %1").arg(skipped.join(SLS("; "))), warningColor(), StatusKind::Warning);
                return;
            }

            updateCounts();
            applyFilter();
            if (lastRow != nullptr) {
                QTimer::singleShot(0, this, [scroll = processScroll, row = QPointer<RuleRowWidget>(lastRow)] {
                    if (!row.isNull() && !row->deleting) scroll->ensureWidgetVisible(row);
                });
            }

            QString done = added.size() == 1
                ? SLS("Добавлено в список: %1 → %2 — нажмите «Сохранить».").arg(added.first(), actionLabel(Configs::proxy))
                : SLS("Добавлено в список: %1 → %2 — нажмите «Сохранить».").arg(added.size()).arg(actionLabel(Configs::proxy));
            if (moved > 0) done += SLS(" (перенесено: %1)").arg(moved);
            if (!skipped.isEmpty()) done += SLS(". Пропущено: %1").arg(skipped.join(SLS("; ")));
            setStatus(done, warningColor(), hasUnsavedChanges() ? StatusKind::Dirty : StatusKind::Warning);
            updateDirtyState();
        }

        void deleteRow(RuleRowWidget *row) {
            if (row == nullptr || row->deleting) return;
            if (row->existedOnLoad && !isWritable()) return;
            const QString val = row->normalizedInput();
            removeRowFromUi(row);
            updateCounts();
            applyFilter();
            if (statusKind == StatusKind::Error || statusKind == StatusKind::Warning || statusKind == StatusKind::Success) {
                setStatus(QString(), QColor(), StatusKind::None);
            }
            if (hasUnsavedChanges()) {
                if (!val.isEmpty()) {
                    setStatus(SLS("Убрано из списка: %1 — нажмите «Сохранить».").arg(val), warningColor(), StatusKind::Dirty);
                } else {
                    updateDirtyState();
                }
            } else {
                setStatus(QString(), QColor(), StatusKind::None);
                updateDirtyState();
            }
        }

        void showBulkMenu() {
            QList<RuleRowWidget *> selected;
            for (RuleRowWidget *row : std::as_const(activeRows())) {
                if (!row->deleting && row->checkBox->isChecked()) selected << row;
            }
            if (selected.isEmpty() || !isWritable()) return;

            QMenu menu(this);
            QList<QAction *> dirActions;
            for (int i = 0; i < actionKinds().size(); ++i) {
                dirActions << menu.addAction(actionKinds()[i].label);
            }
            menu.addSeparator();
            QAction *deleteSelectedAction = menu.addAction(SLS("Удалить выбранные (%1)").arg(selected.size()));
            menu.addSeparator();
            QAction *clearSelectionAction = menu.addAction(SLS("Снять выделение"));

            QAction *chosen = menu.exec(bulkRouteBtn->mapToGlobal(QPoint(0, bulkRouteBtn->height())));
            if (chosen == nullptr) return;

            if (chosen == clearSelectionAction) {
                for (RuleRowWidget *row : std::as_const(activeRows())) {
                    const QSignalBlocker b(row->checkBox);
                    row->checkBox->setChecked(false);
                }
                updateSelectionUi();
                return;
            }

            if (chosen == deleteSelectedAction) {
                if (selected.size() > 1 &&
                    QMessageBox::question(window(), SLS("Удалить правила"),
                                          SLS("Удалить выбранные правила (%1)?").arg(selected.size())) != QMessageBox::Yes) {
                    return;
                }
                for (RuleRowWidget *row : std::as_const(selected)) {
                    removeRowFromUi(row);
                }
                updateCounts();
                applyFilter();
                if (hasUnsavedChanges()) {
                    setStatus(SLS("Убрано из списка: %1 — нажмите «Сохранить».").arg(selected.size()),
                              warningColor(), StatusKind::Dirty);
                } else {
                    setStatus(QString(), QColor(), StatusKind::None);
                }
                updateDirtyState();
                return;
            }

            const int targetIdx = int(dirActions.indexOf(chosen));
            if (targetIdx < 0 || targetIdx >= actionKinds().size()) return;
            int changed = 0;
            for (RuleRowWidget *row : std::as_const(selected)) {
                if (row->selectedAction() == targetIdx) continue;
                const QSignalBlocker b(row->dirBox);
                row->dirBox->setCurrentIndex(targetIdx);
                ++changed;
            }
            if (changed == 0) {
                setStatus(SLS("Выбранные правила уже направлены в «%1».").arg(actionKinds()[targetIdx].label),
                          palette().color(QPalette::PlaceholderText), StatusKind::Info);
                return;
            }
            updateCounts();
            applyFilter();
            if (hasUnsavedChanges()) {
                setStatus(SLS("Направлено в списке: %1 → %2 — нажмите «Сохранить».")
                              .arg(changed).arg(actionKinds()[targetIdx].label),
                          warningColor(), StatusKind::Dirty);
            } else {
                setStatus(QString(), QColor(), StatusKind::None);
            }
            updateDirtyState();
        }

        void saveAllChanges() {
            if (!isWritable()) return;

            auto focusRowError = [this](RuleRowWidget *row, int tabIdx, const QString &msg) {
                row->setInvalid(true);
                if (!filterEdit->text().isEmpty()) filterEdit->clear();
                if (activeTab != tabIdx) {
                    if (tabIdx == 0) procTabBtn->setChecked(true);
                    else domTabBtn->setChecked(true);
                    switchSubTab(tabIdx);
                }
                row->show();
                QScrollArea *scroll = tabIdx == 0 ? processScroll : domainScroll;
                scroll->ensureWidgetVisible(row);
                row->valueEdit->setFocus();
                setStatus(msg, dangerColor(), StatusKind::Error);
            };

            QList<ManagedEntry> entries;
            QSet<QString> seenLines;
            QString firstWarning;

            auto validateList = [&](const QList<RuleRowWidget *> &rows, int tabIdx) -> bool {
                for (RuleRowWidget *row : rows) {
                    if (row->deleting) continue;
                    row->setInvalid(false);
                    const QString val = row->normalizedInput();
                    if (val.isEmpty()) continue;

                    if (row->isProcess && (val.contains(QLatin1Char('/')) || val.contains(QLatin1Char('\\')))) {
                        focusRowError(row, tabIdx, SLS("Укажите только имя файла процесса (например, chrome.exe), без пути."));
                        return false;
                    }

                    const ManagedRuleKind kind = row->selectedKind();
                    const QString line = managedPrefix(kind) + val;
                    const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                    if (!issue.message.isEmpty()) {
                        if (issue.severity == Validation::Severity::Error) {
                            focusRowError(row, tabIdx, issue.message);
                            return false;
                        }
                        if (firstWarning.isEmpty()) firstWarning = issue.message;
                    }
                    if (seenLines.contains(line)) {
                        focusRowError(row, tabIdx, SLS("Дубликат правила: %1 (оставьте одну строку).").arg(line));
                        return false;
                    }
                    seenLines.insert(line);
                    entries.append({kind, val, row->selectedAction()});
                }
                return true;
            };

            if (!validateList(processRows, 0)) return;
            if (!validateList(domainRows, 1)) return;

            if (entries == savedEntries) {
                const auto dropEmpty = [this](const QList<RuleRowWidget *> &rows) {
                    const auto copy = rows;
                    for (RuleRowWidget *row : copy) {
                        if (!row->deleting && row->normalizedInput().isEmpty()) removeRowFromUi(row);
                    }
                };
                dropEmpty(processRows);
                dropEmpty(domainRows);
                updateCounts();
                applyFilter();
                updateDirtyState();
                setStatus(SLS("Нет несохранённых изменений."), palette().color(QPalette::PlaceholderText), StatusKind::Info);
                return;
            }

            const auto fresh = activeRouteCopy();
            if (const QString why = routeBlocker(fresh); !why.isEmpty()) {
                blocker = why;
                blockerLabel->setVisible(true);
                paint(blockerLabel, blocker, dangerColor());
                setStatus(why, dangerColor(), StatusKind::Error);
                return;
            }

            for (const auto &rule : fresh->Rules) {
                switch (rule->type) {
                    case Configs::simpleAddressProxy:
                    case Configs::simpleAddressBypass:
                    case Configs::simpleAddressBlock:
                    case Configs::simpleAddressWarpBypass:
                        rule->domain.clear();
                        rule->domain_suffix.clear();
                        break;
                    case Configs::simpleProcessNameProxy:
                    case Configs::simpleProcessNameBypass:
                    case Configs::simpleProcessNameBlock:
                    case Configs::simpleProcessNameWarpBypass:
                        rule->process_name.clear();
                        break;
                    default:
                        break;
                }
            }

            for (const ManagedEntry &entry : entries) {
                const QString line = managedPrefix(entry.kind) + entry.value;
                const auto target = targetOf(actionKinds()[qBound(0, entry.action, int(actionKinds().size()) - 1)].action);
                if (const QString why = placeRule(fresh, line, target, nullptr); !why.isEmpty()) {
                    setStatus(why, dangerColor(), StatusKind::Error);
                    return;
                }
            }
            fresh->FilterEmptyRules();

            if (const QString why = saveWithGuard(fresh); !why.isEmpty()) {
                setStatus(SLS("Не сохранено: %1").arg(why), dangerColor(), StatusKind::Error);
                return;
            }

            bool restarted = false;
            if (auto *engine = Engine::host(); engine != nullptr) {
                if (auto *conn = engine->connection(); conn != nullptr && engine->restartPending()) {
                    conn->applyPendingRestart();
                    restarted = true;
                }
            }

            reload();
            const int procCount = int(processRows.size());
            const int domCount = int(domainRows.size());
            QString done = restarted
                ? SLS("Сохранено (процессов: %1, доменов: %2). Прокси перезагружается…").arg(procCount).arg(domCount)
                : SLS("Сохранено (процессов: %1, доменов: %2).").arg(procCount).arg(domCount);
            if (!firstWarning.isEmpty()) {
                done += QLatin1Char(' ') + firstWarning;
                setStatus(done, warningColor(), StatusKind::Warning);
            } else {
                setStatus(done, successColor(), StatusKind::Success);
            }
        }

        void showRowContextMenu(RuleRowWidget *row, const QPoint &globalPos) {
            if (row == nullptr || row->deleting) return;
            const bool writable = isWritable();
            const int currentAction = row->selectedAction();
            QMenu menu(this);
            QList<QAction *> dirActions;
            QMenu *dirMenu = menu.addMenu(SLS("Направление"));
            dirMenu->setEnabled(writable);
            for (int i = 0; i < actionKinds().size(); ++i) {
                QAction *a = dirMenu->addAction(actionKinds()[i].label);
                a->setCheckable(true);
                a->setChecked(i == currentAction);
                dirActions << a;
            }
            menu.addSeparator();
            QAction *copyRule = menu.addAction(SLS("Копировать правило"));
            QAction *copyVal = menu.addAction(SLS("Копировать значение"));
            menu.addSeparator();
            QAction *remove = menu.addAction(SLS("Удалить"));
            remove->setEnabled(!row->existedOnLoad || writable);

            QAction *chosen = menu.exec(globalPos);
            if (chosen == nullptr) return;
            if (chosen == copyRule) {
                QGuiApplication::clipboard()->setText(row->currentLine());
                return;
            }
            if (chosen == copyVal) {
                QGuiApplication::clipboard()->setText(row->normalizedInput());
                return;
            }
            if (chosen == remove) {
                deleteRow(row);
                return;
            }
            const int dirIdx = int(dirActions.indexOf(chosen));
            if (dirIdx >= 0 && dirIdx < actionKinds().size()) {
                row->dirBox->setCurrentIndex(dirIdx);
            }
        }
    };

} // namespace Solas
