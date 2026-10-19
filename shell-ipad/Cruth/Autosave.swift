// SPDX-License-Identifier: MIT
import Foundation
import UIKit

/// Asks iPadOS for time to finish a save after the app leaves the foreground.
final class BackgroundTask {
    private var id = UIBackgroundTaskIdentifier.invalid

    init(name: String) {
        id = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in self?.end() }
    }

    func end() {
        guard id != .invalid else { return }
        UIApplication.shared.endBackgroundTask(id)
        id = .invalid
    }
}

struct AutosaveError: Error, CustomStringConvertible {
    let description: String
}

/// The autosave file: the current multires level's mesh as binary PLY (float32
/// positions, so lossless), written through cruth_document_save. It lives in
/// Application Support/Autosave, which is excluded from iCloud backup.
///
/// Debug builds honour `-cruthAutosavePath <dir>` so UI tests can isolate
/// themselves and seed broken files, and `-cruthAutosaveDelay` (below).
struct AutosaveStore {
    let directory: URL

    var file: URL { directory.appendingPathComponent("autosave.ply") }
    /// Written before a restore loads and removed by `confirmRestore` once the
    /// restored mesh has been drawn for a while. Found at launch, it means the
    /// last restore never got that far (the app was killed or crashed loading or
    /// drawing it), so that autosave is set aside rather than retried forever.
    var restoreMarker: URL { directory.appendingPathComponent("restoring") }
    private static let tempPrefix = "autosave-"
    private static let brokenPrefix = "broken-"
    private static let brokenKept = 3

    /// Debug builds: `-cruthAutosaveDelay <seconds>` replaces the quiet delay and the
    /// minimum interval of edit-driven saves (the maximum delay becomes twice it),
    /// so UI tests can control them.
    static var delayOverride: TimeInterval? {
        #if DEBUG
        let value = UserDefaults.standard.double(forKey: "cruthAutosaveDelay")
        if value > 0 { return value }
        #endif
        return nil
    }

    static func standard() -> AutosaveStore {
        #if DEBUG
        if let path = UserDefaults.standard.string(forKey: "cruthAutosavePath"), !path.isEmpty {
            return AutosaveStore(directory: URL(fileURLWithPath: path, isDirectory: true))
        }
        #endif
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return AutosaveStore(directory: support.appendingPathComponent("Autosave", isDirectory: true))
    }

    /// Creates the folder, keeps it out of backups and removes temp files left by
    /// a save that was cut short.
    func prepare() {
        let fm = FileManager.default
        do {
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            var url = directory
            try url.setResourceValues(values)
        } catch {
            Log.autosave.error("Cannot prepare \(directory.path): \(error.localizedDescription)")
        }
        let names = (try? fm.contentsOfDirectory(atPath: directory.path)) ?? []
        for name in names where name.hasPrefix(Self.tempPrefix) {
            try? fm.removeItem(at: directory.appendingPathComponent(name))
            Log.autosave.info("Removed unfinished save \(name)")
        }
    }

    var exists: Bool { FileManager.default.fileExists(atPath: file.path) }

    /// Writes a snapshot (cruth_document_snapshot) to a temp file beside the
    /// autosave, then swaps it in, so a crash mid-write leaves the previous
    /// autosave intact. Returns the size in bytes. The snapshot is independent of
    /// the document, so this runs off the main thread while editing goes on.
    func write(_ snapshot: OpaquePointer) throws -> Int {
        let fm = FileManager.default
        let temp = directory.appendingPathComponent("\(Self.tempPrefix)\(UUID().uuidString).ply")
        let status = cruth_mesh_save(snapshot, temp.path(percentEncoded: false))
        guard status == CRUTH_OK else {
            let detail = String(cString: cruth_last_error())
            try? fm.removeItem(at: temp)
            throw AutosaveError(description: "\(String(cString: cruth_status_string(status))): \(detail)")
        }
        do {
            if fm.fileExists(atPath: file.path) {
                _ = try fm.replaceItemAt(file, withItemAt: temp)
            } else {
                try fm.moveItem(at: temp, to: file)
            }
        } catch {
            try? fm.removeItem(at: temp)
            throw error
        }
        return (try? fm.attributesOfItem(atPath: file.path))?[.size] as? Int ?? 0
    }

    /// Loads the autosave. A missing file gives nil; an unreadable one, or one
    /// whose last restore never finished, is moved aside and also gives nil.
    /// On success the restore marker stays until `confirmRestore`.
    func load() -> OpaquePointer? {
        let fm = FileManager.default
        let interrupted = fm.fileExists(atPath: restoreMarker.path)
        // The marker belongs to one restore attempt: drop it whatever happens next.
        try? fm.removeItem(at: restoreMarker)
        guard exists else { return nil }
        if interrupted {
            Log.autosave.error("The last restore did not finish")
            moveAside()
            return nil
        }
        fm.createFile(atPath: restoreMarker.path, contents: nil)
        var doc: OpaquePointer?
        let status = cruth_document_load(file.path(percentEncoded: false), &doc)
        guard status == CRUTH_OK, let doc else {
            Log.autosave.error("Restore failed: \(String(cString: cruth_status_string(status))): " +
                               String(cString: cruth_last_error()))
            try? fm.removeItem(at: restoreMarker)
            moveAside()
            return nil
        }
        guard cruth_document_triangle_count(doc) > 0 else {
            Log.autosave.error("Restore failed: the autosave has no faces")
            cruth_document_destroy(doc)
            try? fm.removeItem(at: restoreMarker)
            moveAside()
            return nil
        }
        return doc
    }

    /// The restored mesh loaded and drew without killing the app.
    func confirmRestore() {
        try? FileManager.default.removeItem(at: restoreMarker)
        Log.autosave.info("Restore confirmed")
    }

    /// Keeps a broken autosave as broken-<date>.ply for later inspection.
    private func moveAside() {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd-HHmmss"
        let target = directory.appendingPathComponent("\(Self.brokenPrefix)\(formatter.string(from: Date())).ply")
        do {
            try? FileManager.default.removeItem(at: target)
            try FileManager.default.moveItem(at: file, to: target)
            Log.autosave.error("Moved the autosave aside to \(target.lastPathComponent)")
        } catch {
            Log.autosave.error("Cannot move the autosave aside: \(error.localizedDescription); deleting it")
            try? FileManager.default.removeItem(at: file)
        }
        pruneBroken()
    }

    /// Keeps the newest few broken autosaves; their dated names sort oldest first.
    private func pruneBroken() {
        let names = ((try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? [])
            .filter { $0.hasPrefix(Self.brokenPrefix) && $0.hasSuffix(".ply") }
            .sorted()
        for name in names.dropLast(Self.brokenKept) {
            try? FileManager.default.removeItem(at: directory.appendingPathComponent(name))
            Log.autosave.info("Deleted old \(name)")
        }
    }

    func clear() {
        do {
            if exists { try FileManager.default.removeItem(at: file) }
        } catch {
            Log.autosave.error("Cannot delete the autosave: \(error.localizedDescription)")
        }
    }
}
