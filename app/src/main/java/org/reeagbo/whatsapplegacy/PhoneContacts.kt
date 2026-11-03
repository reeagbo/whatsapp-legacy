package org.reeagbo.whatsapplegacy

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.ContactsContract

/** Optional Android-only adapter. No agenda entries are sent to the protocol core. */
internal class PhoneContacts(private val context: Context) {
    fun allowed() = Build.VERSION.SDK_INT < 23 ||
        context.checkSelfPermission(Manifest.permission.READ_CONTACTS) == PackageManager.PERMISSION_GRANTED

    fun names(addresses: Set<String>): Map<String,String> {
        if (!allowed()) return emptyMap()
        val names = mutableMapOf<String,String>()
        for (jid in addresses) {
            if (!jid.matches(Regex("^[1-9][0-9]{7,14}@s\\.whatsapp\\.net$"))) continue
            val uri = Uri.withAppendedPath(ContactsContract.PhoneLookup.CONTENT_FILTER_URI,
                Uri.encode("+" + jid.substringBefore('@')))
            context.contentResolver.query(uri, arrayOf(ContactsContract.PhoneLookup.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0)?.trim()?.takeIf { it.isNotBlank() }?.let { names[jid] = it }
            }
        }
        return names
    }
}
