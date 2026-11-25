// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod;

import android.Manifest;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.MediaStore;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.ActivityTestRule;
import dev.flutter.plugins.integration_test.FlutterTestRunner;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.Rule;
import org.junit.runner.RunWith;

// Runs the Dart integration test the app was built with (./gradlew app:connectedDebugAndroidTest -Ptarget=<file>)
// and reports each Dart test as a JUnit test through instrumentation, so the Gradle task passes or fails with them.
//
// With the instrumentation argument neupodSeedMediaStore=true (tool/android_integration_tests.sh passes it for
// integration_test/media_store_test.dart only), the rule first grants the app the permission that reads the shared
// music collection and puts a short WAV in MediaStore under Music/neuPodIT/. That is the test's fixture, written by the
// test harness; the app itself only reads (test/read_only_guard_test.dart).
@RunWith(FlutterTestRunner.class)
public class MainActivityTest {
  @Rule
  public ActivityTestRule<MainActivity> rule = new SeedingRule();

  /** FlutterTestRunner launches the activity through this rule, so beforeActivityLaunched runs first. */
  public static class SeedingRule extends ActivityTestRule<MainActivity> {
    public SeedingRule() {
      super(MainActivity.class, true, false);
    }

    @Override
    protected void beforeActivityLaunched() {
      if ("true".equals(InstrumentationRegistry.getArguments().getString("neupodSeedMediaStore"))) {
        seedMediaStore();
      }
    }
  }

  static final String SEED_DIR = "Music/neuPodIT/";
  static final String SEED_NAME = "neupod_it_tone.wav";

  static void seedMediaStore() {
    Context ctx = InstrumentationRegistry.getInstrumentation().getTargetContext();
    String permission = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
        ? Manifest.permission.READ_MEDIA_AUDIO
        : Manifest.permission.READ_EXTERNAL_STORAGE;
    InstrumentationRegistry.getInstrumentation().getUiAutomation()
        .grantRuntimePermission(ctx.getPackageName(), permission);

    ContentResolver resolver = ctx.getContentResolver();
    Uri collection = MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
    try (Cursor c = resolver.query(
        collection,
        new String[] {MediaStore.Audio.Media._ID},
        MediaStore.Audio.Media.RELATIVE_PATH + "=? AND " + MediaStore.Audio.Media.DISPLAY_NAME + "=?",
        new String[] {SEED_DIR, SEED_NAME},
        null)) {
      if (c != null && c.moveToFirst()) return; // seeded by an earlier run on this emulator
    }
    ContentValues values = new ContentValues();
    values.put(MediaStore.Audio.Media.DISPLAY_NAME, SEED_NAME);
    values.put(MediaStore.Audio.Media.MIME_TYPE, "audio/x-wav");
    values.put(MediaStore.Audio.Media.RELATIVE_PATH, SEED_DIR);
    values.put(MediaStore.Audio.Media.IS_MUSIC, 1);
    values.put(MediaStore.Audio.Media.IS_PENDING, 1);
    Uri item = resolver.insert(collection, values);
    if (item == null) throw new IllegalStateException("MediaStore refused the seed file");
    try (OutputStream out = resolver.openOutputStream(item)) {
      if (out == null) throw new IllegalStateException("no stream for the seed file");
      out.write(wav());
    } catch (IOException e) {
      throw new IllegalStateException("could not write the seed file", e);
    }
    values.clear();
    values.put(MediaStore.Audio.Media.IS_PENDING, 0);
    resolver.update(item, values, null, null);
  }

  /** One second of silence, 8 kHz mono 16-bit PCM, as a plain RIFF/WAVE file. */
  static byte[] wav() {
    int rate = 8000, channels = 1, bits = 16;
    int data = rate * channels * bits / 8;
    ByteBuffer b = ByteBuffer.allocate(44 + data).order(ByteOrder.LITTLE_ENDIAN);
    b.put("RIFF".getBytes()).putInt(36 + data).put("WAVE".getBytes());
    b.put("fmt ".getBytes()).putInt(16).putShort((short) 1).putShort((short) channels)
        .putInt(rate).putInt(rate * channels * bits / 8).putShort((short) (channels * bits / 8))
        .putShort((short) bits);
    b.put("data".getBytes()).putInt(data);
    return b.array();
  }
}
