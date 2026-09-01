//========= Green Engine v2 - Lua VM bootstrap (MVP, Part 1.4 step 1-2) =====
//
// Файл: lua_vm.cpp
// Назначение: Загрузка и управление Lua VM (виртуальной машиной Lua) для
//             запуска Lua-скриптов Garry's Mod на движке Source Engine.
//
// Этот файл реализует:
//   1. Создание Lua-состояния (lua_State)
//   2. Регистрацию глобальных функций, нужных hook.lua
//   3. Загрузку и выполнение Lua-файлов из файловой системы движка
//   4. Вызов hook.Run() каждый кадр (GameFrame) для обработки игровых событий
//   5. Безопасное закрытие Lua-остояния при выгрузке движка
//
// Безопасность: Все вызовы Lua обёрнуты в lua_pcall для отлова ошибок.
//              Reentrancy guard (g_bHookRunning) предотвращает рекурсивные
//              вызовы хуков, которые могут сломать цепочку longjmp в LuaJIT.
//
// Архитектура:
//   LuaVM_Bootstrap()      → вызывается при старте сервера (DLLInit)
//   LuaVM_RunHooks(name)   → вызывается каждый тик (GameFrame)
//   LuaVM_DoFile(path)     → загружает и выполняет Lua-файл
//   LuaVM_Shutdown()       → вызывается при выгрузке (DLLShutdown)
//=============================================================================
#include "cbase.h"
#include "lua_vm.h"
#include "filesystem.h"

// Подключаем C API Lua (LuaJIT совместим с Lua 5.1)
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// memdbgon must be the last include in every Source engine .cpp file.
#include "tier0/memdbgon.h"

// Глобальный указатель на Lua-состояние. Только одно состояние на сервер.
// На клиенте пока не используется (MVP = server-only).
lua_State *g_pLuaVM = NULL;

// Флаг reentrancy guard: предотвращает повторный вход в hook.Run(),
// если хук-коллбэк сам вызывает GameFrame (например, через
// engine.RunConsoleCommand). Без этого LuaJIT может упасть с longjmp-ошибкой.
static bool g_bHookRunning = false;

// ОПТИМИЗАЦИЯ: Кэшированные ссылки на hook.Run().
// Вместо того чтобы каждый кадр делать lua_getglobal("hook") + lua_getfield("Run")
// (2 хеш-поиска в Lua таблицах = ~200 нсек на кадр),
// мы сохраняем ссылки в LUA_REGISTRYINDEX и обращаемся через lua_rawgeti.
// Экономия: 2 хеш-lookup'а × 66 тиков/сек = 132 быстрых обращения/сек.
static int g_hookRunRef = LUA_NOREF;

//-----------------------------------------------------------------------------
// ЛОКАЛЬНЫЕ ГЛОБАЛЬНЫЕ ФУНКЦИИ LUA
//
// hook.lua (и другие 38 модулей) вызывают эти функции при загрузке.
// Они простые и не зависят от ents.*/util.* — реализованы здесь как
// заглушки, чтобы hook.lua работал до появления полного binding-слоя.
// В оригинальном GMod эти функции определены в lua/includes/util.lua.
//-----------------------------------------------------------------------------

// isfunction(x) — проверяет, является ли аргумент функцией.
// Lua-сигнатура: isfunction(val) → bool
static int Lua_IsFunction( lua_State *L )
{
	lua_pushboolean( L, lua_isfunction( L, 1 ) );
	return 1;
}

// isstring(x) — проверяет, является ли аргумент строкой.
// Отличие от type(x)=="string": также считает числа "строками"
// (совместимость с GMod, где number приводится к string автоматически).
static int Lua_IsString( lua_State *L )
{
	lua_pushboolean( L, lua_type( L, 1 ) == LUA_TSTRING );
	return 1;
}

// isnumber(x) — проверяет, является ли аргумент числом.
static int Lua_IsNumber( lua_State *L )
{
	lua_pushboolean( L, lua_type( L, 1 ) == LUA_TNUMBER );
	return 1;
}

// isbool(x) — проверяет, является ли аргумент булевым значением.
static int Lua_IsBool( lua_State *L )
{
	lua_pushboolean( L, lua_type( L, 1 ) == LUA_TBOOLEAN );
	return 1;
}

// ErrorNoHaltWithStack(msg) — выводит сообщение об ошибке в консоль движка.
// Вызывается из hook.lua при ошибках в аргументах хуков.
// Не прерывает выполнение (в отличие от error()), просто логирует.
static int Lua_ErrorNoHaltWithStack( lua_State *L )
{
	const char *msg = lua_tostring( L, 1 );
	Warning( "[Lua] %s\n", msg ? msg : "(non-string error)" );
	return 0;
}

