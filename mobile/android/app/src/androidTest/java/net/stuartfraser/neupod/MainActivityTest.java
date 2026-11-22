// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod;

import androidx.test.rule.ActivityTestRule;
import dev.flutter.plugins.integration_test.FlutterTestRunner;
import org.junit.Rule;
import org.junit.runner.RunWith;

// Runs the Dart integration test the app was built with (./gradlew app:connectedDebugAndroidTest -Ptarget=<file>)
// and reports each Dart test as a JUnit test through instrumentation, so the Gradle task passes or fails with them.
@RunWith(FlutterTestRunner.class)
public class MainActivityTest {
  @Rule
  public ActivityTestRule<MainActivity> rule = new ActivityTestRule<>(MainActivity.class, true, false);
}
