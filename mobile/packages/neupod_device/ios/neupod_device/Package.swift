// swift-tools-version: 5.9
// SPDX-License-Identifier: MPL-2.0
// From the Flutter 3.50 plugin template. No resources: the plugin uses no required-reason APIs,
// so it ships no privacy manifest.

import PackageDescription

let package = Package(
    name: "neupod_device",
    platforms: [
        .iOS("18.0")
    ],
    products: [
        .library(name: "neupod-device", targets: ["neupod_device"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework")
    ],
    targets: [
        .target(
            name: "neupod_device",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework")
            ]
        )
    ]
)
