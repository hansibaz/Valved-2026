package com.valved.cam

import android.os.SystemClock
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.concurrent.thread
import kotlin.math.abs
import kotlin.math.max

/**
 * Recebe o som do plugin Valved.
 * - Anuncia "VALVEDCAM|<nome>|<porta>" por UDP (porta 47801) em todas as redes (Wi-Fi e ancoragem USB).
 * - O plugin conecta por TCP e manda: "VLVA" + taxa (int32) + canais (int16) + bits (int16) e depois PCM 16 bits.
 * - Durante a gravação, o PCM vai para um arquivo, junto com o horário da primeira amostra.
 */
class AudioLink(private val deviceName: String) {
    @Volatile var connected = false; private set
    @Volatile var sampleRate = 48000; private set
    @Volatile var channels = 2; private set
    @Volatile var level = 0f; private set
    @Volatile var port = 0; private set
    @Volatile var peerName = ""; private set

    @Volatile private var running = false
    private var server: ServerSocket? = null
    private val lock = Any()
    private var capOut: BufferedOutputStream? = null
    private var capBytes = 0L
    @Volatile var capFirstSampleNs = 0L; private set

    fun start() {
        if (running) return
        running = true
        thread(name = "valvedcam-server", isDaemon = true) { serverLoop() }
        thread(name = "valvedcam-beacon", isDaemon = true) { beaconLoop() }
    }

    fun stop() {
        running = false
        try { server?.close() } catch (_: Exception) {}
    }

    /** Começa a guardar o som que chegar a partir de agora. */
    fun startCapture(file: File) {
        synchronized(lock) {
            capOut?.close()
            capOut = BufferedOutputStream(FileOutputStream(file), 1 shl 16)
            capBytes = 0
            capFirstSampleNs = 0
        }
    }

    /** Para de guardar; devolve quantos bytes de áudio foram gravados. */
    fun stopCapture(): Long {
        synchronized(lock) {
            try { capOut?.close() } catch (_: Exception) {}
            capOut = null
            return capBytes
        }
    }

    private fun serverLoop() {
        var s: ServerSocket? = null
        for (p in 47800..47810) {
            s = try { ServerSocket(p) } catch (e: IOException) { null }
            if (s != null) break
        }
        if (s == null) return
        server = s
        port = s.localPort
        while (running) {
            val c = try { s.accept() } catch (e: IOException) { break }
            handle(c)
        }
    }

    private fun handle(c: Socket) {
        try {
            c.tcpNoDelay = true
            c.receiveBufferSize = 1 shl 18
            c.soTimeout = 2000
            val inp = DataInputStream(BufferedInputStream(c.getInputStream(), 1 shl 16))
            val h = ByteArray(12)
            inp.readFully(h)
            if (String(h, 0, 4, Charsets.US_ASCII) != "VLVA") return
            val bb = ByteBuffer.wrap(h).order(ByteOrder.LITTLE_ENDIAN)
            sampleRate = bb.getInt(4)
            channels = max(1, bb.getShort(8).toInt())
            peerName = c.inetAddress.hostAddress ?: ""
            connected = true
            val buf = ByteArray(8192)
            var silentReads = 0
            while (running) {
                val n = try { inp.read(buf) } catch (e: SocketTimeoutException) {
                    // o plugin pode ficar parado (DAW pausada): mantém a conexão por um tempo
                    if (++silentReads > 30) break else { level = 0f; continue }
                }
                if (n <= 0) break
                silentReads = 0
                val now = SystemClock.elapsedRealtimeNanos()
                var pk = 0
                var i = 0
                while (i + 1 < n) {
                    val v = (buf[i].toInt() and 0xff) or (buf[i + 1].toInt() shl 8)
                    pk = max(pk, abs(v.toShort().toInt()))
                    i += 8
                }
                level = max(pk / 32768f, level * 0.85f)
                synchronized(lock) {
                    val out = capOut
                    if (out != null) {
                        if (capBytes == 0L) {
                            val frames = n / (2 * channels)
                            capFirstSampleNs = now - frames * 1_000_000_000L / sampleRate
                        }
                        out.write(buf, 0, n)
                        capBytes += n
                    }
                }
            }
        } catch (_: Exception) {
        } finally {
            connected = false
            level = 0f
            try { c.close() } catch (_: Exception) {}
        }
    }

    private fun beaconLoop() {
        val sock = try { DatagramSocket().apply { broadcast = true } } catch (e: Exception) { return }
        while (running) {
            if (port > 0 && !connected) {
                val msg = "VALVEDCAM|$deviceName|$port".toByteArray()
                for (addr in broadcastAddresses())
                    try { sock.send(DatagramPacket(msg, msg.size, addr, 47801)) } catch (_: Exception) {}
            }
            try { Thread.sleep(1000) } catch (_: InterruptedException) { break }
        }
        sock.close()
    }

    /** Endereços IPv4 do celular (para mostrar na tela). */
    fun localAddresses(): List<String> = try {
        NetworkInterface.getNetworkInterfaces().toList()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { ni -> ni.interfaceAddresses.mapNotNull { a -> a.address?.hostAddress?.takeIf { !it.contains(':') } } }
    } catch (e: Exception) { emptyList() }

    private fun broadcastAddresses(): List<InetAddress> {
        val list = mutableListOf<InetAddress>()
        try {
            for (ni in NetworkInterface.getNetworkInterfaces()) {
                if (!ni.isUp || ni.isLoopback) continue
                for (ia in ni.interfaceAddresses) ia.broadcast?.let { list.add(it) }
            }
        } catch (_: Exception) {}
        try { list.add(InetAddress.getByName("255.255.255.255")) } catch (_: Exception) {}
        return list
    }
}
