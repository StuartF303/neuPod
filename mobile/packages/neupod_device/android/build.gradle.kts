// SPDX-License-Identifier: MPL-2.0
// From the Flutter 3.50 plugin template, trimmed: no unit-test set-up (the
// plugin's logic is tested in Dart; the platform calls are checked on a
// device, docs/app/decisions/0007-haptics.md).

group = "net.stuartfraser.neupod_device"
version = "1.0"

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
    namespace = "net.stuartfraser.neupod_device"

    compileSdk = 36

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs("src/main/kotlin")
        }
    }

    defaultConfig {
        minSdk = 29
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

dependencies {
    // Pigeon 30's generated code runs @async host methods as coroutines.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
}
