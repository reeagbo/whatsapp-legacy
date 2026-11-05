package org.reeagbo.whatsapplegacy

import android.Manifest
import android.content.BroadcastReceiver
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Uri
import android.provider.Settings
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import java.util.UUID
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

class MainActivity : Activity() {
    private external fun nativeHistoryRequest(handle: Long, jid: String, anchor: String, own: Boolean, time: Long, id: String): Boolean
    private external fun nativeHistorySlots(): Int
    private external fun nativeHistoryTask(handle: Long, slot: Int): ByteArray?
    private external fun nativeDeleteChat(handle: Long, peer: String, anchor: String, own: Boolean, time: Long, changed: Long, change: Int): Boolean
    private external fun nativeDeleteSnapshot(handle: Long, cursor: Long)
    private external fun nativeHistoryDone(handle: Long, slot: Int, task: ByteArray, version: Long): Boolean
    private external fun nativeHistoryPath(task: ByteArray): String?
    private external fun nativeHistoryExternalTask(task: ByteArray): ByteArray?
    private external fun nativeHistoryHydrate(task: ByteArray, encrypted: ByteArray): ByteArray?
    private external fun nativeHistoryOpen(session: Long, task: ByteArray, encrypted: ByteArray, missing: Array<String?>): Long
    private external fun nativeIdentityPhone(path: String): String?
    private external fun nativeHistoryOrder(task: ByteArray): Long
    private external fun nativeHistoryVersion(cursor: Long): Long
    private external fun nativeSyncKey(session: Long, key: String, id: String): Boolean
    private external fun nativeHistoryNext(handle: Long): Array<String>?
    private external fun nativeHistoryClose(handle: Long)
    private external fun nativeResolve(handle: Long, jid: String): String?
    private external fun nativeStored(handle: Long, id: String, jid: String, outgoing: Boolean): Boolean
    private external fun nativeEvent(handle: Long): Array<String>?
    private external fun nativeSend(handle: Long, jid: String, id: String, text: ByteArray): Boolean
    private external fun nativeTick(handle: Long, now: Long)
    private external fun nativeLog(handle: Long): String
    private external fun nativeDiagnostics(): String
    private external fun nativeCreate(path: String, phone: String): Long
    private external fun nativeFree(handle: Long)
    private external fun nativeStart(handle: Long)
    private external fun nativeFeed(handle: Long, bytes: ByteArray)
    private external fun nativeOutput(handle: Long): ByteArray?
    private external fun nativeState(handle: Long): Int
    private external fun nativeStatus(handle: Long): String
    private external fun nativeCode(handle: Long): String
    private external fun nativePing(handle: Long)