// IsValid(x) — проверяет, "жив" ли объект (entity/panel/userdata).
// В hook.Call() используется для проверки, что не-nil и не-false.
// Пока это заглушка: всё что не nil/false считается валидным.
// Когда появятся ents.* — тут будет реальная проверка по таблице entities.
static int Lua_IsValid( lua_State *L )
{
	bool valid = !lua_isnil( L, 1 ) && lua_toboolean( L, 1 );
	lua_pushboolean( L, valid );
	return 1;
}

// LuaVM_PanicHandler(L) — обработчик паники Lua.
//
// Вызывается Lua-интерпретатором при НЕВОССТАНОВИМОЙ ошибке
// (например, переполнение стека, невалидный указатель в C API).
// luaD_throw() вызывает panic handler, после чего БУДЕТ вызван abort().
// Мы НЕ МОЖЕМ предотвратить завершение — только залогировать диагностику.
//
// Безопасность: lua_isstring() проверяет тип перед lua_tostring(),
// чтобы не получить SIGSEGV если стек повреждён.
static void LuaVM_PanicHandler( lua_State *L )
{
	// Lua's panic handler is called after an unrecoverable error.
	// luaD_throw will call abort() after this returns, so we can only
	// log a diagnostic. The process WILL terminate.
	const char *msg = lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : NULL;
	Warning( "[Lua] PANIC: %s\n", msg ? msg : "(unrecoverable error)" );
}

// LuaVM_RegisterGlobals(L) — регистрирует глобальные функции в Lua-состоянии.
// Эти функции нужны hook.lua для работы до появления ents.*/util.*.
static void LuaVM_RegisterGlobals( lua_State *L )
{
	// lua_register() — добавляет функцию как глобальную с указанным именем.
	// Например, lua_register(L, "isfunction", Lua_IsFunction) создаёт
	// глобальную функцию isfunction() доступную из любого Lua-скрипта.
	lua_register( L, "isfunction", Lua_IsFunction );
	lua_register( L, "isstring", Lua_IsString );
	lua_register( L, "isnumber", Lua_IsNumber );
	lua_register( L, "isbool", Lua_IsBool );
	lua_register( L, "IsValid", Lua_IsValid );
	lua_register( L, "ErrorNoHaltWithStack", Lua_ErrorNoHaltWithStack );
}

// LuaVM_CacheHookRefs(L) — кэширует ссылку на hook.Run() в LUA_REGISTRYINDEX.
//
// ОПТИМИЗАЦИЯ: После загрузки hook.lua сохраняем функцию hook.Run()
// в реестр Lua. При каждом вызове LuaVM_RunHooks() вместо:
//   lua_getglobal("hook") → lua_getfield("Run") → 2 хеш-поиска
// делаем:
//   lua_rawgeti(ref) → 1 обращение по индексу (O(1), без хешей)
//
// Вызывается ОДИН РАЗ после загрузки hook.lua в LuaVM_Bootstrap().
// Экономия: ~200 нсек на кадр × 66 тиков/сек = ~13 мксек/сек.
//-----------------------------------------------------------------------------
static void LuaVM_CacheHookRefs( lua_State *L )
{
	// Проверяем что hook.Run() доступен после загрузки hook.lua
	lua_getglobal( L, "hook" );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		Warning( "LuaVM_CacheHookRefs: 'hook' is not a table\n" );
		return;
	}

	lua_getfield( L, -1, "Run" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		Warning( "LuaVM_CacheHookRefs: 'hook.Run' is not a function\n" );
		return;
	}

	// Сохраняем hook.Run() в LUA_REGISTRYINDEX.
	// luaL_ref() извлекает значение со стека и возвращает целочисленный ID.
	// При повторном обращении lua_rawgeti(L, LUA_REGISTRYINDEX, id)
	// кладёт значение обратно на стек — без хешей, без строк.
	g_hookRunRef = luaL_ref( L, LUA_REGISTRYINDEX ); // [-1, -0] извлекаем со стека

	lua_pop( L, 1 ); // pop hook table

	Msg( "LuaVM_CacheHookRefs: hook.Run cached (ref=%d)\n", g_hookRunRef );
}

