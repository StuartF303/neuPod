// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod.neupod_audio

import android.content.ComponentName
import android.content.Context
import android.net.Uri
import android.os.Bundle
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.google.common.util.concurrent.ListenableFuture
import io.flutter.embedding.engine.plugins.FlutterPlugin
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.guava.await
import kotlinx.coroutines.launch

/**
 * The Flutter side of the engine. It holds a MediaController connected to [PlaybackService], so
 * every command goes through the same session the notification and the buttons use, and the
 * player keeps running in the service when the Flutter engine goes away.
 */
@OptIn(UnstableApi::class)
class NeupodAudioPlugin : FlutterPlugin, AudioHostApi {
    private var scope: CoroutineScope? = null
    private var flutterApi: AudioFlutterApi? = null
    private var controllerFuture: ListenableFuture<MediaController>? = null

    /**
     * The queue id of an empty queue set by [setQueue]. A non-empty queue carries its id in each
     * item's metadata extras, which live in the session and so outlast this plugin instance.
     */
    private var emptyQueueId: String? = null

    private val listener = object : Player.Listener {
        override fun onEvents(player: Player, events: Player.Events) = emit(player)
    }

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        val context: Context = binding.applicationContext
        val scope = MainScope().also { scope = it }
        AudioHostApi.setUp(binding.binaryMessenger, this)
        flutterApi = AudioFlutterApi(binding.binaryMessenger)
        val token = SessionToken(context, ComponentName(context, PlaybackService::class.java))
        val future = MediaController.Builder(context, token).buildAsync()
        controllerFuture = future
        scope.launch {
            val controller = future.await()
            controller.addListener(listener)
            emit(controller)
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        AudioHostApi.setUp(binding.binaryMessenger, null)
        flutterApi = null
        controllerFuture?.let(MediaController::releaseFuture)
        controllerFuture = null
        scope?.cancel()
        scope = null
    }

    private suspend fun controller(): MediaController =
        (controllerFuture ?: throw FlutterError("detached", "The audio engine is not attached", null)).await()

    /** Sends the player's state to Dart, in the order the changes happened. */
    private fun emit(player: Player) {
        val api = flutterApi ?: return
        val scope = scope ?: return
        val event = snapshot(player)
        scope.launch(start = CoroutineStart.UNDISPATCHED) {
            try {
                api.onEvent(event)
            } catch (_: FlutterError) {
                // No Dart listener (the engine is starting or stopping): the next event catches up.
            }
        }
    }

    override suspend fun setQueue(
        queueId: String,
        items: List<EngineItem>,
        startIndex: Long,
        startPositionMs: Long,
    ) {
        val c = controller()
        if (items.isEmpty()) {
            clear()
            emptyQueueId = queueId
            emit(c)
            return
        }
        emptyQueueId = null
        c.setMediaItems(items.map { toMediaItem(it, queueId) }, startIndex.toInt(), startPositionMs)
        c.prepare()
    }

    override suspend fun insert(index: Long, items: List<EngineItem>) {
        val c = controller()
        val queueId = queueIdOf(c)
        c.addMediaItems(index.toInt(), items.map { toMediaItem(it, queueId) })
    }

    override suspend fun remove(index: Long) {
        val c = controller()
        // Removing the last item empties the queue: keep its id, as iOS does.
        if (c.mediaItemCount == 1) emptyQueueId = queueIdOf(c)
        c.removeMediaItem(index.toInt())
    }

    override suspend fun move(from: Long, to: Long) {
        controller().moveMediaItem(from.toInt(), to.toInt())
    }

    /** Empties the queue and drops the play intent, as on iOS (ExoPlayer's stop() keeps it). */
    override suspend fun clear() {
        val c = controller()
        emptyQueueId = null
        c.pause()
        c.stop()
        c.clearMediaItems()
    }

    override suspend fun play() {
        val c = controller()
        if (c.mediaItemCount == 0) return
        when (c.playbackState) {
            // After an error the player is idle; preparing again retries the current item.
            Player.STATE_IDLE -> c.prepare()
            // Play after the end starts the last track again.
            Player.STATE_ENDED -> c.seekTo(c.currentMediaItemIndex, 0)
        }
        c.play()
    }

    override suspend fun pause() {
        controller().pause()
    }

    override suspend fun seekTo(positionMs: Long) {
        controller().seekTo(positionMs)
    }

    override suspend fun skipTo(index: Long) {
        val c = controller()
        c.seekTo(index.toInt(), 0)
        if (c.playbackState == Player.STATE_IDLE) c.prepare()
    }

    override suspend fun skipToNext() {
        val c = controller()
        c.seekToNextMediaItem()
        if (c.playbackState == Player.STATE_IDLE) c.prepare()
    }