    private val handler = Handler(Looper.getMainLooper())
    private val timer = Executors.newSingleThreadScheduledExecutor()
    private val http = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS)
        .writeTimeout(20, TimeUnit.SECONDS)
        .pingInterval(0, TimeUnit.SECONDS) // Use the encrypted WA IQ heartbeat only.
        .build()
    private lateinit var output: TextView
    private lateinit var code: TextView
    private lateinit var number: EditText
    private lateinit var pair: Button
    private lateinit var check: Button
    private lateinit var stop: Button
    private var connection: PairConnection? = null
    private var destroyed = false
    private lateinit var debugLog: DebugLog
    private var attempts = 0
    private val exportRequest = 301
    private var oldCrashHandler: Thread.UncaughtExceptionHandler? = null
    private var crashHandler: Thread.UncaughtExceptionHandler? = null
    private val appVersion: String by lazy { packageManager.getPackageInfo(packageName, 0).versionName ?: "unknown" }

    private lateinit var store: ChatStore
    private lateinit var page: LinearLayout
    private lateinit var settingsBody: LinearLayout
    private lateinit var statusLabel: TextView
    private var conversation: String? = null
    private var showingArchived=false
    private var messageList: LinearLayout? = null
    private var messageScroll: ScrollView? = null
    private var compose: EditText? = null
    private var sendButton: Button? = null
    private var connected = false
    private var statusText = "Sin conexión"
    private data class ChatChange(val peer: String, val id: String, val outgoing: Boolean, val time: Long, val changed: Long, val change: Int = 0)
    private val queuedChatChanges=java.util.ArrayDeque<ChatChange>()
    private var chatChangeInFlight=false
    private var pendingMessage: String? = null
    private val reconnectPolicy = ReconnectPolicy()
    private var sessionGeneration = 0
    private var automaticReconnect = true
    private var networkWasAvailable = false
    private var networkReceiverRegistered = false
    private val networkReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            val available = networkAvailable()
            val restored = available && !networkWasAvailable
            networkWasAvailable = available
            if (restored && automaticReconnect && !destroyed && activeIdentity().exists()) {
                debugLog.add("NETWORK_RESTORED reconnect=${connection==null}")
                reconnectPolicy.reset()
                if(connection==null) start("")
            }
        }
    }
    private lateinit var phoneContacts: PhoneContacts
    private val contactWorker = Executors.newSingleThreadExecutor()
    private var phoneNames: Map<String,String> = emptyMap()
    private val checkedContacts = mutableSetOf<String>()
    private var contactGeneration = 0
    private val identityDiagnostics = mutableMapOf<String,String>()
    private var chatTitle: TextView? = null
    private var addressLabel: TextView? = null
    private val contactRequest = 302
    private val historyWorker=Executors.newSingleThreadExecutor()
    private val historyHttp=OkHttpClient.Builder().connectTimeout(20,TimeUnit.SECONDS).readTimeout(60,TimeUnit.SECONDS).followRedirects(false).build()
    private var historyBusy=false
    private var historyRequestId: String? = null
    private var historyRequestPeer: String? = null
    private var queuedHistoryPeer: String? = null
    private var historyWindow = 100
    private var renderedMessages: List<ChatStore.Message> = emptyList()
    private var restoringScroll = false
    private var firstChatRender = false
    private var lastScrollTouch = 0L
    private var lastHistoryAttempt=0L
    @Suppress("DEPRECATION")
    private fun networkAvailable() = (getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager).activeNetworkInfo?.isConnected == true
    @Suppress("DEPRECATION")
    private fun palette(id: Int) = resources.getColor(id)
    private val green by lazy { palette(R.color.wa_accent) }
    private val ink by lazy { palette(R.color.wa_text) }
    private val muted by lazy { palette(R.color.wa_muted) }
    private val canvas by lazy { palette(R.color.wa_background) }
    private val surface by lazy { palette(R.color.wa_surface) }
    private val inputSurface by lazy { palette(R.color.wa_input) }
    private val outgoingSurface by lazy { palette(R.color.wa_outgoing) }
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun shape(color: Int, radius: Int = 14) = GradientDrawable().apply { setColor(color); cornerRadius = dp(radius).toFloat() }
    private fun label(value: String, size: Float = 16f, color: Int = ink) = TextView(this).apply { text = value; textSize = size; setTextColor(color) }
    private fun column() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    private fun display(jid: String) = if (jid.endsWith("@lid")) "Pendiente" else "+" + jid.substringBefore('@').substringBefore(':')
    private fun contactName(chat: ChatStore.Chat) = chat.name.takeIf { it.isNotBlank() }
        ?: phoneNames[chat.jid] ?: chat.pushName.takeIf { it.isNotBlank() } ?: display(chat.jid)
    private fun traceIdentity(chat: ChatStore.Chat) {
        val source=when {
            chat.name.isNotBlank() -> "manual"
            phoneNames.containsKey(chat.jid) -> "agenda"
            chat.pushName.isNotBlank() -> "received"
            else -> "identifier"
        }
        val line="CHAT_IDENTITY address=${DebugLog.addressHint(chat.jid)} number_known=${chat.jid.endsWith("@s.whatsapp.net")} name_source=$source agenda_permission=${phoneContacts.allowed()}"
        if(identityDiagnostics[chat.jid]!=line) { identityDiagnostics[chat.jid]=line;debugLog.add(line) }
    }
    private fun title(jid: String) = store.chat(jid)?.let { contactName(it) } ?: display(jid)
    private fun refreshConversation() {
        store.chats().forEach { traceIdentity(it) }
        val current = conversation
        if (current == null) showList() else {
            conversation = store.resolve(current)
            chatTitle?.text = title(conversation!!)
            updateAddressLabel()
            refreshMessages()
        }
    }
    private fun updateAddressLabel() {
        val jid=conversation ?: return
        addressLabel?.text=if(jid.endsWith("@lid")) "Número pendiente de identificar" else display(jid)
        addressLabel?.visibility=if(jid.endsWith("@lid") || title(jid)!=display(jid)) View.VISIBLE else View.GONE
    }
    private fun openAgendaSettings() {
        startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
    }
    private fun editName(jid: String) {
        val value = EditText(this).apply { setSingleLine(true); hint="Nombre"; setText(store.chat(jid)?.name.orEmpty()) }
        AlertDialog.Builder(this).setTitle("Nombre del contacto").setMessage("Nombre local para esta conversación")
            .setView(value).setNegativeButton("Cancelar",null).setPositiveButton("Guardar") { _,_ ->
                try { store.setName(jid,value.text.toString().trim().take(100)); refreshConversation() }
                catch(error: Exception) { storageFailure(error) }
            }.show()
    }
    private fun scheduleContacts(force: Boolean = false) {
        if (!::phoneContacts.isInitialized || destroyed) return
        if (!phoneContacts.allowed()) {
            contactGeneration++; phoneNames=emptyMap(); checkedContacts.clear(); refreshConversation(); return
        }
        if (force) { contactGeneration++; checkedContacts.clear(); phoneNames=emptyMap() }
        val requested=store.chats().map { it.jid }.filter { it.endsWith("@s.whatsapp.net") && it !in checkedContacts }.toSet()
        if (requested.isEmpty()) { if(force)refreshConversation(); return }
        checkedContacts.addAll(requested)
        val generation=contactGeneration
        contactWorker.execute {
            val names=try { phoneContacts.names(requested) { debugLog.add(it) } } catch(error: Exception) {
                debugLog.add("PHONEBOOK_LOOKUP_FAILED type=${error.javaClass.simpleName}")
                emptyMap<String,String>()
            }
            handler.post {
                if(!destroyed && generation==contactGeneration) {
                    phoneNames=if(phoneContacts.allowed())phoneNames+names else emptyMap()
                    refreshConversation()
                }
            }
        }
    }
    private fun requestContactNames() {
        if(Build.VERSION.SDK_INT>=23 && !phoneContacts.allowed()) {
            // Integration is always enabled; Android still requires its own consent.
            getSharedPreferences("permissions",Context.MODE_PRIVATE).edit()
                .putBoolean("agenda_requested",true).putBoolean("agenda_granted",false).apply()
            requestPermissions(arrayOf(Manifest.permission.READ_CONTACTS),contactRequest)
        } else scheduleContacts(true)
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode,permissions,results)
        if(requestCode==contactRequest) {
            getSharedPreferences("permissions",Context.MODE_PRIVATE).edit()
                .putBoolean("agenda_granted",phoneContacts.allowed()).apply()
            scheduleContacts(true)
            if(!phoneContacts.allowed()) AlertDialog.Builder(this)
                .setTitle("Acceso a la agenda")
                .setMessage("Permite el acceso a Contactos en los ajustes de la aplicación para mostrar los nombres de tu agenda.")
                .setPositiveButton("Abrir ajustes") { _,_ -> openAgendaSettings() }
                .setNegativeButton("Cerrar",null).show()
        }
    }
    override fun onResume() {
        super.onResume()
        if(!::store.isInitialized)return
        val permissions=getSharedPreferences("permissions",Context.MODE_PRIVATE)
        if(phoneContacts.allowed()) {
            permissions.edit().putBoolean("agenda_granted",true).apply()
            scheduleContacts(true)
        } else if(!permissions.getBoolean("agenda_requested",false) || permissions.getBoolean("agenda_granted",false)) {
            requestContactNames()
        } else scheduleContacts(true)
    }
    private fun action(text: String, description: String, click: () -> Unit) = Button(this).apply {
        this.text = text; contentDescription = description; setTextColor(green); textSize = 23f
        minWidth = dp(48); minimumWidth = dp(48); minHeight = dp(48); background = shape(Color.TRANSPARENT)
        setOnClickListener { click() }
    }
    private fun header(name: String, chat: Boolean) {
        val bar = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; setPadding(dp(10),dp(9),dp(10),dp(6)); setBackgroundColor(surface) }
        if (chat || showingArchived) bar.addView(action("‹", "Volver a conversaciones") { showList(chat && showingArchived) }, LinearLayout.LayoutParams(dp(48),dp(48)))
        bar.addView(label(name, if(chat) 21f else 26f).apply { setTypeface(null,Typeface.BOLD); maxLines=1; gravity=Gravity.CENTER_VERTICAL; if(chat) { chatTitle=this; contentDescription="Editar nombre del contacto"; setOnClickListener { conversation?.let { editName(it) } } } }, LinearLayout.LayoutParams(0,dp(52),1f).apply { gravity=Gravity.CENTER_VERTICAL })
        if (!chat && !showingArchived) bar.addView(action("+", "Nueva conversación") { newConversation() },LinearLayout.LayoutParams(dp(52),dp(52)))
        bar.addView(action("⋮", "Menú principal") { showMainMenu() },LinearLayout.LayoutParams(dp(48),dp(52)))
        page.addView(bar)
        if(chat) {
            addressLabel=label("",12f,muted).apply { setPadding(dp(20),0,dp(16),dp(5));setBackgroundColor(surface) }
            page.addView(addressLabel)
            updateAddressLabel()
        }
        statusLabel=label(statusText,13f,muted).apply { setPadding(dp(20),dp(4),dp(16),dp(12)); setBackgroundColor(surface) }
        page.addView(statusLabel)
    }
    private fun newPage() {
        chatTitle=null;addressLabel=null
        page=column().apply { setBackgroundColor(canvas) }
        setContentView(page)
    }
    private fun showList(archived: Boolean = showingArchived) {
        showingArchived=archived
        queuedHistoryPeer=null
        conversation=null; renderedMessages=emptyList(); messageList=null; messageScroll=null; compose=null; sendButton=null
        newPage(); header(if(archived)"Archivados" else "Conversaciones",false)
        val rows=column().apply { setPadding(dp(12),dp(10),dp(12),dp(10)) }
        val chats=store.chats(archived)
        if(chats.isEmpty()) rows.addView(column().apply {
            setPadding(dp(28),dp(68),dp(28),dp(28))
            addView(label(if(archived)"No hay conversaciones archivadas" else "Tu primera conversación",23f).apply { setTypeface(null,Typeface.BOLD) })
            if(!archived)addView(label("Pulsa + para añadir un número y escribirle.",16f,muted).apply { setPadding(0,dp(12),0,0) })
        })
        for(chat in chats) {
            traceIdentity(chat)
            val name=contactName(chat)
            val row=LinearLayout(this).apply { gravity=Gravity.CENTER_VERTICAL; setPadding(dp(18),dp(15),dp(18),dp(15)); background=shape(surface,18); minimumHeight=dp(80); isClickable=true; contentDescription="Abrir conversación con $name" }
            row.addView(label(name.firstOrNull()?.uppercase() ?: "?",22f,green).apply { gravity=Gravity.CENTER; background=shape(palette(R.color.wa_avatar),28) },LinearLayout.LayoutParams(dp(52),dp(52)))
            row.addView(column().apply {
                setPadding(dp(14),0,dp(8),0)
                addView(label(name,18f).apply { setTypeface(null,Typeface.BOLD); setSingleLine(true) })
                addView(label(chat.preview,14f,muted).apply { setSingleLine(true); ellipsize=android.text.TextUtils.TruncateAt.END; setPadding(0,dp(5),0,0) })
            },LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f))
            row.addView(label(if(chat.time>0) SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(chat.time)) else "",12f,muted))
            row.setOnClickListener { showChat(chat.jid) }; row.setOnLongClickListener { chatActions(chat.jid); true }; rows.addView(row,LinearLayout.LayoutParams(-1,-2).apply { bottomMargin=dp(6) })
        }
        page.addView(ScrollView(this).apply { addView(rows) },LinearLayout.LayoutParams(-1,0,1f))
    }
    private fun archiveChat(jid: String, archive: Boolean) {
        try {
            val changed=System.currentTimeMillis()
            val anchor=store.latestRemoteMessage(jid)
            store.setArchived(jid,archive,changed)
            showList()
            if(anchor!=null) {
                queuedChatChanges.addLast(ChatChange(anchor.peer,anchor.id,anchor.outgoing,anchor.time,changed,if(archive)1 else 2))
                sendQueuedChatChange()
            } else debugLog.add("ARCHIVE_SYNC_NO_REMOTE_ANCHOR")
        } catch(error: Exception) { storageFailure(error) }
    }
    private fun chatActions(jid: String) {
        val archived=store.chat(jid)?.archived==true
        AlertDialog.Builder(this).setTitle(title(jid))
            .setItems(arrayOf(if(archived)"Desarchivar" else "Archivar", "Editar nombre", "Eliminar conversación")) { _, which ->
                when(which) {
                    0 -> archiveChat(jid,!archived)
                    1 -> editName(jid)
                    2 -> deleteChat(jid)
                }
            }.show()
    }
    private fun deleteChat(jid: String) {
        try {
            val anchor=store.latestRemoteMessage(jid)
            store.deleteChatLocally(jid)
            if(historyRequestPeer?.let { store.resolve(it)==store.resolve(jid) }==true)
                finishHistoryPage(false)
            showList()
            if(anchor!=null) {
                queuedChatChanges.addLast(ChatChange(anchor.peer,anchor.id,anchor.outgoing,anchor.time,System.currentTimeMillis()))
                sendQueuedChatChange()
            } else debugLog.add("DELETE_SYNC_NO_REMOTE_ANCHOR")
        } catch(error: Exception) {
            debugLog.failure("CHAT_DELETE_FAILED",error)
        }
    }
    private fun sendQueuedChatChange() {
        val active=connection ?: return
        if(!connected || chatChangeInFlight)return
        while(queuedChatChanges.isNotEmpty()) {
            val request=queuedChatChanges.removeFirst()
            chatChangeInFlight=active.deleteChat(request)
            if(chatChangeInFlight)return
            debugLog.add("DELETE_SYNC_NOT_STARTED")
        }
    }
    private fun newConversation() {
        val form=column().apply { setPadding(dp(24),dp(8),dp(24),0) }
        val phone=EditText(this).apply { hint="Número internacional (34…)"; inputType=InputType.TYPE_CLASS_PHONE; isSaveEnabled=false }
        val name=EditText(this).apply { hint="Nombre (opcional)"; setSingleLine(true); isSaveEnabled=false }
        form.addView(phone);form.addView(name)
        val dialog=AlertDialog.Builder(this).setTitle("Nueva conversación").setView(form).setNegativeButton("Cancelar",null).setPositiveButton("Abrir",null).create()
        dialog.setOnShowListener { dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            val value=phone.text.toString().trim().removePrefix("+").replace(" ","").replace("-","")
            if(!value.matches(Regex("^[1-9][0-9]{7,14}$")))phone.error="Incluye el prefijo de país: entre 8 y 15 dígitos"
            else { val jid="$value@s.whatsapp.net";debugLog.hide(value);store.addChat(jid,name.text.toString().trim());dialog.dismiss();showChat(jid) }
        } };dialog.show()
    }
    private fun showChat(jid: String) {
        conversation=store.resolve(jid)
        debugLog.add("CHAT_OPEN selected=${DebugLog.addressHint(jid)} canonical=${DebugLog.addressHint(conversation!!)}")
        historyWindow=100
        renderedMessages=emptyList()
        firstChatRender=true
        lastScrollTouch=0L
        newPage();header(title(conversation!!),true)
        messageList=column().apply { setPadding(dp(12),dp(16),dp(12),dp(16)) }
        messageScroll=object : ScrollView(this) {
            private var gestureStart=0f
            private var startedAtTop=false
            override fun dispatchTouchEvent(event: android.view.MotionEvent): Boolean {
                when(event.actionMasked) {
                    android.view.MotionEvent.ACTION_DOWN -> { gestureStart=event.y;startedAtTop=scrollY<=dp(24) }
                    android.view.MotionEvent.ACTION_UP -> if(startedAtTop && scrollY<=dp(24) && event.y-gestureStart>dp(32)) {
                        lastScrollTouch=0L
                        post { if(this===messageScroll && !restoringScroll) loadOlder() }
                    }
                }
                return super.dispatchTouchEvent(event)
            }
        }.apply {
            isFillViewport=true
            addView(messageList)
            setOnTouchListener { _, _ -> lastScrollTouch=SystemClock.elapsedRealtime();false }
            viewTreeObserver.addOnScrollChangedListener {
                if(this===messageScroll && !restoringScroll && scrollY<=dp(24)
                    && lastScrollTouch!=0L && SystemClock.elapsedRealtime()-lastScrollTouch<1_500L) {
                    lastScrollTouch=0L
                    loadOlder()
                }
            }
        }
        page.addView(messageScroll,LinearLayout.LayoutParams(-1,0,1f))
        val bar=LinearLayout(this).apply { gravity=Gravity.BOTTOM;setPadding(dp(10),dp(8),dp(10),dp(10));setBackgroundColor(surface) }
        compose=EditText(this).apply { hint="Escribe un mensaje"; minLines=1;maxLines=5;textSize=17f;setTextColor(ink);setHintTextColor(muted);background=shape(inputSurface,24);setPadding(dp(16),dp(12),dp(16),dp(12));isSaveEnabled=false;inputType=InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES }
        bar.addView(compose,LinearLayout.LayoutParams(0,-2,1f))
        sendButton=action("↑","Enviar mensaje") { sendText() }.apply { setTextColor(canvas);background=shape(green,28) }
        bar.addView(sendButton,LinearLayout.LayoutParams(dp(54),dp(54)).apply { leftMargin=dp(8) });page.addView(bar)
        refreshMessages();updateSend()
    }
    private fun refreshMessages(preservePosition: Boolean = false) {
        val jid=conversation ?: return
        val list=messageList ?: return
        val scroll=messageScroll ?: return
        val messages=store.messages(jid,historyWindow)
        if(!firstChatRender && messages==renderedMessages) return
        val previousY=scroll.scrollY
        val anchor=(0 until list.childCount).map { list.getChildAt(it) }
            .firstOrNull { it.tag!=null && it.bottom>previousY }
        val anchorKey=anchor?.tag
        val anchorOffset=anchor?.top?.minus(previousY) ?: 0
        val atBottom=firstChatRender || previousY+scroll.height>=list.height-dp(48)
        firstChatRender=false
        restoringScroll=true
        list.removeAllViews()
        renderedMessages=messages
        for(message in messages) {
            val line=LinearLayout(this).apply { gravity=if(message.outgoing)Gravity.END else Gravity.START;setPadding(0,dp(4),0,dp(4)) }
            val bubble=column().apply { background=shape(if(message.outgoing)outgoingSurface else surface);setPadding(dp(13),dp(10),dp(13),dp(8));minimumWidth=dp(104) }
            bubble.addView(label(message.text,17f).apply { maxWidth=(resources.displayMetrics.widthPixels*0.73).toInt();setTextIsSelectable(true) })
            val state=if(!message.outgoing) "" else when(message.status) { 0->"Pendiente";1->"Enviado · sin confirmar";2->"Aceptado";3->"Entregado";4->"Leído";-2->"Sin confirmación";else->"No enviado" }
            bubble.addView(label(SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(message.time))+if(state.isBlank()) "" else " · $state",11f,muted).apply { gravity=Gravity.END;setPadding(0,dp(6),0,0) })
            if(message.outgoing&&message.status<0&&message.note.isNotBlank())bubble.addView(label(message.note,12f,muted).apply { maxWidth=(resources.displayMetrics.widthPixels*0.73).toInt() })
            line.tag=message.outgoing to message.id
            line.addView(bubble);list.addView(line)
        }
        scroll.post {
            if(scroll===messageScroll) {
                val replacement=(0 until list.childCount).map { list.getChildAt(it) }
                    .firstOrNull { anchorKey!=null && it.tag==anchorKey }
                if(atBottom && !preservePosition) scroll.scrollTo(0,list.height)
                else scroll.scrollTo(0,replacement?.top?.minus(anchorOffset) ?: previousY)
                restoringScroll=false
            }
        }
    }
    private fun loadOlder() {
        val jid=conversation ?: return
        val oldest=renderedMessages.firstOrNull() ?: return
        if(historyRequestPeer?.let { store.resolve(it)==store.resolve(jid) }==true)return
        try {
            if(store.hasEarlier(jid,oldest)) {
                if(historyWindow<5000) {
                    historyWindow=(historyWindow+100).coerceAtMost(5000)
                    refreshMessages(true)
                }
                return
            }
            queuedHistoryPeer=jid
            requestQueuedHistory()
        } catch(error: Exception) { storageFailure(error) }
    }
    private fun finishHistoryPage(ok: Boolean) {
        historyRequestPeer?.let { store.endHistoryPage(it,ok) }
        historyRequestPeer=null
        historyRequestId=null
    }
    private fun updateSend() { sendButton?.isEnabled=connected&&pendingMessage==null;sendButton?.alpha=if(connected&&pendingMessage==null)1f else .4f }
    private fun sendText() {
        val jid=conversation?:return;val text=compose?.text?.toString()?.trim().orEmpty();if(text.isBlank())return
        val bytes=text.toByteArray(Charsets.UTF_8)
        if(bytes.size>4000){compose?.error="Máximo 4.000 bytes de texto por mensaje";return}
        val active=connection;if(!connected||active==null)return
        val id="3EB0"+UUID.randomUUID().toString().replace("-","").uppercase(Locale.ROOT)
        debugLog.add("MESSAGE_TARGET direction=outgoing selected=${DebugLog.addressHint(jid)} stored=${DebugLog.addressHint(store.resolve(jid))}")
        try { store.append(id,jid,text,true,0) } catch(error: Exception) { storageFailure(error);return }
        pendingMessage=id;compose?.text?.clear();refreshMessages();updateSend()
        if(!active.send(jid,id,bytes)){store.status(id,-1,"No se pudo iniciar el envío");pendingMessage=null;refreshMessages();updateSend()}
    }
    private fun applyEvents(events: List<Array<String>>) {
        try {
        // Reconcile old LID chats as well as addresses in this batch. Persisted
        // core mappings can predate the app update; no number is guessed.
        val candidates=(store.chats().map { it.jid }+events.mapNotNull { it.getOrNull(2) }).filter { it.endsWith("@lid") }.toSet()
        for(lid in candidates) {
            val pn=connection?.resolve(lid)
            if(pn!=null && pn.endsWith("@s.whatsapp.net")) {
                if(store.link(lid,pn)) debugLog.add("CONTACT_ALIAS_RECONCILED from=${DebugLog.addressHint(lid)} to=${DebugLog.addressHint(pn)}")
                else debugLog.add("CONTACT_ALIAS_CONFLICT from=${DebugLog.addressHint(lid)} proposed=${DebugLog.addressHint(pn)} ignored=true")
            } else {
                val line="CONTACT_ALIAS_UNRESOLVED from=${DebugLog.addressHint(lid)}"
                if(identityDiagnostics["alias:$lid"]!=line) { identityDiagnostics["alias:$lid"]=line;debugLog.add(line) }
            }
        }
        for(e in events) {
            if(e.size<5)continue;val state=e[4].toIntOrNull()?:0
            when(e[0]) {
                "ready" -> { connected=true;statusText="Conectado · texto cifrado";reconnectPolicy.connected(SystemClock.elapsedRealtime()) }
                "status" -> if(e[1]==historyRequestId) {
                    debugLog.add("HISTORY_REQUEST_STATUS state=$state")
                    if(state<0)finishHistoryPage(false)
                } else { val matched=store.status(e[1],state,e[3],e[2]);if(matched&&e[1]==pendingMessage&&(state<0||state>=2))pendingMessage=null }
                "message", "sent-copy" -> if(e[1].isNotBlank()&&e[2].isNotBlank()) {
                    debugLog.hide(e[2].substringBefore('@'))
                    debugLog.add("MESSAGE_TARGET kind=${e[0]} event=${DebugLog.addressHint(e[2])} stored=${DebugLog.addressHint(store.resolve(e[2]))}")
                    store.append(e[1],e[2],e[3],e[0]=="sent-copy",state,e.getOrNull(5)?.toLongOrNull()?.takeIf { it>0 } ?: System.currentTimeMillis())
                    // The native inbox keeps the text until this SQLite commit succeeds.
                    connection?.stored(e[1],e[2],e[0]=="sent-copy")
                }
                "history", "history-status" -> {
                    debugLog.add("HISTORY_EVENT kind=${e[0]} state=$state")
                    if(state<0)finishHistoryPage(false)
                }
                "state-key" -> debugLog.add("APP_STATE_KEY_RECEIVED")
                "state-sync" -> debugLog.add("APP_STATE_REPLY collection=${e[3]} state=$state")
                "delete-result" -> {
                    chatChangeInFlight=false
                    debugLog.add("CHAT_CHANGE_SYNC_RESULT action=${e[3]} state=$state")

                }
                "alias-conflict", "alias-storage" -> debugLog.add("CONTACT_MAPPING kind=${e[0]}")
                "contact" -> if(e[2].isNotBlank()) store.learnName(e[2],e[3])
                "notice" -> debugLog.add("MESSAGING_NOTICE state=$state detail=${e[3]}")
            }
        }
        statusLabel.text=statusText
        refreshConversation()
        scheduleContacts()
        scheduleHistory(events.any { it.firstOrNull() in listOf("state-sync","history","ready") })
        requestQueuedHistory()
        sendQueuedChatChange()
        updateSend()
        } catch (error: Exception) { storageFailure(error) }
    }
    private fun requestQueuedHistory() {
        val jid=queuedHistoryPeer ?: return
        if(conversation?.let { store.resolve(it) }!=store.resolve(jid)) { queuedHistoryPeer=null;return }
        val active=connection ?: return
        if(!connected || historyBusy || historyRequestId!=null || pendingMessage!=null)return
        queuedHistoryPeer=null
        val anchor=store.historyAnchor(jid) ?: return
        if(!store.beginHistoryPage(jid,anchor))return
        val id="3EB0"+UUID.randomUUID().toString().replace("-","").uppercase(Locale.ROOT)
        historyRequestId=id;historyRequestPeer=jid
        if(!active.requestHistory(jid,anchor,id)) {
            finishHistoryPage(false)
            queuedHistoryPeer=jid
            return
        }
        debugLog.add("HISTORY_REQUEST scope=older-messages")
        handler.postDelayed({ if(historyRequestId==id && !destroyed) {
            finishHistoryPage(false);debugLog.add("HISTORY_REQUEST_WAIT_EXPIRED")
            requestQueuedHistory()
        } },120_000L)
    }
    private fun downloadHistoryMedia(task: ByteArray): ByteArray {
        val path=nativeHistoryPath(task) ?: throw java.io.IOException("Invalid history path")
        debugLog.add("HISTORY_PATH kind=${if(path.isEmpty()) "inline" else "cdn-relative"}")
        val encrypted=if(path.isEmpty()) ByteArray(0) else {
            // A fixed host and no redirects keep media requests separate from account traffic.
            val request=Request.Builder().url("https://mmg.whatsapp.net$path").header("Origin","https://web.whatsapp.com").build()
            historyHttp.newCall(request).execute().use { response ->
                debugLog.add("HISTORY_DOWNLOAD http=${response.code}")
                if(!response.isSuccessful)throw java.io.IOException("History download unavailable")
                val body=response.body ?: throw java.io.IOException("Empty history download")
                if(body.contentLength()>8L*1024*1024)throw java.io.IOException("History chunk too large")
                body.byteStream().use { input ->
                    val output=java.io.ByteArrayOutputStream();val buffer=ByteArray(16384)
                    while(true) { val n=input.read(buffer);if(n<0)break;if(output.size()+n>8*1024*1024)throw java.io.IOException("History chunk too large");output.write(buffer,0,n) }
                    output.toByteArray()
                }
            }
        }
        debugLog.add("HISTORY_DOWNLOAD_COMPLETE bytes=${encrypted.size}")
        return encrypted
    }
    private fun scheduleHistory(force: Boolean = false) {
        if(historyBusy || destroyed)return
        val active=connection ?: return
        val now=SystemClock.elapsedRealtime()
        if(!force && lastHistoryAttempt!=0L && now-lastHistoryAttempt<60_000L)return
        val tasks=active.historyTasks()
        if(tasks.isEmpty())return
        historyBusy=true;lastHistoryAttempt=now
        val requestedPeer=historyRequestPeer
        val requestedId=historyRequestId
        historyWorker.execute {
            var pageReceived=false;var errorType: String?=null;var stateFailed=false
            val db=ChatStore(applicationContext)
            try {
                for((slot,task) in tasks.sortedBy { nativeHistoryOrder(it.second) }) {
                    val stateTask=nativeHistoryOrder(task)!=Long.MAX_VALUE
                    if(Thread.currentThread().isInterrupted)break
                    var handle=0L
                    var stage="path"
                    try {
                        val encrypted=downloadHistoryMedia(task)
                        var prepared=task
                        var externalCount=0
                        while(true) {
                            if(Thread.currentThread().isInterrupted)throw java.io.InterruptedIOException()
                            stage="external-reference"
                            val reference=nativeHistoryExternalTask(prepared)
                                ?: throw java.io.IOException("Invalid external mutations reference")
                            if(reference.isEmpty())break
                            if(++externalCount>256)throw java.io.IOException("Too many external mutation lists")
                            stage="external-download"
                            val media=downloadHistoryMedia(reference)
                            stage="external-decode"
                            prepared=nativeHistoryHydrate(prepared,media)
                                ?: throw java.io.IOException("External mutations integrity or format check failed")
                            debugLog.add("APP_STATE_EXTERNAL_MUTATIONS_READY count=$externalCount")
                        }
                        stage="decode"
                        val missing=arrayOfNulls<String>(1)
                        handle=active.openHistory(prepared,encrypted,missing)
                        missing[0]?.let {
                            debugLog.add("APP_STATE_KEY_MISSING")
                            active.requestSyncKey(it)
                        }
                        if(handle==0L)throw java.io.IOException("History integrity or format check failed")
                        var importedMessages=0;var importedChats=0;var containsRequestedPeer=false
                        stage="sqlite"
                        db.historyTransaction {
                            while(true) {
                                if(Thread.currentThread().isInterrupted)throw java.io.InterruptedIOException()
                                val e=nativeHistoryNext(handle) ?: break
                                val time=e[5].toLong()
                                val deletion=e[0] in listOf("delete","delete-chat","clear-chat","archive")
                                if(deletion && e[2].endsWith("@lid")) {
                                    val phone=active.resolve(e[2])
                                    if(phone!=null && phone.endsWith("@s.whatsapp.net") && !db.link(e[2],phone))
                                        debugLog.add("SYNC_DELETE_ALIAS_CONFLICT")
                                }
                                when(e[0]) {
                                    "alias" -> if(!db.link(e[1],e[2]))throw java.io.IOException("Conflicting history address")
                                    "chat" -> {
                                        db.historyChat(e[2],e[3]);importedChats++
                                        if(requestedPeer!=null && db.resolve(e[2])==db.resolve(requestedPeer))
                                            containsRequestedPeer=true
                                    }
                                    "delete", "edit" -> db.applyMessageMutation(e[2],e[1],e[4]=="1",e[0],time,e[3])
                                    "archive" -> {
                                        db.setArchived(e[2],e[4]=="1",time)
                                        debugLog.add("SYNC_CHAT_ARCHIVE archived=${e[4]=="1"}")
                                    }
                                    "delete-chat", "clear-chat" -> {
                                        db.applyChatDeletion(e[2],e[3].toLong(),time,e[0]=="delete-chat")
                                        debugLog.add("SYNC_CHAT_REMOVAL kind=${e[0]}")
                                    }
                                    "name" -> db.learnName(e[2],e[3])
                                    "message", "sent-copy" -> { db.append(e[1],e[2],e[3],e[0]=="sent-copy",e[4].toInt(),time,true);importedMessages++ }
                                }
                            }
                        }
                        if(containsRequestedPeer)pageReceived=true
                        // Only a committed complete chunk may remove its durable task.
                        stage="acknowledge"
                        val confirmed=active.historyDone(slot,task,nativeHistoryVersion(handle))
                        debugLog.add("HISTORY_CHUNK_COMMITTED chats=$importedChats messages=$importedMessages task_confirmed=$confirmed")
                        if(!confirmed)throw java.io.IOException("History task acknowledgement unavailable")
                        if(stateTask)active.deleteFromSnapshot(handle)
                    } catch(error: Exception) {
                        if(stateTask)stateFailed=true else errorType=error.javaClass.simpleName
                        debugLog.add("SYNC_CHUNK_FAILED state=$stateTask stage=$stage type=${error.javaClass.simpleName} retained=true")
                    }
                    finally { if(handle!=0L)nativeHistoryClose(handle) }
                }
            } catch(error: Exception) { errorType=error.javaClass.simpleName;debugLog.add("HISTORY_IMPORT_FAILED type=$errorType retained=true") }
            finally { db.close() }
            handler.post {
                if(!destroyed) {
                    historyBusy=false
                    if(pageReceived && requestedPeer!=null && requestedId==historyRequestId) {
                        finishHistoryPage(true)
                        if(conversation?.let { store.resolve(it)==store.resolve(requestedPeer) }==true) {
                            historyWindow=(historyWindow+100).coerceAtMost(5000)
                            refreshMessages(true)
                        }
                    }
                    val open=conversation
                    if(open!=null && !store.containsChat(open))showList() else refreshConversation()
                    statusLabel.text=statusText;scheduleContacts(true)
                    requestQueuedHistory()
                    // Process later notifications, but do not loop on a failed chunk.
                    if(errorType==null&&!stateFailed) { lastHistoryAttempt=0L;scheduleHistory() }
                    else handler.postDelayed({ if(!destroyed)scheduleHistory(true) },120_000L)
                }
            }
        }
    }
    private fun storageFailure(error: Exception) {
        debugLog.failure("MESSAGE_STORAGE_FAILED",error)
        sessionGeneration++
        reconnectPolicy.reset()
        automaticReconnect=false
        connection?.cancel();connection=null
        connected=false;pendingMessage=null
        chatChangeInFlight=false
        render("Sin conexión · No se pudieron guardar los mensajes. Revisa el espacio disponible. Los recibidos pendientes se conservan.","",false)
    }
    private fun showMainMenu() {
        val rows=column().apply { setPadding(dp(14),dp(8),dp(14),dp(14));setBackgroundColor(canvas) }
        val dialog=AlertDialog.Builder(this).setTitle("Menú").setView(rows).create()
        fun item(title: String, detail: String, run: () -> Unit) {
            rows.addView(column().apply {
                background=shape(surface,16)
                setPadding(dp(16),dp(11),dp(16),dp(11))
                addView(label(title,17f).apply { setTypeface(null,Typeface.BOLD) })
                addView(label(detail,13f,muted))
                isFocusable=true
                setOnClickListener { dialog.dismiss();run() }
            },LinearLayout.LayoutParams(-1,-2).apply { bottomMargin=dp(6) })
        }
        conversation?.let { jid -> item("Opciones de conversación","Archivar, editar o eliminar") { chatActions(jid) } }
        if(showingArchived) item("Conversaciones","Volver a la lista principal") { showList(false) }
        else {
            val count=store.chats(true).size
            item("Archivados",if(count==1)"1 conversación archivada" else "$count conversaciones archivadas") { showList(true) }
        }
        item("Vinculación","Dispositivo vinculado y estado de conexión") { showSettings() }
        item("Diagnóstico","Controles de conexión y registro") { showDiagnostics() }
        dialog.show()
    }
    private fun showDiagnostics() {
        val tools=column()
        val save=Button(this).apply { text="Guardar log";setOnClickListener { exportLog() } }
        listOf(check,stop,save).forEach {
            (it.parent as? ViewGroup)?.removeView(it)
            tools.addView(it)
        }
        AlertDialog.Builder(this).setTitle("Diagnóstico").setView(tools)
            .setPositiveButton("Cerrar",null).show()
    }
    private fun showSettings() {
        (settingsBody.parent as? ViewGroup)?.removeView(settingsBody)
        AlertDialog.Builder(this).setTitle("Conexión · $appVersion").setView(ScrollView(this).apply { addView(settingsBody) }).setPositiveButton("Cerrar",null).show()
    }
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        window.statusBarColor=surface
        window.navigationBarColor=canvas
        window.decorView.systemUiVisibility=0 // Light system icons on dark surfaces.
        debugLog=DebugLog(File(filesDir,"diagnostics/latest.txt"))
        debugLog.add("APP_OPEN version=$appVersion sdk=${Build.VERSION.SDK_INT} abis=${Build.SUPPORTED_ABIS.joinToString(",")}")
        oldCrashHandler=Thread.getDefaultUncaughtExceptionHandler()
        crashHandler=Thread.UncaughtExceptionHandler { thread,error -> debugLog.failure("UNCAUGHT",error);oldCrashHandler?.uncaughtException(thread,error) }
        Thread.setDefaultUncaughtExceptionHandler(crashHandler)
        store=ChatStore(this);store.interrupted()
        phoneContacts=PhoneContacts(this)
        settingsBody=column().apply { setPadding(dp(20),dp(4),dp(20),dp(16)) }
        number=EditText(this).apply { hint="Tu número internacional, sin +";inputType=InputType.TYPE_CLASS_PHONE;setSingleLine(true);isSaveEnabled=false }
        pair=Button(this).apply { text="Solicitar código de vinculación con historial" }
        check=Button(this).apply { text="Reconectar sesión guardada" }
        stop=Button(this).apply { text="Desconectar";isEnabled=false }
        code=label("",32f).apply { setTextIsSelectable(true) };output=label("",14f)
        listOf(number,pair,code,output).forEach { settingsBody.addView(it) }
        pair.setOnClickListener {
            val phone=number.text.toString().trim()
            val previous=activeIdentity()
            val savedPhone=if(previous.exists())nativeIdentityPhone(previous.absolutePath) else null
            when {
                !phone.matches(Regex("^[1-9][0-9]{7,14}$")) -> number.error="Usa el prefijo de país y entre 8 y 15 dígitos"
                savedPhone!=null && savedPhone!=phone -> number.error="Usa el mismo número de la sesión guardada"
                else -> {
                    automaticReconnect=true;reconnectPolicy.reset()
                    val identity=if(savedPhone==null)previous else File(filesDir,"identity-sync-${UUID.randomUUID().toString().replace("-","")}.v1")
                    start(phone,identity)
                }
            }
        }
        check.setOnClickListener { automaticReconnect=true;reconnectPolicy.reset();start("") }
        stop.setOnClickListener {
            automaticReconnect=false
            sessionGeneration++;reconnectPolicy.reset();connection?.cancel();connection=null
            connected=false;pendingMessage=null
            if(chatChangeInFlight) {
                chatChangeInFlight=false
                debugLog.add("DELETE_SYNC_INTERRUPTED")
            }
            try {
                queuedHistoryPeer=null
                finishHistoryPage(false)
                store.interrupted()
                render("Conexión pausada desde Diagnóstico.","",false)
            }
            catch(error: Exception) { storageFailure(error) }
        }
        showList();render("Sin conexión · abre ⋮ para vincular", "",false)
        networkWasAvailable=networkAvailable()
        val filter=IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION)
        if(Build.VERSION.SDK_INT>=33) registerReceiver(networkReceiver,filter,Context.RECEIVER_NOT_EXPORTED)
        else registerReceiver(networkReceiver,filter)
        networkReceiverRegistered=true
        if(activeIdentity().exists())start("")

    }
    @Deprecated("Legacy navigation supports Android 5")
    override fun onBackPressed() { if(conversation!=null)showList() else if(showingArchived)showList(false) else super.onBackPressed() }

    private fun render(status: String, pairingCode: String, busy: Boolean) {
        if (destroyed) return
        output.text = status
        statusText = status
        if (::statusLabel.isInitialized) statusLabel.text = status
        updateSend()
        code.text = if (pairingCode.length == 8) "${pairingCode.take(4)}-${pairingCode.takeLast(4)}" else ""
        pair.isEnabled = !busy
        check.isEnabled = activeIdentity().exists()
        stop.isEnabled = busy || (automaticReconnect && activeIdentity().exists())
        number.isEnabled = !busy
    }

    private val identityPreferences by lazy { getSharedPreferences("companion",MODE_PRIVATE) }
    private fun activeIdentity(): File {
        val name=identityPreferences.getString("identity",null).orEmpty()
        return File(filesDir,if(name.matches(Regex("identity-sync-[0-9a-f]{32}\\.v1")))name else "identity.v1")
    }
    private fun start(phone: String, identity: File = activeIdentity()) {
        sessionGeneration++
        connection?.cancel()
        if(chatChangeInFlight) {
            chatChangeInFlight=false
            debugLog.add("DELETE_SYNC_INTERRUPTED")
        }
        finishHistoryPage(false)
        connected=false
        updateSend()
        debugLog.hide(phone)
        val attempt = ++attempts
        debugLog.add("ATTEMPT $attempt BEGIN mode=${if (phone.isEmpty()) "check-session" else "pair-code"}")
        val next = PairConnection(phone, attempt, identity)
        connection = next
        render("Preparando claves y conexión segura…", "", true)
        next.connect()
    }

    private inner class PairConnection(private val phone: String, private val attempt: Int, private val identity: File) : WebSocketListener() {
        private val lock = Any()
        private var handle = 0L
        private var ws: WebSocket? = null
        private var finished = false
        private var lastState: Int? = null
        private var authenticated = false
        private var lastIncoming = SystemClock.elapsedRealtime()
        fun requestHistory(jid: String, anchor: ChatStore.Message?, id: String): Boolean = synchronized(lock) {
            if(finished || handle==0L)return@synchronized false
            val ok=nativeHistoryRequest(handle,jid,anchor?.id.orEmpty(),anchor?.outgoing ?: false,anchor?.time ?: 0L,id)
            ws?.let { flush(it) };publish();ok
        }
        fun historyTasks(): List<Pair<Int,ByteArray>> = synchronized(lock) {
            if(finished || handle==0L) emptyList() else (0 until nativeHistorySlots()).mapNotNull { slot -> nativeHistoryTask(handle,slot)?.let { slot to it } }
        }
        fun openHistory(task: ByteArray, encrypted: ByteArray, missing: Array<String?>): Long = synchronized(lock) {
            if(finished || handle==0L)return@synchronized 0L
            val result=nativeHistoryOpen(handle,task,encrypted,missing)
            drainNative()
            result
        }
        fun requestSyncKey(key: String) = synchronized(lock) {
            if(!finished && handle!=0L) {
                val sent=nativeSyncKey(handle,key,UUID.randomUUID().toString().replace("-","").uppercase(Locale.ROOT))
                log("APP_STATE_KEY_REQUEST queued=$sent")
                if(sent)ws?.let { flush(it) }
            }
        }
        fun deleteChat(request: ChatChange): Boolean = synchronized(lock) {
            if(finished || handle==0L)return@synchronized false
            val sent=nativeDeleteChat(handle,request.peer,request.id,request.outgoing,request.time,request.changed,request.change)
            if(sent)ws?.let { flush(it) }
            sent
        }
        fun deleteFromSnapshot(cursor: Long) = synchronized(lock) {
            if(!finished && handle!=0L) {
                nativeDeleteSnapshot(handle,cursor)
                ws?.let { flush(it) }
                events()
            }
        }
        fun historyDone(slot: Int, task: ByteArray, version: Long): Boolean = synchronized(lock) {
            !finished && handle!=0L && nativeHistoryDone(handle,slot,task,version)
        }
        fun resolve(jid: String): String? = synchronized(lock) {
            if(finished || handle==0L) null else nativeResolve(handle,jid)
        }
        fun stored(id: String, jid: String, outgoing: Boolean) = synchronized(lock) {
            if(!finished&&handle!=0L) {
                val ok=nativeStored(handle,id,jid,outgoing)
                log("INBOX_DATABASE_HANDOFF confirmed=$ok")
                drainNative()
            }
        }
        fun send(jid: String, id: String, text: ByteArray): Boolean = synchronized(lock) {
            if(finished || handle==0L)return@synchronized false
            nativeTick(handle,SystemClock.elapsedRealtime())
            val ok=nativeSend(handle,jid,id,text)
            ws?.let { flush(it) }
            publish()
            ok
        }
        private fun events() {
            if(handle==0L)return
            val pending=mutableListOf<Array<String>>()
            while(true)pending.add(nativeEvent(handle)?:break)
            if(pending.any { it.firstOrNull()=="ready" })deadline?.cancel(false)
            if(pending.isNotEmpty())handler.post { if(connection===this&&!destroyed)applyEvents(pending) }
        }
        private fun log(event: String) { debugLog.add("A$attempt $event") }
        private fun drainNative() {
            if (handle != 0L) nativeLog(handle).lineSequence().filter { it.isNotBlank() }.forEach { log(it) }
        }
        private var heartbeat: ScheduledFuture<*>? = null
        private var deadline: ScheduledFuture<*>? = null

        fun connect() = synchronized(lock) {
            log("NATIVE_CREATE_BEGIN")
            handle = nativeCreate(identity.absolutePath, phone)
            drainNative()
            log("NATIVE_CREATE_END allocated=${handle != 0L}")
            if (handle == 0L) { finish("No se pudo preparar la sesión."); return@synchronized }
            if (nativeState(handle) < 0) { finish(nativeStatus(handle)); return@synchronized }
            nativeTick(handle,SystemClock.elapsedRealtime())
            log("WEBSOCKET_CONNECT_REQUEST")
            val tracedHttp = try { NetworkTrace.client(http) { event -> log(event) } }
                catch (error: Exception) { debugLog.failure("NETWORK_SETUP_FAILED", error); finish("No se pudo preparar TLS."); return@synchronized }
            ws = tracedHttp.newWebSocket(
                Request.Builder().url("wss://web.whatsapp.com/ws/chat")
                    .header("Origin", "https://web.whatsapp.com")
                    .header("User-Agent", "WhatsAppLegacy/$appVersion (experimental)")
                    .build(), this
            )
            deadline = timer.schedule({ synchronized(lock) {
                if (!finished) { log("DEADLINE_EXPIRED"); finish("Tiempo de conexión agotado. Las claves se conservan.",retryNetwork=phone.isEmpty()) }
            } }, 180, TimeUnit.SECONDS)
        }

        override fun onOpen(webSocket: WebSocket, response: Response) = synchronized(lock) {
            if (finished) { webSocket.cancel(); return@synchronized }
            lastIncoming=SystemClock.elapsedRealtime()
            log("WEBSOCKET_OPEN http=${response.code} tls=${response.handshake?.tlsVersion?.javaName}")
            nativeStart(handle)
            drainNative()
            flush(webSocket)
            publish()
            if (!finished) heartbeat = timer.scheduleAtFixedRate({ synchronized(lock) {
                if (!finished && handle != 0L) {
                    val now=SystemClock.elapsedRealtime()
                    if(now-lastIncoming>75000)finish("WhatsApp no responde. Reconectando…",retryNetwork=authenticated||phone.isEmpty())
                    else { nativeTick(handle,now);nativePing(handle);flush(webSocket);publish() }
                }
            } }, 15, 15, TimeUnit.SECONDS)
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) = synchronized(lock) {
            if (finished) return@synchronized
            if (bytes.size > 2 * 1024 * 1024) { finish("Respuesta demasiado grande."); return@synchronized }
            lastIncoming=SystemClock.elapsedRealtime()
            log("WEBSOCKET_RX binary_bytes=${bytes.size}")
            nativeFeed(handle, bytes.toByteArray())
            drainNative()
            flush(webSocket)
            publish()
        }

        override fun onMessage(webSocket: WebSocket, text: String) = synchronized(lock) {
            log("WEBSOCKET_RX unexpected_text_chars=${text.length}")
            if (!finished) finish("El servidor ha enviado un formato inesperado.")
        }

        private fun flush(webSocket: WebSocket) {
            if (finished || handle == 0L) return
            while (true) {
                val data = nativeOutput(handle) ?: break
                log("WEBSOCKET_TX binary_bytes=${data.size}")
                if (!webSocket.send(data.toByteString())) { finish("No se pudo enviar el paquete cifrado.",retryNetwork=authenticated||phone.isEmpty()); break }
            }
        }

        private fun publish() {
            if (finished || handle == 0L) return
            nativeTick(handle,SystemClock.elapsedRealtime())
            ws?.let { flush(it) }
            if(finished || handle==0L)return
            events()
            val state = nativeState(handle)
            val status = nativeStatus(handle)
            val pairingCode = nativeCode(handle)
            if (pairingCode.length == 8) {
                debugLog.hide(pairingCode)
                debugLog.hide("${pairingCode.take(4)}-${pairingCode.takeLast(4)}")
            }
            drainNative()
            if (state != lastState) { log("STATE_CHANGE $lastState->$state"); lastState = state }
            if (state == 4) {
                log("PAIRING_RESTART_REQUIRED reconnect=automatic")
                finish("Identidad verificada. Reconectando para completar la vinculación…", reconnect = true)
                return
            }
            if(state==5) {
                if(!authenticated) {
                    val previous=activeIdentity()
                    if(identity!=previous) {
                        val account=nativeIdentityPhone(previous.absolutePath)
                        if(account==null || account!=nativeIdentityPhone(identity.absolutePath)) {
                            finish("La vinculación no coincide con el número de la sesión guardada.");return
                        }
                        if(!identityPreferences.edit().putString("identity",identity.name).commit()) {
                            finish("No se pudo guardar la nueva sesión. La sesión anterior se conserva.");return
                        }
                    }
                    authenticated=true
                    handler.post { if(connection===this)render(status,"",true) }
                }
                return
            }
            if (state < 0 || state == 6) {
                val revoked=listOf("401","403","440").any { status.contains("(codigo $it)") }
                val transient=listOf("500","502","503","504","515").any { status.contains("(codigo $it)") }
                finish(status,retryNetwork=!revoked&&(authenticated||(phone.isEmpty()&&transient)));return
            }
            handler.post { if (connection === this && !finished) render(status, pairingCode, true) }
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) = synchronized(lock) {
            log("WEBSOCKET_CLOSING close_code=$code")
            webSocket.close(code, null)
            if (!finished) finish("WhatsApp ha cerrado la conexión.",retryNetwork=authenticated||phone.isEmpty())
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = synchronized(lock) {
            log("WEBSOCKET_CLOSED close_code=$code")
            if(!finished) finish("WhatsApp ha cerrado la conexión.",retryNetwork=authenticated||phone.isEmpty())
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = synchronized(lock) {
            if (finished) { log("WEBSOCKET_CLOSED_AFTER_ATTEMPT"); return@synchronized }
            log("WEBSOCKET_FAILURE http=${response?.code ?: "none"}")
            debugLog.failure("A$attempt NETWORK", t)
            if (!finished) finish("Conexión interrumpida. Las claves se conservan.", retryNetwork=authenticated||phone.isEmpty())
        }

        private fun finish(status: String, reconnect: Boolean = false, retryNetwork: Boolean = false) {
            if (finished) return
            drainNative()
            log("ATTEMPT_END state=${if (handle != 0L) nativeState(handle) else -1}")
            finished = true
            heartbeat?.cancel(false)
            deadline?.cancel(false)
            ws?.close(1000, "Finished")
            if (handle != 0L) { nativeFree(handle); handle = 0L }
            handler.post {
                if (connection === this && !destroyed) {
                    if (reconnect) {
                        log("AUTOMATIC_SESSION_RECONNECT")
                        start("",identity)
                    } else {
                        connection = null
                        connected=false
                        if(chatChangeInFlight) {
                            chatChangeInFlight=false
                            debugLog.add("DELETE_SYNC_DISCONNECTED")
                        }
                        try {
                            pendingMessage=null
                            if(retryNetwork)queuedHistoryPeer=queuedHistoryPeer ?: historyRequestPeer
                            finishHistoryPage(false)
                            store.interrupted()
                        } catch(error: Exception) { storageFailure(error);return@post }
                        render("Sin conexión · $status", "", false)
                        val delay=if(retryNetwork)reconnectPolicy.nextDelay(SystemClock.elapsedRealtime()) else null
                        if(delay!=null && automaticReconnect) {
                            val generation=sessionGeneration
                            log("RECONNECT_SCHEDULED delay_ms=$delay mode=${if(delay>=60_000L) "maintenance" else "quick"}")
                            val wait=if(delay>=60_000L) "${delay/60_000L} min" else "${delay/1_000L} s"
                            render("Reconectando en $wait.","",false)
                            handler.postDelayed({ if(automaticReconnect&&connection==null&&!destroyed&&generation==sessionGeneration)start("") },delay)
                        }
                    }
                }
            }
        }

        fun cancel() = synchronized(lock) {
            if (!finished) {
                drainNative()
                log("CANCEL state=${if (handle != 0L) nativeState(handle) else -1}")
                finished = true
                heartbeat?.cancel(false)
                deadline?.cancel(false)
                ws?.cancel()
                if (handle != 0L) { nativeFree(handle); handle = 0L }
            }
        }
    }

    private fun exportLog() {
        debugLog.add("LOG_EXPORT_REQUEST")
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "text/plain"
            putExtra(Intent.EXTRA_TITLE, "whatsapp-legacy-$appVersion-$stamp.txt")
        }
        try { startActivityForResult(intent, exportRequest) }
        catch (error: Exception) { debugLog.failure("EXPORT_PICKER_FAILED", error); Toast.makeText(this, "Usa Copiar log para exportarlo", Toast.LENGTH_LONG).show() }
    }

    @Deprecated("Legacy Activity result API supports Android 5.0")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == exportRequest && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            try {
                val stream = contentResolver.openOutputStream(uri, "wt") ?: throw java.io.IOException("No output")
                stream.use { it.write(debugLog.snapshot().toByteArray(Charsets.UTF_8)) }
                Toast.makeText(this, "Log guardado; puedes adjuntarlo en el chat", Toast.LENGTH_LONG).show()
            } catch (error: Exception) {
                debugLog.failure("LOG_EXPORT_FAILED", error)
                Toast.makeText(this, "No se pudo guardar; usa Copiar log", Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onDestroy() {
        debugLog.add("APP_DESTROY changing_configuration=$isChangingConfigurations")
        if (Thread.getDefaultUncaughtExceptionHandler() === crashHandler) Thread.setDefaultUncaughtExceptionHandler(oldCrashHandler)
        destroyed = true
        contactGeneration++
        if(networkReceiverRegistered) { unregisterReceiver(networkReceiver); networkReceiverRegistered=false }
        contactWorker.shutdownNow()
        historyWorker.shutdownNow();historyHttp.dispatcher.cancelAll()
        historyHttp.dispatcher.executorService.shutdown();historyHttp.connectionPool.evictAll()
        connection?.cancel()
        connection = null
        if(::store.isInitialized)store.close()
        timer.shutdownNow()
        http.dispatcher.executorService.shutdown()
        http.connectionPool.evictAll()
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }
    companion object { init { System.loadLibrary("wa_legacy") } }
}
