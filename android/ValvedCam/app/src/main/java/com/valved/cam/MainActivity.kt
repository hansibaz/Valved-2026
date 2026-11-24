package com.valved.cam

import android.Manifest
import android.annotation.SuppressLint
import android.content.ContentValues
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.MediaStore
import android.util.Range
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.OrientationEventListener
import android.view.ScaleGestureDetector
import android.view.Surface
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.Camera
import androidx.camera.core.CameraSelector
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.MirrorMode
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.video.FallbackStrategy
import androidx.camera.video.FileOutputOptions
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector
import androidx.camera.video.Recorder
import androidx.camera.video.Recording
import androidx.camera.video.VideoCapture
import androidx.camera.video.VideoRecordEvent
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class MainActivity : ComponentActivity() {

    private lateinit var link: AudioLink
    private lateinit var previewView: PreviewView
    private lateinit var overlay: Overlay
    private lateinit var statusText: TextView
    private lateinit var recButton: View
    private lateinit var timerText: TextView
    private lateinit var zoomText: TextView
    private lateinit var expBar: SeekBar
    private lateinit var expLabel: TextView
    private lateinit var syncLabel: TextView
    private lateinit var flipButton: TextView
    private lateinit var flipLabel: TextView
    private val pills = mutableMapOf<String, TextView>()
    private val ui = Handler(Looper.getMainLooper())

    private var provider: ProcessCameraProvider? = null
    private var camera: Camera? = null
    private var videoCapture: VideoCapture<Recorder>? = null
    private var recording: Recording? = null

    // opções da câmera (guardadas entre aberturas do app)
    private val prefs by lazy { getSharedPreferences("valvedcam", Context.MODE_PRIVATE) }
    private var useFront = false
    private var quality = "FHD"
    private var fps = 30
    private var stabilization = false
    private var torch = false
    private var grid = true
    private var syncMs = 120

    private var supportedQualities: List<Quality> = emptyList()
    private var supports60 = false
    private var supportsStab = false

    private var recStartWall = 0L
    private var videoStartNs = 0L
    private var tmpVideo: File? = null
    private var tmpPcm: File? = null
    private var rotation = Surface.ROTATION_0
    @Volatile private var saving = false

    private val permLauncher = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) startCamera() else statusText.text = "Permita o uso da câmera para gravar."
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        useFront = prefs.getBoolean("front", false)
        quality = prefs.getString("quality", "FHD") ?: "FHD"
        fps = prefs.getInt("fps", 30)
        stabilization = prefs.getBoolean("stab", false)
        grid = prefs.getBoolean("grid", true)
        syncMs = prefs.getInt("sync", 120)

        buildUi()
        link = AudioLink(Build.MODEL ?: "Android")
        link.start()
        ui.post(statusTick)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) startCamera()
        else permLauncher.launch(Manifest.permission.CAMERA)

        object : OrientationEventListener(this) {
            override fun onOrientationChanged(o: Int) {
                if (o == ORIENTATION_UNKNOWN) return
                val r = when (o) {
                    in 45 until 135 -> Surface.ROTATION_270
                    in 135 until 225 -> Surface.ROTATION_180
                    in 225 until 315 -> Surface.ROTATION_90
                    else -> Surface.ROTATION_0
                }
                if (r != rotation) { rotation = r; if (recording == null) videoCapture?.targetRotation = r }
            }
        }.enable()
    }

    override fun onDestroy() {
        super.onDestroy()
        recording?.stop()
        link.stop()
        ui.removeCallbacksAndMessages(null)
    }

    // ---------------------------------------------------------------- câmera

    private fun startCamera() {
        val f = ProcessCameraProvider.getInstance(this)
        f.addListener({ provider = f.get(); bindCamera() }, ContextCompat.getMainExecutor(this))
    }

    private fun qualityOf(name: String): Quality = when (name) {
        "SD" -> Quality.SD
        "HD" -> Quality.HD
        "UHD" -> Quality.UHD
        else -> Quality.FHD
    }

    private fun nameOf(q: Quality): String = when (q) {
        Quality.SD -> "SD"
        Quality.HD -> "HD"
        Quality.UHD -> "UHD"
        else -> "FHD"
    }

    private fun bindCamera() {
        val p = provider ?: return
        val selector = if (useFront) CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
        if (!p.hasCamera(selector)) { useFront = !useFront; return bindCamera() }

        val info = p.availableCameraInfos.firstOrNull { selector.filter(listOf(it)).isNotEmpty() }
        if (info != null) {
            val caps = Recorder.getVideoCapabilities(info)
            supportedQualities = caps.getSupportedQualities(androidx.camera.core.DynamicRange.SDR)
            supportsStab = caps.isStabilizationSupported
            supports60 = info.supportedFrameRateRanges.any { it.upper >= 60 }
        }
        if (supportedQualities.isNotEmpty() && supportedQualities.none { nameOf(it) == quality })
            quality = nameOf(supportedQualities.first())
        if (!supports60) fps = 30
        if (!supportsStab) stabilization = false

        val preview = Preview.Builder()
            .setResolutionSelector(ResolutionSelector.Builder().setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY).build())
            .build()
        preview.setSurfaceProvider(previewView.surfaceProvider)

        val recorder = Recorder.Builder()
            .setQualitySelector(QualitySelector.from(qualityOf(quality), FallbackStrategy.lowerQualityOrHigherThan(qualityOf(quality))))
            .build()
        val vcBuilder = VideoCapture.Builder(recorder)
            .setMirrorMode(MirrorMode.MIRROR_MODE_ON_FRONT_ONLY)
            .setTargetFrameRate(Range(fps, fps))
        if (supportsStab) vcBuilder.setVideoStabilizationEnabled(stabilization)
        val vc = vcBuilder.build()
        vc.targetRotation = rotation

        try {
            p.unbindAll()
            camera = p.bindToLifecycle(this, selector, preview, vc)
            videoCapture = vc
        } catch (e: Exception) {
            // combinação não suportada (ex.: 4K a 60 fps): volta para o padrão
            if (fps != 30 || quality != "FHD") {
                toast("Essa combinação não funciona neste celular. Voltando para 1080p 30fps.")
                fps = 30; quality = "FHD"; stabilization = false
                return bindCamera()
            }
            statusText.text = "Erro ao abrir a câmera: ${e.message}"
            return
        }
        torch = false
        setupCameraControls()
        savePrefs()
        refreshPills()
    }

    private fun setupCameraControls() {
        val cam = camera ?: return
        cam.cameraInfo.zoomState.observe(this) { z ->
            zoomText.text = String.format(Locale.US, "%.1fx", z.zoomRatio)
        }
        val es = cam.cameraInfo.exposureState
        if (es.isExposureCompensationSupported) {
            val r = es.exposureCompensationRange
            expBar.isEnabled = true
            expBar.max = r.upper - r.lower
            expBar.progress = es.exposureCompensationIndex - r.lower
            expBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, v: Int, fromUser: Boolean) {
                    val idx = v + r.lower
                    expLabel.text = "Exposição " + (if (idx > 0) "+$idx" else "$idx")
                    if (fromUser) cam.cameraControl.setExposureCompensationIndex(idx)
                }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
            expLabel.text = "Exposição " + es.exposureCompensationIndex
        } else {
            expBar.isEnabled = false
            expLabel.text = "Exposição (não suportada)"
        }
    }

    private fun setZoom(r: Float) {
        val cam = camera ?: return
        val z = cam.cameraInfo.zoomState.value ?: return
        cam.cameraControl.setZoomRatio(r.coerceIn(z.minZoomRatio, z.maxZoomRatio))
    }

    // ---------------------------------------------------------------- gravação

    @SuppressLint("MissingPermission")
    private fun toggleRecording() {
        val active = recording
        if (active != null) { active.stop(); return }
        val vc = videoCapture ?: return
        vc.targetRotation = rotation
        val v = File(cacheDir, "rec_video.mp4").also { it.delete() }
        val a = File(cacheDir, "rec_audio.pcm").also { it.delete() }
        tmpVideo = v; tmpPcm = a
        link.startCapture(a)
        videoStartNs = 0L
        recording = vc.output
            .prepareRecording(this, FileOutputOptions.Builder(v).build())
            .start(ContextCompat.getMainExecutor(this)) { ev ->
                when (ev) {
                    is VideoRecordEvent.Start -> {
                        videoStartNs = SystemClock.elapsedRealtimeNanos()
                        recStartWall = SystemClock.elapsedRealtime()
                        setRecordingUi(true)
                    }
                    is VideoRecordEvent.Finalize -> {
                        val bytes = link.stopCapture()
                        recording = null
                        setRecordingUi(false)
                        if (ev.hasError() && !v.exists()) { toast("Erro na gravação (${ev.error})"); return@start }
                        saveToGallery(v, if (bytes > 0) a else null)
                    }
                    else -> {}
                }
            }
    }

    private fun saveToGallery(video: File, pcm: File?) {
        saving = true
        statusText.text = "Salvando o vídeo..."
        val sr = link.sampleRate
        val ch = link.channels
        val audioStartUs = if (pcm != null && link.capFirstSampleNs > 0 && videoStartNs > 0)
            (link.capFirstSampleNs - videoStartNs) / 1000 - syncMs * 1000L else 0L
        thread(name = "valvedcam-save") {
            val name = "ValvedCam_" + SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date()) + ".mp4"
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, name)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/ValvedCam")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val uri = contentResolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
            var msg: String
            try {
                requireNotNull(uri)
                contentResolver.openFileDescriptor(uri, "rw")!!.use { pfd ->
                    Muxer.mux(video, pcm, sr, ch, audioStartUs, pfd.fileDescriptor)
                }
                values.clear()
                values.put(MediaStore.Video.Media.IS_PENDING, 0)
                contentResolver.update(uri, values, null, null)
                msg = if (pcm != null) "Salvo na galeria (Movies/ValvedCam) com o som do Valved!"
                      else "Salvo SEM o som do Valved (o plugin não estava conectado)."
            } catch (e: Exception) {
                if (uri != null) contentResolver.delete(uri, null, null)
                msg = "Erro ao salvar: ${e.message}"
            }
            video.delete(); pcm?.delete()
            ui.post { saving = false; toast(msg) }
        }
    }

    private fun setRecordingUi(on: Boolean) {
        (recButton.background as GradientDrawable).apply {
            cornerRadius = if (on) dp(10).toFloat() else dp(36).toFloat()
        }
        recButton.scaleX = if (on) 0.55f else 1f
        recButton.scaleY = if (on) 0.55f else 1f
        timerText.visibility = if (on) View.VISIBLE else View.INVISIBLE
        flipButton.alpha = if (on) 0.3f else 1f
        for ((k, v) in pills) if (k != "torch" && !k.startsWith("z")) { v.isEnabled = !on; v.alpha = if (on) 0.35f else 1f }
    }

    private val statusTick = object : Runnable {
        override fun run() {
            val bars = (link.level * 10).toInt().coerceIn(0, 10)
            val meter = "▮".repeat(bars) + "▯".repeat(10 - bars)
            if (!saving) {
                statusText.text = if (link.connected)
                    "● Valved conectado  ${link.sampleRate / 1000.0} kHz   $meter"
                else
                    "Procurando o Valved...  (mesmo Wi-Fi ou ancoragem USB)\nIP do celular: " + link.localAddresses().joinToString("  ")
                statusText.setTextColor(if (link.connected) Color.rgb(120, 220, 140) else Color.rgb(240, 200, 120))
            }
            if (recording != null && recStartWall > 0) {
                val s = (SystemClock.elapsedRealtime() - recStartWall) / 1000
                timerText.text = String.format(Locale.US, "● %02d:%02d", s / 60, s % 60)
            }
            ui.postDelayed(this, 100)
        }
    }

    // ---------------------------------------------------------------- tela

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun pill(key: String, onClick: () -> Unit): TextView = TextView(this).apply {
        setTextColor(Color.WHITE)
        textSize = 13f
        setPadding(dp(12), dp(7), dp(12), dp(7))
        background = GradientDrawable().apply { cornerRadius = dp(18).toFloat(); setColor(Color.argb(150, 20, 18, 22)) }
        setOnClickListener { onClick() }
        pills[key] = this
    }

    private fun refreshPills() {
        pills["res"]?.text = when (quality) { "SD" -> "480p"; "HD" -> "720p"; "UHD" -> "4K"; else -> "1080p" }
        pills["fps"]?.apply { text = "$fps fps"; alpha = if (supports60) 1f else 0.4f }
        pills["stab"]?.apply { text = if (stabilization) "Estab. ON" else "Estab. OFF"; alpha = if (supportsStab) 1f else 0.4f }
        pills["torch"]?.apply { text = if (torch) "Luz ON" else "Luz"; alpha = if (camera?.cameraInfo?.hasFlashUnit() == true) 1f else 0.4f }
        pills["grid"]?.text = if (grid) "Grade ON" else "Grade"
        pills["flip"]?.text = if (useFront) "Frontal" else "Traseira"
        flipLabel.text = if (useFront) "Frontal" else "Traseira"
        overlay.showGrid = grid
        overlay.invalidate()
        syncLabel.text = "Sincronia do som: $syncMs ms"
    }

    private fun savePrefs() {
        prefs.edit().putBoolean("front", useFront).putString("quality", quality).putInt("fps", fps)
            .putBoolean("stab", stabilization).putBoolean("grid", grid).putInt("sync", syncMs).apply()
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    @SuppressLint("ClickableViewAccessibility")
    private fun buildUi() {
        val root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        previewView = PreviewView(this).apply { scaleType = PreviewView.ScaleType.FIT_CENTER }
        root.addView(previewView, FrameLayout.LayoutParams(-1, -1))
        overlay = Overlay(this)
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))

        // gestos: pinça = zoom, toque = foco/exposição naquele ponto
        val scale = ScaleGestureDetector(this, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(d: ScaleGestureDetector): Boolean {
                val z = camera?.cameraInfo?.zoomState?.value ?: return true
                setZoom(z.zoomRatio * d.scaleFactor); return true
            }
        })
        val tap = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onSingleTapUp(e: MotionEvent): Boolean {
                val cam = camera ?: return true
                val pt = previewView.meteringPointFactory.createPoint(e.x, e.y)
                cam.cameraControl.startFocusAndMetering(
                    FocusMeteringAction.Builder(pt).setAutoCancelDuration(4, TimeUnit.SECONDS).build())
                overlay.showFocus(e.x, e.y)
                return true
            }
        })
        previewView.setOnTouchListener { _, e -> scale.onTouchEvent(e); tap.onTouchEvent(e); true }

        // topo: status + opções
        val top = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(10), dp(28), dp(10), 0) }
        statusText = TextView(this).apply {
            textSize = 13f; setTextColor(Color.WHITE)
            background = GradientDrawable().apply { cornerRadius = dp(10).toFloat(); setColor(Color.argb(150, 0, 0, 0)) }
            setPadding(dp(10), dp(6), dp(10), dp(6))
        }
        top.addView(statusText, LinearLayout.LayoutParams(-2, -2))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(8), 0, 0) }
        fun add(v: View) = row.addView(v, LinearLayout.LayoutParams(-2, -2).apply { marginEnd = dp(6) })
        add(pill("res") {
            val order = listOf("SD", "HD", "FHD", "UHD").filter { n -> supportedQualities.isEmpty() || supportedQualities.any { nameOf(it) == n } }
            quality = order[(order.indexOf(quality) + 1) % order.size]; bindCamera()
        })
        add(pill("fps") { if (supports60) { fps = if (fps == 30) 60 else 30; bindCamera() } else toast("Este celular não grava a 60 fps aqui.") })
        add(pill("stab") { if (supportsStab) { stabilization = !stabilization; bindCamera() } else toast("Estabilização não disponível nesta câmera.") })
        add(pill("torch") {
            val cam = camera ?: return@pill
            if (cam.cameraInfo.hasFlashUnit()) { torch = !torch; cam.cameraControl.enableTorch(torch); refreshPills() }
        })
        add(pill("grid") { grid = !grid; savePrefs(); refreshPills() })
        add(pill("flip") { useFront = !useFront; bindCamera() })
        top.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(row) })
        root.addView(top, FrameLayout.LayoutParams(-1, -2, Gravity.TOP))

        // baixo: zoom, exposição, sincronia, REC
        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(16), dp(10), dp(16), dp(24))
            setBackgroundColor(Color.argb(110, 0, 0, 0))
        }
        val zoomRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER }
        zoomText = TextView(this).apply { setTextColor(Color.rgb(240, 160, 69)); textSize = 14f; setPadding(0, 0, dp(10), 0) }
        zoomRow.addView(zoomText)
        for (z in listOf(0.5f, 1f, 2f, 3f)) {
            zoomRow.addView(pill("z$z") { setZoom(z) }.apply { text = if (z < 1f) ".5x" else "${z.toInt()}x" },
                LinearLayout.LayoutParams(-2, -2).apply { marginEnd = dp(6) })
        }
        bottom.addView(zoomRow)

        expLabel = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f; setPadding(0, dp(8), 0, 0) }
        expBar = SeekBar(this)
        bottom.addView(expLabel, LinearLayout.LayoutParams(-1, -2))
        bottom.addView(expBar, LinearLayout.LayoutParams(-1, -2))

        syncLabel = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f }
        val syncBar = SeekBar(this).apply {
            max = 400; progress = syncMs
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, v: Int, fromUser: Boolean) { syncMs = v; syncLabel.text = "Sincronia do som: $v ms" }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) { savePrefs() }
            })
        }
        bottom.addView(syncLabel, LinearLayout.LayoutParams(-1, -2))
        bottom.addView(syncBar, LinearLayout.LayoutParams(-1, -2))

        timerText = TextView(this).apply {
            setTextColor(Color.rgb(255, 80, 70)); textSize = 16f; gravity = Gravity.CENTER
            visibility = View.INVISIBLE; setPadding(0, dp(6), 0, dp(4))
        }
        bottom.addView(timerText)
        val recOuter = FrameLayout(this).apply {
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setStroke(dp(4), Color.WHITE) }
            setOnClickListener { toggleRecording() }
        }
        recButton = View(this).apply {
            background = GradientDrawable().apply { cornerRadius = dp(36).toFloat(); setColor(Color.rgb(230, 50, 45)) }
        }
        recOuter.addView(recButton, FrameLayout.LayoutParams(dp(60), dp(60), Gravity.CENTER))
        // REC no meio; à direita, o botão de trocar câmera (frontal/traseira), como no app de câmera do celular
        val recRow = FrameLayout(this)
        recRow.addView(recOuter, FrameLayout.LayoutParams(dp(78), dp(78), Gravity.CENTER))
        flipButton = TextView(this).apply {
            text = "⟲"
            textSize = 26f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(Color.argb(160, 40, 38, 44)) }
            contentDescription = "Trocar câmera"
            setOnClickListener { if (recording == null) { useFront = !useFront; bindCamera() } }
        }
        val flipCol = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL }
        flipCol.addView(flipButton, LinearLayout.LayoutParams(dp(54), dp(54)))
        flipLabel = TextView(this).apply { setTextColor(Color.WHITE); textSize = 11f; gravity = Gravity.CENTER }
        flipCol.addView(flipLabel)
        recRow.addView(flipCol, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER_VERTICAL or Gravity.END).apply { marginEnd = dp(28) })
        bottom.addView(recRow, LinearLayout.LayoutParams(-1, dp(84)))
        root.addView(bottom, FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM))

        setContentView(root)
        refreshPills()
    }

    /** Grade dos terços e o círculo do foco. */
    private class Overlay(ctx: Context) : View(ctx) {
        var showGrid = true
        private var fx = -1f
        private var fy = -1f
        private var focusUntil = 0L
        private val line = Paint().apply { color = Color.argb(90, 255, 255, 255); strokeWidth = 2f }
        private val ring = Paint().apply { color = Color.rgb(240, 160, 69); style = Paint.Style.STROKE; strokeWidth = 5f; isAntiAlias = true }

        fun showFocus(x: Float, y: Float) { fx = x; fy = y; focusUntil = SystemClock.uptimeMillis() + 1200; invalidate(); postInvalidateDelayed(1250) }

        override fun onDraw(c: Canvas) {
            if (showGrid) {
                for (i in 1..2) {
                    c.drawLine(width * i / 3f, 0f, width * i / 3f, height.toFloat(), line)
                    c.drawLine(0f, height * i / 3f, width.toFloat(), height * i / 3f, line)
                }
            }
            if (fx >= 0 && SystemClock.uptimeMillis() < focusUntil) c.drawCircle(fx, fy, 70f, ring)
        }
    }
}
