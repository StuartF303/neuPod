// SPDX-License-Identifier: MPL-2.0

import XCTest
import integration_test

// Runs the Dart integration test the app was built with (flutter build ios --config-only <file>) and reports each
// Dart test as an XCTest assertion, so `xcodebuild test` passes or fails with the Dart tests.
final class RunnerTests: XCTestCase {
  func testIntegrationTests() {
    var results = 0
    FLTIntegrationTestRunner().testIntegrationTest { testSelector, success, failureMessage in
      results += 1
      XCTAssertTrue(success, "\(NSStringFromSelector(testSelector)): \(failureMessage ?? "failed")")
    }
    XCTAssertGreaterThan(results, 0, "the Dart integration test reported no results")
  }
}
