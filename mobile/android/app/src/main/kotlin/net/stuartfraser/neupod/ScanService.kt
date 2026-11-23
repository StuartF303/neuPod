// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.IBinder

/**
 * The foreground service (type dataSync) that keeps the app's process up while the library scan
 * runs, so the scan survives the app being swiped away (decision 0009). It does no work itself:
 * the scan runs in Dart, in the app's Flutter engine, which MainActivity keeps when it goes away
 * mid-scan. The ongoing notification shows the scan's progress in the firmware's words and a
 * Cancel action.
 */
class ScanService : Service() {
    private var foreground = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        ScanRuntime.service = this
        val manager = getSystemService(NotificationManager::class.java)
        if (manager.getNotificationChannel(CHANNEL_ID) == null) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Library scan", NotificationManager.IMPORTANCE_LOW).apply {
                    description = "Shows while the library is being built"
                    setShowBadge(false)
                },
            )
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                // Called for every startForegroundService: Android requires startForeground even
                // when the scan has already ended (a scan of unchanged files takes milliseconds).
                try {
                    startForeground(NOTIFICATION_ID, build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
                } catch (e: RuntimeException) {
                    // The app went to the background before the service came up, or the dataSync
                    // time is used up: the scan goes on in the app without the service.
                    android.util.Log.w(ScanRuntime.TAG, "could not start the scan service in front: $e")
                    ScanRuntime.stop() // also releases an engine kept meanwhile
                    stopSelf()
                    return START_NOT_STICKY
                }
                foreground = true
                ScanRuntime.session.posted(android.os.SystemClock.elapsedRealtime())
                if (!ScanRuntime.session.active) finish()
            }
            ACTION_CANCEL -> {
                ScanRuntime.requestCancel()
                if (!foreground && !ScanRuntime.session.active) stopSelf()
            }
            else -> if (!foreground) stopSelf()
        }
        // Not restarted after the process is killed: the next launch scans again.
        return START_NOT_STICKY
    }

    /** Android 15+: the dataSync time ran out; the service must stop within seconds. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        ScanRuntime.timedOut()
        finish()
    }

    override fun onDestroy() {
        if (ScanRuntime.service === this) ScanRuntime.service = null
        super.onDestroy()
    }

    /** Posts the notification with the session's current text. */
    internal fun post() {
        if (!foreground) return
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, build())
    }

    /**
     * Stops the service and removes its notification. Before the service is in the foreground it
     * does nothing: a pending ACTION_START must still call startForeground (Android crashes an app
     * whose service, started with startForegroundService, stops without it), and that branch
     * finishes itself when the scan has already ended.
     */
    internal fun finish() {
        if (!foreground) return
        stopForeground(STOP_FOREGROUND_REMOVE)
        foreground = false
        stopSelf()
    }

    private fun build(): Notification {
        val session = ScanRuntime.session
        val cancel = PendingIntent.getService(
            this,
            0,
            Intent(this, ScanService::class.java).setAction(ACTION_CANCEL),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val builder = Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle(session.title)
            .setContentText(session.text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setCategory(Notification.CATEGORY_PROGRESS)
            // The total is unknown until the walk ends, as on the firmware's pane.
            .setProgress(0, 0, true)
            .addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel),
                    session.cancelLabel,
                    cancel,
                ).build(),
            )
        packageManager.getLaunchIntentForPackage(packageName)?.let { launch ->
            launch.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            builder.setContentIntent(
                PendingIntent.getActivity(
                    this,
                    0,
                    launch,
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
                ),
            )
        }
        return builder.build()
    }

    companion object {
        const val ACTION_START = "net.stuartfraser.neupod.scan.START"
        const val ACTION_CANCEL = "net.stuartfraser.neupod.scan.CANCEL"
        const val CHANNEL_ID = "library_scan"
        // The media notification (Media3's DefaultMediaNotificationProvider) uses 1001.
        const val NOTIFICATION_ID = 2001
    }
}
