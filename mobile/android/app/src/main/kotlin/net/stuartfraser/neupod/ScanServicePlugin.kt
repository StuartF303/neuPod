// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/**
 * The Dart side's way to the scan service (decision 0009), on the channel
 * `net.stuartfraser.neupod/scan_service` (lib/data/platform/android_scan_service.dart):
 *
 * - Dart to here: `start` {title, text, cancel, askNotifications}, `update` {title, text}, `stop`.
 * - Here to Dart: `cancel` (the notification's Cancel).
 * - Debug builds only, for the instrumentation test: `debugFinishActivity`, `debugRelaunch`,
 *   `debugNotificationCancel`, `debugState`, `debugKeepEngineAfterScan`.
 *
 * Registered by MainActivity on the app's engine, once per engine. It also follows the activity,
 * which MainActivity needs to know whether the engine is still shown.
 */
class ScanServicePlugin : FlutterPlugin, ActivityAware, MethodChannel.MethodCallHandler {
    private var context: Context? = null
    private var channel: MethodChannel? = null
    private var engine: FlutterEngine? = null

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        @Suppress("DEPRECATION") // the plugin is added by MainActivity to this very engine
        engine = binding.flutterEngine
        channel = MethodChannel(binding.binaryMessenger, CHANNEL).also {
            it.setMethodCallHandler(this)
            ScanRuntime.channel = it
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel?.setMethodCallHandler(null)
        if (ScanRuntime.channel === channel) ScanRuntime.channel = null
        if (ScanRuntime.scanEngine === engine) ScanRuntime.scanEngine = null
        channel = null
        engine = null
        context = null
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        ScanRuntime.activity = binding.activity
    }

    override fun onDetachedFromActivityForConfigChanges() = onDetachedFromActivity()

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) =
        onAttachedToActivity(binding)

    override fun onDetachedFromActivity() {
        ScanRuntime.activity = null
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val ctx = context ?: return result.error("detached", "no engine", null)
        when (call.method) {
            "start" -> {
                if (call.argument<Boolean>("askNotifications") == true) askForNotifications()
                ScanRuntime.scanEngine = engine
                ScanRuntime.start(
                    ctx,
                    call.argument<String>("title") ?: "",
                    call.argument<String>("text") ?: "",
                    call.argument<String>("cancel") ?: "",
                )
                result.success(null)
            }
            "update" -> {
                ScanRuntime.update(call.argument<String>("title") ?: "", call.argument<String>("text") ?: "")
                result.success(null)
            }
            "stop" -> {
                ScanRuntime.stop()
                result.success(null)
            }
            "debugFinishActivity" -> debugOnly(ctx, result) {
                ScanRuntime.activity?.finishAndRemoveTask()
                null
            }
            "debugState" -> debugOnly(ctx, result) {
                mapOf(
                    "scanActive" to ScanRuntime.session.active,
                    "serviceRunning" to (ScanRuntime.service != null),
                    "activityAttached" to (ScanRuntime.activity != null),
                    "engineKept" to ScanRuntime.keeper.holding,
                )
            }
            "debugRelaunch" -> debugOnly(ctx, result) {
                // As tapping the launcher icon: a new activity, which takes the kept engine.
                ctx.packageManager.getLaunchIntentForPackage(ctx.packageName)?.let {
                    it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    ctx.startActivity(it)
                }
                null
            }
            "debugNotificationCancel" -> debugOnly(ctx, result) {
                // What the notification's Cancel action's PendingIntent sends.
                ctx.startService(Intent(ctx, ScanService::class.java).setAction(ScanService.ACTION_CANCEL))
                null
            }
            "debugKeepEngineAfterScan" -> debugOnly(ctx, result) {
                ScanRuntime.keeper.keepAfterScan = call.arguments as? Boolean ?: false
                null
            }
            else -> result.notImplemented()
        }
    }

    /**
     * Android 13+: the scan's notification needs POST_NOTIFICATIONS. Without it the service still
     * runs (the scan still survives) but its notification is only in the system's Task Manager.
     * Asked only when the user started the scan (Dart decides), and never again once the system
     * stops showing the prompt (after two refusals).
     */
    private fun askForNotifications() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val activity = ScanRuntime.activity ?: return
        if (activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
            return
        }
        activity.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFICATIONS)
    }

    private inline fun debugOnly(ctx: Context, result: MethodChannel.Result, body: () -> Any?) {
        if (ctx.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) {
            result.notImplemented()
            return
        }
        result.success(body())
    }

    companion object {
        const val CHANNEL = "net.stuartfraser.neupod/scan_service"
        private const val REQUEST_NOTIFICATIONS = 0x5CA7
    }
}
