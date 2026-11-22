// SPDX-License-Identifier: MPL-2.0
package net.stuartfraser.neupod_device

import android.app.Activity
import android.content.Context
import android.content.pm.ApplicationInfo
import android.media.AudioManager
import android.os.Build
import android.util.Log
import android.view.HapticFeedbackConstants
import android.view.View
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding

/**
 * Click-wheel haptics and the system media volume
 * (docs/app/decisions/0007-haptics.md).
 *
 * Haptics go through View.performHapticFeedback on the activity's window, which needs no
 * VIBRATE permission and obeys the user's touch-feedback setting (the default since API 33 can no
 * longer be overridden by apps, and this plugin never asks to). In debug builds every call is logged
 * under the tag "neupod_device", so that an emulator, which cannot vibrate, still shows that the
 * calls were made; release builds log nothing.
 */
class NeupodDevicePlugin : FlutterPlugin, ActivityAware, DeviceApi {
    private var context: Context? = null
    private var activity: Activity? = null
    private var debuggable = false

    private fun debug(message: String) {
        if (debuggable) Log.d(TAG, message)
    }

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        debuggable = (binding.applicationContext.applicationInfo.flags and
            ApplicationInfo.FLAG_DEBUGGABLE) != 0
        DeviceApi.setUp(binding.binaryMessenger, this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        DeviceApi.setUp(binding.binaryMessenger, null)
        context = null
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
    }

    override fun onDetachedFromActivityForConfigChanges() {
        activity = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        activity = binding.activity
    }

    override fun onDetachedFromActivity() {
        activity = null
    }

    // ---- Haptics ----------------------------------------------------------------------------

    override fun prepareHaptics() {
        // Nothing to prepare on Android.
    }

    override fun playHaptic(event: HapticEvent) {
        val constant = constantFor(event)
        val view: View? = activity?.window?.decorView
        val sent = view?.performHapticFeedback(constant) ?: false
        debug("haptic $event -> ${nameOf(constant)} sent=$sent")
    }

    // ---- Volume -----------------------------------------------------------------------------

    private fun audio(): AudioManager? =
        context?.getSystemService(Context.AUDIO_SERVICE) as AudioManager?

    override fun getVolume(): VolumeState = volumeOf(audio(), VolumeStatus.OK)

    override suspend fun stepVolume(direction: Long): VolumeState {
        val am = audio() ?: return volumeOf(null, VolumeStatus.NO_EFFECT)
        if (am.isVolumeFixed) return volumeOf(am, VolumeStatus.FIXED)
        val before = am.getStreamVolume(AudioManager.STREAM_MUSIC)
        val atLimit = (direction > 0 && before >= am.getStreamMaxVolume(AudioManager.STREAM_MUSIC)) ||
            (direction < 0 && before <= am.getStreamMinVolume(AudioManager.STREAM_MUSIC))
        if (direction != 0L) {
            // Flags 0: no FLAG_SHOW_UI, so the system volume panel stays away; the device's own
            // volume bar is shown instead (owner decision 10).
            am.adjustStreamVolume(
                AudioManager.STREAM_MUSIC,
                if (direction > 0) AudioManager.ADJUST_RAISE else AudioManager.ADJUST_LOWER,
                0,
            )
        }
        // Read back: the system may refuse (Do Not Disturb, the EU safe media volume limit when
        // raising past it on headphones), so the app must not assume the step happened.
        val after = am.getStreamVolume(AudioManager.STREAM_MUSIC)
        val status = if (after != before || atLimit || direction == 0L) VolumeStatus.OK
            else VolumeStatus.NO_EFFECT
        val v = volumeOf(am, status)
        debug("volume step $direction -> ${v.step}/${v.steps} $status")
        return v
    }

    private fun volumeOf(am: AudioManager?, status: VolumeStatus): VolumeState {
        if (am == null) {
            return VolumeState(level = 0.0, step = 0, steps = 0, fixed = true, status = status)
        }
        val min = am.getStreamMinVolume(AudioManager.STREAM_MUSIC)
        val max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val cur = am.getStreamVolume(AudioManager.STREAM_MUSIC)
        val steps = (max - min).coerceAtLeast(0)
        val step = (cur - min).coerceIn(0, steps)
        return VolumeState(
            level = if (steps == 0) 0.0 else step.toDouble() / steps,
            step = step.toLong(),
            steps = steps.toLong(),
            fixed = am.isVolumeFixed,
            status = status,
        )
    }

    companion object {
        private const val TAG = "neupod_device"

        /** The decision 0007 table. minSdk is 29, so two constants need a fallback. */
        fun constantFor(event: HapticEvent): Int = when (event) {
            HapticEvent.DETENT ->
                if (Build.VERSION.SDK_INT >= 34) HapticFeedbackConstants.SEGMENT_FREQUENT_TICK
                else HapticFeedbackConstants.CLOCK_TICK
            HapticEvent.BUTTON_DOWN -> HapticFeedbackConstants.VIRTUAL_KEY
            HapticEvent.BUTTON_UP -> HapticFeedbackConstants.VIRTUAL_KEY_RELEASE
            HapticEvent.HOLD_TOOK_EFFECT -> HapticFeedbackConstants.LONG_PRESS
            HapticEvent.END_REACHED ->
                if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.REJECT
                else HapticFeedbackConstants.LONG_PRESS
        }

        private fun nameOf(constant: Int): String = when (constant) {
            HapticFeedbackConstants.SEGMENT_FREQUENT_TICK -> "SEGMENT_FREQUENT_TICK"
            HapticFeedbackConstants.CLOCK_TICK -> "CLOCK_TICK"
            HapticFeedbackConstants.VIRTUAL_KEY -> "VIRTUAL_KEY"
            HapticFeedbackConstants.VIRTUAL_KEY_RELEASE -> "VIRTUAL_KEY_RELEASE"
            HapticFeedbackConstants.LONG_PRESS -> "LONG_PRESS"
            HapticFeedbackConstants.REJECT -> "REJECT"
            else -> constant.toString()
        }
    }
}
