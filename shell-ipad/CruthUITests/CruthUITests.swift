// SPDX-License-Identifier: MIT
import XCTest

/// Smoke tests for the main flows, driven with synthesised finger touches in the
/// simulator. Every test launches the app fresh on the default 6,146-vertex sphere.
///
/// Geometry the coordinates rely on (landscape, see setUp): the sphere is framed
/// to about 86% of the viewport height around the centre, the brush palette sits
/// at the left edge around the vertical middle, the side panel at the right edge,
/// the toolbar at the top and the status bar at the bottom. So the centre band
/// is on the model and (0.1, 0.88) is empty background.
final class CruthUITests: XCTestCase {
    private var app: XCUIApplication!

    private let initialVertices = 6_146
    private let backgroundCorner = CGVector(dx: 0.1, dy: 0.88)

    /// Each test autosaves into its own empty folder, so no test restores another's sculpt.
    private var autosaveDirectory: URL!
    private var autosaveFile: URL { autosaveDirectory.appendingPathComponent("autosave.ply") }

    override func setUpWithError() throws {
        continueAfterFailure = false
        XCUIDevice.shared.orientation = .landscapeLeft
        autosaveDirectory = FileManager.default.temporaryDirectory
            .appendingPathComponent("cruth-autosave-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: autosaveDirectory, withIntermediateDirectories: true)
        launchApp()
        waitForVertexCount(initialVertices, timeout: 30)
    }

    override func tearDownWithError() throws {
        if let autosaveDirectory { try? FileManager.default.removeItem(at: autosaveDirectory) }
    }

    /// `autosaveDelay` sets the app's debug-only interval between edit-driven autosaves.
    private func launchApp(autosaveDelay: TimeInterval? = nil, restoreConfirmDelay: TimeInterval? = nil) {
        app = XCUIApplication()
        // Pin the number format ("6,146"), the default (right-handed) layout and display.
        app.launchArguments += ["-AppleLanguages", "(en)", "-AppleLocale", "en_US", "-leftHanded", "NO",
                                "-displayShading", "lit", "-displayWireframe", "NO",
                                "-cruthAutosavePath", autosaveDirectory.path]
        if let autosaveDelay { app.launchArguments += ["-cruthAutosaveDelay", "\(autosaveDelay)"] }
        if let restoreConfirmDelay { app.launchArguments += ["-cruthRestoreConfirmDelay", "\(restoreConfirmDelay)"] }
        app.launch()
        XCTAssertTrue(statusCounts.waitForExistence(timeout: 30), "status bar never appeared")
    }

    // MARK: Tests

    func testLaunchShowsViewportAndDefaultSphere() {
        XCTAssertTrue(viewport.exists, "viewport is not in the accessibility tree")
        XCTAssertGreaterThan(viewport.frame.width, 500)
        XCTAssertEqual(vertexCount(), initialVertices)
        XCTAssertEqual(triangleCount(), 12_288)
        XCTAssertFalse(undoButton.isEnabled)
        XCTAssertFalse(redoButton.isEnabled)
    }

    func testFingerDragWithoutFingerSculptingDoesNotSculpt() {
        // Default: fingers navigate. A drag across the model orbits.
        drag(from: CGVector(dx: 0.42, dy: 0.5), to: CGVector(dx: 0.58, dy: 0.5))
        XCTAssertFalse(undoButton.isEnabled, "a finger drag sculpted with Finger Sculpting off")
    }

    func testFingerSculptStrokeIsUndoable() {
        setFingerSculpting(true)
        drag(from: CGVector(dx: 0.42, dy: 0.5), to: CGVector(dx: 0.58, dy: 0.5))
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger stroke on the model did not record an undo step")
        XCTAssertEqual(vertexCount(), initialVertices, "a sculpt stroke must not change topology")

        undoButton.tap()
        waitFor(undoButton, "isEnabled == false", timeout: 10, "undo did not return to the initial state")
        XCTAssertTrue(redoButton.isEnabled, "undoing the stroke should make it redoable")
        XCTAssertEqual(vertexCount(), initialVertices)
    }

    func testFingerOnBackgroundOrbitsInsteadOfSculpting() {
        setFingerSculpting(true)
        // Starts on the background and sweeps over the model: still an orbit.
        drag(from: backgroundCorner, to: CGVector(dx: 0.5, dy: 0.5))
        XCTAssertFalse(undoButton.isEnabled, "a finger that landed on the background started a stroke")

        // Control: the same finger landing on the model does sculpt, so the
        // assertion above is not vacuous.
        drag(from: CGVector(dx: 0.45, dy: 0.45), to: CGVector(dx: 0.55, dy: 0.55))
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger stroke on the model did not record an undo step")
    }

    func testSubdivideAndUndo() {
        runTopologyCommand(["Subdivide (Add Level)"], timeout: 60)
    }

    func testVoxelRemeshAndUndo() {
        runTopologyCommand(["Voxel Remesh", "64 voxels"], timeout: 120)
    }

    func testDecimateAndUndo() {
        runTopologyCommand(["Decimate", "Keep 50%"], timeout: 120)
    }

    func testQuadRemeshAndUndo() {
        // With the wireframe on, so the remeshed quads' edge buffer is drawn too.
        setWireframe(true)
        runTopologyCommand(["Quad Remesh", "2,000 quads"], timeout: 600) {
            XCTAssertEqual(app.state, .runningForeground)
            let wired = viewportPixels()
            setWireframe(false)
            XCTAssertGreaterThan(waitForViewportChange(from: wired), 0.002, "the wireframe did not draw after quad remesh")
            setWireframe(true)
        }
    }

    func testGuideStrokesAreRecordedAndCleared() {
        // A finger draws guides only with Finger Sculpting on (otherwise only the
        // Pencil does), and that toggle lives in the Sculpt panel.
        setFingerSculpting(true)
        selectMode("Guides")
        let count = app.staticTexts["guideCount"]
        let clear = app.buttons["clearGuidesButton"]
        XCTAssertTrue(count.waitForExistence(timeout: 10))
        XCTAssertEqual(count.label, "0 strokes")
        XCTAssertFalse(clear.isEnabled)

        drag(from: CGVector(dx: 0.42, dy: 0.45), to: CGVector(dx: 0.58, dy: 0.55))
        waitFor(count, "label == '1 strokes'", timeout: 10, "guide stroke was not recorded")
        XCTAssertTrue(clear.isEnabled)
        XCTAssertFalse(undoButton.isEnabled, "guides are not document edits")

        drag(from: CGVector(dx: 0.5, dy: 0.38), to: CGVector(dx: 0.5, dy: 0.62))
        waitFor(count, "label == '2 strokes'", timeout: 10, "second guide stroke was not recorded")

        clear.tap()
        waitFor(count, "label == '0 strokes'", timeout: 10, "Clear Guides did not clear")
        XCTAssertFalse(clear.isEnabled)
    }

    func testRetopoContoursBridgeIntoQuads() {
        setFingerSculpting(true)
        selectMode("Retopo")
        let stats = app.staticTexts["retopoStats"]
        XCTAssertTrue(stats.waitForExistence(timeout: 10))
        XCTAssertEqual(retopoQuads(stats), 0)

        // One contour is a lone edge loop (no faces, so the stats line cannot
        // show it); the second bridges to it with one quad per loop vertex (12).
        drag(from: CGVector(dx: 0.4, dy: 0.42), to: CGVector(dx: 0.6, dy: 0.42))
        drag(from: CGVector(dx: 0.4, dy: 0.58), to: CGVector(dx: 0.6, dy: 0.58))
        waitFor(stats, "label BEGINSWITH '12 quads'", timeout: 10,
                "two contour strokes did not bridge into 12 quads")

        app.buttons["cancelRetopoButton"].tap()
        XCTAssertTrue(app.switches["fingerSculptingToggle"].waitForExistence(timeout: 10),
                      "Cancel did not return to Sculpt mode")
        XCTAssertEqual(vertexCount(), initialVertices)
        XCTAssertFalse(undoButton.isEnabled)
    }

    func testRetopoStripApplyAndUndo() {
        setFingerSculpting(true)
        selectMode("Retopo")
        let stats = app.staticTexts["retopoStats"]
        XCTAssertTrue(stats.waitForExistence(timeout: 10))
        selectRetopoTool("strip")

        // Default strip: 6 segments, so one stroke adds 6 quads.
        drag(from: CGVector(dx: 0.4, dy: 0.5), to: CGVector(dx: 0.6, dy: 0.5))
        waitFor(stats, "label BEGINSWITH '6 quads'", timeout: 10, "strip stroke did not add 6 quads")

        let before = statusCounts.label
        app.buttons["applyRetopoButton"].tap()
        waitFor(statusCounts, "label != %@", before, timeout: 120, "Apply did not replace the mesh")
        XCTAssertTrue(app.switches["fingerSculptingToggle"].waitForExistence(timeout: 10),
                      "Apply did not return to Sculpt mode")
        XCTAssertNotEqual(vertexCount(), initialVertices)

        undo(backTo: initialVertices)
    }

    func testRetopoQuadPenTapsMakeAQuadAndUndo() {
        let stats = openRetopo(tool: "quadPen")
        let pending = app.staticTexts["quadPenPending"]
        let lift = app.buttons["retopoLiftButton"]
        XCTAssertTrue(pending.waitForExistence(timeout: 10))
        XCTAssertFalse(lift.isEnabled, "Lift is enabled with no taps pending")

        // Four taps round a square on the front of the sphere; any order closes it.
        let corners = [CGVector(dx: 0.45, dy: 0.42), CGVector(dx: 0.55, dy: 0.42),
                       CGVector(dx: 0.55, dy: 0.58), CGVector(dx: 0.45, dy: 0.58)]
        for (i, corner) in corners.enumerated() {
            tap(corner)
            if i < 3 {
                waitFor(pending, "label BEGINSWITH %@", i == 0 ? "1 tap " : "\(i + 1) taps", timeout: 10,
                        "tap \(i + 1) was not recorded (message: \(statusMessage()))")
            }
        }
        waitFor(stats, "label BEGINSWITH '1 quads'", timeout: 10,
                "four quad-pen taps did not make a quad (message: \(statusMessage()))")
        // The chain carries on from the quad's last edge until Lift.
        XCTAssertTrue(lift.isEnabled, "the chain should leave the shared edge pending")
        lift.tap()
        waitFor(pending, "label BEGINSWITH '0 taps'", timeout: 10, "Lift did not end the chain")
        XCTAssertFalse(lift.isEnabled)

        // Toolbar undo steps the cage, not the document.
        XCTAssertTrue(undoButton.isEnabled, "the quad did not record a cage undo step")
        undoButton.tap()
        waitFor(stats, "label BEGINSWITH '0 quads'", timeout: 10, "undo did not remove the quad")
        XCTAssertFalse(undoButton.isEnabled)
        XCTAssertTrue(redoButton.isEnabled, "undoing the quad should make it redoable")
        XCTAssertEqual(vertexCount(), initialVertices, "cage undo touched the document")
    }

    func testRetopoSlideLoopIsOneCageUndoStep() {
        let stats = openRetopo(tool: "contour")
        drag(from: CGVector(dx: 0.4, dy: 0.42), to: CGVector(dx: 0.6, dy: 0.42))
        drag(from: CGVector(dx: 0.4, dy: 0.58), to: CGVector(dx: 0.6, dy: 0.58))
        waitFor(stats, "label BEGINSWITH '12 quads'", timeout: 10, "two contours did not bridge into 12 quads")

        // Drag from the first loop toward the second: the loop moves, the counts do not.
        selectRetopoTool("slideLoop")
        drag(from: CGVector(dx: 0.5, dy: 0.42), to: CGVector(dx: 0.5, dy: 0.48))
        XCTAssertTrue(stats.label.hasPrefix("12 quads"), "sliding changed the topology: \(stats.label)")
        XCTAssertFalse(redoButton.isEnabled)

        // The slide is its own undo step: undoing it keeps both contours.
        undoButton.tap()
        waitFor(redoButton, "isEnabled == true", timeout: 10, "undo after the slide did nothing")
        XCTAssertTrue(stats.label.hasPrefix("12 quads"),
                      "undo removed a contour, so the slide recorded no undo step (message: \(statusMessage()))")
        XCTAssertTrue(undoButton.isEnabled)
        undoButton.tap()
        waitFor(stats, "label BEGINSWITH '0 quads'", timeout: 10, "second undo did not remove the bridging contour")
    }

    func testRetopoInsertLoopAddsQuadsToAStrip() {
        let stats = openRetopo(tool: "strip")
        drag(from: CGVector(dx: 0.4, dy: 0.5), to: CGVector(dx: 0.6, dy: 0.5))
        waitFor(stats, "label BEGINSWITH '6 quads'", timeout: 10, "strip stroke did not add 6 quads")

        selectRetopoTool("insertLoop")
        tap(CGVector(dx: 0.5, dy: 0.5))
        waitFor(stats, "NOT (label BEGINSWITH '6 quads')", timeout: 10,
                "Insert Loop did not change the cage (message: \(statusMessage()))")
        XCTAssertGreaterThan(retopoQuads(stats), 6)

        undoButton.tap()
        waitFor(stats, "label BEGINSWITH '6 quads'", timeout: 10, "undo did not remove the inserted loop")
    }

    func testRetopoLoopAroundRefusesAShortStrokeAndTheMessageClears() {
        let stats = openRetopo(tool: "contour")
        drag(from: CGVector(dx: 0.4, dy: 0.42), to: CGVector(dx: 0.6, dy: 0.42))
        drag(from: CGVector(dx: 0.4, dy: 0.58), to: CGVector(dx: 0.6, dy: 0.58))
        waitFor(stats, "label BEGINSWITH '12 quads'", timeout: 10, "two contours did not bridge into 12 quads")

        // A straight drag is all XCUITest can draw. The core joins its ends, and a
        // short one across the band encloses too few faces, so it is refused.
        selectRetopoTool("loopAround")
        drag(from: CGVector(dx: 0.45, dy: 0.5), to: CGVector(dx: 0.55, dy: 0.5))
        let message = app.staticTexts["statusMessage"]
        XCTAssertTrue(message.waitForExistence(timeout: 10), "Loop Around reported nothing")
        XCTAssertEqual(message.label, "loop around: the stroke must go round at least three faces")
        XCTAssertEqual(retopoQuads(stats), 12, "a refused Loop Around changed the cage")
        // The refusal is a flash, gone after about 4 s.
        waitFor(message, "exists == false", timeout: 6, "the refusal message did not clear")
    }

    func testTwoFingerTapUndoesInQuadPen() {
        let stats = openRetopo(tool: "strip")
        drag(from: CGVector(dx: 0.4, dy: 0.5), to: CGVector(dx: 0.6, dy: 0.5))
        waitFor(stats, "label BEGINSWITH '6 quads'", timeout: 10, "strip stroke did not add 6 quads")

        // Both fingers land on the model: the tap undoes the strip and places no corner.
        selectRetopoTool("quadPen")
        viewport.twoFingerTap()
        waitFor(stats, "label BEGINSWITH '0 quads'", timeout: 10, "two-finger tap did not undo the strip")
        XCTAssertTrue(app.staticTexts["quadPenPending"].label.hasPrefix("0 taps"),
                      "two-finger tap placed a quad-pen corner: \(app.staticTexts["quadPenPending"].label)")
    }

    func testTwoFingerTapUndoesInInsertLoop() {
        let stats = openRetopo(tool: "strip")
        drag(from: CGVector(dx: 0.4, dy: 0.5), to: CGVector(dx: 0.6, dy: 0.5))
        waitFor(stats, "label BEGINSWITH '6 quads'", timeout: 10, "strip stroke did not add 6 quads")

        selectRetopoTool("insertLoop")
        viewport.twoFingerTap()
        waitFor(stats, "label BEGINSWITH '0 quads'", timeout: 10,
                "two-finger tap did not undo the strip (message: \(statusMessage()))")
    }

    func testPrintCheckReportsTheSphereAsPrintable() {
        app.buttons["printCheckButton"].tap()
        let status = app.staticTexts["printStatus"]
        XCTAssertTrue(status.waitForExistence(timeout: 30), "Print Check sheet did not open")
        XCTAssertEqual(status.label, "Ready to print")
        // LabeledContent reads as "title, value".
        XCTAssertEqual(app.staticTexts["printCheck.Vertices"].label, "Vertices, 6,146")
        XCTAssertEqual(app.staticTexts["printCheck.Holes"].label, "Holes, 0")
        app.buttons["printCheckDone"].tap()
        waitFor(status, "exists == false", timeout: 10, "Done did not close Print Check")
    }

    // MARK: Autosave

    func testSculptSurvivesBackgroundAndRelaunch() {
        // No edit-driven save during the test: the only save is the one leaving the
        // foreground makes, so the file appearing means that save has finished.
        app.terminate()
        launchApp(autosaveDelay: 3600)
        waitForVertexCount(initialVertices, timeout: 30)
        XCTAssertFalse(FileManager.default.fileExists(atPath: autosaveFile.path), "an unedited sphere was autosaved")

        let freshVolume = printCheckVolume()
        setFingerSculpting(true)
        for dy in [0.45, 0.5, 0.55] {
            drag(from: CGVector(dx: 0.42, dy: dy), to: CGVector(dx: 0.58, dy: dy))
        }
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger strokes did not record an undo step")
        let sculptedVolume = printCheckVolume()
        XCTAssertNotEqual(sculptedVolume, freshVolume, "the strokes did not change the volume, so it proves nothing")

        // Leaving the foreground saves; the file appears once the save has been swapped in whole.
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(waitForFile(autosaveFile, timeout: 20), "backgrounding did not write \(autosaveFile.path)")
        app.terminate()

        launchApp()
        let message = app.staticTexts["statusMessage"]
        XCTAssertTrue(message.waitForExistence(timeout: 30), "no restore message after relaunch")
        XCTAssertTrue(message.label.hasPrefix("Restored your last session"), "unexpected message: \(message.label)")
        XCTAssertEqual(vertexCount(), initialVertices)
        XCTAssertFalse(undoButton.isEnabled, "a restored session starts with no undo history")
        XCTAssertEqual(printCheckVolume(), sculptedVolume, "the restored sculpt is not the one that was saved")
    }

    func testEditsAutosaveWhileTheAppStaysOpen() {
        app.terminate()
        launchApp(autosaveDelay: 1)
        waitForVertexCount(initialVertices, timeout: 30)
        setFingerSculpting(true)
        drag(from: CGVector(dx: 0.42, dy: 0.5), to: CGVector(dx: 0.58, dy: 0.5))
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger stroke did not record an undo step")
        XCTAssertTrue(waitForFile(autosaveFile, timeout: 15), "a stroke was not autosaved within the delay")
    }

    func testInterruptedRestoreSetsAsideOnlyThatAutosave() throws {
        let marker = autosaveDirectory.appendingPathComponent("restoring")
        sculptAndBackground()
        app.terminate()
        // As if the last launch died while loading or drawing this autosave.
        try Data().write(to: marker)

        launchApp()
        waitForVertexCount(initialVertices, timeout: 30)
        let message = app.staticTexts["statusMessage"]
        XCTAssertTrue(message.waitForExistence(timeout: 10), "the interrupted restore was not reported")
        XCTAssertEqual(message.label, "The last session could not be restored and was set aside")
        XCTAssertFalse(FileManager.default.fileExists(atPath: marker.path), "the stale restore marker was kept")

        // A fresh autosave made after that must restore on the next launch.
        sculptAndBackground()
        app.terminate()
        launchApp()
        let restored = app.staticTexts["statusMessage"]
        XCTAssertTrue(restored.waitForExistence(timeout: 30), "no message after the second relaunch")
        XCTAssertTrue(restored.label.hasPrefix("Restored your last session"), "unexpected message: \(restored.label)")
        XCTAssertTrue(waitForFile(marker, toExist: false, timeout: 15),
                      "the restore was never confirmed, so the next launch would set it aside")
    }

    func testLeavingRightAfterARestoreKeepsTheAutosave() {
        let marker = autosaveDirectory.appendingPathComponent("restoring")
        sculptAndBackground()
        app.terminate()

        // Restore, then leave while the restore still waits after its first frame
        // (stretched to 10 minutes here, so only leaving can remove the marker).
        launchApp(restoreConfirmDelay: 600)
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(waitForFile(marker, toExist: false, timeout: 10),
                      "leaving the foreground kept the restore marker, so a background kill would set the autosave aside")
        app.terminate()

        launchApp()
        let restored = app.staticTexts["statusMessage"]
        XCTAssertTrue(restored.waitForExistence(timeout: 30), "no message after relaunch")
        XCTAssertTrue(restored.label.hasPrefix("Restored your last session"), "unexpected message: \(restored.label)")
    }

    func testCorruptAutosaveStartsOnTheDefaultSphere() throws {
        app.terminate()
        // A truncated PLY: the header promises 1,000 vertices, the body holds four bytes.
        let header = "ply\nformat binary_little_endian 1.0\nelement vertex 1000\n" +
            "property float x\nproperty float y\nproperty float z\nend_header\n"
        try (Data(header.utf8) + Data([1, 2, 3, 4])).write(to: autosaveFile)

        launchApp()
        waitForVertexCount(initialVertices, timeout: 30)
        XCTAssertEqual(triangleCount(), 12_288)
        XCTAssertFalse(undoButton.isEnabled)
        let message = app.staticTexts["statusMessage"]
        XCTAssertTrue(message.waitForExistence(timeout: 10), "the failed restore was not reported")
        XCTAssertEqual(message.label, "The last session could not be restored and was set aside")
        XCTAssertFalse(FileManager.default.fileExists(atPath: autosaveFile.path), "the broken autosave was left in place")
        let names = try FileManager.default.contentsOfDirectory(atPath: autosaveDirectory.path)
        XCTAssertTrue(names.contains { $0.hasPrefix("broken-") && $0.hasSuffix(".ply") },
                      "the broken autosave was not kept aside: \(names)")
    }

    func testNewSculptAsksThenResetsAndClearsTheAutosave() {
        setFingerSculpting(true)
        drag(from: CGVector(dx: 0.42, dy: 0.5), to: CGVector(dx: 0.58, dy: 0.5))
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger stroke did not record an undo step")
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(waitForFile(autosaveFile, timeout: 20), "backgrounding did not write the autosave")
        app.activate()
        XCTAssertTrue(statusCounts.waitForExistence(timeout: 30))

        openNewSculpt().buttons["Cancel"].firstMatch.tap()
        XCTAssertTrue(undoButton.isEnabled, "cancelling New Sculpt reset the document")
        XCTAssertTrue(FileManager.default.fileExists(atPath: autosaveFile.path), "cancelling New Sculpt removed the autosave")

        openNewSculpt().buttons["newSculptConfirmButton"].firstMatch.tap()
        waitFor(undoButton, "isEnabled == false", timeout: 10, "New Sculpt did not reset the document")
        XCTAssertEqual(vertexCount(), initialVertices)
        XCTAssertFalse(FileManager.default.fileExists(atPath: autosaveFile.path), "New Sculpt left the autosave behind")
    }

    // MARK: Display

    func testDisplayModesAndWireframeChangeTheViewport() {
        // A coarse mesh, so Flat's faceting shows clearly against Lit.
        let before = statusCounts.label
        app.buttons["topologyMenu"].tap()
        for item in ["Decimate", "Keep 10%"] {
            let button = app.buttons[item].firstMatch
            XCTAssertTrue(button.waitForExistence(timeout: 10), "menu item '\(item)' not found")
            button.tap()
        }
        waitFor(statusCounts, "label != %@", before, timeout: 120, "Decimate did not change the mesh")
        let lit = viewportPixels()

        selectShading("normals")
        XCTAssertGreaterThan(waitForViewportChange(from: lit), 0.05, "Normals looks like Lit")
        selectShading("flat")
        XCTAssertGreaterThan(waitForViewportChange(from: lit), 0.0025, "Flat looks like Lit")
        selectShading("lit")
        XCTAssertLessThan(waitForViewportMatch(lit), 0.002, "Lit did not come back unchanged")
        setWireframe(true)
        XCTAssertGreaterThan(waitForViewportChange(from: lit), 0.0025, "the wireframe did not draw over Lit")
        setWireframe(false)
        XCTAssertLessThan(waitForViewportMatch(lit), 0.002, "wireframe off did not clear it")

        selectShading("xray")
        XCTAssertGreaterThan(waitForViewportChange(from: lit), 0.05, "X-Ray looks like Lit")
        let xray = viewportPixels()
        setWireframe(true)
        XCTAssertGreaterThan(waitForViewportChange(from: xray), 0.0025, "the wireframe did not draw over X-Ray")
        setWireframe(false)
        XCTAssertLessThan(waitForViewportMatch(xray), 0.002, "wireframe off did not clear X-Ray")

        // Display stays available outside Sculpt mode.
        selectMode("Retopo")
        selectShading("normals")
        XCTAssertGreaterThan(waitForViewportChange(from: lit), 0.05, "Normals did not apply in Retopo")
    }

    // MARK: Diagnostics

    func testShareDiagnosticsOpensTheShareSheet() {
        app.buttons["cruthMenu"].firstMatch.tap()
        let item = app.buttons["shareDiagnosticsButton"].firstMatch
        XCTAssertTrue(item.waitForExistence(timeout: 10), "Share Diagnostics not in the Cruth menu")
        item.tap()
        let sheet = app.otherElements["ActivityListView"].firstMatch
        XCTAssertTrue(sheet.waitForExistence(timeout: 15), "no share sheet: \(app.debugDescription)")
    }

    // MARK: Elements

    private var viewport: XCUIElement { app.descendants(matching: .any)["viewport"].firstMatch }
    private var statusCounts: XCUIElement { app.staticTexts["statusCounts"] }
    private var undoButton: XCUIElement { app.buttons["undoButton"] }
    private var redoButton: XCUIElement { app.buttons["redoButton"] }

    // MARK: Helpers

    /// Opens the Topology menu, follows `path` through its submenus, waits for
    /// the vertex count to change, then undoes back to the sphere.
    private func runTopologyCommand(_ path: [String], timeout: TimeInterval,
                                    file: StaticString = #filePath, line: UInt = #line,
                                    whileChanged: () -> Void = {}) {
        let before = statusCounts.label
        app.buttons["topologyMenu"].tap()
        for item in path {
            let button = app.buttons[item].firstMatch
            XCTAssertTrue(button.waitForExistence(timeout: 10), "menu item '\(item)' not found", file: file, line: line)
            button.tap()
        }
        waitFor(statusCounts, "label != %@", before, timeout: timeout,
                "\(path.joined(separator: " > ")) did not change the mesh (message: \(statusMessage()))",
                file: file, line: line)
        XCTAssertNotEqual(vertexCount(), initialVertices, file: file, line: line)
        XCTAssertTrue(undoButton.isEnabled, file: file, line: line)
        whileChanged()
        undo(backTo: initialVertices, file: file, line: line)
    }

    /// Display menu > a shading mode, by its raw value ("lit", "xray", ...).
    private func selectShading(_ mode: String, file: StaticString = #filePath, line: UInt = #line) {
        app.buttons["displayMenu"].firstMatch.tap()
        let item = app.buttons["shading.\(mode)"].firstMatch
        XCTAssertTrue(item.waitForExistence(timeout: 10), "shading '\(mode)' not in the Display menu",
                      file: file, line: line)
        item.tap()
        waitFor(item, "exists == false", timeout: 5, "the Display menu did not close", file: file, line: line)
    }

    private func setWireframe(_ on: Bool, file: StaticString = #filePath, line: UInt = #line) {
        app.buttons["displayMenu"].firstMatch.tap()
        let toggle = app.descendants(matching: .any)["wireframeToggle"].firstMatch
        XCTAssertTrue(toggle.waitForExistence(timeout: 10), "Wireframe not in the Display menu", file: file, line: line)
        let isOn = (toggle.value as? String) == "1" || toggle.isSelected
        if isOn != on {
            toggle.tap()
        } else {
            app.buttons["displayMenu"].firstMatch.tap()  // close the menu unchanged
        }
        waitFor(toggle, "exists == false", timeout: 5, "the Display menu did not close", file: file, line: line)
    }

    /// The centre of the viewport (on the model, clear of the panels) as a
    /// small RGBA thumbnail, so screenshots can be compared cheaply.
    private func viewportPixels() -> [UInt8] {
        let side = 128
        var pixels = [UInt8](repeating: 0, count: side * side * 4)
        guard let image = viewport.screenshot().image.cgImage else { return pixels }
        let crop = CGRect(x: CGFloat(image.width) * 0.35, y: CGFloat(image.height) * 0.3,
                          width: CGFloat(image.width) * 0.3, height: CGFloat(image.height) * 0.4)
        guard let centre = image.cropping(to: crop.integral) else { return pixels }
        pixels.withUnsafeMutableBytes { buffer in
            let context = CGContext(data: buffer.baseAddress, width: side, height: side, bitsPerComponent: 8,
                                    bytesPerRow: side * 4, space: CGColorSpaceCreateDeviceRGB(),
                                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
            context?.interpolationQuality = .medium
            context?.draw(centre, in: CGRect(x: 0, y: 0, width: side, height: side))
        }
        return pixels
    }

    /// Mean absolute channel difference, 0...1.
    private func difference(_ a: [UInt8], _ b: [UInt8]) -> Double {
        guard a.count == b.count, !a.isEmpty else { return 1 }
        let total = zip(a, b).reduce(0) { $0 + abs(Int($1.0) - Int($1.1)) }
        return Double(total) / Double(a.count) / 255
    }

    /// Waits for a frame that differs from `reference`; returns the last difference seen.
    private func waitForViewportChange(from reference: [UInt8], timeout: TimeInterval = 3) -> Double {
        waitForViewport(reference, timeout: timeout) { $0 > 0.002 }
    }

    /// Waits for a frame that matches `reference`; returns the last difference seen.
    private func waitForViewportMatch(_ reference: [UInt8], timeout: TimeInterval = 3) -> Double {
        waitForViewport(reference, timeout: timeout) { $0 <= 0.002 }
    }

    private func waitForViewport(_ reference: [UInt8], timeout: TimeInterval, until done: (Double) -> Bool) -> Double {
        let deadline = Date().addingTimeInterval(timeout)
        var last = 0.0
        repeat {
            last = difference(reference, viewportPixels())
            if done(last) { return last }
            Thread.sleep(forTimeInterval: 0.1)
        } while Date() < deadline
        return last
    }

    private func undo(backTo vertices: Int, file: StaticString = #filePath, line: UInt = #line) {
        undoButton.tap()
        waitForVertexCount(vertices, timeout: 30, file: file, line: line)
        XCTAssertFalse(undoButton.isEnabled, "undo stack not back to the initial state", file: file, line: line)
    }

    private func setFingerSculpting(_ on: Bool, file: StaticString = #filePath, line: UInt = #line) {
        let toggle = app.switches["fingerSculptingToggle"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 10), file: file, line: line)
        let want = on ? "1" : "0"
        if toggle.value as? String != want {
            // Tap the switch itself (trailing edge); the label half does not toggle.
            toggle.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
        }
        waitFor(toggle, "value == %@", want, timeout: 5, "Finger Sculpting toggle did not switch", file: file, line: line)
    }

    private func selectMode(_ title: String, file: StaticString = #filePath, line: UInt = #line) {
        let segment = app.segmentedControls["modePicker"].buttons[title]
        XCTAssertTrue(segment.waitForExistence(timeout: 10), "mode '\(title)' not found", file: file, line: line)
        segment.tap()
        waitFor(segment, "isSelected == true", timeout: 5, "mode '\(title)' not selected", file: file, line: line)
    }

    /// Finger input on, Retopo mode, the given tool; returns the cage stats line.
    private func openRetopo(tool: String, file: StaticString = #filePath, line: UInt = #line) -> XCUIElement {
        setFingerSculpting(true, file: file, line: line)
        selectMode("Retopo", file: file, line: line)
        let stats = app.staticTexts["retopoStats"]
        XCTAssertTrue(stats.waitForExistence(timeout: 10), file: file, line: line)
        XCTAssertEqual(retopoQuads(stats), 0, file: file, line: line)
        selectRetopoTool(tool, file: file, line: line)
        return stats
    }

    private func selectRetopoTool(_ tool: String, file: StaticString = #filePath, line: UInt = #line) {
        let button = app.buttons["retopoTool.\(tool)"]
        XCTAssertTrue(button.waitForExistence(timeout: 10), "retopo tool '\(tool)' not found", file: file, line: line)
        button.tap()
        waitFor(button, "isSelected == true", timeout: 5, "retopo tool '\(tool)' not selected", file: file, line: line)
    }

    /// A single finger tap on the viewport at a normalised point.
    private func tap(_ at: CGVector) {
        viewport.coordinate(withNormalizedOffset: at).tap()
    }

    /// A finger press-and-drag on the viewport between normalised points.
    private func drag(from: CGVector, to: CGVector) {
        let start = viewport.coordinate(withNormalizedOffset: from)
        let end = viewport.coordinate(withNormalizedOffset: to)
        start.press(forDuration: 0.1, thenDragTo: end, withVelocity: XCUIGestureVelocity(600), thenHoldForDuration: 0.1)
    }

    private func waitFor(_ element: XCUIElement, _ format: String, _ args: Any..., timeout: TimeInterval,
                         _ message: @autoclosure () -> String,
                         file: StaticString = #filePath, line: UInt = #line) {
        let expectation = XCTNSPredicateExpectation(predicate: NSPredicate(format: format, argumentArray: args),
                                                    object: element)
        let result = XCTWaiter().wait(for: [expectation], timeout: timeout)
        XCTAssertEqual(result, .completed, message(), file: file, line: line)
    }

    private func waitForVertexCount(_ count: Int, timeout: TimeInterval,
                                    file: StaticString = #filePath, line: UInt = #line) {
        let formatted = count.formatted(.number.locale(Locale(identifier: "en_US")))
        waitFor(statusCounts, "label BEGINSWITH %@", "\(formatted) vertices ", timeout: timeout,
                "expected \(formatted) vertices, status shows '\(statusCounts.label)'", file: file, line: line)
    }

    /// "6,146 vertices · 12,288 triangles" -> (6146, 12288)
    private func counts() -> [Int] {
        statusCounts.label.split(separator: "·").map { part in
            Int(part.filter(\.isNumber)) ?? -1
        }
    }

    private func vertexCount() -> Int { counts().first ?? -1 }
    private func triangleCount() -> Int { counts().dropFirst().first ?? -1 }

    /// "12 quads · 0 other · 0 poles" -> 12
    private func retopoQuads(_ stats: XCUIElement) -> Int {
        Int(stats.label.prefix { $0.isNumber }) ?? -1
    }

    /// One finger stroke, then Home; returns once the autosave file exists.
    private func sculptAndBackground(file: StaticString = #filePath, line: UInt = #line) {
        setFingerSculpting(true, file: file, line: line)
        drag(from: CGVector(dx: 0.42, dy: 0.5), to: CGVector(dx: 0.58, dy: 0.5))
        waitFor(undoButton, "isEnabled == true", timeout: 10, "finger stroke did not record an undo step",
                file: file, line: line)
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(waitForFile(autosaveFile, timeout: 20), "backgrounding did not write the autosave",
                      file: file, line: line)
    }

    /// Opens Print Check, reads the volume ("Volume, 4.18") and closes it again.
    private func printCheckVolume(file: StaticString = #filePath, line: UInt = #line) -> String {
        app.buttons["printCheckButton"].tap()
        let volume = app.staticTexts["printCheck.Volume"]
        XCTAssertTrue(volume.waitForExistence(timeout: 30), "Print Check sheet did not open", file: file, line: line)
        let label = volume.label
        app.buttons["printCheckDone"].tap()
        waitFor(volume, "exists == false", timeout: 10, "Done did not close Print Check", file: file, line: line)
        return label
    }

    /// Cruth menu > New Sculpt; returns the confirmation alert.
    private func openNewSculpt(file: StaticString = #filePath, line: UInt = #line) -> XCUIElement {
        app.buttons["cruthMenu"].firstMatch.tap()
        let item = app.buttons["newSculptButton"].firstMatch
        XCTAssertTrue(item.waitForExistence(timeout: 10), "New Sculpt not in the Cruth menu", file: file, line: line)
        item.tap()
        let alert = app.alerts.firstMatch
        XCTAssertTrue(alert.waitForExistence(timeout: 10), "New Sculpt did not ask first", file: file, line: line)
        return alert
    }

    /// The test runner and the simulated app share the Mac's file system, so the
    /// test can watch the app's autosave folder directly.
    private func waitForFile(_ url: URL, toExist: Bool = true, timeout: TimeInterval) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if FileManager.default.fileExists(atPath: url.path) == toExist { return true }
            Thread.sleep(forTimeInterval: 0.25)
        }
        return false
    }

    private func statusMessage() -> String {
        let message = app.staticTexts["statusMessage"]
        return message.exists ? message.label : "none"
    }
}
