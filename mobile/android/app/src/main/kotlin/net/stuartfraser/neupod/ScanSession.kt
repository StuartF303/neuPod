// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

/**
 * The state of the library scan as the scan service sees it (decision 0009): whether one is
 * running, what its notification says, and when the notification may next change. Plain Kotlin,
 * no Android types, so it is tested on the JVM (ScanSessionTest).
 *
 * The scan itself runs in Dart (LibraryScanController); Dart reports start, progress and stop
 * over the scan service channel and the session only keeps what the notification needs.
 */
internal class ScanSession(private val minIntervalMs: Long = UPDATE_INTERVAL_MS) {
    /** What to do with a progress update. */
    sealed interface Update {
        /** Post the notification now. */
        data object Now : Update

        /** Post it after [delayMs]; the latest text is shown then. */
        data class Later(val delayMs: Long) : Update

        /** Nothing: no scan is running, or a later post is already due. */
        data object Nothing : Update
    }

    /** Whether a scan is running (between start and stop). */
    var active = false
        private set

    /** The notification's title: the firmware's "Building library". */
    var title = ""
        private set

    /** The notification's text: "N found · N read". */
    var text = ""
        private set

    /** The label of the notification's action that stops the scan. */
    var cancelLabel = ""
        private set

    private var lastPostedAt: Long? = null
    private var postDue = false

    /**
     * A scan starts (or the running one is restarted). Returns true when the service has to be
     * started, false when it is already running for an earlier start.
     */
    fun start(title: String, text: String, cancelLabel: String): Boolean {
        val wasActive = active
        active = true
        this.title = title
        this.text = text
        this.cancelLabel = cancelLabel
        return !wasActive
    }

    /**
     * New progress at [nowMs]. Android drops a package's notification updates past a few a second,
     * so they are posted at most once per [minIntervalMs]; one that comes sooner is posted when the
     * interval is up, with the text it has then.
     */
    fun update(title: String, text: String, nowMs: Long): Update {
        if (!active) return Update.Nothing
        this.title = title
        this.text = text
        if (postDue) return Update.Nothing
        val last = lastPostedAt
        if (last == null || nowMs - last >= minIntervalMs) return Update.Now
        postDue = true
        return Update.Later(minIntervalMs - (nowMs - last))
    }

    /** The notification was posted at [nowMs]. */
    fun posted(nowMs: Long) {
        lastPostedAt = nowMs
        postDue = false
    }

    /** The scan ended. Returns true when one was running. */
    fun stop(): Boolean {
        val wasActive = active
        active = false
        lastPostedAt = null
        postDue = false
        return wasActive
    }

    companion object {
        const val UPDATE_INTERVAL_MS = 500L
    }
}

/**
 * Holds the Flutter engine of an activity that went away while a scan was running, so that the
 * scan (Dart code in that engine) goes on (decision 0009). The next activity takes it back; when
 * the scan ends with no activity, it is destroyed. Generic over the engine so it is tested on the
 * JVM (ScanSessionTest). Main thread only.
 */
internal class EngineKeeper<E : Any>(private val destroy: (E) -> Unit) {
    private var held: E? = null

    /**
     * Debug builds only, for the instrumentation test: keep the engine after the scan ends, so the
     * Dart test in it can still report its result.
     */
    var keepAfterScan = false

    /** Whether an engine is held. */
    val holding: Boolean get() = held != null

    /** Keeps [engine] (its activity is gone, a scan is running). */
    fun hold(engine: E) {
        val old = held
        held = engine
        if (old != null && old !== engine) destroy(old)
    }

    /** The held engine, for an activity that starts; it is no longer held. */
    fun take(): E? = held.also { held = null }

    /** The scan ended: a held engine has nothing left to do and is destroyed. */
    fun scanEnded() {
        if (keepAfterScan) return
        val engine = held ?: return
        held = null
        destroy(engine)
    }
}
