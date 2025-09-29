# cplot — CLI для контурного тексту Hershey та керування AxiDraw

`cplot` — це невеликий CLI‑застосунок на C (GNU C99), що:

- перетворює UTF‑8 текст або Markdown у контурні шляхи шрифтів Hershey;
- формує превʼю розкладки у SVG (типово) або PNG;
- виконує локальні дії з AxiDraw через контролер EBB (перелік пристроїв, профілювання, керування пером і моторами, сервісні дії);
- зберігає та читає конфігурацію без сторонніх бібліотек.

Вся видима інформація (довідка, журнали та README) українською мовою.


## Вимоги

- macOS або Linux
- `clang` або `gcc`, `make`
- (необов’язково) `clang-format` для `make fmt`

Зовнішні залежності відсутні. Шрифти Hershey постачаються в директорії `hershey/`.


## Збірка, інсталяція, форматування

- Debug‑збірка (типово):
  - `make`
- Release‑збірка:
  - `make BUILD=release`
  - CPU‑специфічна оптимізація: `make BUILD=release PORTABLE=0`
- Інсталяція/деінсталяція (за потреби):
  - `sudo make install` (до `$(PREFIX)/bin`, типово `/usr/local/bin`)
  - `sudo make uninstall`
- Форматування коду (потрібен `clang-format` у PATH):
  - `make fmt`
- Пакування релізу (бінарник + шрифти + ліцензія + README):
  - `make dist` → архів у `dist/*.tar.gz` та `.sha256`

Зібраний бінарник розміщується у `bin/cplot`.


## Швидкий старт

1) Підготуйте профіль пристрою (за наявності AxiDraw):

```
bin/cplot device profile
```

2) Згенеруйте SVG‑превʼю з тексту, переданого через stdin:

```
echo "Привіт, cplot!" | bin/cplot print --preview > out.svg
```

3) PNG‑превʼю у файл:

```
echo "Привіт, PNG" | bin/cplot print --preview --png --output out.png
```

4) Markdown‑вхід:

```
printf "# Заголовок\n*курсив* і **жирний**" | bin/cplot print --preview --format markdown > md.svg
```

5) Перелік шрифтів та родин Hershey:

```
bin/cplot fonts --list
bin/cplot fonts --font-family   # лише родини
```


## Огляд CLI

Доступні підкоманди: `print`, `device`, `config`, `fonts`, `version`.

- Довідка й версія:
  - `bin/cplot --help`
  - `bin/cplot --version`

### print — побудова розкладки та превʼю

Вхід — файл (позиційний аргумент) або stdin (через pipe). За промовчанням побудова виконується локально; для перегляду результату використовуйте `--preview`.

Приклади:
- `echo "Hello" | bin/cplot print --preview > out.svg`
- `bin/cplot print --preview --png --output out.png input.txt`
- `bin/cplot print --preview --format markdown README.md > out.svg`

Корисні опції розкладки:
- `--portrait` / `--landscape` — орієнтація сторінки
- `--margins T[,R,B,L]` — поля в міліметрах (одне число — для всіх сторін)
- `--width мм`, `--height мм` — розміри паперу
- `--family NAME|ID` — родина або конкретний шрифт Hershey для поточного друку
- `--device name` — обрати профіль (впливає на типові розміри паперу тощо)
- `--preview` — згенерувати превʼю у stdout (типово SVG)
- `--png` — з `--preview`: вивести PNG
- `--output PATH` — з `--preview`: зберегти у файл замість stdout
- `--format markdown` — інтерпретувати вхід як Markdown

Примітка: у поточній версії `print` не надсилає дані на пристрій; використовуються попередній перегляд та перевірка розкладки.

### device — робота з AxiDraw через EBB

