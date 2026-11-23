// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import android.content.Context
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine

/**
 * The app's one activity. Its Flutter engine normally lives and dies with it. While the library
 * scan runs, the engine running it outlives the activity (decision 0009):
 *
 * - An activity that goes away (the app swiped from Recents) leaves the engine to
 *   ScanRuntime.keeper, so the scan, which is Dart code in that engine, goes on. If the scan ends
 *   first, the engine is destroyed then.
 * - A new activity takes that engine, kept or still attached to an activity that is finishing
 *   (Back on Android 10 and 11 finishes the root activity, and its onDestroy may come after the
 *   relaunch). So a second engine, and a second scan writing the same database, never starts.
 */
class MainActivity : FlutterActivity() {
    /** The scan's engine, kept or still alive, or null for a new one. */
    override fun provideFlutterEngine(context: Context): FlutterEngine? = ScanRuntime.engineForNewActivity()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        // Registers the generated plugins on a new engine; a provided engine already has them.
        super.configureFlutterEngine(flutterEngine)
        if (!flutterEngine.plugins.has(ScanServicePlugin::class.java)) {
            flutterEngine.plugins.add(ScanServicePlugin())
        }
    }

    /**
     * Destroy the engine with the activity, unless it runs the scan. Also asked when a new activity
     * takes the engine from this one, which must then not destroy it.
     */
    override fun shouldDestroyEngineWithHost(): Boolean = !ScanRuntime.runsScan(flutterEngine)

    override fun onDestroy() {
        val engine = flutterEngine
        val keep = ScanRuntime.runsScan(engine)
        super.onDestroy()
        // Kept only if no other activity has taken it meanwhile.
        if (keep && engine != null && ScanRuntime.activity == null) ScanRuntime.keeper.hold(engine)
    }
}
