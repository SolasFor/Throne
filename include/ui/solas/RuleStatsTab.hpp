#pragma once

// Solas Edition: the "Rules" tab of the bottom panel. Three views behind a segmented switch:
//  - Graph: new connections (or traffic) per matched rule over the last minutes;
//  - Statistics: one row per rule with connections, active, traffic, speed and last match;
//  - Editor: add, change and remove the simple rules of the active routing profile (RuleEditor.hpp).
// Fed by Engine::StatsService::connectionsUpdated, i.e. the core's own rule for every connection.
// Fields of Stats::ConnectionMetadata are read through C++20 requires-accessors, so a renamed field
// degrades one column instead of breaking the build.

#include <QAbstractAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <type_traits>

#include "include/stats/connections/connectionLister.hpp"
#include "include/ui/solas/QuickRules.hpp"
#include "include/ui/solas/RuleEditor.hpp"
#include "include/ui/solas/SolasTheme.hpp"

namespace Solas {

    // ---- value conversion -------------------------------------------------------------------------------

    template <class V>
    QString asText(const V &v) {
        using D = std::decay_t<V>;
        if constexpr (std::is_same_v<D, QString>) return v;
        else if constexpr (std::is_same_v<D, QStringList>) return v.join(SLS(" → "));
        else if constexpr (std::is_same_v<D, std::string>) return QString::fromStdString(v);
        else if constexpr (std::is_same_v<D, bool>) return v ? SLS("1") : QString();
        else if constexpr (std::is_arithmetic_v<D>) return QString::number(v);
        else if constexpr (std::is_convertible_v<D, QString>) return QString(v);
        else return {};
    }

    template <class V>
    qint64 asNumber(const V &v) {
        using D = std::decay_t<V>;
        if constexpr (std::is_arithmetic_v<D>) return qint64(v);
        else if constexpr (std::is_same_v<D, QDateTime>) return v.isValid() ? v.toMSecsSinceEpoch() : 0;
        else return 0;
    }

    // ---- Stats::ConnectionMetadata accessors --------------------------------------------------------------

    template <class T>
    QString metaId(const T &c) {
        if constexpr (requires { c.id; }) return asText(c.id);
        else return {};
    }

    template <class T>
    QString metaRule(const T &c) {
        if constexpr (requires { c.rule; }) return asText(c.rule);
        else if constexpr (requires { c.matchedRule; }) return asText(c.matchedRule);
        else return {};
    }

    template <class T>
    QString metaRuleItem(const T &c) {
        if constexpr (requires { c.ruleItem; }) return asText(c.ruleItem);
        else if constexpr (requires { c.rulePayload; }) return asText(c.rulePayload);
        else return {};
    }

    template <class T>
    QString metaOutbound(const T &c) {
        if constexpr (requires { c.outboundName; }) {
            if (const QString s = asText(c.outboundName); !s.isEmpty()) return s;
        }
        if constexpr (requires { c.outbound; }) {
            if (const QString s = asText(c.outbound); !s.isEmpty()) return s;
        }
        if constexpr (requires { c.chain; }) return asText(c.chain);
        return {};
    }

    template <class T>
    QString metaHost(const T &c) {
        if constexpr (requires { c.domain; }) {
            if (const QString s = asText(c.domain).trimmed(); !s.isEmpty()) return s;
        }
        if constexpr (requires { c.dest; }) return Stats::EndpointHost(asText(c.dest).trimmed());
        return {};
    }

    template <class T>
    QString metaProcess(const T &c) {
        QString s;
        if constexpr (requires { c.process; }) s = asText(c.process);
        else if constexpr (requires { c.processName; }) s = asText(c.processName);
        else if constexpr (requires { c.processPath; }) s = asText(c.processPath);
        s = s.trimmed();
        const int slash = std::max(s.lastIndexOf(QLatin1Char('/')), s.lastIndexOf(QLatin1Char('\\')));
        return slash >= 0 ? s.mid(slash + 1) : s;
    }

    template <class T>
    qint64 metaUp(const T &c) {
        if constexpr (requires { c.upload; }) return asNumber(c.upload);
        else if constexpr (requires { c.uploadTotal; }) return asNumber(c.uploadTotal);
        else if constexpr (requires { c.up; }) return asNumber(c.up);
        else return 0;
    }