Приклади дій:
- `bin/cplot device list` — перелік доступних пристроїв
- `bin/cplot device profile` — підібрати профіль активного пристрою й вивести параметри
- `bin/cplot device pen up|down|toggle` — керування пером
- `bin/cplot device motors on|off` — вмикання/вимикання моторів
- `bin/cplot device jog --dx 5 --dy -3` — ручний зсув у мм
- `bin/cplot device status|position|version` — службова інформація
- `bin/cplot device home|reset|reboot|abort` — сервісні дії

Опції:
- `--device-name NAME` — псевдонім із `device list`
- `--device MODEL` — бажана модель (напр., `minikit2`)
- `--dx мм`, `--dy мм` — параметри для `jog`

Безпека та взаємодія з обладнанням:
- Дії з обладнанням краще перевіряти без підключення пера/ручки.
- Поважайте тайм‑аути та обмеження FIFO/rate (реалізовано всередині `cplot`).
- Доступ до пристрою блокується lock‑файлом у `TMPDIR` (автоматично).

### fonts — перелік шрифтів Hershey

- `bin/cplot fonts --list` — перелік гарнітур
- `bin/cplot fonts --font-family` — лише родини

### config — перегляд і зміна конфігурації

Конфігурація зберігається у `~/.config/cplot/config.json` або за `XDG_CONFIG_HOME`.

Приклади:
- `bin/cplot config show` — показати поточні налаштування
- `bin/cplot config reset` — скинути до типових
- `bin/cplot config set key=value[,key=value...]` — змінити вибрані ключі

Ключі, доступні для `config set` (скорочено; повний список у `--help`):
- `margin`, `margin_t`, `margin_r`, `margin_b`, `margin_l` — поля (мм)
- `font_size` (pt), `font_family` (псевдонім ключа)
- `speed` (мм/с), `accel` (мм/с²)
- `pen_up_speed`, `pen_down_speed`, `pen_up_delay`, `pen_down_delay`
- `servo_timeout` (с)

Примітка: разові параметри сторінки (`--width`, `--height`, орієнтація, тощо) задаються опціями `print` і не зберігаються у конфігу.


## Логування та вивід

- Логування й повідомлення — українською; журнали йдуть на stderr.
- Результати команд (SVG/PNG/списки) друкуються лише у stdout.
- Рівень логування з ENV: `CPLOT_LOG=debug|info|warn|error`
- Вимкнення кольорів з ENV: `CPLOT_LOG_NO_COLOR=1`
- Перевизначення з CLI: `--verbose` (DEBUG), `--no-colors` (без кольорів)


## Тести та smoke‑перевірки

Після збірки перевірте мінімальний набір:

```
bin/cplot --help
echo "Hello" | bin/cplot print --preview > /tmp/hello.svg
echo "Hello" | bin/cplot print --preview --png --output /tmp/hello.png
bin/cplot device list
```


## Архітектура та файли

- `src/main.c` — ініціалізація локалі, логування, запуск CLI
- `src/cli.c` — маршрутизація підкоманд (`print`, `device`, `config`, `fonts`, `version`)
- `src/cmd.c` — виконання команд, превʼю, взаємодія з AxiDraw
- `src/args.c`/`src/help.c` — єдине джерело правди для опцій і довідки
- `src/drawing.c`/`src/svg.c`/`src/png.c` — побудова розкладки та рендер превʼю
- `src/text.c`/`src/font*.c`/`src/glyph.c` — рендеринг тексту Hershey
- `src/axidraw.c`/`src/ebb.c`/`src/serial.c` — протокол EBB і робота з пристроєм
- `src/config.c` — JSON‑конфігурація (XDG‑шлях)
- `docs/ebb.md`, `docs/motion.md`, `docs/grbl.md` — довідкові матеріали

Типова модель пристрою: `minikit2`. Типова родина шрифтів: `EMS Nixish`.


## Ліцензія

Проєкт розповсюджується за ліцензією MIT (див. `LICENSE`).

## Документація API

- Генерація документації Doxygen (HTML):
  - `make docs` (потрібен `doxygen` у PATH)
  - Вихід: `docs/api/html/index.html`

