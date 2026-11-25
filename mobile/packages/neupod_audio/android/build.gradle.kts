// SPDX-License-Identifier: MPL-2.0

group = "net.stuartfraser.neupod.neupod_audio"
version = "0.1.0"

buildscript {
    val kotlinVersion = "2.4.20"
    repositories {
        google()
        mavenCentral()
    }

    dependencies {
        classpath("com.android.tools.build:gradle:9.3.1")
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:$kotlinVersion")
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

plugins {
    id("com.android.library")
}

android {
    namespace = "net.stuartfraser.neupod.neupod_audio"

    compileSdk = 36

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs("src/main/kotlin")
        }
        getByName("test") {
            java.srcDirs("src/test/kotlin")
        }
    }

    defaultConfig {
        // The app's minimum (Android 10, docs/app/ENVIRONMENT.md); Media3 itself needs 23.
        minSdk = 29
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

// Media3 (Apache-2.0): only the player and the session. No HTTP module (media3-datasource-okhttp,
// -cronet) is added. media3-exoplayer's own DefaultHttpDataSource is linked but never reached: the
// engine builds its data sources without it (LocalDataSource.kt).
val media3Version = "1.11.1"
val coroutinesVersion = "1.11.0"

dependencies {
    implementation("androidx.media3:media3-exoplayer:$media3Version")
    implementation("androidx.media3:media3-session:$media3Version")
    // Pigeon's generated host API uses coroutines; the guava artefact awaits Media3's ListenableFutures.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:$coroutinesVersion")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-guava:$coroutinesVersion")

    // JVM unit tests (./gradlew :neupod_audio:testDebugUnitTest from mobile/android).
    testImplementation("org.jetbrains.kotlin:kotlin-test-junit:2.4.20")
}