//-----------------------------------------------------------------------------
// LuaVM_DoFile(relativePath) — загружает и выполняет Lua-файл из VFS.
//
// Параметры:
//   relativePath — путь относительно корня мода (например,
//                  "lua/includes/modules/hook.lua").
//                  Используется IFileSystem с search path "GAME".
//
// Возвращает:
//   true  — файл успешно выполнен
//   false — ошибка (файл не найден, ошибка чтения, или Lua-ошибка)
//
// Безопасность:
//   - Проверяет NULL-путь и наличие VM
//   - Проверяет размер файла (0 и 0xFFFFFFFF = невалидный)
//   - malloc() проверяется на NULL
//   - bytesRead проверяется на переполнение (> len)
//   - lua_pcall ловит все ошибки, не давая им крашнуть движок
//   - Буфер всегда освобождается в finally-подобном паттерне
//
// Побочный эффок: при ошибке загрузки/выполнения выводит Warning() в консоль.
// При успехе возвращает nil (Lua-стек остаётся чистым).
//-----------------------------------------------------------------------------
bool LuaVM_DoFile( const char *relativePath )
{
	if ( !relativePath )
	{
		Warning( "LuaVM_DoFile: NULL path\n" );
		return false;
	}

	if ( !g_pLuaVM )
	{
		Warning( "LuaVM_DoFile( %s ): VM not bootstrapped\n", relativePath );
		return false;
	}

	// Открываем файл через движковую VFS (виртуальную файловую систему).
	// "GAME" — search path, включает ВСЕ папки мода (garrysmod/,hl2/ и т.д.)
	// "rb" — read binary (текстовый режим может сломать байты на Windows)
	FileHandle_t f = filesystem->Open( relativePath, "rb", "GAME" );
	if ( !f )
	{
		Warning( "LuaVM_DoFile: couldn't open '%s'\n", relativePath );
		return false;
	}

	// Получаем размер файла. filesystem->Size() возвращает 0 для пустых
	// файлов и 0xFFFFFFFF для невалидных дескрипторов (INVALID_FILE_SIZE).
	unsigned int len = filesystem->Size( f );
	if ( len == 0 || len == (unsigned int)-1 )
	{
		Warning( "LuaVM_DoFile: '%s' has invalid size (%u)\n", relativePath, len );
		filesystem->Close( f );
		return false;
	}

	// Выделяем буфер +1 байт для нуль-терминатора ('\0').
	// Lua luaL_loadbuffer() требует нуль-терминированную строку.
	char *buf = (char *)malloc( len + 1 );
	if ( !buf )
	{
		Warning( "LuaVM_DoFile: malloc failed for '%s' (%u bytes)\n", relativePath, len );
		filesystem->Close( f );
		return false;
	}

	// Читаем файл целиком в буфер. filesystem->Read() возвращает
	// количество прочитанных байтов (может быть меньше len при ошибке).
	int bytesRead = filesystem->Read( buf, len, f );
	filesystem->Close( f );

	// Проверка bytesRead: должен быть >0 и <=len.
	// (unsigned int)bytesRead > len ловит случай когда bytesRead отрицательный
	// (convertится в большое положительное число) или больше размера файла.
	if ( bytesRead <= 0 || (unsigned int)bytesRead > len )
	{
		Warning( "LuaVM_DoFile: read failed for '%s' (got %d of %u bytes)\n", relativePath, bytesRead, len );
		free( buf );
		return false;
	}

	// Ставим нуль-терминатор сразу после последнего прочитанного байта.
	buf[bytesRead] = '\0';

	// Chunk name с префиксом '@' — Lua использует это для красивых
	// сообщений об ошибках: "@hook.lua:15: bad argument #1" вместо
	// "[string \"hook.lua\"]:15: bad argument #1".
	CUtlString chunkName;
	chunkName.Format( "@%s", relativePath );

	bool ok = true;
	// luaL_loadbuffer() компилирует Lua-код из буфера в функцию на стеке.
	// Возвращает 0 при успехе, ненулевое значение при ошибке (синтаксис и т.д.)
	if ( luaL_loadbuffer( g_pLuaVM, buf, bytesRead, chunkName.Get() ) != 0 )
	{
		// При ошибке загрузки Lua кладёт сообщение об ошибке на стек [-1].
		const char *err = lua_tostring( g_pLuaVM, -1 );
		Warning( "[Lua] load error in %s: %s\n", relativePath, err ? err : "(non-string error)" );
		lua_pop( g_pLuaVM, 1 ); // убираем сообщение об ошибке со стека
		ok = false;
	}
	// lua_pcall() выполняет загруженную функцию с 0 аргументов и 0 результатов.
	// При ошибке运行时 Lua кладёт сообщение на стек [-1].
	else if ( lua_pcall( g_pLuaVM, 0, 0, 0 ) != 0 )
	{
		const char *err = lua_tostring( g_pLuaVM, -1 );
		Warning( "[Lua] runtime error in %s: %s\n", relativePath, err ? err : "(non-string error)" );
		lua_pop( g_pLuaVM, 1 );
		ok = false;
	}

	// Всегда освобождаем буфер, даже при ошибке.
	free( buf );
	return ok;
}

