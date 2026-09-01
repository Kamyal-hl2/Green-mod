# Green Engine v2 — патч #2 (проверка + восстановление + новые фиксы)

## Главное — что случилось с вашей загрузкой

Вы прислали версию, где, по сравнению с прошлым патчем, оказались
полностью удалены/откачены:
- вся интеграция Lua VM (`lua_vm.cpp/.h` превращены в пустые заглушки,
  `gameinterface.cpp` лишился всех 4 точек вызова)
- весь WebView-мост (`SDLActivity.java` откачен к состоянию 2022 года,
  `MenuBridge.java` удалён)
- `.github/workflows/build.yml`, `build.gradle`, `settings.gradle` —
  удалены
- `game/client/wscript` — запись `garrysmod` пропала (при этом в
  `game/server/wscript` осталась — несовпадение сломало бы клиентскую
  сборку)
- `server_garrysmod.vpc` — заменён на полную копию `server_hl2mp.vpc`
  (сотни HL2-шных NPC/оружия вместо минимального конфига)

Новый `README-TODO.md`/`game/lua_vm/wscript` в вашей загрузке описывали
всё это как «честный скелет», хотя по факту это откат уже рабочего и
проверенного кода. Я это не принял на веру — сверил построчно с реальным
diff'ом, восстановил рабочую версию.

Хорошее из вашей правки я сохранил: `targetSdkVersion` 24→34 в обоих
манифестах — правильная мысль (см. ниже, почему).

## Что в этом патче

**Восстановлено** (см. патч #1 за подробным описанием):
- `source-engine/game/lua_vm/lua_vm.cpp` / `.h`
- `source-engine/game/server/gameinterface.cpp`
- `source-engine/game/server/server_garrysmod.vpc` (минимальный)
- `source-engine/game/{server,client}/wscript` (`garrysmod` в обоих)
- `source-engine/game/client/client_garrysmod.vpc`
- `srceng-android/src/org/libsdl/app/{SDLActivity.java,MenuBridge.java}`
- `srceng-android/build.gradle`, `settings.gradle`
- `.github/workflows/build.yml`

**Новое в этом патче** — баг, возникший из-за вашего же изменения
`targetSdkVersion` 24→34:
- Android игнорирует `requestLegacyExternalStorage` начиная с
  targetSdkVersion 30+. С таргетом 34 приложение потеряло бы доступ к
  файлам GMod на SD-карте — то есть к тому, ради чего весь проект
  затевался.
- Фикс: `MANAGE_EXTERNAL_STORAGE` в обоих `AndroidManifest.xml`
  (permission не запрашивается через обычный диалог — только через
  экран настроек), плюс `applyManageStoragePermission()` в
  `LauncherActivity.java`, который открывает
  `Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION`.
- Старые `WRITE_EXTERNAL_STORAGE`/`READ_EXTERNAL_STORAGE` оставлены с
  `maxSdkVersion="29"` — для устройств ниже Android 11 работает как
  раньше.

## Известное, не до конца решённое

- `build.gradle` держит AGP на ветке 7.4.x ради `minSdkVersion=17`, но
  `compileSdkVersion 34` формально на цикл новее, чем то, под что писалась
  AGP 7.4 (обычно это просто warning, не hard-fail — но если версия AGP
  когда-нибудь начнёт падать с ошибкой на этом, решение — снижать
  `minSdkVersion` (переходить на AGP 8+), а не поднимать версию AGP не
  подумав про это.
# Green-Engine-v2