    override suspend fun skipToPrevious() {
        val c = controller()
        c.seekToPrevious()
        if (c.playbackState == Player.STATE_IDLE) c.prepare()
    }

    override suspend fun getState(): EngineEvent = snapshot(controller())

    override suspend fun getSessionInfo(): SessionInfo {
        val c = controller()
        val metadata = c.mediaMetadata
        val commands = buildList {
            if (c.isCommandAvailable(Player.COMMAND_PLAY_PAUSE)) addAll(listOf("play", "pause"))
            if (c.isCommandAvailable(Player.COMMAND_SEEK_TO_NEXT)) add("next")
            if (c.isCommandAvailable(Player.COMMAND_SEEK_TO_PREVIOUS)) add("previous")
            if (c.isCommandAvailable(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM)) add("seek")
        }
        return SessionInfo(
            title = metadata.title?.toString(),
            artist = metadata.artist?.toString(),
            album = metadata.albumTitle?.toString(),
            durationMs = c.duration.takeIf { it != C.TIME_UNSET },
            positionMs = c.currentPosition,
            playbackRate = if (c.isPlaying) c.playbackParameters.speed.toDouble() else 0.0,
            hasArtwork = metadata.artworkData != null || metadata.artworkUri != null,
            commands = commands,
        )
    }

    private fun toMediaItem(item: EngineItem, queueId: String?): MediaItem {
        val uri = Uri.parse(item.uri)
        val metadata = MediaMetadata.Builder()
            .setExtras(Bundle().apply { putString(QUEUE_ID, queueId) })
            .setTitle(item.title)
            .setArtist(item.artist)
            .setAlbumTitle(item.album)
            .setArtworkUri(item.artUri?.let(Uri::parse))
            .setDurationMs(item.durationMs)
            .setIsPlayable(true)
            .setIsBrowsable(false)
            .build()
        return MediaItem.Builder()
            .setMediaId(item.id)
            .setUri(uri)
            // Survives the trip to the session, which drops the URI (PlaybackService.onAddMediaItems).
            .setRequestMetadata(MediaItem.RequestMetadata.Builder().setMediaUri(uri).build())
            .setMediaMetadata(metadata)
            .build()
    }

    private fun snapshot(p: Player): EngineEvent {
        val hasItems = p.mediaItemCount > 0
        val status = when (p.playbackState) {
            Player.STATE_BUFFERING -> EngineStatus.LOADING
            Player.STATE_READY -> EngineStatus.READY
            Player.STATE_ENDED -> EngineStatus.ENDED
            else -> EngineStatus.IDLE
        }
        return EngineEvent(
            queueId = queueIdOf(p),
            status = status,
            playWhenReady = p.playWhenReady,
            // False while playback is suppressed (a transient loss of audio focus).
            isPlaying = p.isPlaying,
            index = if (hasItems) p.currentMediaItemIndex.toLong() else null,
            itemId = p.currentMediaItem?.mediaId,
            positionMs = if (hasItems) p.currentPosition else 0,
            durationMs = p.duration.takeIf { hasItems && it != C.TIME_UNSET },
            queueLength = p.mediaItemCount.toLong(),
            format = audioFormat(p),
            errorMessage = p.playerError?.let { "${it.errorCodeName}: ${it.message}" },
        )
    }

    /** The current queue's id: from the items' metadata, or the id of an empty queue. */
    private fun queueIdOf(p: Player): String? =
        if (p.mediaItemCount == 0) {
            emptyQueueId
        } else {
            p.getMediaItemAt(0).mediaMetadata.extras?.getString(QUEUE_ID)
        }

    private fun audioFormat(p: Player): EngineFormat? {
        val group = p.currentTracks.groups.firstOrNull { it.type == C.TRACK_TYPE_AUDIO && it.isSelected }
            ?: return null
        val track = (0 until group.length).firstOrNull(group::isTrackSelected) ?: return null
        val f = group.getTrackFormat(track)
        fun known(v: Int) = v.takeIf { it != Format.NO_VALUE }?.toLong()
        val bits = when (f.pcmEncoding) {
            C.ENCODING_PCM_16BIT -> 16L
            C.ENCODING_PCM_24BIT -> 24L
            C.ENCODING_PCM_32BIT, C.ENCODING_PCM_FLOAT -> 32L
            C.ENCODING_PCM_8BIT -> 8L
            else -> null
        }
        return EngineFormat(
            codec = f.sampleMimeType,
            sampleRate = known(f.sampleRate),
            channelCount = known(f.channelCount),
            bitsPerSample = bits,
        )
    }

    private companion object {
        const val QUEUE_ID = "net.stuartfraser.neupod.queueId"
    }
}
