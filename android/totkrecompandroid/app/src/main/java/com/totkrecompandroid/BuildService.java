package com.totkrecompandroid;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

// While the game code builds (runtime/gamebuild.c, from the start screen's setup) or game packages are imported (Import
// from NSP/XCI: package_import.cpp): a foreground service with the progress in a notification, and a partial wake lock. The build runs in the app's own process; without this Android
// stopped it when the screen went off or another app came to the front (a Galaxy S21 on Reddit: started over, "do it in
// the background"). It only keeps the process in the foreground and the CPU awake - the build itself is unchanged.
public class BuildService extends Service {
    private static final String TAG = "TOTKRecompAndroid";
    private static final String CHANNEL = "build";
    private static final int ID = 1;
    private static PowerManager.WakeLock wake;
    private static int shown = -1;
    // what it's on for: the import's start and stop leave a build that began after it alone (the import's success starts
    // the game code build from the native side, while the import thread is still finishing)
    private static boolean importing;

    static synchronized void startImport(Context c) {
        importing = true;
        shown = -1;
        launch(c);
    }

    static synchronized void stopImport(Context c) {
        if (!importing) return;
        importing = false;
        stop(c);
    }

    static synchronized void start(Context c) {
        importing = false;
        shown = -1;
        launch(c);
    }

    private static void launch(Context c) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (nm != null) nm.notify(ID, notification(c, shown));  // (the text of this phase, if it's already on)
        try {
            c.startForegroundService(new Intent(c, BuildService.class));
        } catch (Exception e) {  // (from the background Android may refuse: the build goes on without it)
            Log.w(TAG, "build service: " + e);
        }
    }

    static synchronized void stop(Context c) {
        shown = -1;
        c.stopService(new Intent(c, BuildService.class));
    }

    static void progress(Context c, int percent) {
        if (percent == shown) return;
        shown = percent;
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        if (nm != null) nm.notify(ID, notification(c, percent));
    }

    private static Notification notification(Context c, int percent) {
        Intent open = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
        PendingIntent pi = open == null ? null : PendingIntent.getActivity(c, 0, open, PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(c, CHANNEL)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle(c.getString(importing ? R.string.import_title : R.string.build_title))
                .setContentText(c.getString(importing ? R.string.import_text : R.string.build_text))
                .setProgress(100, Math.max(percent, 0), percent < 0)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(pi)
                .build();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null)
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, getString(R.string.build_channel),
                    NotificationManager.IMPORTANCE_LOW));
        Notification n = notification(this, shown);
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else startForeground(ID, n);
        } catch (Exception e) {
            Log.w(TAG, "build service: not in the foreground: " + e);
        }
        if (wake == null) {
            PowerManager pm = getSystemService(PowerManager.class);
            if (pm != null) wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "totkrecomp:build");
        }
        if (wake != null && !wake.isHeld()) wake.acquire(4 * 60 * 60 * 1000L);  // (a stuck build lets go after 4 h)
        Log.i(TAG, "build service: on (the build goes on with the screen off)");
        return START_NOT_STICKY;
    }

    // Android 15+: a dataSync service has 6 hours a day in the foreground (targetSdk 35+). An import is stopped cleanly
    // (its staging folder goes, the game files set up before stay); a build goes on, out of the foreground.
    @Override
    public void onTimeout(int startId, int fgsType) {
        Log.w(TAG, "build service: Android's time limit for it reached" + (importing ? ": the import stops" : ""));
        if (importing) TOTKRecompAndroidActivity.nativePackageCancel();
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override
    public void onDestroy() {
        if (wake != null && wake.isHeld()) wake.release();
        Log.i(TAG, "build service: off");
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
