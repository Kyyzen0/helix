import Foundation

/// One agent connected to the socket. It reads and writes on a queue of its own, off the main
/// thread; what it hears is handed to the closures of the server.
final class AgentConnection: @unchecked Sendable {
    let id = UUID()
    private let descriptor: Int32
    private let queue: DispatchQueue
    private var source: DispatchSourceRead?
    private var buffer: [UInt8] = []
    private var closed = false
    /// Whether the descriptor is closed when the source is cancelled. The end of the input cancels
    /// the source and keeps the descriptor, since the replies still go out on it.
    private var closeOnCancel = true

    /// What came in: a whole line, the end of the input, a message too large to take.
    var onLine: (@Sendable (String) -> Void)?
    var onEnd: (@Sendable () -> Void)?
    var onOversize: (@Sendable () -> Void)?

    /// The longest line it takes, which the tests make short.
    private let maxMessageBytes: Int

    init(descriptor: Int32, maxMessageBytes: Int = AgentSocket.maxMessageBytes) {
        self.descriptor = descriptor
        self.maxMessageBytes = maxMessageBytes
        queue = DispatchQueue(label: "helix.agent.connection.\(descriptor)")
        // A read that finds nothing must not wait: the source says when there is something.
        _ = fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK)
    }

    func start() {
        let source = DispatchSource.makeReadSource(fileDescriptor: descriptor, queue: queue)
        source.setEventHandler { [weak self] in self?.readAvailable() }
        // Held strongly: the handler runs after finish() has returned, when nothing else may keep
        // the connection, and a weak reference would skip the close, leak the descriptor and leave
        // the relay waiting for an end that never comes. The source lets go of its handlers once
        // it is cancelled, so this lasts no longer than the connection's source.
        source.setCancelHandler { [self] in
            if closeOnCancel { Darwin.close(descriptor) }
        }
        self.source = source
        source.resume()
    }

    /// Writes one message and its newline, in order with the ones before it.
    func send(_ message: String) {
        var bytes = Array(message.utf8)
        bytes.append(UInt8(ascii: "\n"))
        queue.async { [self] in
            guard !closed else { return }
            if !AgentSocket.writeAll(descriptor, bytes) { finish() }
        }
    }

    /// Closes the connection, after what was queued to be written has gone out.
    func close() {
        queue.async { [self] in finish() }
    }

    /// Closes a connection that was refused and never started.
    func discard() {
        closed = true
        Darwin.close(descriptor)
    }

    private func finish() {
        guard !closed else { return }
        closed = true
        if let source {
            closeOnCancel = true
            source.cancel()
            self.source = nil
        } else {
            Darwin.close(descriptor)
        }
    }

    private func readAvailable() {
        var chunk = [UInt8](repeating: 0, count: 64 * 1024)
        while !closed {
            let count = read(descriptor, &chunk, chunk.count)
            if count > 0 {
                buffer.append(contentsOf: chunk[0..<count])
                if !extractLines() { return }
            } else if count == 0 {
                // The other end is done writing. It may still be waiting for replies, so the
                // connection stays open; the server closes it once they are written.
                closeOnCancel = false
                source?.cancel()
                source = nil
                onEnd?()
                return
            } else if errno == EINTR {
                continue
            } else {
                if errno != EAGAIN { finish(); onEnd?() }
                return
            }
        }
    }

    /// Hands over each whole line. Returns false when the connection was closed for sending a
    /// message larger than the limit.
    private func extractLines() -> Bool {
        while let newline = buffer.firstIndex(of: UInt8(ascii: "\n")) {
            if newline > maxMessageBytes { return refuse() }
            let line = String(decoding: buffer[0..<newline], as: UTF8.self)
            buffer.removeSubrange(0...newline)
            onLine?(line)
        }
        if buffer.count > maxMessageBytes { return refuse() }
        return true
    }

    private func refuse() -> Bool {
        buffer.removeAll()
        onOversize?()
        return false
    }

    /// The user the other end runs as, or nil when the system does not say.
    var peerUser: uid_t? {
        var user: uid_t = 0
        var group: gid_t = 0
        return getpeereid(descriptor, &user, &group) == 0 ? user : nil
    }
}

