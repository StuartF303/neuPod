// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod.neupod_audio

import android.content.Context
import android.net.Uri
import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.datasource.ContentDataSource
import androidx.media3.datasource.DataSource
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.FileDataSource
import androidx.media3.datasource.TransferListener
import java.io.IOException

/**
 * Opens `content://` and `file://` URIs and nothing else.
 *
 * R1 makes no network connection (docs/app/README.md, Privacy). Media3's DefaultDataSource would
 * also route `http`, `udp`, `rtmp` and `data` URIs, so the player and the artwork loader use this
 * instead, and a URI of any other scheme fails as an unplayable file.
 */
@OptIn(UnstableApi::class)
class LocalDataSource(context: Context) : DataSource {
    private val content = ContentDataSource(context)
    private val file = FileDataSource()
    private var opened: DataSource? = null

    override fun addTransferListener(transferListener: TransferListener) {
        content.addTransferListener(transferListener)
        file.addTransferListener(transferListener)
    }

    override fun open(dataSpec: DataSpec): Long {
        val source = when (dataSpec.uri.scheme?.lowercase()) {
            "content" -> content
            "file", null -> file
            else -> throw IOException("Not a local file: ${dataSpec.uri.scheme} URIs are not opened")
        }
        opened = source
        return source.open(dataSpec)
    }

    override fun read(buffer: ByteArray, offset: Int, length: Int): Int =
        checkNotNull(opened) { "read before open" }.read(buffer, offset, length)

    override fun getUri(): Uri? = opened?.uri

    override fun getResponseHeaders(): Map<String, List<String>> = opened?.responseHeaders ?: emptyMap()

    override fun close() {
        try {
            opened?.close()
        } finally {
            opened = null
        }
    }

    /** A factory for [LocalDataSource], for the player's media sources and the bitmap loader. */
    class Factory(context: Context) : DataSource.Factory {
        private val appContext = context.applicationContext

        override fun createDataSource(): DataSource = LocalDataSource(appContext)
    }
}
