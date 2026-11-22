#pragma once

// Solas Edition: the "Rules" tab of the bottom panel. Lists the simple rules of the active routing profile and lets
// them be added, changed and removed in place, or added from a running program (ProcessPicker). Writes exactly the
// way QuickRulesDialog does: a copy of the stored profile, the refusals of routeBlocker(),
// AppendSimpleRule / RemoveSimpleRule, then Engine::RouteService::Save.
// Header-only and Q_OBJECT-free, like the rest of the fork.

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <optional>

#include "include/ui/setting/ProcessPicker.h"
#include "include/ui/solas/QuickRules.hpp"
#include "include/ui/solas/SolasTheme.hpp"

namespace Solas {

    // Kind index for a line that carries none of the known prefixes: it is edited and written as is.
    inline int rawKindIndex() { return int(ruleKinds().size()); }

    inline int kindIndexOf(const QString &prefix) {
        for (int i = 0; i < ruleKinds().size(); ++i)
            if (ruleKinds()[i].prefix == prefix) return i;
        return -1;
    }

    inline QString processNamePrefix() { return SLS("processName:"); }
    inline QString processPathPrefix() { return SLS("processPath:"); }

    // Splits a stored line into the kind of ruleKinds() and the value after the prefix.
    inline int splitRuleLine(const QString &line, QString *value) {
        for (int i = 0; i < ruleKinds().size(); ++i) {
            const QString &prefix = ruleKinds()[i].prefix;
            if (line.startsWith(prefix, Qt::CaseInsensitive)) {
                if (value != nullptr) *value = line.mid(prefix.size()).trimmed();
                return i;
            }
        }
        if (value != nullptr) *value = line;
        return rawKindIndex();
    }

