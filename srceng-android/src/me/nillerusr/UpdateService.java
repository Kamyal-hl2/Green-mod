package me.nillerusr;

/**
 * Файл: UpdateService.java
 * Назначение: Фоновый Android-сервис для показа уведомления о доступном обновлении.
 *
 * Этот сервис:
 *   1. Принимает Intent с URL-ом для обновления (update_url)
 *   2. Создаёт уведомление (notification) с ссылкой на скачивание
 *   3. При нажатии на уведомление открывает браузер с URL обновления
 *
 * Совместимость:
 *   - API 26+ (Android O): создаёт NotificationChannel (обязательно для API 26+)
 *   - API 23+ (Android M): использует FLAG_IMMUTABLE для PendingIntent
 *     (обязательно с Android 12+, иначе краш)
 *
 * Потокобезопасность:
 *   - service_work — volatile, защищает от гонок при параллельных onStartCommand
 *   - При параллельных вызовах service_work предотвращает дублирование уведомлений
 *
 * Использование:
 *   UpdateSystem.java вызывает startService() с update_url в extras.
 *   Сервис показывает уведомление и умирает (START_NOT_STICKY).
 */

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;

import com.valvesoftware.source.R;

public class UpdateService extends Service {
	// ID канала уведомлений (обязателен для Android 8.0+ / API 26+)
	private static final String CHANNEL_ID = "update_channel";
	NotificationManager nm; // менеджер уведомлений для показа нотификаций

	// volatile — виден всем потокам сразу; предотвращает race condition
	// при параллельных вызовах onStartCommand из разных потоков
	static volatile boolean service_work = false;

	@Override
	public void onCreate() {
		super.onCreate();
		// Получаем менеджер уведомлений из системы
		nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
		// Создаём канал уведомлений (обязательно для Android 8.0+)
		createNotificationChannel();
	}

	/**
	 * Создаёт канал уведомлений для Android 8.0+ (API 26+).
	 * Без этого уведомления НЕ будут показываться на Android 8.0+.
	 * Канал создаётся один раз при первом запуске сервиса.
	 */
	private void createNotificationChannel() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			NotificationChannel channel = new NotificationChannel(
				CHANNEL_ID,
				"Update Notifications", // имя канала (видно в настройках)
				NotificationManager.IMPORTANCE_HIGH // высокий приоритет
			);
			channel.setDescription("Shows when a new update is available");
			nm.createNotificationChannel(channel);
		}
	}

	/**
	 * Вызывается системой при запуске сервиса через startService().
	 * Извлекает update_url из extras Intent и показывает уведомление.
	 *
	 * @return START_NOT_STICKY — сервис НЕ перезапускается после убийства системой
	 */
	public int onStartCommand(Intent intent, int flags, int startId) {
		if (!service_work) {
			service_work = true;
			try {
				// Безопасно извлекаем update_url из extras
				String updateUrl = null;
				if (intent != null && intent.getExtras() != null) {
					updateUrl = intent.getExtras().getString("update_url");
				}
				if (updateUrl != null) {
					sendNotif(updateUrl);
				}
			} catch (Exception e) {
				// Игнорируем ошибки — сервис не должен крашить приложение
			}
		}
		return START_NOT_STICKY;
	}

	/**
	 * Создаёт и показывает уведомление с ссылкой на обновление.
	 * При нажатии открывает браузер с URL для скачивания.
	 *
	 * @param updateUrl URL-страницы обновления (напр. GitHub releases)
	 */
	private void sendNotif(String updateUrl) {
		// Открываем браузер при нажатии на уведомление
		Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(updateUrl));

		// FLAG_IMMUTABLE — обязателен с Android 12+ (API 31+).
		// Без него PendingIntent крашится с "missing FLAG_IMMUTABLE".
		int flags = PendingIntent.FLAG_UPDATE_CURRENT;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			flags |= PendingIntent.FLAG_IMMUTABLE;
		}
		PendingIntent pIntent = PendingIntent.getActivity(this, 0, browserIntent, flags);

		// Notification.Builder: разные конструкторы для разных API-уровней.
		// На API 26+ нужен channel_id, иначе уведомление не показывается.
		Notification.Builder builder;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			builder = new Notification.Builder(this, CHANNEL_ID);
		} else {
			builder = new Notification.Builder(this);
		}

		Notification notif = builder
			.setSmallIcon(R.drawable.ic_launcher) // иконка в шторке
			.setContentTitle("Update available") // заголовок уведомления
			.setContentText("Tap to download the latest version") // текст
			.setContentIntent(pIntent) // действие при нажатии
			.setAutoCancel(true) // уведомление исчезает после нажатия
			.setDefaults(Notification.DEFAULT_ALL) // звук + вибрация
			.setPriority(Notification.PRIORITY_HIGH) // всплывающее уведомление
			.build();

		nm.notify(1, notif); // показываем уведомление с ID=1
	}

	public IBinder onBind(Intent arg0) {
		return null; // bound service не используется
	}
}
