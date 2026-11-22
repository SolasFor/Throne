#pragma once

// Solas Edition: quick routing rules for the active routing profile.
// Writes the same way the connections-menu rule toggle does: AppendSimpleRule, pull the line out of the other lists,
// then Engine::RouteService::Save (which notes the Routing restart and emits RoutesChanged).

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QEasingCurve>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <memory>
#include <type_traits>

#include "include/database/entities/RouteProfile.h"
#include "include/database/GroupsRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/engine/EditLocks.h"
#include "include/engine/services/RouteService.h"
#include "include/global/Common.h"
#include "include/global/RunningProfiles.hpp"
#include "include/ui/solas/SolasTheme.hpp"

namespace Solas {

    struct RuleKind {
        QString label;
        QString prefix;
        QString placeholder;
    };

    inline const QList<RuleKind> &ruleKinds() {
        static const QList<RuleKind> kinds = {
            {SLS("Домен и поддомены"), SLS("suffix:"), SLS("example.com")},
            {SLS("Домен целиком"), SLS("domain:"), SLS("www.example.com")},
            {SLS("Ключевое слово"), SLS("keyword:"), SLS("google")},
            {SLS("Регулярное выражение"), SLS("regex:"), SLS("^(.+\\.)?example\\.com$")},
            {SLS("IP / подсеть"), SLS("ip:"), SLS("192.168.0.0/16")},
            {SLS("Rule-set"), SLS("ruleset:"), SLS("geosite-category-ads-all")},
            {SLS("Имя процесса"), SLS("processName:"), SLS("chrome.exe")},
            {SLS("Путь процесса"), SLS("processPath:"), SLS("C:\\Program Files\\App\\app.exe")},
        };
        return kinds;
    }

    struct ActionKind {
        Configs::simpleAction action;
        QString label;
    };

    inline const QList<ActionKind> &actionKinds() {
        static const QList<ActionKind> actions = {
            {Configs::proxy, SLS("Через прокси")},
            {Configs::bypass, SLS("Напрямую")},
            {Configs::block, SLS("Блокировать")},
            {Configs::warpBypass, SLS("WARP-bypass")},
        };
        return actions;
    }

    inline QString actionLabel(Configs::simpleAction action) {
        for (const auto &a : actionKinds())
            if (a.action == action) return a.label;
        return Configs::simpleActionToString(action);
    }

    inline Configs::SimpleTarget targetOf(Configs::simpleAction action) {
        return Configs::SimpleTarget{action};
    }

    // Text of whatever a service handed back on refusal (std::optional<error>, error struct or a plain string).
    template <class E>
    QString errorText(const E &error) {
        if constexpr (requires { error->message; }) return QString(error->message);
        else if constexpr (requires { error.message; }) return QString(error.message);
        else if constexpr (std::is_convertible_v<E, QString>) return QString(error);
        else return SLS("отказано");
    }

    // True when a service call result means "refused": an engaged optional, true, or a non-empty string.
    template <class R>
    bool refused(const R &result) {
        if constexpr (std::is_same_v<R, bool>) return result;
        else if constexpr (std::is_convertible_v<R, QString>) return !QString(result).isEmpty();
        else return static_cast<bool>(result);
    }

    // Adds an imported routing profile (Engine::RouteService is a namespace, so it is called directly); empty on success.
    inline QString addRouteProfile(const std::shared_ptr<Configs::RouteProfile> &profile) {
        const auto result = Engine::RouteService::Add(profile);
        return refused(result) ? errorText(result) : QString();
    }

    // The same refusals MainWindow::routeRuleAppendBlocker() gives; empty when the profile can take a rule.
    inline QString routeBlocker(const std::shared_ptr<Configs::RouteProfile> &route) {
        if (!route) return SLS("Нет активного профиля маршрутизации.");
        if (route->preventModifications) return SLS("Профиль маршрутизации защищён от изменений.");
        if (route->isRaw) return SLS("Профиль задан сырым JSON — быстрые правила к нему не применяются.");
        if (route->isRemote && route->autoUpdate)
            return SLS("Профиль обновляется по ссылке: следующее обновление затрёт изменения.");
        if (const auto owner = Configs::dataManager->groupsRepo->GetGroup(route->source_gid); owner != nullptr && owner->import_sub_route)
            return SLS("Профиль обновляется подпиской «%1».").arg(owner->name);
        if (Engine::IsBeingEdited(Engine::EditTarget::Routes, route->id))
            return SLS("Открыты «Настройки маршрутизации» — закройте их и повторите.");
        return {};
    }