    inline QString kindLabel(int kind) {
        if (kind >= 0 && kind < ruleKinds().size()) return ruleKinds()[kind].label;
        return SLS("Без префикса");
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

    class RuleEditorPanel : public QWidget {
    public:
        enum Column { ColAction, ColKind, ColValue, ColCount };
        enum Role { RoleAction = Qt::UserRole, RoleLine = Qt::UserRole + 1 };

        explicit RuleEditorPanel(QWidget *parent = nullptr) : QWidget(parent) {
            setObjectName(SLS("solas_rules_tab"));
            auto *root = new QVBoxLayout(this);
            root->setContentsMargins(8, 8, 8, 8);
            root->setSpacing(6);

            auto *head = new QHBoxLayout;
            routeLabel = new QLabel;
            routeLabel->setTextFormat(Qt::PlainText);
            filterEdit = new QLineEdit;
            filterEdit->setPlaceholderText(SLS("Поиск по правилам"));
            filterEdit->setClearButtonEnabled(true);
            filterEdit->setMaximumWidth(260);
            auto *reloadButton = new QToolButton;
            reloadButton->setText(SLS("Обновить"));
            reloadButton->setToolTip(SLS("Перечитать профиль маршрутизации"));
            head->addWidget(routeLabel, 1);
            head->addWidget(filterEdit);
            head->addSpacing(6);
            head->addWidget(reloadButton);
            root->addLayout(head);

            blockerLabel = new QLabel;
            blockerLabel->setWordWrap(true);
            blockerLabel->setTextFormat(Qt::PlainText);
            blockerLabel->hide();
            root->addWidget(blockerLabel);

            auto *form = new QHBoxLayout;
            form->setSpacing(6);
            kindBox = new QComboBox;
            for (const auto &kind : ruleKinds()) kindBox->addItem(kind.label);
            kindBox->addItem(kindLabel(rawKindIndex()));
            valueEdit = new QLineEdit;
            valueEdit->setClearButtonEnabled(true);
            actionBox = new QComboBox;
            for (const auto &action : actionKinds()) actionBox->addItem(action.label);
            addButton = new QPushButton(SLS("Добавить"));
            addButton->setAutoDefault(false);
            saveButton = new QPushButton(SLS("Сохранить"));
            saveButton->setAutoDefault(false);
            cancelButton = new QPushButton(SLS("Отмена"));
            cancelButton->setAutoDefault(false);
            processButton = new QPushButton(SLS("Из процессов…"));
            processButton->setAutoDefault(false);
            processButton->setToolTip(SLS("Выбрать запущенные программы и добавить правила по их имени или пути в выбранный список"));
            form->addWidget(kindBox);
            form->addWidget(valueEdit, 1);
            form->addWidget(new QLabel(SLS("→")));
            form->addWidget(actionBox);
            form->addWidget(addButton);
            form->addWidget(saveButton);
            form->addWidget(cancelButton);
            form->addSpacing(6);
            form->addWidget(processButton);
            root->addLayout(form);

            // Suggestions from the programs the core sees running or connecting, spelled as the core matches them.
            ProcessCompletionModel::Layout names;
            names.names = true;
            nameCompleter = makeCompleter(new ProcessCompletionModel(names, this));
            ProcessCompletionModel::Layout paths;
            paths.paths = true;
            pathCompleter = makeCompleter(new ProcessCompletionModel(paths, this));

            hintLabel = new QLabel;
            hintLabel->setTextFormat(Qt::PlainText);
            hintLabel->setWordWrap(true);
            root->addWidget(hintLabel);

            table = new QTableWidget(0, ColCount);
            table->setHorizontalHeaderLabels({SLS("Список"), SLS("Тип"), SLS("Значение")});
            table->verticalHeader()->hide();
            table->setAlternatingRowColors(true);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setShowGrid(false);
            table->setWordWrap(false);
            table->setContextMenuPolicy(Qt::CustomContextMenu);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Stretch);
            table->horizontalHeader()->setHighlightSections(false);
            table->setColumnWidth(ColAction, 150);
            table->setColumnWidth(ColKind, 190);
            root->addWidget(table, 1);

            auto *foot = new QHBoxLayout;
            editButton = new QPushButton(SLS("Изменить"));
            editButton->setAutoDefault(false);
            removeButton = new QPushButton(SLS("Удалить"));
            removeButton->setAutoDefault(false);
            statusLabel = new QLabel;
            statusLabel->setTextFormat(Qt::PlainText);
            statusLabel->setWordWrap(true);
            foot->addWidget(editButton);
            foot->addWidget(removeButton);
            foot->addSpacing(10);
            foot->addWidget(statusLabel, 1);
            root->addLayout(foot);

            auto *removeAction = new QAction(this);
            removeAction->setShortcut(QKeySequence(QKeySequence::Delete));
            removeAction->setShortcutContext(Qt::WidgetShortcut);
            table->addAction(removeAction);
            auto *escapeAction = new QAction(this);
            escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
            escapeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(escapeAction);

            connect(kindBox, &QComboBox::currentIndexChanged, this, [this](int) {
                updateCompleter();
                validate();
            });
            connect(actionBox, &QComboBox::currentIndexChanged, this, [this](int) { validate(); });
            connect(valueEdit, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
            connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString &) { applyFilter(); });
            connect(reloadButton, &QToolButton::clicked, this, [this] { reload(); });
            connect(addButton, &QPushButton::clicked, this, [this] { addRule(); });
            connect(saveButton, &QPushButton::clicked, this, [this] { saveEdit(); });
            connect(cancelButton, &QPushButton::clicked, this, [this] { stopEditing(); });
            connect(processButton, &QPushButton::clicked, this, [this] { addFromProcesses(); });
            connect(editButton, &QPushButton::clicked, this, [this] { startEditing(table->currentRow()); });
            connect(removeButton, &QPushButton::clicked, this, [this] { removeRule(table->currentRow()); });
            connect(removeAction, &QAction::triggered, this, [this] { removeRule(table->currentRow()); });
            connect(valueEdit, &QLineEdit::returnPressed, this, [this] {
                if (editing && saveButton->isEnabled()) saveEdit();
                else if (!editing && addButton->isEnabled()) addRule();
            });
            connect(escapeAction, &QAction::triggered, this, [this] {
                if (editing) stopEditing();
            });
            connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { startEditing(row); });
            connect(table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
            connect(table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showRowMenu(pos); });

