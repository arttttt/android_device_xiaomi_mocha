/*
 * Copyright (C) 2026 Artem Bambalov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.arttttt.mochamanager.data

import android.content.ContentValues
import android.content.Context
import android.provider.MediaStore
import com.android.arttttt.mochamanager.domain.repository.ArchiveRepository
import com.android.arttttt.mochamanager.domain.usecase.ArchiveFile
import java.io.File
import java.io.IOException
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/*
 * Log archives as ZIP files: made in the app's cache, where a share hands
 * them out from (the app's FileProvider serves that directory), and copied
 * to Downloads on request. Only the last one is kept in the cache.
 */
class ArchiveStore(private val context: Context, private val io: CoroutineDispatcher) : ArchiveRepository {
    val directory: File
        get() = File(context.cacheDir, DIRECTORY)

    override suspend fun write(name: String, files: List<ArchiveFile>): File? = withContext(io) {
        try {
            val dir = directory.apply { mkdirs() }
            dir.listFiles()?.forEach { it.delete() }
            val zip = File(dir, name)
            ZipOutputStream(zip.outputStream().buffered()).use { out ->
                for (f in files) {
                    out.putNextEntry(ZipEntry(f.path))
                    out.write(f.text.toByteArray())
                    out.closeEntry()
                }
            }
            zip
        } catch (e: IOException) {
            null
        }
    }

    override suspend fun toDownloads(zip: File): Boolean = withContext(io) {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, zip.name)
            put(MediaStore.MediaColumns.MIME_TYPE, MIME)
        }
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: return@withContext false
        try {
            resolver.openOutputStream(uri)?.use { out -> zip.inputStream().use { it.copyTo(out) } } != null
        } catch (e: IOException) {
            resolver.delete(uri, null, null)
            false
        }
    }

    companion object {
        const val DIRECTORY = "logs"
        const val MIME = "application/zip"
    }
}
