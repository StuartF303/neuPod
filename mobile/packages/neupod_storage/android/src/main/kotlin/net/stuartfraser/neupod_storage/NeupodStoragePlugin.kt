// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod_storage

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.PluginRegistry
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Read-only folder access through the Storage Access Framework (decision 0004).
 *
 * - [pickFolder] opens ACTION_OPEN_DOCUMENT_TREE, limited to on-device storage
 *   (EXTRA_LOCAL_ONLY: R1 makes no network connection), and persists a
 *   **read-only** grant.
 * - [listRoots] lists the persisted tree grants.
 * - [startWalk] / [nextWalkBatch] walk a tree breadth-first with one
 *   DocumentsContract child query per directory (not DocumentFile, which costs a
 *   binder call per file per attribute). A walk that could not list every folder
 *   ends with `complete = false`.
 * - [openFd] hands Dart a detached, read-only file descriptor.
 *
 * Every ContentResolver (binder) call runs off the platform thread: walks and
 * root queries on one executor, opens on another (a provider can block an open).
 * Nothing here writes, moves or deletes a document, and no write permission is
 * ever requested or taken.
 */
class NeupodStoragePlugin :
    FlutterPlugin,
    ActivityAware,
    PluginRegistry.ActivityResultListener,
    NeupodStorageApi {

    private var context: Context? = null
    private var activityBinding: ActivityPluginBinding? = null
    private var pendingPick: ((Result<StorageRoot?>) -> Unit)? = null

    private val mainHandler = Handler(Looper.getMainLooper())

    /** Roots and walks. Single thread, so a walk's state is touched by one thread only. */
    private var executor: ExecutorService? = null

    /** openFd only, so a slow provider open never delays a walk (or the reverse). */
    private var openExecutor: ExecutorService? = null
    private val walks = ConcurrentHashMap<String, Walk>()

    private class Walk(val rootId: String, val tree: Uri) {
        /** Directories still to list: (document id, path relative to the root, depth). */
        val dirs = ArrayDeque<Triple<String, String, Int>>()

        /** Document ids already queued, so a provider loop cannot walk forever. */
        val seen = HashSet<String>()

        /** Set when a folder was skipped: unreadable, too deep, or already seen. */
        var incomplete = false
    }

    private val resolver: ContentResolver
        get() = context?.contentResolver ?: throw FlutterError("detached", "plugin is detached")

    /** Runs [work] on [on] and posts its result to [callback] on the platform thread. */
    private fun <T> background(on: ExecutorService?, callback: (Result<T>) -> Unit, work: () -> T) {
        if (on == null) {
            callback(Result.failure(FlutterError("detached", "plugin is detached")))
            return
        }
        try {
            on.execute {
                val result = runCatching(work)
                mainHandler.post { callback(result) }
            }
        } catch (e: Exception) { // RejectedExecutionException after detach
            callback(Result.failure(FlutterError("detached", e.message)))
        }
    }

    // FlutterPlugin

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        executor = Executors.newSingleThreadExecutor()
        openExecutor = Executors.newSingleThreadExecutor()
        NeupodStorageApi.setUp(binding.binaryMessenger, this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        NeupodStorageApi.setUp(binding.binaryMessenger, null)
        executor?.shutdownNow()
        openExecutor?.shutdownNow()
        executor = null
        openExecutor = null
        walks.clear()
        context = null
    }

    // ActivityAware

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activityBinding = binding
        binding.addActivityResultListener(this)
    }

    // A config change keeps a pending pick: the recreated activity gets its result.
    override fun onDetachedFromActivityForConfigChanges() = detachActivity()

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) =
        onAttachedToActivity(binding)

    /**
     * The activity is gone for good (the app swiped away). The engine may live on (the library
     * scan keeps it, decision 0009), but the picker's result can never arrive: end the pending pick
     * as cancelled, so Dart's pickFolder completes and the next Add Folder is not refused as busy.
     */
    override fun onDetachedFromActivity() {
        detachActivity()
        pendingPick?.let { callback ->
            pendingPick = null
            callback(Result.success(null))
        }
    }

    private fun detachActivity() {
        activityBinding?.removeActivityResultListener(this)
        activityBinding = null
    }

    // Picking

    override fun pickFolder(callback: (Result<StorageRoot?>) -> Unit) {
        val activity = activityBinding?.activity
        if (activity == null) {
            callback(Result.failure(FlutterError("no-activity", "no activity to show the picker from")))
            return
        }
        if (pendingPick != null) {
            callback(Result.failure(FlutterError("busy", "the folder picker is already open")))
            return
        }
        // Read only: no FLAG_GRANT_WRITE_URI_PERMISSION, here or when the grant is taken.
        // On-device storage only: cloud providers would make reads network fetches.
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
            .putExtra(Intent.EXTRA_LOCAL_ONLY, true)
        pendingPick = callback
        try {
            activity.startActivityForResult(intent, REQUEST_PICK_TREE)
        } catch (e: Exception) {
            pendingPick = null
            callback(Result.failure(FlutterError("no-picker", e.message)))
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode != REQUEST_PICK_TREE) return false
        val callback = pendingPick ?: return true
        pendingPick = null
        val tree = data?.data
        if (resultCode != Activity.RESULT_OK || tree == null) {
            callback(Result.success(null))
            return true
        }
        background(executor, callback) {
            try {
                resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: SecurityException) {
                throw FlutterError("not-persistable", e.message)
            }
            rootFor(tree)
        }
        return true
    }

    // Roots

    override fun listRoots(callback: (Result<List<StorageRoot>>) -> Unit) =
        background(executor, callback) {
            resolver.persistedUriPermissions
                .filter { it.isReadPermission && DocumentsContract.isTreeUri(it.uri) }
                .sortedBy { it.persistedTime }
                .map { rootFor(it.uri) }
        }

    override fun removeRoot(rootId: String, callback: (Result<Unit>) -> Unit) {
        // Cancel its walks first, so no further page is served for a released root.
        walks.entries.removeIf { it.value.rootId == rootId }
        background(executor, callback) {
            val tree = Uri.parse(rootId)
            for (permission in resolver.persistedUriPermissions.filter { it.uri == tree }) {
                var flags = 0
                if (permission.isReadPermission) flags = flags or Intent.FLAG_GRANT_READ_URI_PERMISSION
                if (permission.isWritePermission) flags = flags or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                try {
                    resolver.releasePersistableUriPermission(tree, flags)
                } catch (_: SecurityException) {
                    // Already gone.
                }
            }
        }
    }

    /** Builds a root for [tree]; `available` is false if the tree's top document cannot be read. */
    private fun rootFor(tree: Uri): StorageRoot {
        var name: String? = null
        try {
            val doc = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
            resolver.query(doc, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) name = c.getString(0)
            }
        } catch (_: Exception) {
            // Revoked grant, provider gone or ejected card: reported as unavailable.
        }
        val fallback = tree.lastPathSegment?.substringAfterLast(':')?.ifEmpty { null } ?: tree.toString()
        return StorageRoot(
            id = tree.toString(),
            displayName = name ?: fallback,
            kind = RootKind.PICKED_FOLDER,
            available = name != null,
        )
    }

    // Walking

    override fun startWalk(rootId: String, callback: (Result<String>) -> Unit) =
        background(executor, callback) {
            val tree = Uri.parse(rootId)
            val held = resolver.persistedUriPermissions.any { it.uri == tree && it.isReadPermission }
            if (!held) throw FlutterError("root-unavailable", "no read grant for $rootId")
            val walk = Walk(rootId, tree)
            val top = DocumentsContract.getTreeDocumentId(tree)
            walk.dirs.addLast(Triple(top, "", 0))
            walk.seen.add(top)
            val id = UUID.randomUUID().toString()
            walks[id] = walk
            id
        }

    override fun nextWalkBatch(walkId: String, maxEntries: Long, callback: (Result<WalkBatch>) -> Unit) =
        background(executor, callback) { walkStep(walkId, maxEntries.toInt().coerceAtLeast(1)) }

    private fun walkStep(walkId: String, maxEntries: Int): WalkBatch {
        val walk = walks[walkId] ?: throw FlutterError("no-walk", "unknown, cancelled or finished walk $walkId")
        val out = ArrayList<StorageEntry>()
        while (out.size < maxEntries && walk.dirs.isNotEmpty()) {
            val (docId, prefix, depth) = walk.dirs.removeFirst()
            val isRoot = depth == 0
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(walk.tree, docId)
            val cursor = try {
                resolver.query(children, PROJECTION, null, null, null)
            } catch (e: Exception) {
                if (isRoot) {
                    walks.remove(walkId)
                    throw FlutterError("root-unavailable", e.message)
                }
                null
            }
            if (cursor == null) {
                if (isRoot) {
                    walks.remove(walkId)
                    throw FlutterError("root-unavailable", "the provider returned no listing")
                }
                walk.incomplete = true // an unreadable subfolder: the walk goes on, but is not complete
                continue
            }
            cursor.use { c ->
                while (c.moveToNext()) {
                    val id = c.getString(0) ?: continue
                    val name = c.getString(1) ?: continue
                    if (name.startsWith(".")) continue // hidden files and folders (.thumbnails, .nomedia)
                    val mime = c.getString(2)
                    val flags = if (c.isNull(5)) 0 else c.getInt(5)
                    // A virtual document has no bytes of its own (it is converted on open): not music.
                    if (flags and Document.FLAG_VIRTUAL_DOCUMENT != 0) continue
                    val rel = if (isRoot) name else "$prefix/$name"
                    if (mime == Document.MIME_TYPE_DIR) {
                        if (depth + 1 > MAX_DEPTH || !walk.seen.add(id)) {
                            walk.incomplete = true
                        } else {
                            walk.dirs.addLast(Triple(id, rel, depth + 1))
                        }
                    } else {
                        out.add(
                            StorageEntry(
                                relPath = rel,
                                uri = DocumentsContract.buildDocumentUriUsingTree(walk.tree, id).toString(),
                                size = if (c.isNull(3)) 0L else c.getLong(3),
                                modifiedMs = if (c.isNull(4)) 0L else c.getLong(4),
                                state = EntryState.LOCAL,
                            ),
                        )
                    }
                }
            }
        }
        val done = walk.dirs.isEmpty()
        if (done) walks.remove(walkId)
        return WalkBatch(entries = out, done = done, complete = done && !walk.incomplete)
    }

    override fun cancelWalk(walkId: String) {
        walks.remove(walkId)
    }

    // Reading

    override fun openFd(uri: String, callback: (Result<Long>) -> Unit) =
        background(openExecutor, callback) {
            val pfd = try {
                resolver.openFileDescriptor(Uri.parse(uri), "r")
            } catch (e: Exception) {
                throw FlutterError("open-failed", e.message)
            } ?: throw FlutterError("open-failed", "the provider returned no descriptor")
            pfd.detachFd().toLong()
        }

    override fun appSupportPath(): String =
        context?.noBackupFilesDir?.absolutePath ?: throw FlutterError("detached", "plugin is detached")

    // filesDir/stats: the one path the app's backup rules include (res/xml/backup_rules.xml for Android 11 and
    // earlier, res/xml/data_extraction_rules.xml for 12 and later; owner decision 20).
    override fun backedUpDataPath(): String {
        val files = context?.filesDir ?: throw FlutterError("detached", "plugin is detached")
        val dir = java.io.File(files, "stats")
        if (!dir.isDirectory && !dir.mkdirs()) throw FlutterError("no-directory", "cannot create ${dir.path}")
        return dir.absolutePath
    }

    override fun isExcludedFromBackup(path: String): Boolean =
        throw FlutterError("unsupported", "isExcludedFromBackup is iOS only: Android's backup rules decide")

    override fun debugAddRoot(path: String): StorageRoot =
        throw FlutterError("unsupported", "debugAddRoot is iOS only")

    private companion object {
        const val REQUEST_PICK_TREE = 0x4e50 // "NP"

        /** Deeper folders are not walked (and the walk is reported incomplete). */
        const val MAX_DEPTH = 64
        val PROJECTION = arrayOf(
            Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE,
            Document.COLUMN_LAST_MODIFIED,
            Document.COLUMN_FLAGS,
        )
    }
}