    template <class T>
    qint64 metaDown(const T &c) {
        if constexpr (requires { c.download; }) return asNumber(c.download);
        else if constexpr (requires { c.downloadTotal; }) return asNumber(c.downloadTotal);
        else if constexpr (requires { c.down; }) return asNumber(c.down);
        else return 0;
    }

    template <class T>
    qint64 metaSpeed(const T &c) {
        qint64 s = 0;
        if constexpr (requires { c.uploadSpeed; }) s += asNumber(c.uploadSpeed);
        else if constexpr (requires { c.upSpeed; }) s += asNumber(c.upSpeed);
        if constexpr (requires { c.downloadSpeed; }) s += asNumber(c.downloadSpeed);
        else if constexpr (requires { c.downSpeed; }) s += asNumber(c.downSpeed);
        return s;
    }

    template <class T>
    bool metaClosed(const T &c) {
        if constexpr (requires { c.closedAtMs; }) return asNumber(c.closedAtMs) != 0;
        else if constexpr (requires { c.closedAt; }) return asNumber(c.closedAt) != 0;
        else if constexpr (requires { c.closed; }) return bool(c.closed);
        else if constexpr (requires { c.isClosed; }) return bool(c.isClosed);
        else if constexpr (requires { c.active; }) return !bool(c.active);
        else return false;
    }

    // ---- helpers ------------------------------------------------------------------------------------------

    inline QString fmtBytes(qint64 bytes) {
        return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
    }

    inline QString shortRule(const QString &key, int maxLen = 42) {
        QString s = key;
        const int arrow = s.indexOf(SLS(" => "));
        if (arrow > 0) s = s.left(arrow);
        s = s.simplified();
        if (s.size() > maxLen) s = s.left(maxLen - 1) + SLS("…");
        return s;
    }

    // "domain_suffix=github.com" (the core's rule text) -> "suffix:github.com"; falls back to the host or the process.
    inline QString quickLineFor(const QString &matched, const QString &host, const QString &process) {
        static const QList<QPair<QString, QString>> map = {
            {SLS("domain_suffix"), SLS("suffix:")}, {SLS("domain_keyword"), SLS("keyword:")},
            {SLS("domain_regex"), SLS("regex:")},   {SLS("domain"), SLS("domain:")},
            {SLS("ip_cidr"), SLS("ip:")},           {SLS("process_name"), SLS("processName:")},
            {SLS("process_path"), SLS("processPath:")}, {SLS("rule_set"), SLS("ruleset:")},
        };
        const QString first = matched.section(QLatin1Char(' '), 0, 0).trimmed();
        const int eq = first.indexOf(QLatin1Char('='));
        if (eq > 0) {
            const QString attr = first.left(eq).trimmed();
            QString value = first.mid(eq + 1).trimmed();
            value.remove(QLatin1Char('[')).remove(QLatin1Char(']')).remove(QLatin1Char('"'));
            for (const auto &pair : map)
                if (attr == pair.first && !value.isEmpty()) return pair.second + value;
        }
        if (!host.isEmpty()) return (QHostAddress(host).isNull() ? SLS("suffix:") : SLS("ip:")) + host;
        if (!process.isEmpty()) return SLS("processName:") + process;
        return {};
    }

    // ---- model --------------------------------------------------------------------------------------------

    struct RuleAgg {
        QString key;
        QString matched;
        QString outbound;
        QString lastHost;
        QString lastProcess;
        qint64 connections = 0;
        qint64 active = 0;
        qint64 up = 0;
        qint64 down = 0;
        qint64 speed = 0;
        qint64 lastSeenMs = 0;
    };

    struct RuleSample {
        qint64 atMs = 0;
        QHash<QString, qint64> conns;
        QHash<QString, qint64> bytes;
    };

    class RuleStatsModel {
    public:
        static constexpr int kMaxSamples = 300; // about five minutes at the 1 Hz poll

        QHash<QString, RuleAgg> rules;
        QList<RuleSample> samples;

        void reset() {
            rules.clear();
            samples.clear();
            seen.clear();
        }

