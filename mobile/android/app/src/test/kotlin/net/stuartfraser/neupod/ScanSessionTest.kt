// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

class ScanSessionTest {
    private fun started() = ScanSession(minIntervalMs = 500).apply {
        start("Building library", "0 found · 0 read", "Cancel")
    }

    @Test
    fun `the first start starts the service, a restart does not start it again`() {
        val s = ScanSession()
        assertTrue(s.start("Building library", "0 found · 0 read", "Cancel"))
        assertTrue(s.active)
        assertFalse(s.start("Building library", "0 found · 0 read", "Cancel"))
        assertEquals("Cancel", s.cancelLabel)
    }

    @Test
    fun `stop ends the scan once`() {
        val s = started()
        assertTrue(s.stop())
        assertFalse(s.active)
        assertFalse(s.stop())
        assertTrue(s.start("Building library", "0 found · 0 read", "Cancel"), "a new scan starts the service again")
    }

    @Test
    fun `progress is posted at once when nothing was posted yet`() {
        val s = started()
        assertEquals(ScanSession.Update.Now, s.update("Building library", "4 found · 1 read", nowMs = 1_000))
        assertEquals("4 found · 1 read", s.text)
    }

    @Test
    fun `progress within the interval is posted once, later, with the latest text`() {
        val s = started()
        s.posted(nowMs = 1_000)
        assertEquals(ScanSession.Update.Later(400), s.update("Building library", "5 found · 1 read", nowMs = 1_100))
        assertEquals(ScanSession.Update.Nothing, s.update("Building library", "9 found · 2 read", nowMs = 1_200))
        assertEquals("9 found · 2 read", s.text, "the later post shows the newest text")
        s.posted(nowMs = 1_500)
        assertEquals(ScanSession.Update.Now, s.update("Building library", "12 found · 3 read", nowMs = 2_000))
    }

    @Test
    fun `progress after stop is ignored`() {
        val s = started()
        s.stop()
        assertEquals(ScanSession.Update.Nothing, s.update("Building library", "1 found · 0 read", nowMs = 1_000))
    }

    @Test
    fun `stop clears a due post, so the next scan posts at once`() {
        val s = started()
        s.posted(nowMs = 1_000)
        s.update("Building library", "1 found · 0 read", nowMs = 1_100)
        s.stop()
        s.start("Building library", "0 found · 0 read", "Cancel")
        assertEquals(ScanSession.Update.Now, s.update("Building library", "2 found · 0 read", nowMs = 1_200))
    }

    @Test
    fun `an unanswered Cancel stops the scan it was sent for, not a later one`() {
        val s = started()
        val first = s.generation
        assertTrue(s.cancelUnanswered(first), "Dart did not stop it: the service stops anyway")
        s.stop()
        assertFalse(s.cancelUnanswered(first), "Dart stopped it in time")
        s.start("Building library", "0 found · 0 read", "Cancel")
        assertFalse(s.cancelUnanswered(first), "a new scan started since is left alone")
        assertTrue(s.cancelUnanswered(s.generation))
    }

    @Test
    fun `a restart of a running scan keeps its generation`() {
        val s = started()
        val g = s.generation
        s.start("Building library", "0 found · 0 read", "Cancel")
        assertEquals(g, s.generation)
    }

    private class Engine

    @Test
    fun `a kept engine is handed to the next activity, not destroyed`() {
        val destroyed = mutableListOf<Engine>()
        val keeper = EngineKeeper<Engine> { destroyed += it }
        val engine = Engine()
        keeper.hold(engine)
        assertTrue(keeper.holding)
        assertSame(engine, keeper.take())
        assertFalse(keeper.holding)
        assertNull(keeper.take())
        keeper.scanEnded()
        assertTrue(destroyed.isEmpty())
    }

    @Test
    fun `a kept engine is destroyed when the scan ends with no activity`() {
        val destroyed = mutableListOf<Engine>()
        val keeper = EngineKeeper<Engine> { destroyed += it }
        val engine = Engine()
        keeper.hold(engine)
        keeper.scanEnded()
        assertEquals(listOf(engine), destroyed)
        assertFalse(keeper.holding)
        keeper.scanEnded()
        assertEquals(1, destroyed.size)
    }

    @Test
    fun `the test hook keeps the engine after the scan`() {
        val destroyed = mutableListOf<Engine>()
        val keeper = EngineKeeper<Engine> { destroyed += it }.apply { keepAfterScan = true }
        keeper.hold(Engine())
        keeper.scanEnded()
        assertTrue(destroyed.isEmpty())
        assertTrue(keeper.holding)
    }

    @Test
    fun `holding a second engine destroys the first`() {
        val destroyed = mutableListOf<Engine>()
        val keeper = EngineKeeper<Engine> { destroyed += it }
        val first = Engine()
        keeper.hold(first)
        keeper.hold(Engine())
        assertEquals(listOf(first), destroyed)
    }
}
