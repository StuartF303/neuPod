// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod

import android.content.Context
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine

/**
 * The app's one activity. Its Flutter engine normally lives and dies with it. While the library
 * scan runs (the scan service is up), an activity that goes away (the user swipes the app from
 * Recents) leaves its engine to ScanRuntime.keeper instead, so the scan, which is Dart code in
 * that engine, goes on (decision 0009). The next activity takes the engine back; if the scan ends
 * first, the engine is destroyed then.
 */
class MainActivity : FlutterActivity() {
    private var keepEngine = false

    /** The engine kept from an activity that went away mid-scan, or null for a new one. */
    override fun provideFlutterEngine(context: Context): FlutterEngine? = ScanRuntime.keeper.take()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        // Registers the generated plugins on a new engine; a kept engine already has them.
        super.configureFlutterEngine(flutterEngine)
        if (!flutterEngine.plugins.has(ScanServicePlugin::class.java)) {
            flutterEngine.plugins.add(ScanServicePlugin())
        }
    }

    /** Destroy the engine with the activity, unless a scan is running in it. */
    override fun shouldDestroyEngineWithHost(): Boolean = !keepEngine

    override fun onDestroy() {
        val engine = flutterEngine
        keepEngine = engine != null && ScanRuntime.session.active
        super.onDestroy()
        if (keepEngine && engine != null) ScanRuntime.keeper.hold(engine)
        keepEngine = false
    }
}