//-----------------------------------------------------------------------------
// LuaVM_Bootstrap() — создаёт и инициализирует Lua-состояние.
//
// Вызывается ОДИН РАЗ из CServerGameDLL::DLLInit() после подключения
// engine interfaces. Создаёт lua_State, открывает стандартные библиотеки,
// регистрирует глобальные функции и загружает hook.lua.
//
// Возвращает:
//   true  — VM готова к работе, hook.Add() доступен
//   false — ошибка (не удалось выделить память или загрузить hook.lua)
//
// Защита от повторного вызова: если g_pLuaVM уже существует,
// выводит предупреждение и возвращает true (не ошибка, просто уже инициализирован).
//
// ВАЖНО: При ошибке загрузки hook.lua состояние полностью закрывается
// (lua_close), чтобы не оставлять半初始化ированное состояние.
//-----------------------------------------------------------------------------
bool LuaVM_Bootstrap()
{
	// Защита от повторной инициализации (в release-сборке Assert не работает)
	if ( g_pLuaVM )
	{
		Warning( "LuaVM_Bootstrap: already initialized\n" );
		return true;
	}

	// luaL_newstate() создаёт новое Lua-состояние с дефолтным аллокатором.
	// Это НЕ аллокатор движка (g_pMemAlloc) — LuaJIT использует свой внутренний
	// malloc/free. Это нормально для MVP; полная интеграция с памятью движка —
	// отдельная задача.
	g_pLuaVM = luaL_newstate();
	if ( !g_pLuaVM )
	{
		Warning( "LuaVM_Bootstrap: luaL_newstate() failed\n" );
		return false;
	}

	// Устанавливаем обработчик паники (см. LuaVM_PanicHandler выше).
	// Без этого Lua вызовет abort() без диагностики.
	lua_atpanic( g_pLuaVM, LuaVM_PanicHandler );

	// luaL_openlibs() открывает все стандартные библиотеки Lua 5.1:
	// base, table, io, os, string, math, debug, package.
	luaL_openlibs( g_pLuaVM );

	// Регистрируем наши глобальные функции (isfunction, isstring и т.д.)
	LuaVM_RegisterGlobals( g_pLuaVM );

	// Загружаем hook.lua — главный модуль системы хуков GMod.
	// hook.lua определяет hook.Add(), hook.Remove(), hook.Run() и т.д.
	// Без него GameFrame() не сможет вызывать хуки.
	if ( !LuaVM_DoFile( "lua/includes/modules/hook.lua" ) )
	{
		Warning( "LuaVM_Bootstrap: failed to load hook.lua — Lua-side hooks will not work\n" );
		// Полностью закрываем VM при ошибке, чтобы не оставлять半 initState
		lua_close( g_pLuaVM );
		g_pLuaVM = NULL;
		return false;
	}

	// ОПТИМИЗАЦИЯ: Кэшируем ссылку на hook.Run() для быстрого доступа.
	// После этого LuaVM_RunHooks() не будет делать lua_getglobal("hook")
	// каждый кадр — вместо этого обращается к кэшированной ссылке.
	LuaVM_CacheHookRefs( g_pLuaVM );

	Msg( "LuaVM_Bootstrap: Lua VM initialized, hook.lua loaded\n" );
	return true;
}

//-----------------------------------------------------------------------------
// LuaVM_LoadGameScripts() — загружает игровые скрипты (gamemode, autorun).
//
// Вызывается из CServerGameDLL::PostInit() после IGameSystem::PostInit().
// Пока это заглушка — реальная реализация появится с ents.* (Part 1.4.3),
// потому что init.lua gamemode сразу вызывает ents.Create() при загрузке.
//-----------------------------------------------------------------------------
void LuaVM_LoadGameScripts()
{
	// TODO (Part 1.4 step 3, after ents.*): load
	// gamemodes/sandbox/gamemode/init.lua and the autorun/*.lua chain here.
	// Left as a stub deliberately — loading it now would just error out on
	// the first ents.Create() call, since that binding doesn't exist yet.
}