    inline std::shared_ptr<Configs::RouteProfile> activeRouteCopy() {
        const auto stored = Configs::dataManager->routesRepo->GetRouteProfile(Configs::ActiveRouteId());
        return stored ? std::make_shared<Configs::RouteProfile>(*stored) : nullptr;
    }

    inline int countRuleLines(const QString &text) {
        int n = 0;
        for (const auto &line : text.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty()) n++;
        return n;
    }

    class QuickRulesDialog : public QDialog {
    public:
        explicit QuickRulesDialog(QWidget *parent) : QDialog(parent) {
            setAttribute(Qt::WA_DeleteOnClose);
            setWindowTitle(SLS("Быстрые правила"));
            resize(680, 580);

            auto *root = new QVBoxLayout(this);
            root->setContentsMargins(16, 16, 16, 16);
            root->setSpacing(12);

            auto *head = new QHBoxLayout;
            auto *title = new QLabel(SLS("Быстрые правила"));
            QFont titleFont = title->font();
            titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
            titleFont.setBold(true);
            title->setFont(titleFont);
            routeLabel = new QLabel;
            routeLabel->setTextFormat(Qt::PlainText);
            routeLabel->setForegroundRole(QPalette::PlaceholderText);
            head->addWidget(title);
            head->addStretch(1);
            head->addWidget(routeLabel);
            root->addLayout(head);

            blockerLabel = new QLabel;
            blockerLabel->setTextFormat(Qt::PlainText);
            blockerLabel->setWordWrap(true);
            blockerLabel->hide();
            root->addWidget(blockerLabel);

            auto *card = new QFrame;
            card->setObjectName(SLS("solasCard"));
            card->setFrameShape(QFrame::StyledPanel);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(12, 12, 12, 12);
            cardLayout->setSpacing(8);
            auto *row = new QHBoxLayout;
            row->setSpacing(8);
            kindBox = new QComboBox;
            for (const auto &k : ruleKinds()) kindBox->addItem(k.label);
            valueEdit = new QLineEdit;
            valueEdit->setClearButtonEnabled(true);
            actionBox = new QComboBox;
            for (const auto &a : actionKinds()) actionBox->addItem(a.label);
            addButton = new QPushButton(SLS("Добавить"));
            addButton->setDefault(true);
            addButton->setAutoDefault(true);
            row->addWidget(kindBox);
            row->addWidget(valueEdit, 1);
            row->addWidget(actionBox);
            row->addWidget(addButton);
            cardLayout->addLayout(row);
            hintLabel = new QLabel;
            hintLabel->setTextFormat(Qt::PlainText);
            hintLabel->setWordWrap(true);
            cardLayout->addWidget(hintLabel);
            root->addWidget(card);

            auto *listsTitle = new QLabel(SLS("Списки правил активного профиля — по одному правилу на строку"));
            listsTitle->setForegroundRole(QPalette::PlaceholderText);
            root->addWidget(listsTitle);

            tabs = new QTabWidget;
            for (int i = 0; i < actionKinds().size(); ++i) {
                auto *editor = new QPlainTextEdit;
                editor->setLineWrapMode(QPlainTextEdit::NoWrap);
                editor->setPlaceholderText(SLS("suffix:example.com\nprocessName:telegram.exe\nruleset:geosite-ru"));
                editors << editor;
                tabs->addTab(editor, actionKinds()[i].label);
                connect(editor, &QPlainTextEdit::textChanged, this, [this] { updateDirty(); });
            }
            root->addWidget(tabs, 1);

            auto *bottom = new QHBoxLayout;
            auto *importButton = new QPushButton(SLS("Импорт профиля"));
            auto *importMenu = new QMenu(importButton);
            importMenu->addAction(SLS("Из файла…"), this, [this] { importFromFile(); });
            importMenu->addAction(SLS("Из буфера обмена"), this, [this] { importFromText(QGuiApplication::clipboard()->text()); });
            importButton->setMenu(importMenu);
            auto *exportButton = new QPushButton(SLS("Экспорт"));
            auto *exportMenu = new QMenu(exportButton);
            exportMenu->addAction(SLS("В файл JSON…"), this, [this] { exportToFile(); });
            exportMenu->addAction(SLS("Ссылку throne:// в буфер"), this, [this] { exportLink(); });
            exportButton->setMenu(exportMenu);
            statusLabel = new QLabel;
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            applyButton = new QPushButton(SLS("Сохранить списки"));
            applyButton->setAutoDefault(false);
            auto *closeButton = new QPushButton(SLS("Закрыть"));
            closeButton->setAutoDefault(false);
            importButton->setAutoDefault(false);
            exportButton->setAutoDefault(false);
            bottom->addWidget(importButton);
            bottom->addWidget(exportButton);
            bottom->addWidget(statusLabel, 1);
            bottom->addWidget(applyButton);
            bottom->addWidget(closeButton);
            root->addLayout(bottom);

            connect(kindBox, &QComboBox::currentIndexChanged, this, [this](int) { validate(); });
            connect(actionBox, &QComboBox::currentIndexChanged, this, [this](int) { validate(); });
            connect(valueEdit, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
            connect(valueEdit, &QLineEdit::returnPressed, this, [this] { if (addButton->isEnabled()) addRule(); });
            connect(addButton, &QPushButton::clicked, this, [this] { addRule(); });
            connect(applyButton, &QPushButton::clicked, this, [this] { applyLists(); });
            connect(closeButton, &QPushButton::clicked, this, [this] { close(); });

            fade = new QPropertyAnimation(this, "windowOpacity", this);
            fade->setDuration(170);
            fade->setStartValue(0.0);
            fade->setEndValue(1.0);
            fade->setEasingCurve(QEasingCurve::OutCubic);

            reload();
        }

        // line is "prefix:value" (or a bare value); actionIndex is an index into actionKinds(), -1 keeps the current one.
        void prefill(const QString &line, int actionIndex) {
            if (!line.isEmpty()) {
                int kind = 0;
                QString value = line;
                for (int i = 0; i < ruleKinds().size(); ++i) {
                    if (line.startsWith(ruleKinds()[i].prefix)) {
                        kind = i;
                        value = line.mid(ruleKinds()[i].prefix.size());
                        break;
                    }
                }
                kindBox->setCurrentIndex(kind);
                valueEdit->setText(value);
            }
            if (actionIndex >= 0 && actionIndex < actionKinds().size()) actionBox->setCurrentIndex(actionIndex);
            valueEdit->setFocus();
            valueEdit->selectAll();
            validate();
        }

    protected:
        void showEvent(QShowEvent *event) override {
            QDialog::showEvent(event);
            if (!faded) {
                faded = true;
                setWindowOpacity(0.0);
                fade->start();
            }
        }

        void closeEvent(QCloseEvent *event) override {
            if (anyDirty() && QMessageBox::question(this, windowTitle(), SLS("Изменения в списках не сохранены. Закрыть без сохранения?")) != QMessageBox::Yes) {
                event->ignore();
                return;
            }
            QDialog::closeEvent(event);
        }

    private:
        QLabel *routeLabel = nullptr;
        QLabel *blockerLabel = nullptr;
        QComboBox *kindBox = nullptr;
        QLineEdit *valueEdit = nullptr;
        QComboBox *actionBox = nullptr;
        QPushButton *addButton = nullptr;
        QLabel *hintLabel = nullptr;
        QTabWidget *tabs = nullptr;
        QList<QPlainTextEdit *> editors;
        QStringList loaded;
        QLabel *statusLabel = nullptr;
        QPushButton *applyButton = nullptr;
        QPropertyAnimation *fade = nullptr;
        bool faded = false;

        std::shared_ptr<Configs::RouteProfile> route;
        QString blocker;

        static void paint(QLabel *label, const QString &text, const QColor &color) {
            label->setText(text);
            label->setStyleSheet(color.isValid() ? SLS("color: %1;").arg(color.name()) : QString());
        }

        void setStatus(const QString &text, const QColor &color) { paint(statusLabel, text, color); }

        Configs::simpleAction currentAction() const { return actionKinds()[qMax(0, actionBox->currentIndex())].action; }

        QString currentLine() const {
            return ruleKinds()[qMax(0, kindBox->currentIndex())].prefix + valueEdit->text().trimmed();
        }

        bool anyDirty() const {
            for (int i = 0; i < editors.size() && i < loaded.size(); ++i)
                if (editors[i]->toPlainText() != loaded[i]) return true;
            return false;
        }

        void reload() {
            const auto stored = Configs::dataManager->routesRepo->GetRouteProfile(Configs::ActiveRouteId());
            route = stored ? std::make_shared<Configs::RouteProfile>(*stored) : nullptr;
            blocker = routeBlocker(stored);
            routeLabel->setText(route ? SLS("Профиль: %1").arg(route->name) : QString());
            blockerLabel->setVisible(!blocker.isEmpty());
            paint(blockerLabel, blocker, dangerColor());
            loaded.clear();
            for (int i = 0; i < editors.size(); ++i) {
                const QString text = route ? route->GetSimpleRules(actionKinds()[i].action) : QString();
                loaded << text;
                const QSignalBlocker block(editors[i]);
                editors[i]->setPlainText(text);
                editors[i]->setReadOnly(!blocker.isEmpty());
            }
            updateDirty();
            validate();
        }

        void updateDirty() {
            bool dirty = false;
            for (int i = 0; i < editors.size() && i < loaded.size(); ++i) {
                const QString text = editors[i]->toPlainText();
                const bool changed = text != loaded[i];
                dirty = dirty || changed;
                tabs->setTabText(i, SLS("%1 (%2)%3").arg(actionKinds()[i].label).arg(countRuleLines(text)).arg(changed ? SLS(" •") : QString()));
            }
            applyButton->setEnabled(dirty && blocker.isEmpty());
        }

        void validate() {
            const int kind = qMax(0, kindBox->currentIndex());
            valueEdit->setPlaceholderText(ruleKinds()[kind].placeholder);
            const QString value = valueEdit->text().trimmed();
            bool ok = !value.isEmpty() && route != nullptr && blocker.isEmpty();
            QString text;
            QColor color = palette().color(QPalette::PlaceholderText);
            if (value.isEmpty()) {
                text = SLS("Правило будет записано как %1значение").arg(ruleKinds()[kind].prefix);
            } else {
                const QString line = currentLine();
                const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                if (!issue.message.isEmpty()) {
                    const bool error = issue.severity == Validation::Severity::Error;
                    color = error ? dangerColor() : warningColor();
                    text = issue.message;
                    if (error) ok = false;
                } else if (route) {
                    const auto target = targetOf(currentAction());
                    Configs::SimpleTarget covering;
                    const QString cover = route->CoveringSimpleRule(line, target, &covering);
                    if (route->HasSimpleRule(line, target)) {
                        text = SLS("Правило %1 уже есть в списке «%2».").arg(line, actionLabel(target.action));
                        color = warningColor();
                        ok = false;
                    } else if (!cover.isEmpty()) {
                        text = SLS("Не сработает: раньше его перехватит %1 из списка «%2». Добавить всё равно можно.")
                                   .arg(cover, actionLabel(covering.action));
                        color = warningColor();
                    } else {
                        text = SLS("Будет добавлено: %1 → %2").arg(line, actionLabel(target.action));
                        color = successColor();
                    }
                }
            }
            paint(hintLabel, text, color);
            addButton->setEnabled(ok);
        }

        bool commit(const std::shared_ptr<Configs::RouteProfile> &changed) {
            const auto result = Engine::RouteService::Save(changed);
            if (refused(result)) {
                setStatus(SLS("Не сохранено: %1").arg(errorText(result)), dangerColor());
                return false;
            }
            return true;
        }

        void addRule() {
            const auto fresh = activeRouteCopy();
            if (const QString why = routeBlocker(fresh); !why.isEmpty()) {
                setStatus(why, dangerColor());
                reload();
                return;
            }
            if (anyDirty() && QMessageBox::question(this, windowTitle(), SLS("В списках есть несохранённые правки — после добавления правила они пропадут. Продолжить?")) != QMessageBox::Yes)
                return;

            const QString line = currentLine();
            const auto target = targetOf(currentAction());
            QString why;
            if (!fresh->AppendSimpleRule(line, target, &why)) {
                setStatus(why.isEmpty() ? SLS("Не удалось добавить %1").arg(line) : why, dangerColor());
                return;
            }
            // One line in two lists means the earlier rule silently wins, so it is taken out of the others.
            QStringList movedFrom;
            for (const auto &other : actionKinds()) {
                const auto otherTarget = targetOf(other.action);
                if (otherTarget != target && fresh->RemoveSimpleRule(line, otherTarget)) movedFrom << other.label;
            }
            for (const int server : fresh->ServersHoldingSimpleRule(line)) {
                const Configs::SimpleTarget serverTarget{Configs::proxy, server};
                if (serverTarget != target && fresh->RemoveSimpleRule(line, serverTarget)) movedFrom << SLS("сервер #%1").arg(server);
            }
            if (!commit(fresh)) return;

            setStatus(movedFrom.isEmpty()
                          ? SLS("Добавлено: %1 → %2").arg(line, actionLabel(target.action))
                          : SLS("Перенесено %1 из «%2» в «%3»").arg(line, movedFrom.join(SLS(", ")), actionLabel(target.action)),
                      successColor());
            const int tab = actionBox->currentIndex();
            {
                const QSignalBlocker block(valueEdit);
                valueEdit->clear();
            }
            reload();
            tabs->setCurrentIndex(tab);
            valueEdit->setFocus();
        }

        void applyLists() {
            const auto fresh = activeRouteCopy();
            if (const QString why = routeBlocker(fresh); !why.isEmpty()) {
                setStatus(why, dangerColor());
                return;
            }
            QStringList reasons;
            for (int i = 0; i < editors.size() && i < loaded.size(); ++i) {
                const QString text = editors[i]->toPlainText();
                if (text == loaded[i]) continue;
                const QString reason = fresh->UpdateSimpleRules(text, actionKinds()[i].action, actionKinds()[i].label);
                if (!reason.isEmpty()) reasons << reason;
            }
            if (!reasons.isEmpty() &&
                QMessageBox::question(this, windowTitle(), SLS("Есть замечания:\n\n%1\n\nСохранить всё равно?").arg(reasons.join(SLS("\n")))) != QMessageBox::Yes)
                return;
            if (!commit(fresh)) return;
            setStatus(SLS("Списки сохранены."), successColor());
            const int tab = tabs->currentIndex();
            reload();
            tabs->setCurrentIndex(tab);
        }

        void exportToFile() {
            if (!route) return;
            QString warnings;
            const QJsonObject object = route->ToShareObject(&warnings);
            const QString path = QFileDialog::getSaveFileName(this, SLS("Экспорт профиля маршрутизации"),
                                                              route->name + SLS(".json"), SLS("JSON (*.json)"));
            if (path.isEmpty()) return;
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                setStatus(SLS("Не удалось записать %1").arg(path), dangerColor());
                return;
            }
            file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
            file.close();
            setStatus(warnings.isEmpty() ? SLS("Сохранено: %1").arg(path) : SLS("Сохранено с замечаниями: %1").arg(warnings),
                      warnings.isEmpty() ? successColor() : warningColor());
        }

