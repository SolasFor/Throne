<div align="center">

<img src="res/public/Throne.png" width="128" alt="Throne Solas Edition">

# Throne — Solas Edition

**Десктопный прокси-клиент на Qt и [sing-box](https://github.com/SagerNet/sing-box) — с тёплой темой, быстрыми правилами и статистикой маршрутизации**

[![Release](https://img.shields.io/github/v/release/SolasFor/Throne?style=flat-square&label=релиз&color=D97757)](https://github.com/SolasFor/Throne/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/SolasFor/Throne/total?style=flat-square&logo=github&label=загрузки&color=C96442)](https://github.com/SolasFor/Throne/releases)
[![License](https://img.shields.io/github/license/SolasFor/Throne?style=flat-square&label=лицензия&color=87867F)](LICENSE)

</div>

Throne — Solas Edition управляет прокси-профилями и подписками, направляет трафик по профилям маршрутизации и работает как системный прокси или в режиме Tun. В этой версии к нему добавлены своё оформление и инструменты, чтобы править правила маршрутизации и видеть, какие из них срабатывают.

## Возможности Solas Edition

### Тема Solas

Тёплая палитра в стиле Claude: фон `#21201E` в тёмном варианте и `#FAF9F5` в светлом, акцент `#D97757`. Скруглённые кнопки, поля, меню и вкладки, мягкая подсветка при наведении и фокусе.

Меню программы → **Тема Solas**: как в системе, тёмная, светлая или выключена (тогда работает обычный выбор темы в настройках).

### Быстрые правила — `Ctrl+R`

Окно для правил активного профиля маршрутизации, открывается по `Ctrl+R` или из меню маршрутизации.

- Тип правила (домен, поддомены, ключевое слово, regex, IP / подсеть, rule-set, процесс), значение и действие: через прокси, напрямую, блокировать, WARP-bypass.
- Проверка прямо при вводе: ошибка в строке, повтор, предупреждение «правило не сработает — его перехватит другое».
- Если строка уже была в другом списке, она переносится, а не дублируется.
- Списки правил по действиям с правкой целиком.
- Импорт и экспорт профиля маршрутизации: JSON `throne-route-profile` или ссылка `throne://`.

Профили, заданные сырым JSON, защищённые и обновляемые по ссылке или подпиской, окно не меняет.

### Вкладка «Правила»: График | Статистика

Нижняя панель → **Правила**. Данные берутся у ядра: для каждого соединения оно сообщает сработавшее правило.

- **График** — пять самых активных правил за последние ~5 минут: новые соединения или трафик, значения при наведении.
- **Статистика** — таблица: правило, по чему совпало, выход, соединения, активные, трафик ↓ / ↑, скорость, последнее срабатывание. Двойной клик или правый клик по строке открывает быстрое правило с уже подставленным доменом.
- Галочка **Собирать в фоне** продолжает подсчёт на других вкладках и при свёрнутом окне.

Настройки Solas хранятся в `config/solas.ini` рядом с программой.

## Скачать

Готовые сборки — на странице [Releases](https://github.com/SolasFor/Throne/releases/latest):

| Система | Файл |
|:--|:--|
| Windows | `Throne-<версия>-windows64.zip` — portable x64, установщик `windows-universal-installer.exe` |
| macOS | `Throne-<версия>-macos-arm64.zip` (Apple Silicon), `macos-amd64.zip` (Intel) |
| Debian / Ubuntu | `Throne-<версия>-debian-amd64.deb` |
| Fedora / openSUSE | `Throne-<версия>-fedora-amd64.rpm` |
| Linux portable | `Throne-<версия>-linux-amd64.zip` |

Portable-версию для Windows достаточно распаковать и запустить `Throne.exe`.

### macOS

Сборки не подписаны, поэтому перед первым запуском снимите карантин: `xattr -d com.apple.quarantine /path/to/Throne.app`. Перенесите `Throne.app` в `/Applications` до первого запуска — иначе может не сработать запрос прав для ядра.

## Протоколы

SOCKS, HTTP(S), Shadowsocks, Trojan, VMess, VLESS, TUIC, Hysteria, Hysteria2, AnyTLS, Mieru, Snell, NaïveProxy, Juicity, TrustTunnel, ShadowTLS, WireGuard, AmneziaWG, MASQUE, SSH, DNSTT / VayDNS, MasterDNS, Xray VLESS (XHTTP / XDRIVE), OpenVPN / OpenConnect, собственные outbound и конфиги sing-box и Xray, цепочки outbound.

Подписки: ссылки, JSON-конфиги sing-box, формат ссылок v2rayN, частично Shadowsocks и Clash.

## Сборка

Сборка идёт в GitHub Actions: Actions → **Throne build matrix** → Run workflow, ветка `dev`, `tag` — номер версии. Пустой `publish` собирает файлы в артефакты запуска, `p` публикует предрелиз, `r` — релиз.

История изменений — в [CHANGELOG.md](CHANGELOG.md).

## Лицензия

[GPL-3.0](LICENSE).
