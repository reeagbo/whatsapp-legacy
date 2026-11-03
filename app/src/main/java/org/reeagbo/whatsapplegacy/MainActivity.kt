package org.reeagbo.whatsapplegacy

import android.Manifest
import android.content.BroadcastReceiver
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.ConnectivityManager
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
    private var messageList: LinearLayout? = null
    private var messageScroll: ScrollView? = null
    private var compose: EditText? = null
    private var sendButton: Button? = null
    private var connected = false
    private var statusText = "Sin conexión"
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
            if (restored && automaticReconnect && !destroyed && File(filesDir,"identity.v1").exists()) {
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
    private var chatTitle: TextView? = null
    private val contactRequest = 302
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
    private fun display(jid: String) = if (jid.endsWith("@lid")) "Contacto · …" + jid.substringBefore('@').takeLast(6) else "+" + jid.substringBefore('@').substringBefore(':')
    private fun contactName(chat: ChatStore.Chat) = chat.name.takeIf { it.isNotBlank() }
        ?: phoneNames[chat.jid] ?: chat.pushName.takeIf { it.isNotBlank() } ?: display(chat.jid)
    private fun title(jid: String) = store.chats().firstOrNull { it.jid == store.resolve(jid) }?.let { contactName(it) } ?: display(jid)
    private fun refreshConversation() {
        val current = conversation
        if (current == null) showList() else {
            conversation = store.resolve(current)
            chatTitle?.text = title(conversation!!)
            refreshMessages()
        }
    }
    private fun editName(jid: String) {
        val value = EditText(this).apply { setSingleLine(true); hint="Nombre"; setText(store.chats().firstOrNull { it.jid==store.resolve(jid) }?.name.orEmpty()) }
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
            val names=try { phoneContacts.names(requested) } catch(error: Exception) {
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
        if(Build.VERSION.SDK_INT>=23 && !phoneContacts.allowed()) requestPermissions(arrayOf(Manifest.permission.READ_CONTACTS),contactRequest)
        else scheduleContacts(true)
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode,permissions,results)
        if(requestCode==contactRequest) {
            scheduleContacts(true)
            if(!phoneContacts.allowed()) Toast.makeText(this,"Sin acceso a la agenda: se usarán los nombres recibidos o los números",Toast.LENGTH_LONG).show()
        }
    }
    override fun onResume() { super.onResume(); if(::store.isInitialized) scheduleContacts(true) }
    private fun action(text: String, description: String, click: () -> Unit) = Button(this).apply {
        this.text = text; contentDescription = description; setTextColor(green); textSize = 23f
        minWidth = dp(48); minimumWidth = dp(48); minHeight = dp(48); background = shape(Color.TRANSPARENT)
        setOnClickListener { click() }
    }
    private fun header(name: String, chat: Boolean) {
        val bar = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; setPadding(dp(10),dp(9),dp(10),dp(6)); setBackgroundColor(surface) }
        if (chat) bar.addView(action("‹", "Volver a conversaciones") { showList() }, LinearLayout.LayoutParams(dp(48),dp(48)))
        bar.addView(label(name, if(chat) 21f else 26f).apply { setTypeface(null,Typeface.BOLD); maxLines=1; gravity=Gravity.CENTER_VERTICAL; if(chat) { chatTitle=this; contentDescription="Editar nombre del contacto"; setOnClickListener { conversation?.let { editName(it) } } } }, LinearLayout.LayoutParams(0,dp(52),1f).apply { gravity=Gravity.CENTER_VERTICAL })
        if (!chat) bar.addView(action("+", "Nueva conversación") { newConversation() },LinearLayout.LayoutParams(dp(52),dp(52)))
        bar.addView(action("⋮", "Conexión y diagnóstico") { showSettings() },LinearLayout.LayoutParams(dp(48),dp(52)))
        page.addView(bar)
        statusLabel=label(statusText,13f,muted).apply { setPadding(dp(20),dp(4),dp(16),dp(12)); setBackgroundColor(surface) }
        page.addView(statusLabel)
    }
    private fun newPage() {
        chatTitle=null
        page=column().apply { setBackgroundColor(canvas) }
        setContentView(page)
    }
    private fun showList() {
        conversation=null; messageList=null; messageScroll=null; compose=null; sendButton=null
        newPage(); header("Conversaciones",false)
        val rows=column()
        val chats=store.chats()
        if(chats.isEmpty()) rows.addView(column().apply {
            setPadding(dp(28),dp(68),dp(28),dp(28))
            addView(label("Tu primera conversación",23f).apply { setTypeface(null,Typeface.BOLD) })
            addView(label("Pulsa + para añadir un número y escribirle.",16f,muted).apply { setPadding(0,dp(12),0,0) })
        })
        for(chat in chats) {
            val name=contactName(chat)
            val row=LinearLayout(this).apply { gravity=Gravity.CENTER_VERTICAL; setPadding(dp(18),dp(15),dp(18),dp(15)); setBackgroundColor(surface); minimumHeight=dp(86); isClickable=true; contentDescription="Abrir conversación con $name" }
            row.addView(label(name.firstOrNull()?.uppercase() ?: "?",22f,green).apply { gravity=Gravity.CENTER; background=shape(palette(R.color.wa_avatar),28) },LinearLayout.LayoutParams(dp(52),dp(52)))
            row.addView(column().apply {
                setPadding(dp(14),0,dp(8),0)
                addView(label(name,18f).apply { setTypeface(null,Typeface.BOLD); setSingleLine(true) })
                addView(label(chat.preview.ifBlank { "Conversación nueva" },14f,muted).apply { setSingleLine(true); ellipsize=android.text.TextUtils.TruncateAt.END; setPadding(0,dp(5),0,0) })
            },LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f))
            row.addView(label(SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(chat.time)),12f,muted))
            row.setOnClickListener { showChat(chat.jid) }; row.setOnLongClickListener { editName(chat.jid); true }; rows.addView(row)
            rows.addView(View(this).apply { setBackgroundColor(palette(R.color.wa_divider)) },LinearLayout.LayoutParams(-1,dp(1)))
        }
        page.addView(ScrollView(this).apply { addView(rows) },LinearLayout.LayoutParams(-1,0,1f))
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
        conversation=store.resolve(jid);newPage();header(title(conversation!!),true)
        messageList=column().apply { setPadding(dp(12),dp(16),dp(12),dp(16)) }
        messageScroll=ScrollView(this).apply { isFillViewport=true;addView(messageList) }
        page.addView(messageScroll,LinearLayout.LayoutParams(-1,0,1f))
        val bar=LinearLayout(this).apply { gravity=Gravity.BOTTOM;setPadding(dp(10),dp(8),dp(10),dp(10));setBackgroundColor(surface) }
        compose=EditText(this).apply { hint="Escribe un mensaje"; minLines=1;maxLines=5;textSize=17f;setTextColor(ink);setHintTextColor(muted);background=shape(inputSurface,24);setPadding(dp(16),dp(12),dp(16),dp(12));isSaveEnabled=false;inputType=InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES }
        bar.addView(compose,LinearLayout.LayoutParams(0,-2,1f))
        sendButton=action("↑","Enviar mensaje") { sendText() }.apply { setTextColor(canvas);background=shape(green,28) }
        bar.addView(sendButton,LinearLayout.LayoutParams(dp(54),dp(54)).apply { leftMargin=dp(8) });page.addView(bar)
        refreshMessages();updateSend()
    }
    private fun refreshMessages() {
        val jid=conversation?:return;val list=messageList?:return;list.removeAllViews()
        for(message in store.messages(jid)) {
            val line=LinearLayout(this).apply { gravity=if(message.outgoing)Gravity.END else Gravity.START;setPadding(0,dp(4),0,dp(4)) }
            val bubble=column().apply { background=shape(if(message.outgoing)outgoingSurface else surface);setPadding(dp(13),dp(10),dp(13),dp(8));minimumWidth=dp(104) }
            bubble.addView(label(message.text,17f).apply { maxWidth=(resources.displayMetrics.widthPixels*0.73).toInt();setTextIsSelectable(true) })
            val state=if(!message.outgoing) "" else when(message.status) { 0->"Pendiente";1->"Enviado · sin confirmar";2->"Aceptado";3->"Entregado";4->"Leído";-2->"Sin confirmación";else->"No enviado" }
            bubble.addView(label(SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(message.time))+if(state.isBlank()) "" else " · $state",11f,muted).apply { gravity=Gravity.END;setPadding(0,dp(6),0,0) })
            if(message.outgoing&&message.status<0&&message.note.isNotBlank())bubble.addView(label(message.note,12f,muted).apply { maxWidth=(resources.displayMetrics.widthPixels*0.73).toInt() })
            line.addView(bubble);list.addView(line)
        }
        messageScroll?.post { messageScroll?.fullScroll(View.FOCUS_DOWN) }
    }
    private fun updateSend() { sendButton?.isEnabled=connected&&pendingMessage==null;sendButton?.alpha=if(connected&&pendingMessage==null)1f else .4f }
    private fun sendText() {
        val jid=conversation?:return;val text=compose?.text?.toString()?.trim().orEmpty();if(text.isBlank())return
        val bytes=text.toByteArray(Charsets.UTF_8)
        if(bytes.size>4000){compose?.error="Máximo 4.000 bytes de texto por mensaje";return}
        val active=connection;if(!connected||active==null){Toast.makeText(this,"Conecta la sesión primero",Toast.LENGTH_SHORT).show();return}
        val id="3EB0"+UUID.randomUUID().toString().replace("-","").uppercase(Locale.ROOT)
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
                if(store.link(lid,pn)) debugLog.add("CONTACT_ALIAS_RECONCILED")
                else debugLog.add("CONTACT_ALIAS_CONFLICT ignored=true")
            }
        }
        for(e in events) {
            if(e.size!=5)continue;val state=e[4].toIntOrNull()?:0
            when(e[0]) {
                "ready" -> { connected=true;statusText="Conectado · texto cifrado";reconnectPolicy.connected(SystemClock.elapsedRealtime()) }
                "status" -> { val matched=store.status(e[1],state,e[3],e[2]);if(matched&&e[1]==pendingMessage&&(state<0||state>=2))pendingMessage=null }
                "message", "sent-copy" -> if(e[1].isNotBlank()&&e[2].isNotBlank()) {
                    debugLog.hide(e[2].substringBefore('@'))
                    store.append(e[1],e[2],e[3],e[0]=="sent-copy",state)
                    // The native inbox keeps the text until this SQLite commit succeeds.
                    connection?.stored(e[1],e[2],e[0]=="sent-copy")
                }
                "contact" -> if(e[2].isNotBlank()) store.learnName(e[2],e[3])
                "notice" -> { statusText=e[3];if(state<0)Toast.makeText(this,e[3],Toast.LENGTH_LONG).show() }
            }
        }
        statusLabel.text=statusText
        refreshConversation()
        scheduleContacts()
        updateSend()
        } catch (error: Exception) { storageFailure(error) }
    }
    private fun storageFailure(error: Exception) {
        debugLog.failure("MESSAGE_STORAGE_FAILED",error)
        sessionGeneration++
        reconnectPolicy.reset()
        automaticReconnect=false
        connection?.cancel();connection=null
        connected=false;pendingMessage=null
        render("No se pudieron guardar los mensajes. Revisa el espacio disponible y vuelve a conectar. Los recibidos pendientes se conservan.","",false)
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
        pair=Button(this).apply { text="Solicitar código de vinculación" }
        check=Button(this).apply { text="Conectar sesión guardada" }
        stop=Button(this).apply { text="Desconectar";isEnabled=false }
        code=label("",32f).apply { setTextIsSelectable(true) };output=label("",14f)
        listOf(number,pair,check,stop,code,output).forEach { settingsBody.addView(it) }
        val save=Button(this).apply { text="Guardar log";setOnClickListener { exportLog() } }
        val view=Button(this).apply { text="Ver log";setOnClickListener { AlertDialog.Builder(this@MainActivity).setTitle("Diagnóstico").setView(ScrollView(this@MainActivity).apply { addView(label(debugLog.snapshot(),12f).apply { setTextIsSelectable(true) }) }).setPositiveButton("Cerrar",null).show() } }
        settingsBody.addView(Button(this).apply { text="Usar nombres de la agenda"; setOnClickListener { requestContactNames() } })
        settingsBody.addView(save);settingsBody.addView(view)
        pair.setOnClickListener { val phone=number.text.toString().trim();if(!phone.matches(Regex("^[1-9][0-9]{7,14}$")))number.error="Usa el prefijo de país y entre 8 y 15 dígitos" else { automaticReconnect=true;reconnectPolicy.reset();start(phone) } }
        check.setOnClickListener { automaticReconnect=true;reconnectPolicy.reset();start("") }
        stop.setOnClickListener {
            automaticReconnect=false
            sessionGeneration++;reconnectPolicy.reset();connection?.cancel();connection=null
            connected=false;pendingMessage=null
            try { store.interrupted();render("Desconectado. Las claves se conservan.","",false);updateSend() }
            catch(error: Exception) { storageFailure(error) }
        }
        showList();render("Sin conexión · abre ⋮ para vincular", "",false)
        networkWasAvailable=networkAvailable()
        val filter=IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION)
        if(Build.VERSION.SDK_INT>=33) registerReceiver(networkReceiver,filter,Context.RECEIVER_NOT_EXPORTED)
        else registerReceiver(networkReceiver,filter)
        networkReceiverRegistered=true
        if(File(filesDir,"identity.v1").exists())start("")
    }
    @Deprecated("Legacy navigation supports Android 5")
    override fun onBackPressed() { if(conversation!=null)showList() else super.onBackPressed() }

    private fun render(status: String, pairingCode: String, busy: Boolean) {
        if (destroyed) return
        output.text = status
        statusText = status
        if (::statusLabel.isInitialized) statusLabel.text = status
        updateSend()
        code.text = if (pairingCode.length == 8) "${pairingCode.take(4)}-${pairingCode.takeLast(4)}" else ""
        pair.isEnabled = !busy
        check.isEnabled = !busy
        stop.isEnabled = busy || (automaticReconnect && File(filesDir,"identity.v1").exists())
        number.isEnabled = !busy
    }

    private fun start(phone: String) {
        sessionGeneration++
        connection?.cancel()
        connected=false
        updateSend()
        debugLog.hide(phone)
        val attempt = ++attempts
        debugLog.add("ATTEMPT $attempt BEGIN mode=${if (phone.isEmpty()) "check-session" else "pair-code"}")
        val next = PairConnection(phone, attempt)
        connection = next
        render("Preparando claves y conexión segura…", "", true)
        next.connect()
    }

    private inner class PairConnection(private val phone: String, private val attempt: Int) : WebSocketListener() {
        private val lock = Any()
        private var handle = 0L
        private var ws: WebSocket? = null
        private var finished = false
        private var lastState: Int? = null
        private var authenticated = false
        private var lastIncoming = SystemClock.elapsedRealtime()
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
            handle = nativeCreate(File(filesDir, "identity.v1").absolutePath, phone)
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
                if (!webSocket.send(data.toByteString())) { finish("No se pudo enviar el paquete cifrado."); break }
            }
        }

        private fun publish() {
            if (finished || handle == 0L) return
            nativeTick(handle,SystemClock.elapsedRealtime())
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
                if(!authenticated) { authenticated=true;deadline?.cancel(false);handler.post { if(connection===this)render(status,"",true) } }
                return
            }
            if (state < 0 || state == 6) { finish(status); return }
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
                        start("")
                    } else {
                        connection = null
                        connected=false
                        pendingMessage=null
                        try { store.interrupted() } catch(error: Exception) { storageFailure(error);return@post }
                        render(status, "", false)
                        val delay=if(retryNetwork)reconnectPolicy.nextDelay(SystemClock.elapsedRealtime()) else null
                        if(delay!=null && automaticReconnect) {
                            val generation=sessionGeneration
                            log("RECONNECT_SCHEDULED delay_ms=$delay")
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