        void exportLink() {
            if (!route) return;
            QString warnings;
            QGuiApplication::clipboard()->setText(route->ToShareLink(&warnings));
            setStatus(warnings.isEmpty() ? SLS("Ссылка скопирована.") : SLS("Ссылка скопирована, замечания: %1").arg(warnings),
                      warnings.isEmpty() ? successColor() : warningColor());
        }

        void importFromFile() {
            const QString path = QFileDialog::getOpenFileName(this, SLS("Импорт профиля маршрутизации"), QString(),
                                                              SLS("JSON (*.json);;Все файлы (*)"));
            if (path.isEmpty()) return;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                setStatus(SLS("Не удалось открыть %1").arg(path), dangerColor());
                return;
            }
            importFromText(QString::fromUtf8(file.readAll()));
        }

        // A throne-route-profile JSON (like throne/solas-route.json in SolasFor/Configs) or a throne://route/ link.
        void importFromText(const QString &input) {
            if (input.trimmed().isEmpty()) {
                setStatus(SLS("Пусто: нечего импортировать."), warningColor());
                return;
            }
            QString fatal, warnings;
            bool wasOldArray = false;
            const auto imported = Configs::RouteProfile::FromShareInput(input.trimmed(), &fatal, &warnings, &wasOldArray);
            if (!imported) {
                setStatus(fatal.isEmpty() ? SLS("Это не похоже на профиль маршрутизации.") : fatal, dangerColor());
                return;
            }
            if (imported->name.trimmed().isEmpty()) imported->name = SLS("Импорт");
            if (const QString error = addRouteProfile(imported); !error.isEmpty()) {
                setStatus(SLS("Не добавлен: %1").arg(error), dangerColor());
                return;
            }
            setStatus(warnings.isEmpty()
                          ? SLS("Добавлен профиль «%1» — выбрать его можно в меню маршрутизации.").arg(imported->name)
                          : SLS("Добавлен профиль «%1», замечания: %2").arg(imported->name, warnings),
                      warnings.isEmpty() ? successColor() : warningColor());
        }
    };

    // One dialog at a time; a second call brings it forward and fills in the new line.
    inline void openQuickRules(QWidget *parent, const QString &line = {}, int actionIndex = -1) {
        static QPointer<QuickRulesDialog> dialog;
        if (!dialog) {
            dialog = new QuickRulesDialog(parent);
            dialog->show();
        }
        dialog->prefill(line, actionIndex);
        dialog->raise();
        dialog->activateWindow();
    }

} // namespace Solas
