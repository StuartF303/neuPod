// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod.neupod_audio

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlaybackException
import androidx.media3.exoplayer.ExoPlayer

/**
 * Gets the player going again when it has moved on from a track that failed.
 *
 * After an error ExoPlayer is idle until `prepare()`. The plugin prepares when it sees the idle
 * state, but it sees it through a MediaController whose state is masked. A play (which retries the
 * failed track) followed at once by a skip to the next track then goes wrong: the controller shows
 * "loading" when the skip arrives, so no second prepare is sent, and the retry's error, delivered
 * after the seek, leaves the player idle on the next track (the PR #111 CI flake, reproduced with
 * a forced ordering). So the service, which sees the real player, prepares whenever the player is
 * idle with an error that belongs to a track other than the current one.
 */
@OptIn(UnstableApi::class)
class ErrorRecovery(private val player: ExoPlayer) : Player.Listener {
    override fun onEvents(player: Player, events: Player.Events) {
        val error = this.player.playerError ?: return
        val failed = windowOf(error, this.player.currentTimeline)
        if (shouldPrepare(this.player.playbackState, failed, this.player.currentMediaItemIndex,
                this.player.mediaItemCount)) {
            this.player.prepare()
        }
    }

    companion object {
        /**
         * True when the player is idle after an error in the track at window [failedWindow] while
         * the current track is another one: preparing plays (or readies) the current track. A
         * failure of the current track itself is left alone, so a bad file is not retried in a loop;
         * Dart's skip-after-error moves on from it.
         */
        fun shouldPrepare(playbackState: Int, failedWindow: Int?, currentWindow: Int, itemCount: Int): Boolean =
            playbackState == Player.STATE_IDLE &&
                itemCount > 0 &&
                failedWindow != null &&
                failedWindow != currentWindow

        /** The window (queue index) of the track an error came from, if the error says. */
        fun windowOf(error: ExoPlaybackException, timeline: Timeline): Int? {
            val periodUid = error.mediaPeriodId?.periodUid ?: return null
            val periodIndex = timeline.getIndexOfPeriod(periodUid)
            if (periodIndex == C.INDEX_UNSET) return null
            return timeline.getPeriod(periodIndex, Timeline.Period()).windowIndex
        }
    }
}
