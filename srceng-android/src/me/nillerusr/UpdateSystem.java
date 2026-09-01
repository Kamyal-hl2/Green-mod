package me.nillerusr;

/**
 * Файл: UpdateSystem.java
 * Назначение: Проверка обновлений приложения на GitHub (в фоновом потоке).
 *
 * AsyncTask: загружает файл "version" → сравнивает с текущей →
 *           если отличается → запускает UpdateService → уведомление.
 *
 * Безопасность: getApplicationContext() предотвращает утечку Activity,
 *              все InputStream закрываются в finally-блоках.
 */
import android.content.*;
import java.io.*;
import java.net.*;
import com.valvesoftware.source.R;
import android.os.AsyncTask;
import java.net.URL;
import java.net.URLConnection;
import java.io.InputStream;
import java.io.BufferedInputStream;
import java.io.BufferedReader;
import android.util.Log;
import me.nillerusr.UpdateService;

public class UpdateSystem extends AsyncTask<String, Integer, String> {
	private static final String git_url = "https://raw.githubusercontent.com/nillerusr/srceng-deploy";
	private static final String app = "srceng-debug.apk";

	String deploy_branch, last_commit;
	Context mContext;

	public UpdateSystem( Context context )
	{
		mContext = context.getApplicationContext();
		deploy_branch = context.getResources().getString(R.string.deploy_branch);
		last_commit = context.getResources().getString(R.string.last_commit);
	}

	private static String toString(InputStream inputStream)
	{
        BufferedReader bufferedReader = null;
        try {
			bufferedReader = new BufferedReader(new InputStreamReader(inputStream, "UTF-8"));
			String inputLine;
			StringBuilder stringBuilder = new StringBuilder();
			while ((inputLine = bufferedReader.readLine()) != null) {
				stringBuilder.append(inputLine);
			}
			return stringBuilder.toString();
        }
		catch(Exception e) {
			e.printStackTrace();
		}
		finally {
			try { inputStream.close(); } catch( Exception e ) { }
			if (bufferedReader != null) try { bufferedReader.close(); } catch( Exception e ) { }
		}

		return "";
	}

	@Override
	protected String doInBackground(String... params) {
		URL urlObject;
		URLConnection urlConnection;

		try {
			urlObject = new URL(git_url+"/"+deploy_branch+"/version");
			urlConnection = urlObject.openConnection();
			// ОПТИМИЗАЦИЯ: Устанавливаем таймауты чтобы не зависать при медленном интернете.
			// 5 сек на подключение, 5 сек на чтение — разумные лимиты для проверки версии.
			urlConnection.setConnectTimeout(5000);
			urlConnection.setReadTimeout(5000);
			InputStream is = urlConnection.getInputStream();
			try {
				return toString(is);
			} finally {
				try { is.close(); } catch( Exception e ) { }
			}
		} catch (IOException e) {
			e.printStackTrace();
		}

		return null;
	}

	protected void onPostExecute(String result) {
		if( result != null && !result.equals("") && !last_commit.equals(result) ) {
			Intent notif = new Intent(mContext, UpdateService.class);
			notif.putExtra("update_url", git_url+"/"+deploy_branch+"/"+app);
			mContext.startService(notif);
		}
	}
}
