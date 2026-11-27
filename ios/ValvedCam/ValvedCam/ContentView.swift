import AVFoundation
import SwiftUI
import UIKit

@main
struct ValvedCamApp: App {
    var body: some Scene { WindowGroup { ContentView() } }
}

private let orange = Color(red: 0.94, green: 0.63, blue: 0.27)

struct ContentView: View {
    @StateObject private var cam = CameraModel()
    @Environment(\.scenePhase) private var phase
    @State private var showSync = false
    @State private var wasInBackground = false

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            CameraPreview(cam: cam).ignoresSafeArea()
            if cam.grid { GridOverlay().allowsHitTesting(false).ignoresSafeArea() }

            VStack(spacing: 10) {
                statusBar
                pills
                Spacer()
                if let m = cam.message {
                    Text(m).font(.system(size: 14, weight: .semibold)).multilineTextAlignment(.center)
                        .padding(.horizontal, 14).padding(.vertical, 9)
                        .background(.black.opacity(0.7)).clipShape(RoundedRectangle(cornerRadius: 12))
                        .foregroundColor(.white)
                }
                zoomRow
                exposureRow
                if showSync { syncRow }
                bottomBar
            }
            .padding(.horizontal, 14)
            .padding(.bottom, 8)
        }
        .preferredColorScheme(.dark)
        .statusBarHidden()
        .onAppear { cam.start() }
        .onChange(of: phase) { p in
            if p == .active { if wasInBackground { cam.resume() } else { cam.start() }; wasInBackground = false }
            else if p == .background { wasInBackground = true; cam.pause() }
        }
    }

    // conexão com o plugin
    private var statusBar: some View {
        VStack(spacing: 6) {
            if cam.recording {
                HStack(spacing: 6) {
                    Circle().fill(Color.red).frame(width: 10, height: 10)
                    Text(String(format: "REC %02d:%02d", cam.seconds / 60, cam.seconds % 60))
                        .font(.system(size: 15, weight: .bold, design: .monospaced))
                }
                .padding(.horizontal, 12).padding(.vertical, 5)
                .background(.black.opacity(0.6)).clipShape(Capsule())
            }
            if cam.connected {
                HStack(spacing: 8) {
                    Text(tr("● Plugin connected", "● Plugin conectado"))
                        .font(.system(size: 13, weight: .semibold)).foregroundColor(Color(red: 0.5, green: 0.89, blue: 0.6))
                    GeometryReader { g in
                        ZStack(alignment: .leading) {
                            Capsule().fill(Color.white.opacity(0.2))
                            Capsule().fill(Color(red: 0.33, green: 0.82, blue: 0.48))
                                .frame(width: g.size.width * CGFloat(min(1, cam.level * 1.4)))
                        }
                    }.frame(width: 70, height: 6)
                }
                .padding(.horizontal, 12).padding(.vertical, 6)
                .background(.black.opacity(0.55)).clipShape(Capsule())
            } else {
                Text(tr("Looking for the plugin...  (same Wi-Fi or USB)", "Procurando o plugin...  (mesmo Wi-Fi ou cabo USB)")
                     + (cam.ips.isEmpty ? "" : "\n" + tr("Phone IP: ", "IP do celular: ") + cam.ips.joined(separator: "  ")))
                    .font(.system(size: 12, weight: .semibold)).multilineTextAlignment(.center)
                    .foregroundColor(Color(red: 1, green: 0.82, blue: 0.55))
                    .padding(.horizontal, 12).padding(.vertical, 6)
                    .background(.black.opacity(0.55)).clipShape(RoundedRectangle(cornerRadius: 12))
            }
        }
        .padding(.top, 6)
    }

    private var pills: some View {
        HStack(spacing: 6) {
            Pill(text: cam.res == "4K" ? "4K" : cam.res + "p") { cam.cycleRes() }
            Pill(text: "\(cam.fps) fps") { cam.cycleFps() }
            Pill(text: cam.stab ? tr("Stab. ON", "Estab. ON") : tr("Stab. OFF", "Estab. OFF"), on: cam.stab) { cam.toggleStab() }
            if cam.hasTorch { Pill(text: cam.torch ? tr("Light ON", "Luz ON") : tr("Light", "Luz"), on: cam.torch) { cam.toggleTorch() } }
            Pill(text: cam.grid ? tr("Grid ON", "Grade ON") : tr("Grid", "Grade"), on: cam.grid) { cam.toggleGrid() }
            Pill(text: tr("Sync", "Sinc."), on: showSync) { showSync.toggle() }
        }
        .disabled(cam.recording || cam.busy)
    }

    private var zoomRow: some View {
        HStack(spacing: 8) {
            ForEach(cam.zoomPresets, id: \.self) { z in
                let active = abs(cam.zoom - z) < 0.05
                Button { cam.setZoom(z) } label: {
                    Text(active ? String(format: "%.1fx", cam.zoom) : (z < 1 ? ".5" : String(format: "%.0f", z)))
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(active ? orange : .white)
                        .frame(width: 38, height: 38)
                        .background(.black.opacity(0.55)).clipShape(Circle())
                }
            }
        }
    }

    private var exposureRow: some View {
        HStack(spacing: 10) {
            Image(systemName: "sun.max.fill").foregroundColor(.white.opacity(0.8))
            Slider(value: Binding(get: { Double(cam.exposure) }, set: { cam.setExposure(Float($0)) }), in: -2...2)
                .tint(orange)
            Text(String(format: "%+.1f", cam.exposure)).font(.system(size: 12, weight: .semibold, design: .monospaced))
                .foregroundColor(.white).frame(width: 40)
        }
        .padding(.horizontal, 12).padding(.vertical, 4)
        .background(.black.opacity(0.45)).clipShape(Capsule())
    }

    private var syncRow: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(tr("Audio sync: ", "Sincronia do som: ") + (cam.syncMs > 0 ? "+" : "") + "\(cam.syncMs) ms")
                .font(.system(size: 12, weight: .semibold)).foregroundColor(.white)
            Slider(value: Binding(get: { Double(cam.syncMs) }, set: { cam.setSync(Int(($0 / 5).rounded()) * 5) }), in: -300...300)
                .tint(orange)
            Text(tr("If the image is late, slide to the left. If the sound is late, slide to the right.",
                    "Se a imagem atrasar, arraste para a esquerda. Se o som atrasar, arraste para a direita."))
                .font(.system(size: 10)).foregroundColor(.white.opacity(0.7))
        }
        .padding(.horizontal, 14).padding(.vertical, 6)
        .background(.black.opacity(0.55)).clipShape(RoundedRectangle(cornerRadius: 14))
    }

    private var bottomBar: some View {
        HStack {
            Color.clear.frame(width: 64, height: 64)
            Spacer()
            Button { cam.toggleRecord() } label: {
                ZStack {
                    Circle().stroke(Color.white, lineWidth: 4).frame(width: 78, height: 78)
                    RoundedRectangle(cornerRadius: cam.recording ? 8 : 30)
                        .fill(Color.red)
                        .frame(width: cam.recording ? 32 : 60, height: cam.recording ? 32 : 60)
                        .animation(.easeInOut(duration: 0.2), value: cam.recording)
                }
            }
            .disabled(cam.busy)
            Spacer()
            Button { cam.flip() } label: {
                VStack(spacing: 3) {
                    Image(systemName: "arrow.triangle.2.circlepath.camera")
                        .font(.system(size: 22, weight: .semibold))
                        .frame(width: 52, height: 52)
                        .background(.black.opacity(0.55)).clipShape(Circle())
                    Text(cam.front ? tr("Front", "Frontal") : tr("Back", "Traseira"))
                        .font(.system(size: 11, weight: .semibold))
                }
                .foregroundColor(.white)
            }
            .frame(width: 64)
            .disabled(cam.recording || cam.busy)
            .accessibilityLabel(tr("Switch camera", "Trocar câmera"))
        }
    }
}

