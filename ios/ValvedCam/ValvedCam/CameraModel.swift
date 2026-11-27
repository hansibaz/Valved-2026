import AVFoundation
import Photos
import SwiftUI
import UIKit

let isPT = (Locale.preferredLanguages.first ?? "en").lowercased().hasPrefix("pt")
func tr(_ en: String, _ pt: String) -> String { isPT ? pt : en }

/// Câmera + gravação + ligação com o plugin.
final class CameraModel: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    private let sessionQ = DispatchQueue(label: "valvedcam.session")
    private let writerQ = DispatchQueue(label: "valvedcam.writer")
    private let videoOut = AVCaptureVideoDataOutput()
    private var device: AVCaptureDevice?
    private var input: AVCaptureDeviceInput?
    private let recorder = Recorder()
    let link: AudioLink
    weak var previewLayer: AVCaptureVideoPreviewLayer?
    private var rotationCoordinator: AnyObject?
    private var baseZoom: CGFloat = 1
    private var uiTimer: Timer?
    private var recStart = Date()
    private var configured = false

    // estado mostrado na tela
    @Published var connected = false
    @Published var level: Float = 0
    @Published var recording = false
    @Published var busy = false
    @Published var seconds = 0
    @Published var message: String?
    @Published var ips: [String] = []
    @Published var zoom: CGFloat = 1
    @Published var zoomPresets: [CGFloat] = [1, 2]
    @Published var maxZoom: CGFloat = 10
    @Published var exposure: Float = 0
    @Published var hasTorch = false

    // ajustes (ficam salvos)
    @Published var front: Bool
    @Published var res: String
    @Published var fps: Int
    @Published var stab: Bool
    @Published var torch = false
    @Published var grid: Bool
    @Published var syncMs: Int

    private let prefs = UserDefaults.standard

    override init() {
        front = prefs.bool(forKey: "front")
        res = prefs.string(forKey: "res") ?? "1080"
        fps = prefs.object(forKey: "fps") as? Int ?? 30
        stab = prefs.object(forKey: "stab") as? Bool ?? true
        grid = prefs.bool(forKey: "grid")
        // "sync2": no iPhone a imagem já sai com a hora certa, então o padrão é bem menor que no Android
        syncMs = prefs.object(forKey: "sync2") as? Int ?? 0
        link = AudioLink(deviceName: UIDevice.current.name)
        super.init()
        link.onAudio = { [weak self] pcm, sr, ch, t in
            guard let self = self else { return }
            self.writerQ.async { self.recorder.appendAudio(pcm, sr: sr, ch: ch, arrival: t) }
        }
    }

    private func save() {
        prefs.set(front, forKey: "front"); prefs.set(res, forKey: "res"); prefs.set(fps, forKey: "fps")
        prefs.set(stab, forKey: "stab"); prefs.set(grid, forKey: "grid"); prefs.set(syncMs, forKey: "sync2")
    }

    func toast(_ s: String) {
        DispatchQueue.main.async {
            self.message = s
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.5) { if self.message == s { self.message = nil } }
        }
    }

    // MARK: início

    func start() {
        UIApplication.shared.isIdleTimerDisabled = true
        UIDevice.current.beginGeneratingDeviceOrientationNotifications()
        link.start()
        if uiTimer == nil {
            uiTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.tick() }
        }
        AVCaptureDevice.requestAccess(for: .video) { ok in
            guard ok else { self.toast(tr("Allow camera access to record.", "Permita o uso da câmera para gravar.")); return }
            self.sessionQ.async {
                if !self.configured { self.configure(); self.configured = true }
                if !self.session.isRunning { self.session.startRunning() }
            }
        }
    }

    /// Voltou do fundo: liga a câmera e reabre a conexão com o plugin.
    func resume() {
        start()
        link.restart()
    }

    func pause() {
        if recording { stopRecord() }
        link.stop()
        sessionQ.async { if self.session.isRunning { self.session.stopRunning() } }
    }

    private var ipTick = 0
    private func tick() {
        let c = link.isConnected
        if c != connected { connected = c }
        level = link.level
        if recording { seconds = Int(Date().timeIntervalSince(recStart)) }
        ipTick += 1
        if ipTick % 20 == 1 && !c { ips = AudioLink.localAddresses() }
    }

    // MARK: câmera

    private func configure() {
        session.beginConfiguration()
        if let i = input { session.removeInput(i) }
        let pos: AVCaptureDevice.Position = front ? .front : .back
        let types: [AVCaptureDevice.DeviceType] = front
            ? [.builtInWideAngleCamera]
            : [.builtInTripleCamera, .builtInDualWideCamera, .builtInDualCamera, .builtInWideAngleCamera]
        let disc = AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: pos)
        var chosen: AVCaptureDevice?
        for t in types { if let d = disc.devices.first(where: { $0.deviceType == t }) { chosen = d; break } }
        guard let dev = chosen, let inp = try? AVCaptureDeviceInput(device: dev), session.canAddInput(inp) else {
            session.commitConfiguration()
            toast(tr("Could not open the camera.", "Erro ao abrir a câmera."))
            return
        }
        session.addInput(inp)
        device = dev
        input = inp
        if !session.outputs.contains(videoOut) {
            videoOut.alwaysDiscardsLateVideoFrames = true
            videoOut.setSampleBufferDelegate(self, queue: writerQ)
            if session.canAddOutput(videoOut) { session.addOutput(videoOut) }
        }
        applyFormat(dev)
        session.commitConfiguration()
        applyConnection(rotation: nil)

        if #available(iOS 17.0, *) {
            let pl = previewLayer
            rotationCoordinator = AVCaptureDevice.RotationCoordinator(device: dev, previewLayer: pl)
        }

        // zoom: nas câmeras com ultra-angular, "1x" é a lente principal e 0.5x é a ultra-angular
        let hasUltra = dev.deviceType == .builtInTripleCamera || dev.deviceType == .builtInDualWideCamera
        let sw = dev.virtualDeviceSwitchOverVideoZoomFactors.map { CGFloat(truncating: $0) }
        baseZoom = hasUltra ? (sw.first ?? 2) : 1
        let maxUI = min(dev.maxAvailableVideoZoomFactor, 15 * baseZoom) / baseZoom
        let presets: [CGFloat] = (hasUltra ? [0.5, 1, 2, 3] : [1, 2, 3]).filter { $0 <= maxUI + 0.01 }
        let torchOK = dev.hasTorch
        DispatchQueue.main.async {
            self.zoomPresets = presets
            self.maxZoom = maxUI
            self.hasTorch = torchOK
            self.torch = false
            self.exposure = 0
        }
        setZoomNow(1)
    }

    @discardableResult
    private func applyFormat(_ dev: AVCaptureDevice) -> Bool {
        func dims(_ r: String) -> (Int32, Int32) { r == "4K" ? (3840, 2160) : r == "720" ? (1280, 720) : (1920, 1080) }
        func pick(_ w: Int32, _ h: Int32, _ fps: Int) -> AVCaptureDevice.Format? {
            let ok = dev.formats.filter { f in
                let d = CMVideoFormatDescriptionGetDimensions(f.formatDescription)
                let sub = CMFormatDescriptionGetMediaSubType(f.formatDescription)
                return d.width == w && d.height == h
                    && (sub == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange || sub == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
                    && f.videoSupportedFrameRateRanges.contains { $0.maxFrameRate >= Double(fps) - 0.01 }
            }
            return ok.first(where: { !$0.isVideoBinned }) ?? ok.first
        }
        let (w, h) = dims(res)
        var fmt = pick(w, h, fps)
        var rate = fps
        var fallback = false
        if fmt == nil {
            fmt = pick(1920, 1080, 30); rate = 30; fallback = true
        }
        guard let f = fmt else { return false }
        do {
            try dev.lockForConfiguration()
            dev.activeFormat = f
            dev.activeVideoMinFrameDuration = CMTime(value: 1, timescale: CMTimeScale(rate))
            dev.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: CMTimeScale(rate))
            if dev.isFocusModeSupported(.continuousAutoFocus) { dev.focusMode = .continuousAutoFocus }
            if dev.isExposureModeSupported(.continuousAutoExposure) { dev.exposureMode = .continuousAutoExposure }
            dev.unlockForConfiguration()
        } catch { return false }
        if fallback {
            DispatchQueue.main.async { self.res = "1080"; self.fps = 30; self.save() }
            toast(tr("This combination doesn't work on this phone. Going back to 1080p 30fps.",
                     "Essa combinação não funciona neste celular. Voltando para 1080p 30fps."))
        }
        return !fallback
    }

    /// rotation: ângulo para gravar (nil = em pé).
    private func applyConnection(rotation: CGFloat?) {
        guard let c = videoOut.connection(with: .video) else { return }
        if c.isVideoStabilizationSupported { c.preferredVideoStabilizationMode = stab ? .standard : .off }
        let angle = rotation ?? 90
        if #available(iOS 17.0, *) {
            if c.isVideoRotationAngleSupported(angle) { c.videoRotationAngle = angle }
        } else if c.isVideoOrientationSupported {
            switch angle {
            case 0: c.videoOrientation = .landscapeRight
            case 180: c.videoOrientation = .landscapeLeft
            case 270: c.videoOrientation = .portraitUpsideDown
            default: c.videoOrientation = .portrait
            }
        }
        if c.isVideoMirroringSupported {
            c.automaticallyAdjustsVideoMirroring = false
            c.isVideoMirrored = front
        }
    }

    /// Como o celular está sendo segurado agora (em pé ou deitado).
    private func captureAngle() -> CGFloat {
        if #available(iOS 17.0, *), let rc = rotationCoordinator as? AVCaptureDevice.RotationCoordinator {
            return rc.videoRotationAngleForHorizonLevelCapture
        }
        switch UIDevice.current.orientation {
        case .landscapeLeft: return 0
        case .landscapeRight: return 180
        case .portraitUpsideDown: return 270
        default: return 90
        }
    }

    // MARK: botões

    func cycleRes() {
        guard !recording else { return }
        res = res == "720" ? "1080" : res == "1080" ? "4K" : "720"
        save(); reformat()
    }

    func cycleFps() {
        guard !recording else { return }
        fps = fps == 30 ? 60 : 30
        save(); reformat()
    }

    private func reformat() {
        sessionQ.async {
            guard let d = self.device else { return }
            self.session.beginConfiguration()
            self.applyFormat(d)
            self.session.commitConfiguration()
            self.applyConnection(rotation: nil)
            self.setZoomNow(self.zoomValue)
        }
    }

    func toggleStab() {
        guard !recording else { return }
        stab.toggle(); save()
        sessionQ.async { self.applyConnection(rotation: nil) }
    }

    func toggleGrid() { grid.toggle(); save() }

    func toggleTorch() {
        let on = !torch
        sessionQ.async {
            guard let d = self.device, d.hasTorch, (try? d.lockForConfiguration()) != nil else { return }
            d.torchMode = on ? .on : .off
            d.unlockForConfiguration()
            DispatchQueue.main.async { self.torch = on }
        }
    }

    func flip() {
        guard !recording else { return }
        front.toggle(); save()
        sessionQ.async { self.configure() }
    }

    func setSync(_ v: Int) { syncMs = v; save() }

    private var zoomValue: CGFloat = 1
    func setZoom(_ z: CGFloat) {
        let v = max(zoomPresets.first ?? 1, min(z, maxZoom))
        zoom = v
        sessionQ.async { self.setZoomNow(v) }
    }

    private func setZoomNow(_ ui: CGFloat) {
        zoomValue = ui
        guard let d = device, (try? d.lockForConfiguration()) != nil else { return }
        let f = max(d.minAvailableVideoZoomFactor, min(ui * baseZoom, d.maxAvailableVideoZoomFactor))
        d.videoZoomFactor = f
        d.unlockForConfiguration()
        DispatchQueue.main.async { self.zoom = f / self.baseZoom }
    }

    func focus(atLayerPoint p: CGPoint) {
        guard let pl = previewLayer else { return }
        let dp = pl.captureDevicePointConverted(fromLayerPoint: p)
        sessionQ.async {
            guard let d = self.device, (try? d.lockForConfiguration()) != nil else { return }
            if d.isFocusPointOfInterestSupported { d.focusPointOfInterest = dp; d.focusMode = .continuousAutoFocus }
            if d.isExposurePointOfInterestSupported { d.exposurePointOfInterest = dp; d.exposureMode = .continuousAutoExposure }
            d.unlockForConfiguration()
        }
    }

    func setExposure(_ v: Float) {
        exposure = v
        sessionQ.async {
            guard let d = self.device, (try? d.lockForConfiguration()) != nil else { return }
            let b = max(d.minExposureTargetBias, min(v, d.maxExposureTargetBias))
            d.setExposureTargetBias(b, completionHandler: nil)
            d.unlockForConfiguration()
        }
    }

    // MARK: gravação

    func toggleRecord() { recording ? stopRecord() : startRecord() }

    private func startRecord() {
        guard !busy else { return }
        busy = true
        let angle = captureAngle()
        let sync = Double(syncMs) / 1000
        sessionQ.async {
            self.applyConnection(rotation: angle)
            let settings = self.videoOut.recommendedVideoSettings(forVideoCodecType: .h264, assetWriterOutputFileType: .mp4)
            let connected = self.link.isConnected
            let sr = self.link.sampleRate, ch = self.link.channels
            let clock = self.session.synchronizationClock
            self.writerQ.asyncAfter(deadline: .now() + 0.15) {
                self.recorder.syncSeconds = sync
                self.recorder.clockConvert = { t in
                    guard let clk = clock else { return t }
                    return CMSyncConvertTime(t, from: CMClockGetHostTimeClock(), to: clk)
                }
                do {
                    try self.recorder.start(videoSettings: settings, audioRate: connected ? sr : nil, audioChannels: ch)
                    DispatchQueue.main.async {
                        self.recStart = Date(); self.seconds = 0
                        self.recording = true; self.busy = false
                    }
                } catch {
                    DispatchQueue.main.async { self.busy = false }
                    self.toast(tr("Recording error ", "Erro na gravação ") + error.localizedDescription)
                }
            }
        }
    }

    func stopRecord() {
        guard recording else { return }
        recording = false
        busy = true
        toast(tr("Saving video...", "Salvando o vídeo..."))
        writerQ.async {
            self.recorder.stop { url, hadAudio in
                DispatchQueue.main.async { self.busy = false }
                self.sessionQ.async { self.applyConnection(rotation: nil) }
                guard let url = url else {
                    self.toast(tr("Recording error", "Erro na gravação")); return
                }
                self.saveToPhotos(url, hadAudio: hadAudio)
            }
        }
    }

    private func saveToPhotos(_ url: URL, hadAudio: Bool) {
        PHPhotoLibrary.requestAuthorization(for: .addOnly) { st in
            guard st == .authorized || st == .limited else {
                self.toast(tr("Allow Photos access to save your videos.", "Permita o acesso ao Fotos para salvar os vídeos."))
                return
            }
            PHPhotoLibrary.shared().performChanges({
                let r = PHAssetCreationRequest.forAsset()
                let o = PHAssetResourceCreationOptions()
                o.shouldMoveFile = true
                r.addResource(with: .video, fileURL: url, options: o)
            }) { ok, err in
                if ok {
                    self.toast(hadAudio
                        ? tr("Saved to Photos with your plugin sound!", "Salvo no Fotos com o som do plugin!")
                        : tr("Saved WITHOUT the plugin sound (the plugin was not connected).",
                             "Salvo SEM o som do plugin (o plugin não estava conectado)."))
                } else {
                    self.toast(tr("Error saving: ", "Erro ao salvar: ") + (err?.localizedDescription ?? ""))
                }
            }
        }
    }

    // quadros da câmera (fila do gravador)
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        if recorder.isRecording { recorder.appendVideo(sampleBuffer) }
    }
}
