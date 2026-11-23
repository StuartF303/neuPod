// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * Ties the scan's parts together (decision 0009): the session (what the scan says), the service
 * (the foreground service that keeps the process up), the channel to Dart (where the scan runs)
 * and the engine keeper (the engine of an activity that went away mid-scan). One per process;
 * every member is used on the main thread only.
 */
internal object ScanRuntime {
    const val TAG = "neupod_scan"

    val session = ScanSession()

    val keeper = EngineKeeper<FlutterEngine> { engine ->
        Log.i(TAG, "scan ended with no activity: destroying the kept engine")
        engine.destroy()
    }

    /** The running service, between its onCreate and onDestroy. */
    var service: ScanService? = null

    /** The activity the app's engine is attached to, if any. */
    var activity: Activity? = null

    /** The channel to the Dart side, while an engine with the plugin exists. */
    var channel: MethodChannel? = null

    /** The engine whose Dart side started the running scan. */
    var scanEngine: FlutterEngine? = null

    /**
     * The engine a new activity must use while a scan runs, so that no second engine (and no
     * second scan, a second writer on the database) is ever started: the kept one, or the one still
     * attached to an activity that is finishing but not yet destroyed (Back on Android 10 and 11
     * finishes the root activity, and its onDestroy can come after the next launch).
     */
    fun engineForNewActivity(): FlutterEngine? =
        keeper.take() ?: scanEngine?.takeIf { session.active }

    /** Whether [engine] runs the scan, so must outlive its activity. */
    fun runsScan(engine: FlutterEngine?): Boolean =
        engine != null && session.active && engine === scanEngine

    private val main = Handler(Looper.getMainLooper())
    private val postLater = Runnable { postNow() }

    /** A scan started in Dart: start the service, or update it when it already runs. */
    fun start(context: Context, title: String, text: String, cancelLabel: String) {
        if (!session.start(title, text, cancelLabel)) {
            update(title, text)
            return
        }
        try {
            context.startForegroundService(Intent(context, ScanService::class.java).setAction(ScanService.ACTION_START))
        } catch (e: RuntimeException) {
            // ForegroundServiceStartNotAllowedException (the app was not visible, or Android 15's
            // dataSync time is used up) or a SecurityException. The scan goes on in the app as
            // before this service existed; it stops if the app is closed and resumes at launch.
            Log.w(TAG, "could not start the scan service: $e")
            session.stop()
        }
    }

    /** New progress from Dart. */
    fun update(title: String, text: String) {
        when (val u = session.update(title, text, SystemClock.elapsedRealtime())) {
            ScanSession.Update.Now -> postNow()
            is ScanSession.Update.Later -> main.postDelayed(postLater, u.delayMs)
            ScanSession.Update.Nothing -> {}
        }
    }

    private fun postNow() {
        val s = service ?: return
        if (!session.active) return
        s.post()
        session.posted(SystemClock.elapsedRealtime())
    }

    /** The scan ended in Dart (done, failed or cancelled): stop the service. */
    fun stop() {
        main.removeCallbacks(postLater)
        if (!session.stop()) return
        service?.finish()
        // After the reply to this call is on its way: destroying the engine from inside its own
        // method call would cut the reply off.
        main.post { if (!session.active) keeper.scanEnded() }
    }

    /**
     * The notification's Cancel: ask Dart to cancel, which then stops the service once the scan's
     * last write is done. If the scan is still running [ScanSession.CANCEL_FALLBACK_MS] later (Dart
     * did not answer), the service stops anyway and a kept engine is released.
     */
    fun requestCancel() {
        val c = channel
        if (c == null || !session.active) {
            stop() // no Dart side to ask, or nothing running
            return
        }
        c.invokeMethod("cancel", null)
        val generation = session.generation
        main.postDelayed({
            if (session.cancelUnanswered(generation)) {
                Log.w(TAG, "Dart did not stop the scan within ${ScanSession.CANCEL_FALLBACK_MS} ms of Cancel: stopping")
                stop()
            }
        }, ScanSession.CANCEL_FALLBACK_MS)
    }

    /**
     * Android 15+: the dataSync time (6 h in 24 h, reset when the app comes to the front) ran out.
     * The service stops. An open app goes on scanning; an engine kept for a closed one is
     * destroyed, and the next launch scans again from where the database is.
     */
    fun timedOut() {
        Log.w(TAG, "the scan service reached its time limit")
        stop()
    }
}
