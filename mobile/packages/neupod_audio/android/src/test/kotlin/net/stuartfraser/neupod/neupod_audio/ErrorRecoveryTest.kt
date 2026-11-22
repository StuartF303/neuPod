// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod.neupod_audio

import androidx.media3.common.Player
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ErrorRecoveryTest {
    private fun prepare(state: Int, failed: Int?, current: Int, count: Int = 2) =
        ErrorRecovery.shouldPrepare(state, failed, current, count)

    @Test
    fun `the PR 111 ordering - idle on the next track with the retried track's error - prepares`() {
        // Play retried track 0, the skip moved to track 1, then track 0's error arrived.
        assertTrue(prepare(Player.STATE_IDLE, failed = 0, current = 1))
    }

    @Test
    fun `a skip back to an earlier track after a later one failed prepares`() {
        assertTrue(prepare(Player.STATE_IDLE, failed = 1, current = 0))
    }

    @Test
    fun `the current track's own failure is not retried here`() {
        // Dart's skip-after-error moves on from it; retrying here could loop on a bad file.
        assertFalse(prepare(Player.STATE_IDLE, failed = 1, current = 1))
    }

    @Test
    fun `an error that names no track is left alone`() {
        assertFalse(prepare(Player.STATE_IDLE, failed = null, current = 1))
    }

    @Test
    fun `nothing happens unless the player is idle, or with an empty queue`() {
        assertFalse(prepare(Player.STATE_BUFFERING, failed = 0, current = 1))
        assertFalse(prepare(Player.STATE_READY, failed = 0, current = 1))
        assertFalse(prepare(Player.STATE_ENDED, failed = 0, current = 1))
        assertFalse(prepare(Player.STATE_IDLE, failed = 0, current = 1, count = 0))
    }
}
