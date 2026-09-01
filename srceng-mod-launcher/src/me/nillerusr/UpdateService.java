package me.nillerusr;

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

public class UpdateService extends Service {
    private static final String CHANNEL_ID = "update_channel";
    NotificationManager nm;
	static volatile boolean service_work = false;

    @Override
    public void onCreate() {
        super.onCreate();
        nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Update Notifications",
                NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("Shows when a new update is available");
            nm.createNotificationChannel(channel);
        }
    }

    public int onStartCommand(Intent intent, int flags, int startId) {
        if (!service_work) {
            service_work = true;
            try {
                SharedPreferences prefs = getSharedPreferences("mod", 0);
                String updateUrl = prefs.getString("update_url", null);
                if (updateUrl == null && intent != null && intent.getExtras() != null) {
                    updateUrl = intent.getExtras().getString("update_url");
                }
                if (updateUrl != null) {
                    sendNotif(updateUrl);
                }
            } catch (Exception e) {
                // ignore
            }
        }
        return START_NOT_STICKY;
    }

    private void sendNotif(String updateUrl) {
        Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(updateUrl));
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pIntent = PendingIntent.getActivity(this, 0, browserIntent, flags);

        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }

        Notification notif = builder
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle("Update available")
            .setContentText("Tap to download the latest version")
            .setContentIntent(pIntent)
            .setAutoCancel(true)
            .setDefaults(Notification.DEFAULT_ALL)
            .setPriority(Notification.PRIORITY_HIGH)
            .build();

        nm.notify(1, notif);
    }

    public IBinder onBind(Intent arg0) {
        return null;
    }
}
