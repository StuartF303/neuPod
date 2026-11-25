// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod.neupod_audio

import android.app.PendingIntent
import android.content.Intent
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.datasource.DataSourceBitmapLoader
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.session.CacheBitmapLoader
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Owns the ExoPlayer and its MediaSession (decision 0002). Media3 runs it as a foreground service
 * while playing and posts the media notification from the session's state, which also drives the
 * lock screen, Bluetooth and headset buttons. The Flutter side talks to it through a MediaController
 * (NeupodAudioPlugin), so the notification, the buttons and the app all see one player.
 */
@OptIn(UnstableApi::class)
class PlaybackService : MediaSessionService() {
    private var session: MediaSession? = null
    private var artExecutor: ExecutorService? = null

    override fun onCreate() {
        super.onCreate()
        val dataSources = LocalDataSource.Factory(this)
        val player = ExoPlayer.Builder(this)
            .setMediaSourceFactory(DefaultMediaSourceFactory(dataSources))
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(C.USAGE_MEDIA)
                    .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                    .build(),
                // handleAudioFocus: another app taking audio pauses us. The full interruption rules
                // (calls, resume after a transient loss) are checked in phase 4.
                true,
            )
            // Headphones unplugged pauses.
            .setHandleAudioBecomingNoisy(true)
            .setWakeMode(C.WAKE_MODE_LOCAL)
            // The skip-back rule: previous restarts the track after its first three seconds.
            .setMaxSeekToPreviousPositionMs(SKIP_BACK_RESTARTS_AFTER_MS)
            .build()
        // Moving on from a track that failed restarts playback (ErrorRecovery.kt).
        player.addListener(ErrorRecovery(player))

        val executor = Executors.newSingleThreadExecutor().also { artExecutor = it }
        val artLoader = DataSourceBitmapLoader(MoreExecutors.listeningDecorator(executor), dataSources)
        val builder = MediaSession.Builder(this, player)
            .setCallback(SessionCallback())
            .setBitmapLoader(CacheBitmapLoader(artLoader))
        launchIntent()?.let(builder::setSessionActivity)
        session = builder.build()
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = session

    override fun onDestroy() {
        session?.run {
            player.release()
            release()
        }
        session = null
        artExecutor?.shutdown()
        artExecutor = null
        super.onDestroy()
    }

    /** Opens the app when the notification is tapped. */
    private fun launchIntent(): PendingIntent? {
        val intent = packageManager.getLaunchIntentForPackage(packageName) ?: return null
        intent.flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        return PendingIntent.getActivity(
            this,
            0,
            intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
    }

    private inner class SessionCallback : MediaSession.Callback {
        /**
         * A MediaController's items arrive without their URI (Media3 strips the local configuration
         * when it crosses the session boundary), so the plugin carries it in the request metadata
         * and it is restored here. Only this app may set the queue: the service is exported so the
         * system UI and Bluetooth can control it, not so other apps can make it open their URIs.
         */
        override fun onAddMediaItems(
            mediaSession: MediaSession,
            controller: MediaSession.ControllerInfo,
            mediaItems: MutableList<MediaItem>,
        ): ListenableFuture<MutableList<MediaItem>> {
            if (controller.packageName != packageName) {
                return Futures.immediateFailedFuture(UnsupportedOperationException("queue is app-only"))
            }
            val resolved = mediaItems.map { item ->
                val uri = item.requestMetadata.mediaUri ?: item.localConfiguration?.uri
                if (uri == null) item else item.buildUpon().setUri(uri).build()
            }
            return Futures.immediateFuture(resolved.toMutableList())
        }
    }

    companion object {
        const val SKIP_BACK_RESTARTS_AFTER_MS = 3000L
    }
}