/// Listens on the socket and accepts the connections of the user who runs the app, and nobody
/// else. It knows nothing of the protocol: lines go in and lines go out.
final class AgentSocketServer: @unchecked Sendable {
    enum Start: Equatable {
        case listening
        /// Another Helix answers at the path: it is not taken from it.
        case ownedByAnother
        case failed(String)
    }

    let path: String
    private var descriptor: Int32 = -1
    private var source: DispatchSourceRead?
    private let queue = DispatchQueue(label: "helix.agent.listener")

    /// A connection was accepted. It reads nothing until the receiver has set what it does with
    /// what comes in and has started it: a line that came before that would be lost.
    var onConnection: (@Sendable (AgentConnection) -> Void)?

    private let maxMessageBytes: Int

    init(path: String, maxMessageBytes: Int = AgentSocket.maxMessageBytes) {
        self.path = path
        self.maxMessageBytes = maxMessageBytes
    }

    /// Starts listening. A socket file that something answers on is another Helix's: it is left
    /// alone. One that nothing answers on is what a Helix that crashed left, and is replaced.
    func start(privateFolder: Bool = true) -> Start {
        guard AgentSocket.fits(path) else { return .failed("The socket path is too long: \(path)") } // l10n-ok: a log line
        do {
            try AgentSocket.prepareFolder(of: path, privateFolder: privateFolder)
        } catch {
            return .failed(error.localizedDescription)
        }
        switch AgentSocket.occupant(of: path) {
        case .nothing:
            break
        case .socket(let owner):
            if let probe = AgentSocket.connect(to: path) {
                close(probe)
                return .ownedByAnother
            }
            // Nothing answers: a socket left behind. Another user's is not ours to remove.
            guard owner == getuid() else { return .failed("The socket at \(path) belongs to another user.") } // l10n-ok: a log line
            unlink(path)
        case .other:
            return .failed("\(path) exists and is not a socket.") // l10n-ok: a log line
        }

        guard var address = AgentSocket.address(of: path), let listener = AgentSocket.makeSocket() else {
            return .failed("The socket could not be made.") // l10n-ok: a log line
        }
        // Only its owner may connect: the mode is set before the file is reachable.
        let previous = umask(0o177)
        let bound = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(listener, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        umask(previous)
        guard bound == 0 else {
            let reason = String(cString: strerror(errno))
            close(listener)
            return .failed("Could not bind \(path): \(reason)") // l10n-ok: a log line
        }
        chmod(path, 0o600)
        guard listen(listener, 8) == 0 else {
            let reason = String(cString: strerror(errno))
            close(listener)
            unlink(path)
            return .failed("Could not listen on \(path): \(reason)") // l10n-ok: a log line
        }
        _ = fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK)
        descriptor = listener

        let source = DispatchSource.makeReadSource(fileDescriptor: listener, queue: queue)
        source.setEventHandler { [weak self] in self?.acceptAll() }
        source.setCancelHandler { Darwin.close(listener) }
        self.source = source
        source.resume()
        return .listening
    }

    /// Stops listening and removes the socket file.
    func stop() {
        queue.sync {
            guard source != nil else { return }
            source?.cancel()
            source = nil
            descriptor = -1
        }
        // Only a socket is removed, and only the one this server made.
        if case .socket(let owner) = AgentSocket.occupant(of: path), owner == getuid() { unlink(path) }
    }

    private func acceptAll() {
        while true {
            let client = accept(descriptor, nil, nil)
            if client < 0 { return }
            var on: Int32 = 1
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, socklen_t(MemoryLayout<Int32>.size))
            let connection = AgentConnection(descriptor: client, maxMessageBytes: maxMessageBytes)
            // Someone who is not the person at the keyboard gets nothing.
            guard connection.peerUser == getuid() else {
                connection.discard()
                continue
            }
            onConnection?(connection)
        }
    }
}
