import AVFoundation
import CoreMedia

/// Grava o vídeo da câmera e o som do plugin direto num MP4 (H.264 + AAC), já sincronizados.
/// Tudo roda na mesma fila (a da câmera), então não precisa de travas.
final class Recorder {
    private var writer: AVAssetWriter?
    private var vIn: AVAssetWriterInput?
    private var aIn: AVAssetWriterInput?
    private var url: URL?
    private var started = false
    private var lastVideoPTS = CMTime.invalid
    private var expectW = 0, expectH = 0

    private var pendingAudio: [CMSampleBuffer] = []
    private var audioAnchor = CMTime.invalid
    private var audioFrames: Int64 = 0
    private var srOut = 48000, chOut = 2, factor = 1
    private var audioFmt: CMAudioFormatDescription?
    private(set) var gotAudio = false

    /// Quanto o som deve ser adiantado para bater com a imagem (latência do plugin + rede).
    var syncSeconds: Double = 0.12
    /// Converte a hora do sistema para o relógio da câmera.
    var clockConvert: ((CMTime) -> CMTime)?

    var isRecording: Bool { writer != nil }

    func start(videoSettings: [String: Any]?, audioRate: Int?, audioChannels: Int) throws {
        let name = "ValvedCam_\(Int(Date().timeIntervalSince1970)).mp4"
        let u = FileManager.default.temporaryDirectory.appendingPathComponent(name)
        try? FileManager.default.removeItem(at: u)
        let w = try AVAssetWriter(outputURL: u, fileType: .mp4)
        w.shouldOptimizeForNetworkUse = true

        let v = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        v.expectsMediaDataInRealTime = true
        guard w.canAdd(v) else { throw NSError(domain: "ValvedCam", code: 1) }
        w.add(v)
        expectW = (videoSettings?[AVVideoWidthKey] as? NSNumber)?.intValue ?? 0
        expectH = (videoSettings?[AVVideoHeightKey] as? NSNumber)?.intValue ?? 0

        aIn = nil
        audioFmt = nil
        if let sr = audioRate {
            // AAC vai até 48 kHz: 88,2/96/192 kHz são reduzidos pela metade (média de pares)
            factor = 1
            var s = sr
            while s > 48000 && s % 2 == 0 { s /= 2; factor *= 2 }
            srOut = s
            chOut = min(max(audioChannels, 1), 2)
            let a = AVAssetWriterInput(mediaType: .audio, outputSettings: [
                AVFormatIDKey: kAudioFormatMPEG4AAC,
                AVSampleRateKey: srOut,
                AVNumberOfChannelsKey: chOut,
                AVEncoderBitRateKey: srOut >= 44100 ? 256_000 : 128_000,
            ])
            a.expectsMediaDataInRealTime = true
            if w.canAdd(a) {
                w.add(a)
                aIn = a
                var asbd = AudioStreamBasicDescription(
                    mSampleRate: Float64(srOut), mFormatID: kAudioFormatLinearPCM,
                    mFormatFlags: kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
                    mBytesPerPacket: UInt32(2 * chOut), mFramesPerPacket: 1, mBytesPerFrame: UInt32(2 * chOut),
                    mChannelsPerFrame: UInt32(chOut), mBitsPerChannel: 16, mReserved: 0)
                CMAudioFormatDescriptionCreate(allocator: kCFAllocatorDefault, asbd: &asbd, layoutSize: 0, layout: nil,
                                               magicCookieSize: 0, magicCookie: nil, extensions: nil,
                                               formatDescriptionOut: &audioFmt)
            }
        }
        guard w.startWriting() else { throw w.error ?? NSError(domain: "ValvedCam", code: 2) }
        writer = w; vIn = v; url = u
        started = false
        pendingAudio.removeAll()
        audioAnchor = .invalid
        audioFrames = 0
        gotAudio = false
        lastVideoPTS = .invalid
    }

    func appendVideo(_ sb: CMSampleBuffer) {
        guard let w = writer, let v = vIn, w.status == .writing else { return }
        if !started {
            // espera o primeiro quadro já na orientação certa
            if let pb = CMSampleBufferGetImageBuffer(sb), expectW > 0,
               CVPixelBufferGetWidth(pb) != expectW || CVPixelBufferGetHeight(pb) != expectH { return }
            let pts = CMSampleBufferGetPresentationTimeStamp(sb)
            w.startSession(atSourceTime: pts)
            started = true
            for a in pendingAudio { appendAudioBuffer(a) }
            pendingAudio.removeAll()
        }
        if v.isReadyForMoreMediaData {
            v.append(sb)
            lastVideoPTS = CMSampleBufferGetPresentationTimeStamp(sb)
        }
    }

