import Foundation
import Network
import CoreMedia

/// Recebe o som do plugin Valved Cam Link pela rede local.
///
/// O iPhone não pode anunciar por UDP (broadcast) como o Android, então o app só escuta a porta TCP 47800
/// (e se anuncia por Bonjour). O plugin procura o celular na rede, conecta e:
///   1. o app se apresenta: "VALVEDCAM|<nome>|47800\n"
///   2. o plugin manda o cabeçalho "VLVA" + taxa (int32) + canais (int16) + bits (int16)
///   3. depois vem o áudio PCM 16 bits intercalado, sem parar.
final class AudioLink {
    static let port: UInt16 = 47800

    let queue = DispatchQueue(label: "valvedcam.net")
    private let deviceName: String
    private var listener: NWListener?
    private var conn: NWConnection?
    private var remainder = Data()

    private let lock = NSLock()
    private var _connected = false
    private var _level: Float = 0
    private var _sampleRate = 48000
    private var _channels = 2

    var isConnected: Bool { lock.lock(); defer { lock.unlock() }; return _connected }
    var level: Float { lock.lock(); defer { lock.unlock() }; return _level }
    var sampleRate: Int { lock.lock(); defer { lock.unlock() }; return _sampleRate }
    var channels: Int { lock.lock(); defer { lock.unlock() }; return _channels }

    /// Chamado na fila de rede: PCM 16 bits intercalado (quadros inteiros), taxa, canais e a hora em que chegou.
    var onAudio: ((Data, Int, Int, CMTime) -> Void)?

    init(deviceName: String) { self.deviceName = deviceName }

    private var lastData = Date.distantPast
    private var watchdog: DispatchSourceTimer?

    func start() {
        queue.async {
            if self.listener == nil { self.startListener() }
            if self.watchdog == nil {
                // se o som parar de chegar por 3 s, a conexão morreu: libera para o plugin conectar de novo
                let t = DispatchSource.makeTimerSource(queue: self.queue)
                t.schedule(deadline: .now() + 1, repeating: 1)
                t.setEventHandler { [weak self] in
                    guard let self = self, self.conn != nil, self.isConnected,
                          Date().timeIntervalSince(self.lastData) > 3 else { return }
                    self.conn?.cancel(); self.conn = nil; self.setConnected(false)
                }
                t.resume()
                self.watchdog = t
            }
        }
    }

    /// App foi para o fundo: fecha tudo (o iOS mataria as conexões de qualquer jeito).
    func stop() {
        queue.async {
            self.conn?.cancel(); self.conn = nil; self.setConnected(false)
            self.listener?.cancel(); self.listener = nil
        }
    }

    /// App voltou para a tela: abre a porta de novo do zero.
    func restart() {
        queue.async {
            self.conn?.cancel(); self.conn = nil; self.setConnected(false)
            self.listener?.cancel(); self.listener = nil
            self.startListener()
        }
    }

    private func startListener() {
        listener?.cancel()
        let params = NWParameters.tcp
        params.allowLocalEndpointReuse = true
        if let tcp = params.defaultProtocolStack.transportProtocol as? NWProtocolTCP.Options { tcp.noDelay = true }
        guard let port = NWEndpoint.Port(rawValue: Self.port),
              let l = try? NWListener(using: params, on: port) else {
            queue.asyncAfter(deadline: .now() + 2) { self.startListener() }
            return
        }
        l.service = NWListener.Service(name: "Valved Cam", type: "_valvedcam._tcp")
        l.newConnectionHandler = { [weak self] c in self?.accept(c) }
        l.stateUpdateHandler = { [weak self] st in
            if case .failed = st {
                self?.queue.asyncAfter(deadline: .now() + 1) { if self?.listener != nil { self?.startListener() } }
            }
        }
        listener = l
        l.start(queue: queue)
    }

    private func setConnected(_ v: Bool) {
        lock.lock(); _connected = v; if !v { _level = 0 }; lock.unlock()
    }

    private func accept(_ c: NWConnection) {
        // já tem um plugin mandando som agora: recusa. Se a conexão antiga estiver parada, troca pela nova.
        if conn != nil && isConnected && Date().timeIntervalSince(lastData) < 2 { c.cancel(); return }
        conn?.cancel()
        conn = c
        remainder.removeAll()
        c.stateUpdateHandler = { [weak self, weak c] st in
            guard let self = self, let c = c else { return }
            switch st {
            case .failed, .cancelled:
                if self.conn === c { self.conn = nil; self.setConnected(false) }
            default: break
            }
        }
        c.start(queue: queue)
        let hello = "VALVEDCAM|\(deviceName)|\(Self.port)\n"
        c.send(content: hello.data(using: .utf8), completion: .contentProcessed { _ in })
        c.receive(minimumIncompleteLength: 12, maximumLength: 12) { [weak self] data, _, _, err in
            guard let self = self else { return }
            guard err == nil, let d = data, d.count == 12,
                  String(data: d.prefix(4), encoding: .ascii) == "VLVA" else { c.cancel(); return }
            let b = [UInt8](d)
            let sr = Int(Int32(b[4]) | Int32(b[5]) << 8 | Int32(b[6]) << 16 | Int32(b[7]) << 24)
            let ch = Int(Int16(b[8]) | Int16(b[9]) << 8)
            self.lock.lock()
            self._sampleRate = sr > 0 ? sr : 48000
            self._channels = max(1, ch)
            self.lock.unlock()
            self.lastData = Date()
            self.setConnected(true)
            self.receiveLoop(c)
        }
    }

    private func receiveLoop(_ c: NWConnection) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 1 << 16) { [weak self] data, _, isComplete, err in
            guard let self = self else { return }
            if let d = data, !d.isEmpty { self.handle(d) }
            if isComplete || err != nil { c.cancel(); return }
            self.receiveLoop(c)
        }
    }

    private func handle(_ d: Data) {
        lastData = Date()
        let now = CMClockGetTime(CMClockGetHostTimeClock())
        let ch = channels, sr = sampleRate
        var buf = remainder
        buf.append(d)
        let frameBytes = 2 * ch
        let whole = buf.count / frameBytes * frameBytes
        remainder = whole < buf.count ? Data(buf[(buf.startIndex + whole)...]) : Data()
        guard whole > 0 else { return }
        let pcm = Data(buf.prefix(whole))

        var pk: Int32 = 0
        pcm.withUnsafeBytes { raw in
            let s = raw.bindMemory(to: Int16.self)
            var i = 0
            while i < s.count { pk = max(pk, abs(Int32(Int16(littleEndian: s[i])))); i += 8 }
        }
        lock.lock(); _level = max(Float(pk) / 32768, _level * 0.85); lock.unlock()
        onAudio?(pcm, sr, ch, now)
    }

    /// Endereços IPv4 do celular no Wi-Fi e no cabo (para mostrar na tela).
    static func localAddresses() -> [String] {
        var out: [String] = []
        var ifaddr: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&ifaddr) == 0, let first = ifaddr else { return [] }
        defer { freeifaddrs(ifaddr) }
        var p: UnsafeMutablePointer<ifaddrs>? = first
        while let ptr = p {
            let ifa = ptr.pointee
            if let sa = ifa.ifa_addr, sa.pointee.sa_family == UInt8(AF_INET) {
                let name = String(cString: ifa.ifa_name)
                if name.hasPrefix("en") || name.hasPrefix("bridge") {
                    var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                    if getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
                        out.append(String(cString: host))
                    }
                }
            }
            p = ifa.ifa_next
        }
        return out
    }
}