private struct Pill: View {
    let text: String
    var on: Bool = false
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(text).font(.system(size: 12, weight: .bold)).lineLimit(1)
                .foregroundColor(on ? .black : .white)
                .padding(.horizontal, 9).padding(.vertical, 6)
                .background(on ? orange : Color.black.opacity(0.55))
                .clipShape(Capsule())
        }
    }
}

private struct GridOverlay: View {
    var body: some View {
        GeometryReader { g in
            Path { p in
                for i in 1...2 {
                    let x = g.size.width * CGFloat(i) / 3, y = g.size.height * CGFloat(i) / 3
                    p.move(to: CGPoint(x: x, y: 0)); p.addLine(to: CGPoint(x: x, y: g.size.height))
                    p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: g.size.width, y: y))
                }
            }
            .stroke(Color.white.opacity(0.45), lineWidth: 1)
        }
    }
}

/// Imagem da câmera, com toque para focar e pinça para zoom.
struct CameraPreview: UIViewRepresentable {
    let cam: CameraModel

    final class PreviewView: UIView {
        override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
        var previewLayer: AVCaptureVideoPreviewLayer { layer as! AVCaptureVideoPreviewLayer }
    }

    final class Coordinator: NSObject {
        let cam: CameraModel
        var pinchStart: CGFloat = 1
        init(cam: CameraModel) { self.cam = cam }

        @objc func tap(_ g: UITapGestureRecognizer) {
            guard let v = g.view else { return }
            let p = g.location(in: v)
            cam.focus(atLayerPoint: p)
            let ring = UIView(frame: CGRect(x: p.x - 36, y: p.y - 36, width: 72, height: 72))
            ring.layer.borderColor = UIColor(red: 0.94, green: 0.63, blue: 0.27, alpha: 1).cgColor
            ring.layer.borderWidth = 2
            ring.layer.cornerRadius = 10
            v.addSubview(ring)
            UIView.animate(withDuration: 0.8, delay: 0.4, options: []) { ring.alpha = 0 } completion: { _ in ring.removeFromSuperview() }
        }

        @objc func pinch(_ g: UIPinchGestureRecognizer) {
            if g.state == .began { pinchStart = cam.zoom }
            cam.setZoom(pinchStart * g.scale)
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(cam: cam) }

    func makeUIView(context: Context) -> PreviewView {
        let v = PreviewView()
        v.backgroundColor = .black
        v.previewLayer.session = cam.session
        v.previewLayer.videoGravity = .resizeAspectFill
        cam.previewLayer = v.previewLayer
        v.addGestureRecognizer(UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tap(_:))))
        v.addGestureRecognizer(UIPinchGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.pinch(_:))))
        return v
    }

    func updateUIView(_ uiView: PreviewView, context: Context) {}
}
