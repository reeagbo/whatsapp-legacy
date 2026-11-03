package org.reeagbo.whatsapplegacy

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.ContactsContract
import android.telephony.PhoneNumberUtils
import java.util.Locale

/** Always-enabled Android-only adapter. No agenda entries are sent to the protocol core. */
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
            val expected="+" + jid.substringBefore('@')
            // PhoneLookup may return candidates that match only a suffix. Verify
            // the complete international number before adopting a display name.
            val matches=mutableMapOf<Long,String>()
            val projection=arrayOf(ContactsContract.PhoneLookup.DISPLAY_NAME,
                ContactsContract.PhoneLookup.NUMBER,ContactsContract.PhoneLookup.NORMALIZED_NUMBER,
                ContactsContract.PhoneLookup._ID)
            context.contentResolver.query(uri, projection, null, null, null)?.use { c ->
                while(c.moveToNext()) {
                    val normalized=c.getString(2)?.takeIf { it.isNotBlank() }
                        ?: PhoneNumberUtils.formatNumberToE164(c.getString(1),Locale.getDefault().country)
                    if(normalized==expected) c.getString(0)?.trim()?.takeIf { it.isNotBlank() }?.let { matches[c.getLong(3)]=it }
                }
            }
            // Multiple different contacts with the same number are ambiguous.
            if(matches.size==1) names[jid]=matches.values.first()

        }
        return names
    }
}
