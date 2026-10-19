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
    /// Exists while a restore is loading. Found at launch, it means the last
    /// restore never finished (the app was killed or crashed in it), so the
    /// autosave is set aside rather than retried forever.
    private var restoreMarker: URL { directory.appendingPathComponent("restoring") }
    private static let tempPrefix = "autosave-"

    /// Debug builds: `-cruthAutosaveDelay <seconds>` replaces both the interval between
    /// edit-driven saves and the quiet delay before one, so UI tests can control them.
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

    /// Writes the document to a temp file beside the autosave, then swaps it in,
    /// so a crash mid-write leaves the previous autosave intact. Returns the size
    /// in bytes. Call it only while nothing edits the document.
    func write(_ document: OpaquePointer) throws -> Int {
        let fm = FileManager.default
        let temp = directory.appendingPathComponent("\(Self.tempPrefix)\(UUID().uuidString).ply")
        let status = cruth_document_save(document, temp.path(percentEncoded: false))
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
    func load() -> OpaquePointer? {
        let fm = FileManager.default
        guard exists else { return nil }
        if fm.fileExists(atPath: restoreMarker.path) {
            Log.autosave.error("The last restore did not finish")
            moveAside()
            return nil
        }
        fm.createFile(atPath: restoreMarker.path, contents: nil)
        defer { try? fm.removeItem(at: restoreMarker) }
        var doc: OpaquePointer?
        let status = cruth_document_load(file.path(percentEncoded: false), &doc)
        guard status == CRUTH_OK, let doc else {
            Log.autosave.error("Restore failed: \(String(cString: cruth_status_string(status))): " +
                               String(cString: cruth_last_error()))
            moveAside()
            return nil
        }
        guard cruth_document_triangle_count(doc) > 0 else {
            Log.autosave.error("Restore failed: the autosave has no faces")
            cruth_document_destroy(doc)
            moveAside()
            return nil
        }
        return doc
    }

    /// Keeps a broken autosave as broken-<date>.ply for later inspection.
    private func moveAside() {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd-HHmmss"
        let target = directory.appendingPathComponent("broken-\(formatter.string(from: Date())).ply")
        do {
            try? FileManager.default.removeItem(at: target)
            try FileManager.default.moveItem(at: file, to: target)
            Log.autosave.error("Moved the autosave aside to \(target.lastPathComponent)")
        } catch {
            Log.autosave.error("Cannot move the autosave aside: \(error.localizedDescription); deleting it")
            try? FileManager.default.removeItem(at: file)
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
