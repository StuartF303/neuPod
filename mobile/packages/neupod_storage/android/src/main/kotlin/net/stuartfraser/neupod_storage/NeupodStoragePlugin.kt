// SPDX-License-Identifier: MPL-2.0

package net.stuartfraser.neupod_storage

import android.Manifest
import android.app.Activity
import android.content.ContentResolver
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.MediaStore
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
 * - [addSharedCollection] turns on the phone's shared music collection (MediaStore audio) as one more
 *   root, asking for READ_MEDIA_AUDIO (Android 13+) or READ_EXTERNAL_STORAGE (before) only if the app
 *   does not hold it, so only when the user acts. Its walk is one MediaStore query: MediaStore only
 *   lists and opens the files; their tags are read from the bytes by the app, as the device reads them.
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
    PluginRegistry.RequestPermissionsResultListener,
    NeupodStorageApi {

    private var context: Context? = null
    private var activityBinding: ActivityPluginBinding? = null
    private var pendingPick: ((Result<StorageRoot?>) -> Unit)? = null
    private var pendingCollection: ((Result<StorageRoot?>) -> Unit)? = null

    private val mainHandler = Handler(Looper.getMainLooper())

    /** Roots and walks. Single thread, so a walk's state is touched by one thread only. */
    private var executor: ExecutorService? = null

    /** openFd only, so a slow provider open never delays a walk (or the reverse). */
    private var openExecutor: ExecutorService? = null
    private val walks = ConcurrentHashMap<String, Walk>()

    /** Walks of the shared collection: the query's rows, served a page at a time. */
    private val collectionWalks = ConcurrentHashMap<String, ArrayDeque<StorageEntry>>()

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
        collectionWalks.clear()
        context = null
    }

    // ActivityAware

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activityBinding = binding
        binding.addActivityResultListener(this)
        binding.addRequestPermissionsResultListener(this)
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
        // The same for a permission question left open: the collection stays off.
        pendingCollection?.let { callback ->
            pendingCollection = null
            callback(Result.success(null))
        }
    }

    private fun detachActivity() {
        activityBinding?.removeActivityResultListener(this)
        activityBinding?.removeRequestPermissionsResultListener(this)
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
            val folders = resolver.persistedUriPermissions
                .filter { it.isReadPermission && DocumentsContract.isTreeUri(it.uri) }
                .sortedBy { it.persistedTime }
                .map { rootFor(it.uri) }
            // The shared collection, when the user turned it on, after the folders.
            if (collectionOn()) folders + collectionRoot() else folders
        }

    // The shared music collection

    /** The permission that reads other apps' audio in MediaStore on this Android version. */
    private val audioPermission: String
        get() = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.READ_MEDIA_AUDIO
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

    private fun audioPermissionHeld(): Boolean {
        val ctx = context ?: return false
        return ctx.checkSelfPermission(audioPermission) == PackageManager.PERMISSION_GRANTED
    }

    private fun prefs() = (context ?: throw FlutterError("detached", "plugin is detached"))
        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** Whether the user turned the collection on (the app's own setting, not the permission). */
    private fun collectionOn(): Boolean = prefs().getBoolean(PREF_COLLECTION_ON, false)

    private fun setCollectionOn(on: Boolean) {
        prefs().edit().putBoolean(PREF_COLLECTION_ON, on).apply()
    }

    /** The collection's root; unavailable while the permission is not held (taken away in Settings). */
    private fun collectionRoot() = StorageRoot(
        id = SHARED_COLLECTION_ID,
        displayName = SHARED_COLLECTION_NAME,
        kind = RootKind.SHARED_COLLECTION,
        available = audioPermissionHeld(),
    )

    override fun addSharedCollection(callback: (Result<StorageRoot?>) -> Unit) {
        if (context == null) {
            callback(Result.failure(FlutterError("detached", "plugin is detached")))
            return
        }
        if (audioPermissionHeld()) {
            setCollectionOn(true)
            callback(Result.success(collectionRoot()))
            return
        }
        val activity = activityBinding?.activity
        if (activity == null) {
            callback(Result.failure(FlutterError("no-activity", "no activity to ask for the permission from")))
            return
        }
        if (pendingCollection != null) {
            callback(Result.failure(FlutterError("busy", "the permission question is already open")))
            return
        }
        pendingCollection = callback
        activity.requestPermissions(arrayOf(audioPermission), REQUEST_AUDIO)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ): Boolean {
        if (requestCode != REQUEST_AUDIO) return false
        val callback = pendingCollection ?: return true
        pendingCollection = null
        // An empty result is a question the system cut short: taken as a refusal.
        if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            setCollectionOn(true)
            callback(Result.success(collectionRoot()))
        } else {
            callback(Result.success(null))
        }
        return true
    }

    /**
     * Every music item (IS_MUSIC) MediaStore holds on every external volume (the phone's storage and any SD
     * card), as entries: `relPath` is RELATIVE_PATH + DISPLAY_NAME, the item's path from the top of its volume;
     * `uri` the item's content URI on its own volume; `modifiedMs` DATE_MODIFIED (seconds) in milliseconds, so
     * the scanner's size-and-time check makes a rescan read only new or changed items. Hidden names are skipped,
     * as in a folder walk. Two volumes holding the same relative path keep the phone's own storage's one (a
     * track's path is unique in its source).
     */
    private fun collectionEntries(): ArrayDeque<StorageEntry> {
        val rows = ArrayList<Pair<Boolean, StorageEntry>>()
        val cursor = try {
            resolver.query(
                MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL),
                COLLECTION_PROJECTION,
                "${MediaStore.Audio.Media.IS_MUSIC} != 0",
                null,
                null,
            )
        } catch (e: Exception) { // SecurityException: the permission was taken away
            throw FlutterError("root-unavailable", e.message)
        } ?: throw FlutterError("root-unavailable", "MediaStore returned no listing")
        cursor.use { c ->
            while (c.moveToNext()) {
                val id = c.getLong(0)
                val volume = c.getString(1) ?: continue
                val dir = c.getString(2) ?: ""
                val name = c.getString(3) ?: continue
                if (name.startsWith(".") || dir.split('/').any { it.startsWith(".") }) continue
                val prefix = if (dir.isEmpty() || dir.endsWith("/")) dir else "$dir/"
                val uri = ContentUris.withAppendedId(MediaStore.Audio.Media.getContentUri(volume), id)
                rows.add(
                    (volume == MediaStore.VOLUME_EXTERNAL_PRIMARY) to StorageEntry(
                        relPath = prefix + name,
                        uri = uri.toString(),
                        size = if (c.isNull(4)) 0L else c.getLong(4),
                        modifiedMs = if (c.isNull(5)) 0L else c.getLong(5) * 1000L,
                        state = EntryState.LOCAL,
                    ),
                )
            }
        }
        // The phone's own storage first, then by path: the first of a relative path wins.
        rows.sortWith(compareBy({ !it.first }, { it.second.relPath }))
        val seen = HashSet<String>()
        val out = ArrayDeque<StorageEntry>()
        for ((_, e) in rows) if (seen.add(e.relPath)) out.addLast(e)
        return out
    }

    override fun removeRoot(rootId: String, callback: (Result<Unit>) -> Unit) {
        if (rootId == SHARED_COLLECTION_ID) {
            collectionWalks.clear()
            background(executor, callback) { setCollectionOn(false) }
            return
        }
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
            if (rootId == SHARED_COLLECTION_ID) {
                if (!collectionOn() || !audioPermissionHeld()) {
                    throw FlutterError("root-unavailable", "the shared collection is off or not permitted")
                }
                val id = UUID.randomUUID().toString()
                collectionWalks[id] = collectionEntries()
                return@background id
            }
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
        collectionWalks[walkId]?.let { rest ->
            val out = ArrayList<StorageEntry>()
            while (out.size < maxEntries && rest.isNotEmpty()) out.add(rest.removeFirst())
            val done = rest.isEmpty()
            if (done) collectionWalks.remove(walkId)
            // One query lists the whole collection: a finished walk is always complete.
            return WalkBatch(entries = out, done = done, complete = done)
        }
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
        collectionWalks.remove(walkId)
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
        const val REQUEST_AUDIO = 0x4e51

        /** The shared collection's root id; the Dart side's `sharedCollectionRootId`. */
        const val SHARED_COLLECTION_ID = "mediastore:audio"

        /** Its name as a root; the app's screens use their own words for it. */
        const val SHARED_COLLECTION_NAME = "Music on This Phone"
        const val PREFS = "neupod_storage"
        const val PREF_COLLECTION_ON = "shared_collection_on"
        val COLLECTION_PROJECTION = arrayOf(
            MediaStore.Audio.Media._ID,
            MediaStore.Audio.Media.VOLUME_NAME,
            MediaStore.Audio.Media.RELATIVE_PATH,
            MediaStore.Audio.Media.DISPLAY_NAME,
            MediaStore.Audio.Media.SIZE,
            MediaStore.Audio.Media.DATE_MODIFIED,
        )

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
