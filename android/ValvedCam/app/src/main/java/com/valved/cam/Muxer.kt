package com.valved.cam

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.media.MediaMuxer
import java.io.BufferedInputStream
import java.io.File
import java.io.FileDescriptor
import java.io.FileInputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.min

/**
 * Junta o vídeo da câmera (sem som) com o som do Valved (PCM 16 bits) num MP4 só.
 * audioStartUs = em que ponto do vídeo a primeira amostra de áudio deve tocar
 * (negativo: corta o começo do áudio; positivo: põe silêncio antes).
 */
object Muxer {
    private class Sample(val data: ByteArray, val ptsUs: Long, val flags: Int)
    private class AudioTrack(val format: MediaFormat, val samples: List<Sample>)

    fun mux(video: File, pcm: File?, sampleRate: Int, channels: Int, audioStartUs: Long, out: FileDescriptor) {
        val ex = MediaExtractor()
        ex.setDataSource(video.absolutePath)
        var vTrack = -1
        var vFmt: MediaFormat? = null
        for (i in 0 until ex.trackCount) {
            val f = ex.getTrackFormat(i)
            if ((f.getString(MediaFormat.KEY_MIME) ?: "").startsWith("video/")) { vTrack = i; vFmt = f; break }
        }
        require(vTrack >= 0 && vFmt != null) { "vídeo sem trilha de imagem" }
        ex.selectTrack(vTrack)
        val durUs = if (vFmt.containsKey(MediaFormat.KEY_DURATION)) vFmt.getLong(MediaFormat.KEY_DURATION) else 0L
        val rotation = try {
            val r = MediaMetadataRetriever()
            r.setDataSource(video.absolutePath)
            val v = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
            r.release(); v
        } catch (e: Exception) { 0 }

        val audio = if (pcm != null && pcm.length() > 0) encodeAac(pcm, sampleRate, channels, audioStartUs, durUs) else null

        val mux = MediaMuxer(out, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
        mux.setOrientationHint(rotation)
        val mv = mux.addTrack(vFmt)
        val ma = if (audio != null) mux.addTrack(audio.format) else -1
        mux.start()

        val maxIn = if (vFmt.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) vFmt.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE) else 0
        val buf = ByteBuffer.allocateDirect(maxOf(maxIn, 8 * 1024 * 1024))
        val info = MediaCodec.BufferInfo()
        var vBase = -1L
        var ai = 0
        fun writeAudioUpTo(limitUs: Long) {
            if (audio == null) return
            while (ai < audio.samples.size && audio.samples[ai].ptsUs + maxOf(vBase, 0L) <= limitUs) {
                val s = audio.samples[ai++]
                info.set(0, s.data.size, s.ptsUs + maxOf(vBase, 0L), s.flags)
                mux.writeSampleData(ma, ByteBuffer.wrap(s.data), info)
            }
        }
        while (true) {
            buf.clear()
            val size = ex.readSampleData(buf, 0)
            if (size < 0) break
            val pts = ex.sampleTime
            if (vBase < 0) vBase = pts
            writeAudioUpTo(pts)
            val flags = if (ex.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0
            info.set(0, size, pts, flags)
            mux.writeSampleData(mv, buf, info)
            ex.advance()
        }
        writeAudioUpTo(Long.MAX_VALUE)
        mux.stop()
        mux.release()
        ex.release()
    }

    private fun encodeAac(pcm: File, srIn: Int, ch: Int, startUs: Long, maxDurUs: Long): AudioTrack {
        // AAC vai até 48 kHz com folga: 88,2/96/192 kHz são reduzidos pela metade (média de pares)
        var factor = 1
        var sr = srIn
        while (sr > 48000 && sr % 2 == 0) { factor *= 2; sr /= 2 }
        val frameIn = 2 * ch

        val fmt = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sr, ch).apply {
            setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, 256_000)
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 1 shl 16)
        }
        val enc = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        enc.configure(fmt, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        enc.start()

        val input: InputStream = BufferedInputStream(FileInputStream(pcm), 1 shl 16)
        var padFrames = 0L
        if (startUs >= 0) padFrames = startUs * sr / 1_000_000
        else {
            var skip = (-startUs) * srIn / 1_000_000 * frameIn
            while (skip > 0) { val k = input.skip(skip); if (k <= 0) break; skip -= k }
        }
        val totalFrames = if (maxDurUs > 0) maxDurUs * sr / 1_000_000 else Long.MAX_VALUE

        val samples = ArrayList<Sample>()
        var outFormat: MediaFormat? = null
        val info = MediaCodec.BufferInfo()
        var framesFed = 0L
        var eosSent = false
        var srcDone = false
        val raw = ByteArray(1024 * frameIn * factor)
        val cooked = ByteArray(1024 * frameIn)
        val zeros = ByteArray(1024 * frameIn)

        while (true) {
            if (!eosSent) {
                val ii = enc.dequeueInputBuffer(10_000)
                if (ii >= 0) {
                    val ib = enc.getInputBuffer(ii)!!
                    ib.clear()
                    val want = min(min(1024L, ib.capacity().toLong() / frameIn), totalFrames - framesFed).toInt()
                    var n = 0
                    if (want > 0 && padFrames > 0) {
                        val p = min(padFrames, want.toLong()).toInt()
                        ib.put(zeros, 0, p * frameIn); n += p; padFrames -= p
                    }
                    if (n < want && !srcDone) {
                        val needBytes = (want - n) * frameIn * factor
                        var got = 0
                        while (got < needBytes) {
                            val k = input.read(raw, got, needBytes - got)
                            if (k <= 0) { srcDone = true; break }
                            got += k
                        }
                        val frames = got / (frameIn * factor)
                        if (factor == 1) ib.put(raw, 0, frames * frameIn)
                        else {
                            val rb = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN)
                            val cb = ByteBuffer.wrap(cooked).order(ByteOrder.LITTLE_ENDIAN)
                            for (f in 0 until frames) for (c in 0 until ch) {
                                var acc = 0
                                for (k in 0 until factor) acc += rb.getShort(((f * factor + k) * ch + c) * 2).toInt()
                                cb.putShort((f * ch + c) * 2, (acc / factor).toShort())
                            }
                            ib.put(cooked, 0, frames * frameIn)
                        }
                        n += frames
                    }
                    val pts = framesFed * 1_000_000 / sr
                    if (n == 0) {
                        enc.queueInputBuffer(ii, 0, 0, pts, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        eosSent = true
                    } else {
                        enc.queueInputBuffer(ii, 0, n * frameIn, pts, 0)
                        framesFed += n
                    }
                }
            }
            val oi = enc.dequeueOutputBuffer(info, 10_000)
            if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                outFormat = enc.outputFormat
            } else if (oi >= 0) {
                val ob = enc.getOutputBuffer(oi)!!
                if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0 && info.size > 0) {
                    val d = ByteArray(info.size)
                    ob.position(info.offset); ob.limit(info.offset + info.size); ob.get(d)
                    samples.add(Sample(d, info.presentationTimeUs, info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM.inv()))
                }
                enc.releaseOutputBuffer(oi, false)
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) break
            }
        }
        input.close()
        enc.stop()
        enc.release()
        return AudioTrack(outFormat ?: fmt, samples)
    }
}
