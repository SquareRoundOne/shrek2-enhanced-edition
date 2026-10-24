# Shrek 2 Enhanced Edition

Unofficial fan add-on that makes the PC version of *Shrek 2* (2004/2005, Unreal Engine 2) look and play well on modern computers. It needs an installed copy of the game and is not a replacement for it.
Made by **SquareRoundOne**. The author used the AI assistant Claude (Anthropic) while making it. Not made or supported by Activision.

**Русская версия описания — ниже.**

## ⚠ First launch
The first start of the game will stutter and lag for a while: shaders are compiled and caches are built (DXVK pipelines, menu fonts, level shadows). It takes a few minutes of play; after that everything is smooth. Do not close the game because of this.

## Install
Download the installer from [Releases](../../releases) (all three files into one folder: `Shrek2EE_Setup_*.exe`, `-1.bin`, `-2.bin`), run the `.exe` and choose the folder of the installed game. It installs everything below (patches, HD textures, sound and video, English and Russian language packs, DXVK), creates shortcuts and an uninstaller that brings the original files back.

## What it adds

### Picture
- Modern renderer on Vulkan (DXVK) with a layer of our own (`shrz.dll`): per-pixel lighting from the game's own lights, relief from the textures, lightmaps with smooth gradients.
- Soft shadows from the sun and the moon (sharp near the object, soft far from it), shadows of characters, pickups and objects that move; shadows from visible torches and fires; no shadow inside a shadow.
- Water reflections, ambient occlusion, bloom, sun rays through leaves and windows, aerial perspective, fine detail where textures are stretched, optional AMD FidelityFX CAS sharpening.
- MSAA up to 8x with supersampling, 16x anisotropic filtering, highest texture detail, characters keep their full model at a distance.
- 32-bit floating-point depth buffer (no flicker in the distance), any resolution and widescreen with Hor+ field of view (16:9, 21:9), borderless fullscreen with instant Alt+Tab.
- Field of view: old (85), modern (95), new (105) or your own; it is restored after cutscenes and loads.
- Widescreen HUD pinned to the screen edges, HD menu and subtitle fonts built for your screen height (with Cyrillic), menus in a centred 4:3 frame, wanted posters at full height, potion shop in its own frame.
- Graphics presets: Original, Default, Weak PCs; every effect can be switched off in the launcher.

### Sound
- OpenAL Soft instead of the 2004 Creative library: 3D positioning, the levels' EAX environments (echo, reverb), 128 voices instead of 32, 48 kHz with good resampling, headphone (HRTF), stereo, quad, 5.1 and 7.1 output.
- HD sound effects and voices, stereo HD music, HD videos (1440p, shown at 2160p on a 4K monitor).

### Gamepad (Xbox, DualShock 4, DualSense and others)
- Xbox and every XInput pad; **DualShock 4 and DualSense directly over USB and Bluetooth**, without Steam or DS4Windows (Steam Input and DSX work too).
- **DualSense:** adaptive triggers (R2 for the attack clicks, L2 for the walk gives light resistance; free in menus; weaker triggers for Donkey and Puss in Boots), **light bar** that shows the hero (Shrek green, Donkey grey, Puss orange) and turns red and blinks as health falls, touchpad press works as Back, light bar returns to blue after the game.
- **Rumble from the game's sounds**: hits, explosions, landings, destruction, giants' steps, magic; strength follows the loudness and the distance to the camera. Works with Xbox, DualShock 4 and DualSense.
- Dead zones of both sticks, separate triggers, hot plugging, camera speed and curve, Y inversion, A/B as on Nintendo, cursor speed in menus; button icons as on Xbox, PlayStation or Nintendo; an action on every button can be reassigned in the launcher.
- Menus from the pad: D-pad moves between items, A selects, LB/RB switch, arrows change sliders; the left stick moves the cursor.
- In-game hints ("press the left mouse button") are rewritten for the controls you actually use, pad or keyboard.

