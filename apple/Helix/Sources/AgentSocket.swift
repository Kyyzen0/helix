import Foundation

/// The Unix socket the app listens on for AI agents, and what both ends need to find and open it.
///
/// Nothing here touches the main actor or the engine: the relay (`Helix --mcp`) uses it before any
/// engine exists, and the server uses it from its own queue.
enum AgentSocket {
    /// With this in the environment, the socket is at that path. The smoke tests use it so that
    /// they never meet the Helix of the person running them.
    static let environmentKey = "HELIX_MCP_SOCKET"
    static let fileName = "mcp.sock"

    /// The longest path a socket address holds: `sun_path` is 104 bytes on macOS, the last for the
    /// terminating zero.
    static let maxPathBytes = 103

    /// The longest message either end takes: one line of JSON-RPC.
    static let maxMessageBytes = 64 * 1024 * 1024

    /// Where the socket is: `~/Library/Application Support/<bundle id>/mcp.sock`. When that is too
    /// long for a socket address (a long user name, a folder far from the home), a short path under
    /// /tmp with the user id in its name: `/tmp/helix-<uid>/mcp.sock`.
    static func path(
        applicationSupport: URL?, bundleIdentifier: String, uid: uid_t,
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> String {
        if let chosen = environment[environmentKey], !chosen.isEmpty, fits(chosen) { return chosen }
        if let applicationSupport {
            let long = applicationSupport.appending(path: bundleIdentifier).appending(path: fileName).path
            if fits(long) { return long }
        }
        return "/tmp/helix-\(uid)/\(fileName)"
    }

    /// The path for this process, which the app and the relay both compute the same way.
    static var current: String {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
        return path(
            applicationSupport: support, bundleIdentifier: Bundle.main.bundleIdentifier ?? "com.kyyzen.helix", // l10n-ok: a folder name
            uid: getuid()
        )
    }

    static func fits(_ path: String) -> Bool {
        let bytes = path.utf8.count
        return bytes > 0 && bytes <= maxPathBytes
    }

    /// The address of a path, or nil when it does not fit.
    static func address(of path: String) -> sockaddr_un? {
        guard fits(path) else { return nil }
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        withUnsafeMutableBytes(of: &address.sun_path) { buffer in
            for (index, byte) in path.utf8.enumerated() { buffer[index] = byte }
            buffer[path.utf8.count] = 0
        }
        return address
    }

    /// A new stream socket that does not raise SIGPIPE when the other end is gone.
    static func makeSocket() -> Int32? {
        let descriptor = socket(AF_UNIX, SOCK_STREAM, 0)
        guard descriptor >= 0 else { return nil }
        var on: Int32 = 1
        setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, socklen_t(MemoryLayout<Int32>.size))
        return descriptor
    }

    /// Connects to whatever listens at the path. Nil when nothing does, or the path is no socket.
    static func connect(to path: String) -> Int32? {
        guard var address = address(of: path), let descriptor = makeSocket() else { return nil }
        let result = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.connect(descriptor, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard result == 0 else {
            close(descriptor)
            return nil
        }
        return descriptor
    }

    /// Writes all the bytes, or says it could not. Waits when the other end is slow to read.
    static func writeAll(_ descriptor: Int32, _ bytes: [UInt8]) -> Bool {
        bytes.withUnsafeBytes { buffer in
            var offset = 0
            while offset < buffer.count {
                let written = write(descriptor, buffer.baseAddress! + offset, buffer.count - offset)
                if written < 0 {
                    if errno == EINTR { continue }
                    if errno == EAGAIN {
                        var poller = pollfd(fd: descriptor, events: Int16(POLLOUT), revents: 0)
                        _ = poll(&poller, 1, 1000)
                        continue
                    }
                    return false
                }
                offset += written
            }
            return true
        }
    }

    /// What a path holds: nothing, a socket, or something else.
    enum Occupant: Equatable {
        case nothing
        case socket(owner: uid_t)
        case other
    }

    static func occupant(of path: String) -> Occupant {
        var info = stat()
        guard lstat(path, &info) == 0 else { return .nothing }
        return (info.st_mode & S_IFMT) == S_IFSOCK ? .socket(owner: info.st_uid) : .other
    }

    /// Makes the folder of the socket, readable by its owner alone, and checks that it is theirs: a
    /// folder someone else made under /tmp is not a place to put a socket.
    static func prepareFolder(of path: String, privateFolder: Bool) throws {
        let folder = (path as NSString).deletingLastPathComponent
        var info = stat()
        if lstat(folder, &info) != 0 {
            try FileManager.default.createDirectory(
                atPath: folder, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700]
            )
            guard lstat(folder, &info) == 0 else { throw AgentSocketError.folder(folder) }
        }
        guard (info.st_mode & S_IFMT) == S_IFDIR, info.st_uid == getuid() else { throw AgentSocketError.folder(folder) }
        if privateFolder, (info.st_mode & 0o077) != 0 { chmod(folder, 0o700) }
    }
}

enum AgentSocketError: Error, Equatable {
    case folder(String)
    case system(String)
}

extension AgentSocketError: LocalizedError {
    var errorDescription: String? {
        switch self {
        case .folder(let folder): "The folder \(folder) is not private to this user." // l10n-ok: a log line
        case .system(let message): message
        }
    }
}
