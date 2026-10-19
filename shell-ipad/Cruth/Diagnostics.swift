// SPDX-License-Identifier: MIT
import Foundation
import os
import SwiftUI
import UIKit

/// One log line's source. Each is also an os.Logger category under `org.cruth.app`.
enum LogCategory: String {
    case session, input, autosave, commands
}

/// Short handles for logging: `Log.commands.info("…")`.
struct Log {
    static let session = Log(category: .session)
    static let input = Log(category: .input)
    static let autosave = Log(category: .autosave)
    static let commands = Log(category: .commands)

    let category: LogCategory

    func info(_ message: String) { Diagnostics.shared.write(category, .info, message) }
    func error(_ message: String) { Diagnostics.shared.write(category, .error, message) }
}

/// On-device diagnostics. Every line goes to the unified log (Console.app,
/// subsystem org.cruth.app) and to a plain-text file in Application Support/Logs
/// that Share Diagnostics hands out. The file rolls over at about 1 MB, keeping
/// one previous file. File writes go through a serial utility queue, so a log
/// call costs the caller a string and a dispatch.
final class Diagnostics {
    static let shared = Diagnostics()
    static let subsystem = "org.cruth.app"
    private static let maxFileSize = 1_000_000

    let directory: URL
    var currentLog: URL { directory.appendingPathComponent("cruth.log") }
    var previousLog: URL { directory.appendingPathComponent("cruth.previous.log") }

    private let queue = DispatchQueue(label: "org.cruth.diagnostics", qos: .utility)
    private let loggers: [LogCategory: Logger]
    // Touched on `queue` only.
    private var handle: FileHandle?
    private var size = 0
    private let timestamp: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()

    private init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        directory = support.appendingPathComponent("Logs", isDirectory: true)
        var loggers: [LogCategory: Logger] = [:]
        for category in [LogCategory.session, .input, .autosave, .commands] {
            loggers[category] = Logger(subsystem: Diagnostics.subsystem, category: category.rawValue)
        }
        self.loggers = loggers
    }

    func write(_ category: LogCategory, _ type: OSLogType, _ text: String) {
        let message = Self.redactingPaths(text)
        loggers[category]?.log(level: type, "\(message, privacy: .public)")
        let date = Date()
        queue.async {
            let level = type == .error ? "ERROR " : ""
            self.append("\(self.timestamp.string(from: date)) [\(category.rawValue)] \(level)\(message)\n")
        }
    }

    /// An absolute path: a "/" that does not follow a letter, digit or dot (so
    /// "i/o error" stays), up to a quote, ": " or the end. Paths may hold spaces.
    private static let absolutePath = try? NSRegularExpression(pattern: #"(?<![\w.])/[^'"\n]*?(?=['"]|: |$)"#)

    /// Logs are shared, and paths carry user and file names, so they become "<path>".
    static func redactingPaths(_ text: String) -> String {
        guard text.contains("/"), let absolutePath else { return text }
        return absolutePath.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text),
                                                     withTemplate: "<path>")
    }

    /// Copies the log files and a summary into a fresh temporary folder, so the
    /// share sheet gets a stable snapshot while logging carries on.
    func snapshot(summary: String) -> [URL] {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("Cruth Diagnostics", isDirectory: true)
        try? FileManager.default.removeItem(at: folder)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        var files: [URL] = []
        let summaryURL = folder.appendingPathComponent("summary.txt")
        if (try? summary.write(to: summaryURL, atomically: true, encoding: .utf8)) != nil { files.append(summaryURL) }
        queue.sync {
            try? handle?.synchronize()
            for log in [previousLog, currentLog] where FileManager.default.fileExists(atPath: log.path) {
                let copy = folder.appendingPathComponent(log.lastPathComponent)
                if (try? FileManager.default.copyItem(at: log, to: copy)) != nil { files.append(copy) }
            }
        }
        return files
    }

    // MARK: File (on `queue`)

    private func append(_ line: String) {
        let data = Data(line.utf8)
        if handle == nil { open() }
        if size + data.count > Diagnostics.maxFileSize {
            roll()
        }
        guard let handle else { return }
        do {
            try handle.write(contentsOf: data)
            size += data.count
        } catch {
            loggers[.session]?.error("log file write failed: \(error.localizedDescription, privacy: .public)")
            self.handle = nil
        }
    }

    private func open() {
        let fm = FileManager.default
        try? fm.createDirectory(at: directory, withIntermediateDirectories: true)
        if !fm.fileExists(atPath: currentLog.path) { fm.createFile(atPath: currentLog.path, contents: nil) }
        handle = try? FileHandle(forWritingTo: currentLog)
        size = Int((try? handle?.seekToEnd()) ?? 0)
    }

    private func roll() {
        try? handle?.close()
        handle = nil
        let fm = FileManager.default
        try? fm.removeItem(at: previousLog)
        try? fm.moveItem(at: currentLog, to: previousLog)
        open()
    }

    // MARK: Device facts

    static var appVersion: String {
        let info = Bundle.main.infoDictionary
        let version = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        return "\(version) (\(build))"
    }

    static var systemVersion: String {
        let v = ProcessInfo.processInfo.operatingSystemVersion
        return "iPadOS \(v.majorVersion).\(v.minorVersion).\(v.patchVersion)"
    }

    /// Hardware identifier such as "iPad16,5"; the simulator reports the model it simulates.
    static var deviceModel: String {
        if let simulated = ProcessInfo.processInfo.environment["SIMULATOR_MODEL_IDENTIFIER"] {
            return "\(simulated) (Simulator)"
        }
        var info = utsname()
        uname(&info)
        return withUnsafeBytes(of: &info.machine) { bytes in
            String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self)
        }
    }
}

/// UIKit's share sheet for SwiftUI.
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