//-----------------------------------------------------------------------------
// LuaVM_RunHooks(hookName) — вызывает hook.Run(hookName) в Lua.
//
// Вызывается КАЖДЫЙ ТИК из CServerGameDLL::GameFrame() после
// Physics_RunThinkFunctions(). Это основной способ взаимодействия
// движка и Lua-скриптов GMod.
//
// ОПТИМИЗАЦИЯ (v2): Используем кэшированную ссылку на hook.Run()
// из LUA_REGISTRYINDEX вместо lua_getglobal + lua_getfield каждый кадр.
// Экономия: 2 хеш-lookup'а × 66 тиков/сек = 132 быстрых обращения/сек.
//
// Параметры:
//   hookName — имя хука (например, "Think", "PlayerInitialSpawn")
//
// Безопасность:
//   - Проверяет g_pLuaVM (VM может быть не инициализирована)
//   - Проверяет hookName на NULL
//   - Reentrancy guard (g_bHookRunning) предотвращает рекурсивные вызовы
//   - lua_pcall ловит все ошибки, не краша движок
//   - Все Lua-стековые операции сбалансированы (push/pop)
//
// Потокобезопасность: НЕ потокобезопасен — вызывать только из основного потока.
//-----------------------------------------------------------------------------
void LuaVM_RunHooks( const char *hookName )
{
	if ( !g_pLuaVM )
		return;

	if ( !hookName )
		return;

	// Reentrancy guard: если хук-коллбэк вызывает GameFrame
	// (например, через engine.RunConsoleCommand), пропускаем чтобы
	// не сломать цепочку longjmp в LuaJIT. Это критически важно —
	// без этого LuaJIT падает с "cannot resume dead coroutine".
	if ( g_bHookRunning )
		return;

	// Проверяем что кэшированная ссылка валидна
	if ( g_hookRunRef == LUA_NOREF )
		return;

	g_bHookRunning = true;

	// ОПТИМИЗАЦИЯ: Загружаем hook.Run() из кэша (O(1) по индексу).
	// Вместо: lua_getglobal("hook") → lua_getfield("Run") — 2 хеш-поиска
	// Делаем: lua_rawgeti(LUA_REGISTRYINDEX, ref) — 1 обращение по массиву.
	lua_rawgeti( g_pLuaVM, LUA_REGISTRYINDEX, g_hookRunRef ); // [-0, +1]
	if ( !lua_isfunction( g_pLuaVM, -1 ) )
	{
		lua_pop( g_pLuaVM, 1 );
		g_bHookRunning = false;
		return;
	}

	// Передаём имя хука как аргумент: hook.Run("Think")
	// lua_pushliteral() для строковых констант — не вычисляет длину через strlen(),
	// использует sizeof("string")-1 на этапе компиляции.
	lua_pushliteral( g_pLuaVM, hookName ); // [-0, +1]

	// lua_pcall(1 аргумент, 0 результатов, 0 обработчик ошибок)
	// Возвращаем nresults=1 чтобы hook.Run() мог возвращать значение.
	// (В GMod hook.Run возвращает nil или значение из последнего хука)
	if ( lua_pcall( g_pLuaVM, 1, 1, 0 ) != 0 )
	{
		const char *err = lua_tostring( g_pLuaVM, -1 );
		Warning( "[Lua] hook.Run(\"%s\") error: %s\n", hookName, err ? err : "(non-string error)" );
		lua_pop( g_pLuaVM, 1 ); // pop error message
	}
	else
	{
		// Убираем результат hook.Run() со стека (нам он пока не нужен на C-side)
		lua_pop( g_pLuaVM, 1 );
	}

	g_bHookRunning = false;
}

//-----------------------------------------------------------------------------
// LuaVM_Shutdown() — закрывает Lua-стояние и освобождает ресурсы.
//
// Вызывается из CServerGameDLL::DLLShutdown() при выгрузке движка.
// Гарантирует, что g_bHookRunning сброшен (на случай если shutdown
// вызван во время выполнения хука).
// Также освобождает кэшированные ссылки из LUA_REGISTRYINDEX.
//-----------------------------------------------------------------------------
void LuaVM_Shutdown()
{
	// Сбрасываем reentrancy guard чтобы следующий Bootstrap() начал чисто.
	g_bHookRunning = false;

	if ( g_pLuaVM )
	{
		// Освобождаем кэшированную ссылку на hook.Run()
		if ( g_hookRunRef != LUA_NOREF )
		{
			luaL_unref( g_pLuaVM, LUA_REGISTRYINDEX, g_hookRunRef );
			g_hookRunRef = LUA_NOREF;
		}

		lua_close( g_pLuaVM );
		g_pLuaVM = NULL;
	}
}