        template <class C>
        static QString keyOf(const C &c) {
            const QString rule = metaRule(c).trimmed();
            if (!rule.isEmpty()) return rule;
            const QString out = metaOutbound(c);
            return SLS("final → %1").arg(out.isEmpty() ? SLS("?") : out);
        }

        template <class List>
        void ingest(const List &rows) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            RuleSample sample;
            sample.atMs = now;
            for (auto &agg : rules) {
                agg.active = 0;
                agg.speed = 0;
            }
            QSet<QString> present;
            present.reserve(int(rows.size()));
            for (const auto &c : rows) {
                QString id = metaId(c);
                if (id.isEmpty()) id = metaHost(c) + QLatin1Char('|') + metaProcess(c) + QLatin1Char('|') + keyOf(c);
                present.insert(id);
                auto it = seen.find(id);
                const bool isNew = it == seen.end();
                if (isNew) it = seen.insert(id, Seen{keyOf(c), 0, 0});
                const QString key = it->key;
                RuleAgg &agg = rules[key];
                if (agg.key.isEmpty()) agg.key = key;
                if (isNew) {
                    agg.connections++;
                    sample.conns[key] += 1;
                    agg.lastSeenMs = now;
                }
                const qint64 up = metaUp(c), down = metaDown(c);
                const qint64 du = std::max<qint64>(0, up - it->up);
                const qint64 dd = std::max<qint64>(0, down - it->down);
                it->up = std::max<qint64>(it->up, up);
                it->down = std::max<qint64>(it->down, down);
                if (du + dd > 0) {
                    agg.up += du;
                    agg.down += dd;
                    sample.bytes[key] += du + dd;
                    agg.lastSeenMs = now;
                }
                if (!metaClosed(c)) {
                    agg.active++;
                    agg.speed += metaSpeed(c);
                }
                if (const QString item = metaRuleItem(c); !item.isEmpty()) agg.matched = item;
                if (const QString out = metaOutbound(c); !out.isEmpty()) agg.outbound = out;
                if (const QString host = metaHost(c); !host.isEmpty()) agg.lastHost = host;
                if (const QString process = metaProcess(c); !process.isEmpty()) agg.lastProcess = process;
            }
            // Ids the core no longer reports have left its closed ring for good.
            for (auto it = seen.begin(); it != seen.end();) {
                if (!present.contains(it.key())) it = seen.erase(it);
                else ++it;
            }
            samples.append(sample);
            while (samples.size() > kMaxSamples) samples.removeFirst();
        }

        qint64 totalConnections() const {
            qint64 n = 0;
            for (const auto &a : rules) n += a.connections;
            return n;
        }

        qint64 totalBytes() const {
            qint64 n = 0;
            for (const auto &a : rules) n += a.up + a.down;
            return n;
        }

