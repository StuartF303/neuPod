// swift-tools-version: 5.9
// SPDX-License-Identifier: MPL-2.0

import PackageDescription

let package = Package(
    name: "neupod_audio",
    platforms: [
        // The app's minimum (docs/app/ENVIRONMENT.md).
        .iOS("18.0")
    ],
    products: [
        .library(name: "neupod-audio", targets: ["neupod_audio"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework")
    ],
    targets: [
        .target(
            name: "neupod_audio",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework")
            ],
            resources: [
                // File timestamp API (lstat, LocalFile.swift); no tracking, no data collected.
                .process("PrivacyInfo.xcprivacy")
            ],
            linkerSettings: [
                .linkedFramework("AVFoundation"),
                .linkedFramework("MediaPlayer"),
            ]
        )
    ]
)