### Keyboard and mouse
- Layouts "Original" and "Modern" and rebinding of any action, two keys per action.
- Raw mouse input without Windows acceleration, own sensitivity, inversion and smoothing.

### Gameplay changes
- **Modern layout** (the launcher also keeps the "Original" one): WASD, left button attacks, **Space jumps and skips cutscenes**, the **mouse wheel switches potions**, the **right button drinks or throws the chosen potion**, **Q opens the potion menu with pictures**, keys 1–9 still use a potion at once, Ctrl stays free. Any action can be rebound, two keys each.
- **Potions: choosing and using are separate.** On the pad the D-pad left/right chooses, Y uses, D-pad up opens the menu. The wheel and the D-pad go through all nine potions in a circle (the ones you do not have are grey and cannot be used).
- **Cutscenes:** skipping puts the camera exactly in its final place instead of letting it drive there for seconds; the opening book scene before the main menu can be skipped (Space or Esc); Space no longer cuts off a hero's line during play, only real scenes are skipped; the field of view comes back to the game's after every scene and every load.
- **Double jump forgives more:** the second jump also works just after the top of the jump and a moment after walking off an edge (the original rule for pulling up onto a ledge is kept whole).
- **Camera:** it no longer works faster at higher FPS (the original turned 2.4 times faster at 144 FPS than at 60); things standing between the camera and the hero turn transparent (switchable); the pause menu opens straight from the pad; Mongo's footsteps shake the camera 25% less.
- **Heroes:** the DualSense triggers are weaker for Donkey and Puss in Boots; the game's hints follow the controls you use.
- **Free look and hidden places** (the launcher's "Places" tab): the Fairy Godmother's corridor and office and other places reachable only with the photo mode; **photo mode** with a free camera that stays in the level; cheats tab, speedrun timer, level select, browser of the cut and English lines.
- **Fixes in play:** wanted posters shown big, pickups keep their size at any distance, the player is no longer lost on the beanstalk bonus levels, the potion menu has no empty frame, save slots keep their chapter pictures.

### Language
- Text and subtitles: Russian (2005), a new translation of all 2494 lines done from the English original, or English with the English signs and posters on the textures (61 textures from the English disc).
- Voices: Russian or English, chosen independently of the text; the Fairy Godmother's commercial changes with the voice.
- Subtitles on every line (scenes and gameplay) with the speaker's name and colour, a choice of font, size and colours. **The black cinematic bars in cutscenes can be removed** (launcher → Game → Subtitles → View → "No black bars"), or kept as in the original.
- English lines that were cut from the Russian dubbing are restored and can be listened to in the launcher.

### Launcher
- Settings for the screen, graphics, sound, keyboard and mouse, gamepad, language, game, with a book-style look made from the game's own pictures; Russian and English interface.
- Start from any level (all chapters, hunt parts, hamlet and bonus levels) with chapter pictures; hidden places (the Fairy Godmother's corridor and office) reachable with the photo mode.
- Cutscene and cut lines browser, cheats tab, module status, speedrun timer, bug report builder (F12 in game or View+LB on the pad: screenshot, mark and logs).
- Photo mode (F9, pad: View+Y): the world freezes, the interface hides, a free camera that stays inside the level.
- Discord status with the level and the hero.

### Engine and performance
- Large Address Aware (4 GB of memory for the game), faster memory copying (the game's old Pentium 4 routine slowed the main thread by half), engine cache 512 MB, main thread kept on the fast cores.
- Independence from the frame rate: camera turn limit, camera smoothing, spinning potions and projectiles behave the same at 60 and 144 FPS.
- Frame limit and V-sync from the launcher, game stays responsive (maximum frame latency 1).
- Fixes: lost player on the beanstalk bonus levels, missing frames on save slots, empty selection square in the potion menu, pause menu askew when opened from the pad, camera and photo mode in hidden places, pickups that shrank with distance, Mongo's camera shake 25% weaker, and more (see the change log in `rel0933/README.txt`).

## What is new in 0.9.73
- Discord status. Subtitle font choice in the launcher and a thinner outline.
- Potion shop in a centred 4:3 frame with dimmed sides; wanted posters are no longer squeezed.
- Better shadows: only from visible flames, real shadows for distant characters, no double shadows, stronger moon shadows at night.
- Pickups no longer shrink with distance; Mongo's camera shake is 25% weaker; sun rays no longer come from interface digits.
- One installer with everything, shortcuts and an uninstaller.

# Шрек 2: Улучшенное Издание

Неофициальная фанатская надстройка, которая делает PC-версию «Шрек 2» (2004/2005, Unreal Engine 2) красивой и удобной на современных компьютерах. Нужна установленная игра, надстройка её не заменяет.
Автор сборки: **SquareRoundOne**. При создании использовался ИИ-помощник Claude (Anthropic). Не сделано и не поддерживается Activision.

## ⚠ Первый запуск
При первом запуске игра какое-то время будет лагать и подтормаживать: идёт компиляция шейдеров и построение кэшей (конвейеры DXVK, шрифты меню, тени уровней). Это несколько минут игры, дальше всё плавно. Не закрывайте игру из-за этого.

## Установка
Скачайте установщик в разделе [Releases](../../releases) (все три файла в одну папку: `Shrek2EE_Setup_*.exe`, `-1.bin`, `-2.bin`), запустите `.exe` и укажите папку с установленной игрой. Ставится всё перечисленное ниже (патчи, HD-текстуры, звук и видео, русский и английский языковые пакеты, DXVK), создаются ярлыки и деинсталлятор, возвращающий оригинальные файлы.

## Что добавлено

### Картинка
- Современный рендер на Vulkan (DXVK) и собственный слой (`shrz.dll`): попиксельный свет от родных источников игры, рельеф из самих текстур, плавные карты освещения.
- Мягкие тени от солнца и луны (чёткие у предмета, мягкие вдали), тени персонажей, подбираемых предметов и всего, что движется; тени от видимых факелов и огня; без «тени в тени».
- Отражения в воде, затенение в углах, свечение, лучи солнца сквозь листву и окна, воздушная перспектива, мелкая деталь на растянутых текстурах, по желанию резкость AMD FidelityFX CAS.
- MSAA до 8x и суперсэмплинг, анизотропная фильтрация 16x, наивысшая детализация текстур, персонажи не упрощаются вдали.
- 32-битный буфер глубины с плавающей точкой (нет мерцания вдали), любое разрешение и широкий экран с углом обзора Hor+ (16:9, 21:9), окно без рамки на весь экран с мгновенным Alt+Tab.
- Угол обзора: старый (85), современный (95), новый (105) или свой; после катсцен и загрузок возвращается.
- Широкоформатный HUD по краям экрана, HD-шрифты меню и субтитров под высоту вашего экрана (с кириллицей), меню в рамке 4:3 по центру, плакаты «Разыскивается» во весь рост, магазин зелий в своей рамке.
- Пресеты графики: «Оригинал», «По умолчанию», «Для слабых ПК»; каждый эффект выключается в лаунчере.

### Звук
- OpenAL Soft вместо библиотеки Creative 2004 года: объёмное позиционирование, эффекты среды EAX из уровней (эхо, реверберация), 128 голосов вместо 32, 48 кГц с хорошим ресемплингом, вывод в наушники (HRTF), стерео, квадро, 5.1 и 7.1.
- HD-эффекты и озвучка, стерео-музыка в HD, HD-видео (1440p, на 4K-мониторе показываются в 2160p).

### Геймпад (Xbox, DualShock 4, DualSense и другие)
- Xbox и все XInput-геймпады; **DualShock 4 и DualSense напрямую по USB и Bluetooth**, без Steam и DS4Windows (Steam Input и DSX тоже работают).
- **DualSense:** адаптивные курки (R2 при ударе нажимается с щелчком, L2 при шаге даёт лёгкое сопротивление; в меню курки свободны; за Осла и Кота слабее), **световая панель** показывает героя (Шрек зелёный, Осёл серый, Кот рыжий) и краснеет и мигает, когда здоровья мало; нажатие тачпада работает как Back, после игры панель снова синяя.
- **Вибрация по звукам игры**: удары, взрывы, приземления, разрушения, топот великанов, магия; сила зависит от громкости и расстояния до камеры. Работает на Xbox, DualShock 4 и DualSense.
- Мёртвые зоны обоих стиков, раздельные курки, подключение на ходу, скорость и кривая камеры, инверсия Y, A/B как у Nintendo, скорость курсора в меню; значки кнопок как на Xbox, PlayStation или Nintendo; действие на каждой кнопке переназначается в лаунчере.
- Меню с геймпада: крестовина переходит по пунктам, A выбирает, LB/RB переключают, стрелки меняют ползунки; левый стик двигает курсор.
- Подсказки игры («нажми левую кнопку мыши») переписываются под ваше управление, геймпад или клавиатуру.

### Клавиатура и мышь
- Раскладки «Оригинал» и «Современная» и переназначение любого действия, по две клавиши на действие.
- Прямой ввод мыши без ускорения Windows, своя скорость, инверсия и сглаживание.

### Изменения в игровом процессе
- **«Современная» раскладка** (в лаунчере есть и «Оригинал»): WASD, левая кнопка — удар, **пробел — прыжок и пропуск катсцены**, **колесо мыши листает зелья**, **правая кнопка — выпить или бросить выбранное зелье**, **Q — меню зелий с картинками**, клавиши 1–9 по-прежнему сразу применяют зелье, Ctrl свободен. Любое действие переназначается, по две клавиши.
- **Зелья: выбор и применение разделены.** На геймпаде крестовина ←/→ выбирает, Y применяет, ↑ открывает меню. Колесо и крестовина листают все девять зелий по кругу (те, которых нет, серые и не применяются).
- **Катсцены:** пропуск ставит камеру точно в конечную точку, а не доезжает до неё секундами; вступление с книгой перед главным меню можно пропустить (пробел или Esc); пробел больше не обрывает реплику героя во время игры, пропускаются только настоящие сцены; угол обзора после каждой сцены и загрузки возвращается к игровому.
- **Двойной прыжок прощает больше:** второй прыжок срабатывает и сразу после верхней точки, и чуть после того, как герой сошёл с края (оригинальное правило подтягивания на уступ сохранено полностью).
- **Камера:** больше не крутится быстрее при высоком FPS (в оригинале при 144 FPS она вращалась в 2,4 раза быстрее, чем при 60); предметы между камерой и героем становятся прозрачными (можно выключить); меню паузы открывается ровно прямо с геймпада; тряска камеры от шагов Монго слабее на 25%.
- **Герои:** курки DualSense слабее за Осла и Кота; подсказки игры подстраиваются под ваше управление.
- **Свободный осмотр и скрытые места** (вкладка «Места» в лаунчере): коридор и приёмная Крёстной Феи и другие места, куда обычно не попасть, через фоторежим; **фоторежим** со свободной камерой, которая не выходит за уровень; вкладка читов, таймер для спидрана, выбор уровня, список вырезанных и английских реплик.
- **Исправления в игре:** плакаты «Разыскивается» показываются крупно, подбираемые предметы не уменьшаются с расстоянием, игрок не теряется на бонусных уровнях «Бобовый стебель», в меню зелий нет пустой рамки, ячейки сохранений сохраняют картинки глав.

### Язык
- Текст и субтитры: русский (2005), новый перевод всех 2494 реплик заново с английского, или английский с английскими вывесками и плакатами на текстурах (61 текстура с английского диска).
- Озвучка: русская или английская, выбирается отдельно от текста; рекламный ролик Крёстной Феи меняется вместе с озвучкой.
- Субтитры на все реплики (в сценах и в игре) с именем и цветом говорящего, выбор шрифта, размера и цветов, **Чёрные полосы в катсценах можно убрать** (лаунчер → Игра → Субтитры → Вид → «Без чёрных полос») или оставить как в оригинале.
- Английские реплики, вырезанные из русской озвучки, восстановлены, их можно послушать в лаунчере.

### Лаунчер
- Настройки экрана, графики, звука, клавиатуры и мыши, геймпада, языка, игры в виде книги из картинок самой игры; интерфейс на русском и английском.
- Запуск с любого уровня (все главы, части охоты, деревня и бонусные уровни) с картинками глав; скрытые места (коридор и приёмная Крёстной Феи) через фоторежим.
- Вырезанные реплики, вкладка читов, состояние модулей, таймер для спидрана, отчёт об ошибке (F12 в игре или View+LB на геймпаде: снимок, отметка и журналы).
- Фоторежим (F9, геймпад: View+Y): мир замирает, интерфейс прячется, свободная камера, которая не выходит за уровень.
- Статус в Discord: уровень и герой.

### Движок и производительность
- Large Address Aware (4 ГБ памяти игре), быстрое копирование памяти (старая процедура под Pentium 4 съедала половину времени главного потока), кеш движка 512 МБ, главный поток на быстрых ядрах.
- Независимость от FPS: ограничение поворота камеры, сглаживание камеры, вращение зелий и снарядов одинаковы на 60 и 144 FPS.
- Ограничение кадров и вертикальная синхронизация из лаунчера, отзывчивость сохранена (задержка кадров 1).
- Исправления: потерянный игрок на бонусных уровнях «Бобовый стебель», пропавшие картинки на ячейках сохранений, пустой квадрат в меню зелий, кривое меню паузы с геймпада, камера и фоторежим в скрытых местах, предметы, уменьшавшиеся с расстоянием, тряска камеры Монго слабее на 25% и другое (см. историю изменений в `rel0933/README.txt`).

## Что нового в 0.9.73
- Статус в Discord. Выбор шрифта субтитров в лаунчере и тонкая обводка.
- Магазин зелий в рамке 4:3 по центру с затемнёнными краями; плакаты больше не сжаты.
- Тени лучше: только от видимого пламени, настоящие тени у далёких персонажей, без двойных теней, лунные тени ночью сильнее.
- Подбираемые предметы не уменьшаются с расстоянием; тряска камеры у Монго слабее на 25%; лучи солнца больше не идут от цифр интерфейса.
- Один установщик со всем содержимым, ярлыки и деинсталлятор.

## Third-party / Использованное
DXVK (zlib), OpenAL Soft (LGPL), Real-ESRGAN (BSD-3, HD textures), AudioSR (MIT) and Apollo (CC BY-SA 4.0) (HD sound), FFmpeg-based Bink audio decoder (LGPL), AMD FidelityFX CAS (MIT), stb (public domain), rgbcx (MIT), DejaVu and Lora fonts (free licenses). Full texts are in the installed `System\ShrekRemaster\licenses`. "Shrek" and the game belong to their owners; this is a free fan project that needs your own copy of the game.

DXVK (zlib), OpenAL Soft (LGPL), Real-ESRGAN (BSD-3, HD-текстуры), AudioSR (MIT) и Apollo (CC BY-SA 4.0) (HD-звук), аудиодекодер Bink на основе FFmpeg (LGPL), AMD FidelityFX CAS (MIT), stb, rgbcx (MIT), шрифты DejaVu и Lora. Полные тексты лицензий лежат в установленной папке `System\ShrekRemaster\licenses`. «Шрек» и игра принадлежат их владельцам; это бесплатный фанатский проект, которому нужна ваша собственная копия игры.

For developers / Для разработчиков: `rz/` (shrz.dll: renderer), `pad/` (dinput8.dll: gamepad, Discord, photo mode), `launcher/`, `tools/`, `rel0933/`. Code: GPL-3.0.
