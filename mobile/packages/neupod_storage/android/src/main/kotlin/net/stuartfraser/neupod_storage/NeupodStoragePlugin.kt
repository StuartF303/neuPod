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
 * - [pickFolder] opens ACTION_OPEN_DOCUMENT_TREE and persists a **read-only** grant.
 * - [listRoots] lists the persisted tree grants.
 * - [startWalk] / [nextWalkBatch] walk a tree breadth-first with one
 *   DocumentsContract child query per directory (not DocumentFile, which costs a
 *   binder call per file per attribute).
 * - [openFd] hands Dart a detached, read-only file descriptor.
 *
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
    private var executor: ExecutorService? = null
    private val walks = ConcurrentHashMap<String, Walk>()

    private class Walk(val tree: Uri) {
        /** Directories still to list: (document id, path relative to the root). */
        val dirs = ArrayDeque<Pair<String, String>>()
    }

    private val resolver: ContentResolver
        get() = context?.contentResolver ?: throw FlutterError("detached", "plugin is detached")

    // FlutterPlugin

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        executor = Executors.newSingleThreadExecutor()
        NeupodStorageApi.setUp(binding.binaryMessenger, this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        NeupodStorageApi.setUp(binding.binaryMessenger, null)
        executor?.shutdownNow()
        executor = null
        walks.clear()
        context = null
    }

    // ActivityAware

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activityBinding = binding
        binding.addActivityResultListener(this)
    }

    override fun onDetachedFromActivityForConfigChanges() = onDetachedFromActivity()

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) =
        onAttachedToActivity(binding)

    override fun onDetachedFromActivity() {
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
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION,
        )
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
        try {
            resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            callback(Result.failure(FlutterError("not-persistable", e.message)))
            return true
        }
        val ex = executor ?: return true
        ex.execute {
            val root = rootFor(tree)
            mainHandler.post { callback(Result.success(root)) }
        }
        return true
    }

    // Roots

    override fun listRoots(callback: (Result<List<StorageRoot>>) -> Unit) {
        val ex = executor ?: return callback(Result.failure(FlutterError("detached", null)))
        ex.execute {
            val result = runCatching {
                resolver.persistedUriPermissions
                    .filter { it.isReadPermission && DocumentsContract.isTreeUri(it.uri) }
                    .sortedBy { it.persistedTime }
                    .map { rootFor(it.uri) }
            }
            mainHandler.post { callback(result) }
        }
    }

    override fun removeRoot(rootId: String) {
        val tree = Uri.parse(rootId)
        val held = resolver.persistedUriPermissions.filter { it.uri == tree }
        for (permission in held) {
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

    override fun startWalk(rootId: String): String {
        val tree = Uri.parse(rootId)
        val held = resolver.persistedUriPermissions.any { it.uri == tree && it.isReadPermission }
        if (!held) throw FlutterError("root-unavailable", "no read grant for $rootId")
        val walk = Walk(tree)
        walk.dirs.addLast(DocumentsContract.getTreeDocumentId(tree) to "")
        val id = UUID.randomUUID().toString()
        walks[id] = walk
        return id
    }

    override fun nextWalkBatch(walkId: String, maxEntries: Long, callback: (Result<WalkBatch>) -> Unit) {
        val ex = executor ?: return callback(Result.failure(FlutterError("detached", null)))
        ex.execute {
            val result = runCatching { walkStep(walkId, maxEntries.toInt().coerceAtLeast(1)) }
            mainHandler.post { callback(result) }
        }
    }

    private fun walkStep(walkId: String, maxEntries: Int): WalkBatch {
        val walk = walks[walkId] ?: throw FlutterError("no-walk", "unknown or finished walk $walkId")
        val out = ArrayList<StorageEntry>()
        while (out.size < maxEntries && walk.dirs.isNotEmpty()) {
            val (docId, prefix) = walk.dirs.removeFirst()
            val isRoot = prefix.isEmpty()
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(walk.tree, docId)
            val cursor = try {
                resolver.query(children, PROJECTION, null, null, null)
            } catch (e: Exception) {
                if (isRoot) {
                    walks.remove(walkId)
                    throw FlutterError("root-unavailable", e.message)
                }
                null // An unreadable subfolder is skipped; the rest of the tree is still walked.
            }
            if (cursor == null && isRoot) {
                walks.remove(walkId)
                throw FlutterError("root-unavailable", "the provider returned no listing")
            }
            cursor?.use { c ->
                while (c.moveToNext()) {
                    val id = c.getString(0) ?: continue
                    val name = c.getString(1) ?: continue
                    if (name.startsWith(".")) continue // hidden files and folders (.thumbnails, .nomedia)
                    val mime = c.getString(2)
                    val rel = if (isRoot) name else "$prefix/$name"
                    if (mime == Document.MIME_TYPE_DIR) {
                        walk.dirs.addLast(id to rel)
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
        return WalkBatch(entries = out, done = done)
    }

    override fun cancelWalk(walkId: String) {
        walks.remove(walkId)
    }

    // Reading

    override fun openFd(uri: String): Long {
        val pfd = try {
            resolver.openFileDescriptor(Uri.parse(uri), "r")
        } catch (e: Exception) {
            throw FlutterError("open-failed", e.message)
        } ?: throw FlutterError("open-failed", "the provider returned no descriptor")
        return pfd.detachFd().toLong()
    }

    override fun debugAddRoot(path: String): StorageRoot =
        throw FlutterError("unsupported", "debugAddRoot is iOS only")

    private companion object {
        const val REQUEST_PICK_TREE = 0x4e50 // "NP"
        val PROJECTION = arrayOf(
            Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE,
            Document.COLUMN_LAST_MODIFIED,
        )
    }
}