    func appendAudio(_ pcm: Data, sr: Int, ch: Int, arrival: CMTime) {
        guard writer != nil, aIn != nil, let fmt = audioFmt, ch > 0 else { return }
        let framesIn = pcm.count / (2 * ch)
        guard framesIn > 0 else { return }
        let out = (factor == 1 && ch == chOut) ? pcm : convert(pcm, ch: ch)
        let frames = out.count / (2 * chOut)
        guard frames > 0 else { return }

        if !audioAnchor.isValid {
            // o primeiro pedaço terminou de chegar agora: ele começou "duração" antes,
            // e o som saiu do plugin um pouco antes ainda (sincronia)
            let arrivalCam = clockConvert?(arrival) ?? arrival
            let dur = CMTime(value: CMTimeValue(framesIn), timescale: CMTimeScale(sr))
            let start = CMTimeSubtract(CMTimeSubtract(arrivalCam, dur), CMTime(seconds: syncSeconds, preferredTimescale: 1_000_000))
            audioAnchor = CMTimeConvertScale(start, timescale: CMTimeScale(srOut), method: .roundHalfAwayFromZero)
        }
        let pts = CMTimeAdd(audioAnchor, CMTime(value: audioFrames, timescale: CMTimeScale(srOut)))
        audioFrames += Int64(frames)
        guard let sb = makeSampleBuffer(out, frames: frames, pts: pts, fmt: fmt) else { return }
        gotAudio = true
        if started { appendAudioBuffer(sb) }
        else if pendingAudio.count < 400 { pendingAudio.append(sb) }
    }

    private func appendAudioBuffer(_ sb: CMSampleBuffer) {
        guard let a = aIn, writer?.status == .writing else { return }
        if a.isReadyForMoreMediaData { a.append(sb) }
    }

    /// Para e fecha o arquivo. Devolve o arquivo (ou nil) e se tinha som do plugin.
    func stop(_ done: @escaping (URL?, Bool) -> Void) {
        guard let w = writer else { done(nil, false); return }
        let had = gotAudio, u = url
        writer = nil
        let v = vIn, a = aIn
        vIn = nil; aIn = nil
        pendingAudio.removeAll()
        guard started else { w.cancelWriting(); done(nil, had); return }
        if lastVideoPTS.isValid { w.endSession(atSourceTime: lastVideoPTS) }
        v?.markAsFinished()
        a?.markAsFinished()
        w.finishWriting { done(w.status == .completed ? u : nil, had) }
    }

    private func convert(_ pcm: Data, ch: Int) -> Data {
        let framesIn = pcm.count / (2 * ch)
        let framesOut = framesIn / factor
        var out = Data(count: framesOut * 2 * chOut)
        let f = factor, co = chOut
        pcm.withUnsafeBytes { (src: UnsafeRawBufferPointer) in
            out.withUnsafeMutableBytes { (dst: UnsafeMutableRawBufferPointer) in
                let s = src.bindMemory(to: Int16.self)
                let d = dst.bindMemory(to: Int16.self)
                for fr in 0..<framesOut {
                    for c in 0..<co {
                        let sc = min(c, ch - 1)
                        var acc: Int32 = 0
                        for k in 0..<f { acc += Int32(Int16(littleEndian: s[(fr * f + k) * ch + sc])) }
                        d[fr * co + c] = Int16(acc / Int32(f)).littleEndian
                    }
                }
            }
        }
        return out
    }

    private func makeSampleBuffer(_ data: Data, frames: Int, pts: CMTime, fmt: CMAudioFormatDescription) -> CMSampleBuffer? {
        let len = data.count
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: len,
                                                 blockAllocator: kCFAllocatorDefault, customBlockSource: nil,
                                                 offsetToData: 0, dataLength: len, flags: kCMBlockBufferAssureMemoryNowFlag,
                                                 blockBufferOut: &block) == kCMBlockBufferNoErr,
              let b = block else { return nil }
        let st = data.withUnsafeBytes { raw -> OSStatus in
            guard let base = raw.baseAddress else { return -1 }
            return CMBlockBufferReplaceDataBytes(with: base, blockBuffer: b, offsetIntoDestination: 0, dataLength: len)
        }
        guard st == kCMBlockBufferNoErr else { return nil }
        var sb: CMSampleBuffer?
        let s2 = CMAudioSampleBufferCreateReadyWithPacketDescriptions(allocator: kCFAllocatorDefault, dataBuffer: b,
                                                                      formatDescription: fmt, sampleCount: frames,
                                                                      presentationTimeStamp: pts, packetDescriptions: nil,
                                                                      sampleBufferOut: &sb)
        return s2 == noErr ? sb : nil
    }
}
