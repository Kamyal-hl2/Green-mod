package me.nillerusr;
/**
 * Файл: ExtractAssets.java
 * Назначение: Извлечение VPK-архива (garrysmod content) из assets приложения
 *             в файловую систему устройства при первом запуске.
 *
 * Этот класс:
 *   1. Проверяет версию извлечённого контента (PAK_VERSION)
 *   2. Если версия устарела или контента нет — извлекает extras_dir.vpk
 *   3. Устанавливает правильные права доступа (chmod 0755/0644)
 *
 * VPK (Valve Pak) — формат архивов Valve для хранения игровых данных.
 * Файл extras_dir.vpk содержит Lua-скрипты GMod (hook.lua, gamemode и т.д.)
 *
 * Безопасность:
 *   - chmod используется через String[] (без командного injection)
 *   - Все потоки закрываются в try-finally
 *   - appinf.dataDir проверяется на null для совместимости с API < 24
 *
 * Совместимость:
 *   - API 24+: context.getFilesDir() доступен напрямую
 *   - API < 24: используется appinf.dataDir (fallback)
 */
import android.content.SharedPreferences;
import java.io.FileOutputStream;
import java.io.File;
import java.io.InputStream;
import java.lang.reflect.Method;
import android.util.Log;
import android.content.Context;
import android.content.pm.ApplicationInfo;

public class ExtractAssets
{
	// Тег для логов (в logcat: "ExtractAssets: ...")
	public static String TAG = "ExtractAssets";
	// SharedPreferences для хранения версии извлечённого контента
	static SharedPreferences mPref;

	// Имя VPK-файла в assets приложения
	public static final String VPK_NAME = "extras_dir.vpk";
	// Версия формата контента. При изменении форсирует переизвлечение.
	public static int PAK_VERSION = 9;

	/**
	 * Устанавливает права доступа на файл/папку через chmod.
	 *
	 * Пробует два способа:
	 *   1. Runtime.exec("chmod 0755 /path") — стандартный Unix-способ
	 *   2. android.os.FileUtils.setPermissions() — Android API (скрытый)
	 *
	 * @param path путь к файлу/папке
	 * @param mode права доступа в octal (0755 = rwxr-xr-x, 0644 = rw-r--r--)
	 * @return 0 при успехе, -1 при ошибке
	 */
    private static int chmod(String path, int mode)
    {
		int ret = -1;

		try
		{
			// Используем String[] вместо String для предотвращения command injection.
			String[] cmd = { "chmod", Integer.toOctalString(mode), path };
			Process proc = Runtime.getRuntime().exec(cmd);
			proc.waitFor();
			// Закрываем все потоки процесса чтобы не было утечки
			try { proc.getInputStream().close(); } catch( Exception e ) { }
			try { proc.getOutputStream().close(); } catch( Exception e ) { }
			try { proc.getErrorStream().close(); } catch( Exception e ) { }
			ret = 0;
			Log.d(TAG, "chmod " + Integer.toOctalString(mode) + " " + path + ": ok" );
			return ret; // ОПТИМИЗАЦИЯ: ранний выход еслиchmod сработал
		}
		catch(Exception e)
		{
			ret = -1;
			Log.d(TAG, "chmod: Runtime not worked: " + e.toString() );
		}

		// Запасной способ: android.os.FileUtils.setPermissions()
		// Это скрытый API, доступен не на всех прошивках (may not exist)
		try
		{
			Class fileUtils = Class.forName("android.os.FileUtils");
			Method setPermissions = fileUtils.getMethod("setPermissions", String.class, int.class, int.class, int.class);
			ret = (Integer) setPermissions.invoke(null, path, mode, -1, -1);
		}
		catch(Exception e)
		{
			ret = -1;
			Log.d(TAG, "chmod: FileUtils not worked: " + e.toString() );
		}

		return ret;
	}

	/**
	 * Извлекает VPK-файл из assets в файловую систему устройства.
	 *
	 * Поток выполнения:
	 *   1. Проверяет, извлечён ли уже VPK с текущей версией
	 *   2. Если нет — открывает assets/extras_dir.vpk
	 *   3. Копирует в context.getFilesDir()/extras_dir.vpk
	 *   4. Устанавливает права доступа (0755 для папок, 0644 для файлов)
	 *   5. Сохраняет версию в SharedPreferences
	 *
	 * @param context Android Context (обычно Activity или Application)
	 * @param force принудительное переизвлечение (игнорировать версию)
	 */
	public static void extractVPK(Context context, Boolean force) 
	{
		// Получаем информацию о приложении (нужна для dataDir)
		ApplicationInfo appinf = context.getApplicationInfo();

		try {
			// Инициализируем SharedPreferences (лениво)
			if( mPref == null )
				mPref = context.getSharedPreferences("mod", 0);

			// Проверяем, существует ли VPK-файл
			File file = new File( context.getFilesDir().getPath() +"/"+ VPK_NAME );
			if( !file.exists() )
				force = true; // форсируем извлечение если файла нет

			// Проверяем версию: если совпадает с текущей — пропускаем
			if( mPref.getInt( "pakversion", 0 ) == PAK_VERSION && !force )
				return;

			// Копируем VPK из assets в files dir
			InputStream is = null;
			FileOutputStream os = null;
			try {
				is = context.getAssets().open(VPK_NAME);
				os = new FileOutputStream( context.getFilesDir().getPath() +"/"+ VPK_NAME);
				byte[] buffer = new byte[8192]; // 8KB буфер для эффективного копирования
				while (true) {
					int length = is.read(buffer);
					if (length <= 0)
						break; // конец файла

					os.write(buffer, 0, length);
				}
			} finally {
				// Гарантированно закрываем потоки (даже при исключении)
				if ( is != null ) try { is.close(); } catch( Exception e ) { }
				if ( os != null ) try { os.close(); } catch( Exception e ) { }
			}

			// Сохраняем версию в SharedPreferences чтобы не извлекать повторно
			SharedPreferences.Editor editor = mPref.edit();
			editor.putInt( "pakversion", PAK_VERSION );
			editor.apply(); // apply() — асинхронная запись (достаточно для same-process чтения)

			// Устанавливаем права доступа:
			// 0755 (rwxr-xr-x) для директорий — чтобы движок мог читать/искать
			// 0644 (rw-r--r--) для VPK-файла — чтобы движок мог читать
			// appinf.dataDir: на API < 24 может быть null, поэтому проверяем
			chmod(appinf.dataDir != null ? appinf.dataDir : context.getFilesDir().getPath(), 0755);
			chmod(context.getFilesDir().getPath(), 0755);
			chmod(context.getFilesDir().getPath() +"/"+ VPK_NAME, 0644);
		}
		catch (Exception e) {
			// Логируем ошибку но не крашим — игра запустится без контента
			Log.e(TAG, "Failed to extract vpk:" + e.toString());
		}
	}
}
