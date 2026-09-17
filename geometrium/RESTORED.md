# Geometrium — восстановленный недо-майнкрафт

Эта папка — не часть Cubic Battle 4. Это **восстановленный прототип
«недо-майнкрафта»**: тот самый блоковый мир из истории разработки, который
потом был удалён.

## Откуда взято

| | |
| --- | --- |
| Репозиторий-источник | `andreibioazum-cell/Project-Enjoer` (сейчас переделан под Vulkan + DimScript) |
| Коммит | **`c69a634`** — «Minecraft-style menu, FPS-focused options, pockmarked terrain and crouch/crawl», 11.09.2026 |
| Как туда попало | `f651bbe` (07.09, «rename rbx to Geometrium», пещеры, прозрачная вода, короткая дальность), `041f9d7` (10.09, «Restore the Geometrium block world as a playset») |
| Как было удалено | `fcd4fe2` (13.09, «Replace voxel prototype with Dawn cube») — блоковый мир пропал из main |

Код перенесён в этот репозиторий **как есть**, без правок логики: ровно то
состояние, в котором недо-майнкрафт был на 11 сентября (меню Play / Servers /
Options / Quit, присед и ползание, изрытый рельеф, пещеры, вода, 3D-рука).

## Что это

Первая игра от первого лица на C99 без внешних зависимостей: копаешь,
строишь и летаешь по стримящемуся миру из травы, камня, песка, воды, брёвен и
листвы; мягкий небесный свет, текущая вода и пещеры, которые выходят на
поверхность. Меню — плоские кнопки в стиле Minecraft, за ними замороженный
мир под тёмной плёнкой.

Внутри:

```text
assets/            тайлы (PNG), шрифт, звуки
game/              упаковка под Android: AndroidManifest.xml и Java-активити
src/               все исходники на C
  core/            состояние, ошибки, логи, чтение ассетов, настройки, game.c (роутер меню/игры)
  graphics/        2D-рендер: примитивы, текстуры, текст (+ ttf/)
  sound/           PCM-микшер и AudioTrack
  geometrium/      мир от первого лица: игрок, прицел-луч, тач-раскладка, HUD, 3D-рука
  engine/render/   софтверный воксельный рендер (voxel_*)
  engine/          сценовый движок (eng_*), нужен только тестам
tools/preview/     превью-сервер для ПК/браузера (обычный gcc)
tools/tests/       регрессии: рендер, мир, вода, звук, рука, управление, правки мира
third_party/       stb_image / stb_image_write
```

## Как запустить

ПК/браузер (то, что открыто в live-превью):

```sh
cd geometrium
sh tools/preview/build.sh      # gcc, без CMake и без Android SDK
./preview --port 8090 --assets assets
# открыть http://localhost:8090 — меню, кнопка Play
```

Флаги: `--port`, `--w`, `--h`, `--assets`, `--storage`. Сервер слушает
`0.0.0.0` и использует относительные URL, поэтому работает и через внешний
HTTPS-прокси.

Android (нужен NDK, CMake и aapt; пакет манифеста — `com.cb4`, метка `Enjoer`,
библиотека `libds_game.so`):

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_NDK=$ANDROID_NDK_ROOT
cmake --build build -j
python3 stage_assets.py assets staging/assets
```

Тесты движка/мира (собираются на хосте со стабами NDK-заголовков):

```sh
sh tools/tests/run.sh
```

## Что не переносилось

* **CI-воркфлоу** исходного репозитория (`.github/workflows/main.yml`) в этот
  репозиторий не добавлен, чтобы пуши Cubic Battle не запускали чужую сборку
  APK; его копия лежит рядом как `docs/ci-main.yml`, и при желании её можно
  положить в `.github/workflows/` (нужен доступ к workflow-файлам).
* История коммитов Project-Enjoer: сюда приехал только снимок дерева на
  `c69a634`.
* Папка `data/` и бинарник `preview` — локальные, они в `.gitignore`.

Полное описание — в `README.md` рядом.