            setEditing(false);
            updateCompleter();
            reload();
        }

        // Re-reads the active routing profile; keeps the selected rule selected when it is still there.
        void reload() {
            const auto stored = Configs::dataManager->routesRepo->GetRouteProfile(Configs::ActiveRouteId());
            route = stored ? std::make_shared<Configs::RouteProfile>(*stored) : nullptr;
            blocker = routeBlocker(stored);
            routeLabel->setText(route ? SLS("Профиль: %1").arg(route->name) : SLS("Нет активного профиля маршрутизации"));
            blockerLabel->setVisible(!blocker.isEmpty());
            paint(blockerLabel, blocker, dangerColor());

            int keepAction = -1;
            QString keepLine;
            if (const int row = table->currentRow(); row >= 0 && table->item(row, ColAction) != nullptr) {
                keepAction = table->item(row, ColAction)->data(RoleAction).toInt();
                keepLine = table->item(row, ColAction)->data(RoleLine).toString();
            }

            {
                const QSignalBlocker block(table);
                table->setRowCount(0);
                if (route) {
                    for (int a = 0; a < actionKinds().size(); ++a) {
                        const QString text = route->GetSimpleRules(actionKinds()[a].action);
                        for (const auto &raw : text.split(QLatin1Char('\n'))) {
                            const QString line = raw.trimmed();
                            if (line.isEmpty()) continue;
                            appendRow(a, line);
                        }
                    }
                }
            }
            if (keepAction >= 0) {
                if (const auto row = rowOf(keepAction, keepLine); row.has_value()) table->selectRow(*row);
            }
            if (editing && !rowOf(editAction, editLine).has_value()) stopEditing();
            applyFilter();
            updateButtons();
            validate();
        }

    protected:
        void showEvent(QShowEvent *event) override {
            QWidget::showEvent(event);
            reload();
        }

    private:
        QLabel *routeLabel = nullptr;
        QLabel *blockerLabel = nullptr;
        QLineEdit *filterEdit = nullptr;
        QComboBox *kindBox = nullptr;
        QLineEdit *valueEdit = nullptr;
        QComboBox *actionBox = nullptr;
        QPushButton *addButton = nullptr;
        QPushButton *saveButton = nullptr;
        QPushButton *cancelButton = nullptr;
        QPushButton *processButton = nullptr;
        QCompleter *nameCompleter = nullptr;
        QCompleter *pathCompleter = nullptr;
        QLabel *hintLabel = nullptr;
        QTableWidget *table = nullptr;
        QPushButton *editButton = nullptr;
        QPushButton *removeButton = nullptr;
        QLabel *statusLabel = nullptr;

        std::shared_ptr<Configs::RouteProfile> route;
        QString blocker;
        bool editing = false;
        int editAction = -1;
        QString editLine;

        static void paint(QLabel *label, const QString &text, const QColor &color) {
            label->setText(text);
            label->setStyleSheet(color.isValid() ? SLS("color: %1;").arg(color.name()) : QString());
        }

        void setStatus(const QString &text, const QColor &color) { paint(statusLabel, text, color); }

        QCompleter *makeCompleter(QAbstractItemModel *model) {
            auto *completer = new QCompleter(model, this);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCompletionMode(QCompleter::PopupCompletion);
            completer->setMaxVisibleItems(12);
            return completer;
        }

        // Process kinds complete from the running programs; the other kinds have nothing to suggest.
        void updateCompleter() {
            const int kind = kindBox->currentIndex();
            if (kind >= 0 && kind == kindIndexOf(processNamePrefix())) valueEdit->setCompleter(nameCompleter);
            else if (kind >= 0 && kind == kindIndexOf(processPathPrefix())) valueEdit->setCompleter(pathCompleter);
            else valueEdit->setCompleter(nullptr);
        }

        void appendRow(int action, const QString &line) {
            QString value;
            const int kind = splitRuleLine(line, &value);
            const int row = table->rowCount();
            table->insertRow(row);
            auto *actionItem = new QTableWidgetItem(actionKinds()[action].label);
            actionItem->setData(RoleAction, action);
            actionItem->setData(RoleLine, line);
            auto *kindItem = new QTableWidgetItem(kindLabel(kind));
            auto *valueItem = new QTableWidgetItem(value);
            valueItem->setToolTip(line);
            table->setItem(row, ColAction, actionItem);
            table->setItem(row, ColKind, kindItem);
            table->setItem(row, ColValue, valueItem);
        }

        std::optional<int> rowOf(int action, const QString &line) const {
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto *item = table->item(row, ColAction);
                if (item != nullptr && item->data(RoleAction).toInt() == action && item->data(RoleLine).toString() == line)
                    return row;
            }
            return std::nullopt;
        }

        void selectRule(int action, const QString &line) {
            if (const auto row = rowOf(action, line); row.has_value()) {
                table->selectRow(*row);
                table->scrollToItem(table->item(*row, ColValue));
            }
        }

        void applyFilter() {
            const QString needle = filterEdit->text().trimmed();
            for (int row = 0; row < table->rowCount(); ++row) {
                bool match = needle.isEmpty();
                for (int col = 0; col < ColCount && !match; ++col) {
                    const auto *item = table->item(row, col);
                    match = item != nullptr && item->text().contains(needle, Qt::CaseInsensitive);
                }
                if (!match && !needle.isEmpty() && table->item(row, ColAction) != nullptr)
                    match = table->item(row, ColAction)->data(RoleLine).toString().contains(needle, Qt::CaseInsensitive);
                table->setRowHidden(row, !match);
            }
        }

        void updateButtons() {
            const bool picked = table->currentRow() >= 0 && !table->selectedItems().isEmpty();
            const bool writable = route != nullptr && blocker.isEmpty();
            editButton->setEnabled(picked && writable && !editing);
            removeButton->setEnabled(picked && writable);
            processButton->setEnabled(writable && !editing);
            kindBox->setEnabled(writable);
            valueEdit->setEnabled(writable);
            actionBox->setEnabled(writable);
        }

        void setEditing(bool on) {
            editing = on;
            addButton->setVisible(!on);
            saveButton->setVisible(on);
            cancelButton->setVisible(on);
            if (!on) {
                editAction = -1;
                editLine.clear();
            }
            updateButtons();
        }

        void startEditing(int row) {
            if (row < 0 || row >= table->rowCount() || route == nullptr || !blocker.isEmpty()) return;
            const auto *item = table->item(row, ColAction);
            if (item == nullptr) return;
            editAction = item->data(RoleAction).toInt();
            editLine = item->data(RoleLine).toString();
            QString value;
            const int kind = splitRuleLine(editLine, &value);
            {
                const QSignalBlocker b1(kindBox);
                const QSignalBlocker b2(valueEdit);
                const QSignalBlocker b3(actionBox);
                kindBox->setCurrentIndex(kind);
                valueEdit->setText(value);
                actionBox->setCurrentIndex(editAction);
            }
            updateCompleter();
            setEditing(true);
            table->selectRow(row);
            setStatus(SLS("Изменение правила %1").arg(editLine), QColor());
            validate();
            valueEdit->setFocus();
            valueEdit->selectAll();
        }

        void stopEditing() {
            setEditing(false);
            {
                const QSignalBlocker block(valueEdit);
                valueEdit->clear();
            }
            setStatus(QString(), QColor());
            validate();
        }

        Configs::simpleAction currentAction() const { return actionKinds()[qMax(0, actionBox->currentIndex())].action; }

        QString currentLine() const {
            const int kind = kindBox->currentIndex();
            const QString value = valueEdit->text().trimmed();
            if (kind < 0 || kind >= ruleKinds().size()) return value;
            return ruleKinds()[kind].prefix + value;
        }

        void validate() {
            const int kind = kindBox->currentIndex();
            valueEdit->setPlaceholderText(kind >= 0 && kind < ruleKinds().size() ? ruleKinds()[kind].placeholder
                                                                               : SLS("suffix:example.com"));
            const QString value = valueEdit->text().trimmed();
            bool ok = !value.isEmpty() && route != nullptr && blocker.isEmpty();
            QString text;
            QColor color = palette().color(QPalette::PlaceholderText);
            if (value.isEmpty()) {
                text = kind >= 0 && kind < ruleKinds().size()
                           ? SLS("Правило будет записано как %1значение").arg(ruleKinds()[kind].prefix)
                           : SLS("Строка будет записана как есть");
            } else {
                const QString line = currentLine();
                const auto target = targetOf(currentAction());
                const bool unchanged = editing && line == editLine && actionBox->currentIndex() == editAction;
                const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                if (unchanged) {
                    text = SLS("Без изменений.");
                    ok = false;
                } else if (!issue.message.isEmpty()) {
                    const bool error = issue.severity == Validation::Severity::Error;
                    color = error ? dangerColor() : warningColor();
                    text = issue.message;
                    if (error) ok = false;
                } else if (route) {
                    Configs::SimpleTarget covering;
                    const QString cover = route->CoveringSimpleRule(line, target, &covering);
                    if (route->HasSimpleRule(line, target)) {
                        text = SLS("Правило %1 уже есть в списке «%2».").arg(line, actionLabel(target.action));
                        color = warningColor();
                        ok = false;
                    } else if (!cover.isEmpty() && cover != editLine) {
                        text = SLS("Не сработает: раньше его перехватит %1 из списка «%2». Сохранить всё равно можно.")
                                   .arg(cover, actionLabel(covering.action));
                        color = warningColor();
                    } else {
                        text = editing ? SLS("Будет: %1 → %2 вместо %3 → %4")
                                             .arg(line, actionLabel(target.action), editLine,
                                                  actionKinds()[qBound(0, editAction, int(actionKinds().size()) - 1)].label)
                                       : SLS("Будет добавлено: %1 → %2").arg(line, actionLabel(target.action));
                        color = successColor();
                    }
                }
            }
            paint(hintLabel, text, color);
            addButton->setEnabled(ok && !editing);
            saveButton->setEnabled(ok && editing);
        }

        // A fresh copy of the stored profile, or nullptr with the refusal shown.
        std::shared_ptr<Configs::RouteProfile> writableCopy() {
            const auto fresh = activeRouteCopy();
            if (const QString why = routeBlocker(fresh); !why.isEmpty()) {
                setStatus(why, dangerColor());
                reload();
                return nullptr;
            }
            return fresh;
        }

        void addRule() {
            const auto fresh = writableCopy();
            if (!fresh) return;
            const QString line = currentLine();
            const auto target = targetOf(currentAction());
            QStringList movedFrom;
            if (const QString why = placeRule(fresh, line, target, &movedFrom); !why.isEmpty()) {
                setStatus(why, dangerColor());
                return;
            }
            if (const QString why = saveRoute(fresh); !why.isEmpty()) {
                setStatus(SLS("Не сохранено: %1").arg(why), dangerColor());
                return;
            }
            setStatus(movedFrom.isEmpty()
                          ? SLS("Добавлено: %1 → %2").arg(line, actionLabel(target.action))
                          : SLS("Перенесено %1 из «%2» в «%3»").arg(line, movedFrom.join(SLS(", ")), actionLabel(target.action)),
                      successColor());
            const int action = actionBox->currentIndex();
            {
                const QSignalBlocker block(valueEdit);
                valueEdit->clear();
            }
            reload();
            selectRule(action, line);
            valueEdit->setFocus();
        }

        // Opens the program list (running and recently connected programs, or a file chosen with Browse) and writes
        // one processName: or processPath: rule per selected program into the list chosen in the form, in one save.
        void addFromProcesses() {
            if (editing || route == nullptr || !blocker.isEmpty()) return;
            ProcessPicker::Match match = ProcessPicker::Match::Name;
            const QStringList values =
                ProcessPicker::Pick(window(), {ProcessPicker::Match::Name, ProcessPicker::Match::Path}, &match);
            if (values.isEmpty()) return;
            const QString prefix = match == ProcessPicker::Match::Path ? processPathPrefix() : processNamePrefix();

            const auto fresh = writableCopy();
            if (!fresh) return;
            const int action = qMax(0, actionBox->currentIndex());
            const auto target = targetOf(currentAction());
            QStringList added;
            QStringList skipped;
            int moved = 0;
            for (const auto &raw : values) {
                const QString value = raw.trimmed();
                if (value.isEmpty()) continue;
                const QString line = prefix + value;
                const Configs::RuleIssue issue = Configs::RouteProfile::CheckSimpleRule(line);
                if (!issue.message.isEmpty() && issue.severity == Validation::Severity::Error) {
                    skipped << SLS("%1 (%2)").arg(value, issue.message);
                    continue;
                }
                if (fresh->HasSimpleRule(line, target)) {
                    skipped << SLS("%1 (уже есть)").arg(value);
                    continue;
                }
                QStringList movedFrom;
                if (const QString why = placeRule(fresh, line, target, &movedFrom); !why.isEmpty()) {
                    skipped << SLS("%1 (%2)").arg(value, why);
                    continue;
                }
                added << line;
                if (!movedFrom.isEmpty()) ++moved;
            }
            if (added.isEmpty()) {
                setStatus(SLS("Ничего не добавлено: %1").arg(skipped.join(SLS("; "))), warningColor());
                return;
            }
            if (const QString why = saveRoute(fresh); !why.isEmpty()) {
                setStatus(SLS("Не сохранено: %1").arg(why), dangerColor());
                return;
            }
            reload();
            selectRule(action, added.last());

            QString done = added.size() == 1 ? SLS("Добавлено: %1 → %2").arg(added.first(), actionLabel(target.action))
                                             : SLS("Добавлено правил: %1 → %2").arg(added.size()).arg(actionLabel(target.action));
            if (moved > 0) done += SLS(" (перенесено из других списков: %1)").arg(moved);
            if (!skipped.isEmpty()) done += SLS(". Пропущено: %1").arg(skipped.join(SLS("; ")));
            setStatus(done, skipped.isEmpty() ? successColor() : warningColor());
        }

        void saveEdit() {
            if (!editing) return;
            const auto fresh = writableCopy();
            if (!fresh) return;
            const int oldAction = editAction;
            const QString oldLine = editLine;
            const auto oldTarget = targetOf(actionKinds()[qBound(0, oldAction, int(actionKinds().size()) - 1)].action);
            if (!fresh->RemoveSimpleRule(oldLine, oldTarget)) {
                setStatus(SLS("Правила %1 уже нет в профиле: его изменили в другом месте.").arg(oldLine), warningColor());
                stopEditing();
                reload();
                return;
            }
            const QString line = currentLine();
            const auto target = targetOf(currentAction());
            QStringList movedFrom;
            if (const QString why = placeRule(fresh, line, target, &movedFrom); !why.isEmpty()) {
                setStatus(why, dangerColor());
                return;
            }
            if (const QString why = saveRoute(fresh); !why.isEmpty()) {
                setStatus(SLS("Не сохранено: %1").arg(why), dangerColor());
                return;
            }
            const int action = actionBox->currentIndex();
            QString done = SLS("Изменено: %1 → %2").arg(line, actionLabel(target.action));
            if (!movedFrom.isEmpty()) done += SLS(" (убрано из «%1»)").arg(movedFrom.join(SLS(", ")));
            setEditing(false);
            {
                const QSignalBlocker block(valueEdit);
                valueEdit->clear();
            }
            reload();
            selectRule(action, line);
            setStatus(done, successColor());
        }

        void removeRule(int row) {
            if (row < 0 || row >= table->rowCount() || table->item(row, ColAction) == nullptr) return;
            const int action = table->item(row, ColAction)->data(RoleAction).toInt();
            const QString line = table->item(row, ColAction)->data(RoleLine).toString();
            if (action < 0 || action >= actionKinds().size()) return;
            if (QMessageBox::question(window(), SLS("Удалить правило"),
                                      SLS("Удалить %1 из списка «%2»?").arg(line, actionKinds()[action].label)) != QMessageBox::Yes)
                return;
            const auto fresh = writableCopy();
            if (!fresh) return;
            if (!fresh->RemoveSimpleRule(line, targetOf(actionKinds()[action].action))) {
                setStatus(SLS("Правила %1 уже нет в профиле.").arg(line), warningColor());
                reload();
                return;
            }
            if (const QString why = saveRoute(fresh); !why.isEmpty()) {
                setStatus(SLS("Не сохранено: %1").arg(why), dangerColor());
                return;
            }
            if (editing && editAction == action && editLine == line) stopEditing();
            reload();
            if (table->rowCount() > 0) table->selectRow(qMin(row, table->rowCount() - 1));
            setStatus(SLS("Удалено: %1 из «%2»").arg(line, actionKinds()[action].label), successColor());
        }

        void showRowMenu(const QPoint &pos) {
            const int row = table->rowAt(pos.y());
            if (row < 0) return;
            table->selectRow(row);
            const QString line = table->item(row, ColAction)->data(RoleLine).toString();
            const bool writable = route != nullptr && blocker.isEmpty();
            QMenu menu(this);
            auto *edit = menu.addAction(SLS("Изменить"));
            edit->setEnabled(writable);
            auto *remove = menu.addAction(SLS("Удалить"));
            remove->setEnabled(writable);
            menu.addSeparator();
            auto *copy = menu.addAction(SLS("Копировать правило"));
            QAction *chosen = menu.exec(table->viewport()->mapToGlobal(pos));
            if (chosen == edit) startEditing(row);
            else if (chosen == remove) removeRule(row);
            else if (chosen == copy) QGuiApplication::clipboard()->setText(line);
        }
    };

} // namespace Solas