    private:
        struct Seen {
            QString key;
            qint64 up = 0;
            qint64 down = 0;
        };
        QHash<QString, Seen> seen;
    };

    // ---- graph --------------------------------------------------------------------------------------------

    class RuleChart : public QWidget {
    public:
        explicit RuleChart(RuleStatsModel *model, QWidget *parent = nullptr) : QWidget(parent), model(model) {
            setMouseTracking(true);
            setMinimumHeight(150);
            reveal = new QVariantAnimation(this);
            reveal->setDuration(650);
            reveal->setStartValue(0.0);
            reveal->setEndValue(1.0);
            reveal->setEasingCurve(QEasingCurve::OutCubic);
            connect(reveal, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                progress = v.toDouble();
                update();
            });
        }

        bool bytesMode = false;

        // Draws the lines in from the left, like a React Bits reveal.
        void replay() {
            reveal->stop();
            progress = 0.0;
            reveal->start();
        }

    protected:
        void mouseMoveEvent(QMouseEvent *event) override {
            hoverX = event->position().x();
            update();
        }

        void leaveEvent(QEvent *) override {
            hoverX = -1;
            update();
        }

        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QPalette pal = palette();
            const QColor text = pal.color(QPalette::WindowText);
            const QColor muted = pal.color(QPalette::PlaceholderText);
            QColor grid = text;
            grid.setAlpha(26);
            QFont small = font();
            small.setPointSizeF(qMax(7.0, small.pointSizeF() * 0.9));
            p.setFont(small);

            const QRectF area = QRectF(rect()).adjusted(56, 34, -16, -26);
            const auto &samples = model->samples;
            const int n = int(samples.size());

            QHash<QString, qint64> totals;
            for (const auto &s : samples) {
                const auto &h = bytesMode ? s.bytes : s.conns;
                for (auto it = h.cbegin(); it != h.cend(); ++it) totals[it.key()] += it.value();
            }
            QStringList keys = totals.keys();
            std::sort(keys.begin(), keys.end(), [&totals](const QString &a, const QString &b) { return totals.value(a) > totals.value(b); });
            const QStringList top = keys.mid(0, 5);

            if (n < 2 || top.isEmpty()) {
                p.setPen(muted);
                p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                           SLS("Пока нет данных. Откройте сайт или приложение — совпадения правил появятся здесь."));
                return;
            }

            QVector<QVector<qint64>> series(top.size(), QVector<qint64>(n, 0));
            qint64 maxValue = 1;
            for (int i = 0; i < n; ++i) {
                const auto &h = bytesMode ? samples[i].bytes : samples[i].conns;
                for (int s = 0; s < top.size(); ++s) {
                    const qint64 v = h.value(top[s]);
                    series[s][i] = v;
                    maxValue = std::max(maxValue, v);
                }
            }
            maxValue = niceCeil(maxValue);

            auto xAt = [&](int i) { return area.left() + area.width() * i / qreal(n - 1); };
            auto yAt = [&](qint64 v) { return area.bottom() - area.height() * qreal(v) / qreal(maxValue); };

            for (int g = 0; g <= 4; ++g) {
                const qreal y = area.bottom() - area.height() * g / 4.0;
                p.setPen(QPen(grid, 1));
                p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                p.setPen(muted);
                p.drawText(QRectF(0, y - 9, area.left() - 8, 18), Qt::AlignRight | Qt::AlignVCenter, valueLabel(maxValue * g / 4));
            }
            const qint64 spanMs = samples.last().atMs - samples.first().atMs;
            p.drawText(QRectF(area.left(), area.bottom() + 6, 160, 16), Qt::AlignLeft, SLS("−%1").arg(spanLabel(spanMs)));
            p.drawText(QRectF(area.right() - 160, area.bottom() + 6, 160, 16), Qt::AlignRight, SLS("сейчас"));

            const qreal shown = std::clamp(progress, 0.0, 1.0);
            p.save();
            p.setClipRect(QRectF(area.left() - 4, area.top() - 6, area.width() * shown + 8, area.height() + 12));
            for (int s = int(top.size()) - 1; s >= 0; --s) {
                const QColor c = seriesColor(s);
                QPainterPath line;
                line.moveTo(xAt(0), yAt(series[s][0]));
                for (int i = 1; i < n; ++i) {
                    const qreal x0 = xAt(i - 1), x1 = xAt(i);
                    const qreal y0 = yAt(series[s][i - 1]), y1 = yAt(series[s][i]);
                    const qreal mx = (x0 + x1) / 2.0;
                    line.cubicTo(mx, y0, mx, y1, x1, y1);
                }
                QPainterPath fill = line;
                fill.lineTo(xAt(n - 1), area.bottom());
                fill.lineTo(xAt(0), area.bottom());
                fill.closeSubpath();
                QLinearGradient gradient(0, area.top(), 0, area.bottom());
                QColor top0 = c;
                top0.setAlpha(s == 0 ? 80 : 45);
                QColor bottom0 = c;
                bottom0.setAlpha(0);
                gradient.setColorAt(0, top0);
                gradient.setColorAt(1, bottom0);
                p.fillPath(fill, gradient);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(c, s == 0 ? 2.2 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPath(line);
            }
            p.restore();

            // Legend.
            qreal lx = area.left();
            for (int s = 0; s < top.size(); ++s) {
                const QString name = shortRule(top[s], 28);
                const qreal w = p.fontMetrics().horizontalAdvance(name);
                if (lx + 14 + w > width() - 8) break;
                p.setPen(Qt::NoPen);
                p.setBrush(seriesColor(s));
                p.drawRoundedRect(QRectF(lx, 11, 10, 10), 3, 3);
                p.setPen(text);
                p.drawText(QPointF(lx + 14, 20), name);
                lx += 14 + w + 18;
            }

            // Hover guide with the values under the cursor.
            if (hoverX >= area.left() && hoverX <= area.right()) {
                const int i = std::clamp(int(std::lround((hoverX - area.left()) / area.width() * (n - 1))), 0, n - 1);
                const qreal x = xAt(i);
                p.setPen(QPen(muted, 1, Qt::DashLine));
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                QStringList lines;
                lines << QDateTime::fromMSecsSinceEpoch(samples[i].atMs).toString(SLS("HH:mm:ss"));
                for (int s = 0; s < top.size(); ++s) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(seriesColor(s));
                    p.drawEllipse(QPointF(x, yAt(series[s][i])), 3.5, 3.5);
                    lines << SLS("%1: %2").arg(shortRule(top[s], 28), valueLabel(series[s][i]));
                }
                const QFontMetricsF fm(small);
                qreal boxW = 0;
                for (const auto &l : lines) boxW = std::max(boxW, fm.horizontalAdvance(l));
                boxW += 20;
                const qreal boxH = lines.size() * (fm.height() + 2) + 12;
                qreal bx = x + 12;
                if (bx + boxW > width() - 4) bx = x - 12 - boxW;
                const QRectF box(bx, area.top() + 4, boxW, boxH);
                QColor bg = pal.color(QPalette::ToolTipBase);
                bg.setAlpha(240);
                p.setPen(QPen(grid, 1));
                p.setBrush(bg);
                p.drawRoundedRect(box, 8, 8);
                qreal ty = box.top() + 6 + fm.ascent();
                for (int l = 0; l < lines.size(); ++l) {
                    p.setPen(l == 0 ? muted : seriesColor(l - 1));
                    p.drawText(QPointF(box.left() + 10, ty), lines[l]);
                    ty += fm.height() + 2;
                }
            }
        }

    private:
        RuleStatsModel *model = nullptr;
        QVariantAnimation *reveal = nullptr;
        qreal progress = 1.0;
        qreal hoverX = -1;

        QString valueLabel(qint64 v) const { return bytesMode ? fmtBytes(v) : QString::number(v); }

        static QString spanLabel(qint64 ms) {
            const qint64 s = ms / 1000;
            if (s < 90) return SLS("%1 с").arg(s);
            return SLS("%1 мин").arg((s + 30) / 60);
        }

        static qint64 niceCeil(qint64 v) {
            if (v <= 4) return 4;
            const double magnitude = std::pow(10.0, std::floor(std::log10(double(v))));
            for (const double m : {1.0, 2.0, 2.5, 5.0, 10.0})
                if (m * magnitude >= double(v)) return qint64(std::ceil(m * magnitude));
            return v;
        }
    };

    // Numeric columns sort by the raw value kept in Qt::UserRole, not by the formatted text.
    class NumItem : public QTableWidgetItem {
    public:
        bool operator<(const QTableWidgetItem &other) const override {
            return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
        }
    };

    // ---- tab ----------------------------------------------------------------------------------------------

    class RuleStatsTab : public QWidget {
    public:
        enum Column { ColRule, ColMatched, ColOutbound, ColConnections, ColActive, ColTraffic, ColSpeed, ColLast, ColCount };

        std::function<void()> feedChanged;

        explicit RuleStatsTab(QWidget *parent = nullptr) : QWidget(parent) {
            setObjectName(SLS("solas_rules_tab"));
            auto *root = new QVBoxLayout(this);
            root->setContentsMargins(8, 8, 8, 8);
            root->setSpacing(8);

            auto *bar = new QHBoxLayout;
            bar->setSpacing(0);
            graphButton = segment(SLS("График"), SLS("solasSegLeft"));
            statsButton = segment(SLS("Статистика"), SLS("solasSegMid"));
            editorButton = segment(SLS("Редактор"), SLS("solasSegRight"));
            auto *group = new QButtonGroup(this);
            group->setExclusive(true);
            group->addButton(graphButton, 0);
            group->addButton(statsButton, 1);
            group->addButton(editorButton, 2);
            graphButton->setChecked(true);
            bar->addWidget(graphButton);
            bar->addWidget(statsButton);
            bar->addWidget(editorButton);
            bar->addSpacing(12);
            metricBox = new QComboBox;
            metricBox->addItem(SLS("Новые соединения"));
            metricBox->addItem(SLS("Трафик"));
            bar->addWidget(metricBox);
            bar->addSpacing(10);
            summary = new QLabel;
            summary->setTextFormat(Qt::PlainText);
            summary->setForegroundRole(QPalette::PlaceholderText);
            bar->addWidget(summary, 1);
            background = new QCheckBox(SLS("Собирать в фоне"));
            background->setToolTip(SLS("Считать совпадения, даже когда открыта другая вкладка или окно свёрнуто"));
            background->setChecked(settings().value(SLS("stats/background"), false).toBool());
            auto *resetButton = new QToolButton;
            resetButton->setText(SLS("Сбросить"));
            auto *quickButton = new QPushButton(SLS("Быстрое правило…"));
            quickButton->setToolTip(SLS("Ctrl+R"));
            quickButton->setAutoDefault(false);
            bar->addWidget(background);
            bar->addSpacing(8);
            bar->addWidget(resetButton);
            bar->addSpacing(6);
            bar->addWidget(quickButton);
            root->addLayout(bar);

            stack = new QStackedWidget;
            chart = new RuleChart(&model);
            table = new QTableWidget(0, ColCount);
            table->setHorizontalHeaderLabels({SLS("Правило"), SLS("Совпало по"), SLS("Выход"), SLS("Соединений"),
                                              SLS("Активных"), SLS("Трафик ↓ / ↑"), SLS("Скорость"), SLS("Последнее")});
            table->verticalHeader()->hide();
            table->setAlternatingRowColors(true);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setShowGrid(false);
            table->setWordWrap(false);
            table->setContextMenuPolicy(Qt::CustomContextMenu);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            table->horizontalHeader()->setSectionResizeMode(ColRule, QHeaderView::Stretch);
            table->horizontalHeader()->setHighlightSections(false);
            table->setColumnWidth(ColMatched, 200);
            table->setColumnWidth(ColOutbound, 120);
            table->setColumnWidth(ColConnections, 95);
            table->setColumnWidth(ColActive, 80);
            table->setColumnWidth(ColTraffic, 150);
            table->setColumnWidth(ColSpeed, 95);
            table->setColumnWidth(ColLast, 85);
            table->setSortingEnabled(true);
            table->sortByColumn(ColConnections, Qt::DescendingOrder);
            stack->addWidget(chart);
            stack->addWidget(table);
            editor = new RuleEditorPanel;
            stack->addWidget(editor);
            root->addWidget(stack, 1);

            setStyleSheet(SLS(
                "QToolButton#solasSegLeft, QToolButton#solasSegMid, QToolButton#solasSegRight { padding: 4px 14px; border: 1px solid palette(mid); background: transparent; }"
                "QToolButton#solasSegLeft { border-top-left-radius: 8px; border-bottom-left-radius: 8px; border-top-right-radius: 0px; border-bottom-right-radius: 0px; border-right: none; }"
                "QToolButton#solasSegMid { border-radius: 0px; border-right: none; }"
                "QToolButton#solasSegRight { border-top-right-radius: 8px; border-bottom-right-radius: 8px; border-top-left-radius: 0px; border-bottom-left-radius: 0px; }"
                "QToolButton#solasSegLeft:hover:!checked, QToolButton#solasSegMid:hover:!checked, QToolButton#solasSegRight:hover:!checked { background: palette(alternate-base); }"
                "QToolButton#solasSegLeft:checked, QToolButton#solasSegMid:checked, QToolButton#solasSegRight:checked { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); }"));

            connect(group, &QButtonGroup::idClicked, this, [this](int id) { switchTo(id); });
            connect(metricBox, &QComboBox::currentIndexChanged, this, [this](int index) {
                chart->bytesMode = index == 1;
                chart->replay();
            });
            connect(resetButton, &QToolButton::clicked, this, [this] {
                model.reset();
                rows.clear();
                table->setRowCount(0);
                chart->update();
                updateSummary();
            });
            connect(quickButton, &QPushButton::clicked, this, [this] { openQuickRules(window()); });
            connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { ruleFromRow(row, -1); });
            connect(table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showRowMenu(pos); });
            connect(background, &QCheckBox::toggled, this, [this](bool on) {
                settings().setValue(SLS("stats/background"), on);
                settings().sync();
                if (feedChanged) feedChanged();
            });
            updateSummary();
        }

        bool collectInBackground() const { return background->isChecked(); }

        template <class List>
        void ingest(const List &connections) {
            model.ingest(connections);
            if (isVisible()) {
                if (stack->currentIndex() == 0) chart->update();
                else if (stack->currentIndex() == 1) refreshTable();
            }
            updateSummary();
        }

    protected:
        void showEvent(QShowEvent *event) override {
            QWidget::showEvent(event);
            if (stack->currentIndex() == 0) chart->replay();
            else if (stack->currentIndex() == 1) refreshTable();
        }

    private:
        RuleStatsModel model;
        QToolButton *graphButton = nullptr;
        QToolButton *statsButton = nullptr;
        QToolButton *editorButton = nullptr;
        QComboBox *metricBox = nullptr;
        QLabel *summary = nullptr;
        QCheckBox *background = nullptr;
        QStackedWidget *stack = nullptr;
        RuleChart *chart = nullptr;
        QTableWidget *table = nullptr;
        RuleEditorPanel *editor = nullptr;
        QHash<QString, QVector<QTableWidgetItem *>> rows;

        static QToolButton *segment(const QString &text, const QString &name) {
            auto *button = new QToolButton;
            button->setText(text);
            button->setObjectName(name);
            button->setCheckable(true);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            return button;
        }

        void switchTo(int id) {
            if (stack->currentIndex() == id) return;
            stack->setCurrentIndex(id);
            if (id == 1) refreshTable();
            else if (id == 0) chart->replay();
            // The editor re-reads the profile in its own showEvent; the metric switch means nothing there.
            metricBox->setVisible(id != 2);
            QWidget *page = stack->currentWidget();
            auto *effect = new QGraphicsOpacityEffect(page);
            effect->setOpacity(0.0);
            page->setGraphicsEffect(effect);
            auto *animation = new QPropertyAnimation(effect, "opacity", page);
            animation->setDuration(180);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            QPointer<QWidget> guard(page);
            connect(animation, &QPropertyAnimation::finished, page, [guard] {
                QTimer::singleShot(0, guard.data(), [guard] { if (guard) guard->setGraphicsEffect(nullptr); });
            });
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        }

        void updateSummary() {
            summary->setText(SLS("Правил: %1 · соединений: %2 · трафик: %3")
                                 .arg(model.rules.size())
                                 .arg(model.totalConnections())
                                 .arg(fmtBytes(model.totalBytes())));
        }

        static void setNum(QTableWidgetItem *item, qint64 value, const QString &text) {
            item->setData(Qt::UserRole, value);
            item->setText(text);
        }

        void refreshTable() {
            const bool sorting = table->isSortingEnabled();
            table->setSortingEnabled(false);
            for (auto it = model.rules.cbegin(); it != model.rules.cend(); ++it) {
                const RuleAgg &a = it.value();
                auto r = rows.find(a.key);
                if (r == rows.end()) {
                    const int row = table->rowCount();
                    table->insertRow(row);
                    QVector<QTableWidgetItem *> items;
                    for (int c = 0; c < ColCount; ++c) {
                        QTableWidgetItem *item = c >= ColConnections ? static_cast<QTableWidgetItem *>(new NumItem) : new QTableWidgetItem;
                        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                        if (c >= ColConnections) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                        table->setItem(row, c, item);
                        items << item;
                    }
                    items[ColRule]->setData(Qt::UserRole + 1, a.key);
                    r = rows.insert(a.key, items);
                }
                const auto &items = r.value();
                items[ColRule]->setText(shortRule(a.key, 80));
                items[ColRule]->setToolTip(a.key);
                items[ColMatched]->setText(!a.matched.isEmpty() ? a.matched : a.lastHost);
                items[ColMatched]->setToolTip(SLS("Последний хост: %1\nПроцесс: %2").arg(a.lastHost, a.lastProcess));
                items[ColOutbound]->setText(a.outbound);
                setNum(items[ColConnections], a.connections, QString::number(a.connections));
                setNum(items[ColActive], a.active, a.active > 0 ? QString::number(a.active) : QString());
                setNum(items[ColTraffic], a.up + a.down, SLS("%1 / %2").arg(fmtBytes(a.down), fmtBytes(a.up)));
                setNum(items[ColSpeed], a.speed, a.speed > 0 ? fmtBytes(a.speed) + SLS("/с") : QString());
                setNum(items[ColLast], a.lastSeenMs, a.lastSeenMs > 0 ? QDateTime::fromMSecsSinceEpoch(a.lastSeenMs).toString(SLS("HH:mm:ss")) : QString());
            }
            table->setSortingEnabled(sorting);
        }

        const RuleAgg *aggAt(int row) const {
            if (row < 0) return nullptr;
            const auto *item = table->item(row, ColRule);
            if (item == nullptr) return nullptr;
            const auto it = model.rules.constFind(item->data(Qt::UserRole + 1).toString());
            return it == model.rules.cend() ? nullptr : &it.value();
        }

        void ruleFromRow(int row, int actionIndex) {
            const RuleAgg *a = aggAt(row);
            if (a == nullptr) return;
            const QString source = !a->matched.isEmpty() ? a->matched : a->key;
            openQuickRules(window(), quickLineFor(source, a->lastHost, a->lastProcess), actionIndex);
        }

        void showRowMenu(const QPoint &pos) {
            const int row = table->rowAt(pos.y());
            const RuleAgg *a = aggAt(row);
            if (a == nullptr) return;
            const QString source = !a->matched.isEmpty() ? a->matched : a->key;
            const QString line = quickLineFor(source, a->lastHost, a->lastProcess);
            const QString key = a->key;
            QMenu menu(this);
            if (!line.isEmpty()) {
                menu.addSection(line);
                for (int i = 0; i < actionKinds().size(); ++i)
                    menu.addAction(SLS("%1…").arg(actionKinds()[i].label), this, [this, row, i] { ruleFromRow(row, i); });
                menu.addSeparator();
            }
            menu.addAction(SLS("Копировать правило"), this, [key] { QGuiApplication::clipboard()->setText(key); });
            menu.exec(table->viewport()->mapToGlobal(pos));
        }
    };

    // Keeps the core's connection poll pushing to the UI while the Rules tab is on screen (or always, with
    // "collect in background"). MainWindow::syncConnectionViewState() only counts the Connections tab, so this
    // re-asserts SetInView(true) right after every event that made it re-evaluate.
    class FeedKeeper : public QObject {
    public:
        FeedKeeper(QWidget *window, QTabWidget *tabs, RuleStatsTab *tab) : QObject(window), window(window), tabs(tabs), tab(tab) {}

        void schedule() {
            QTimer::singleShot(0, this, [this] { apply(); });
        }

        bool eventFilter(QObject *watched, QEvent *event) override {
            if (watched == window) {
                switch (event->type()) {
                    case QEvent::Show:
                    case QEvent::Hide:
                    case QEvent::WindowStateChange:
                        schedule();
                        break;
                    default:
                        break;
                }
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        QPointer<QWidget> window;
        QPointer<QTabWidget> tabs;
        QPointer<RuleStatsTab> tab;
        bool forcing = false;

        void apply() {
            if (!window || !tabs || !tab || Stats::connection_lister == nullptr) return;
            const bool onScreen = window->isVisible() && !window->isMinimized();
            const bool want = (onScreen && tabs->currentWidget() == tab) || tab->collectInBackground();
            if (want) {
                Stats::connection_lister->SetInView(true);
            } else if (forcing) {
                // Hand the poll back to the state MainWindow would pick: only the Connections tab keeps it.
                QWidget *current = tabs->currentWidget();
                Stats::connection_lister->SetInView(onScreen && current != nullptr && current->objectName() == SLS("connections_tab"));
            }
            forcing = want;
        }
    };

} // namespace Solas
