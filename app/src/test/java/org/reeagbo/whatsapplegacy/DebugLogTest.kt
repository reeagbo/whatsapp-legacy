package org.reeagbo.whatsapplegacy

import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class DebugLogTest {
    @Test fun sensitiveValuesAreExcludedAndMilestonesSurviveReopen() {
        val dir = Files.createTempDirectory("wa-log-test").toFile()
        try {
            val file = dir.resolve("latest.txt")
            val log = DebugLog(file)
            log.hide("34123456789")
            log.hide("ABCD1234")
            log.hide("ABCD-1234")
            val opaque = "qwertyuiopasdfghjklzxcvbnm1234567890abcd=="
            log.add("TLS_OK; accidental phone=34123456789 code=ABCD1234 pretty=ABCD-1234 jid=34123456789:1@s.whatsapp.net opaque=$opaque")
            log.failure("NETWORK", IllegalStateException("secret code ABCD1234 and phone 34123456789"))
            val saved = file.readText()
            listOf("34123456789", "ABCD1234", "ABCD-1234", "s.whatsapp.net", opaque).forEach { assertFalse(it, saved.contains(it)) }
            assertTrue(saved.contains("TLS_OK"))
            assertTrue(saved.contains("IllegalStateException"))
            assertTrue(DebugLog(file).snapshot().contains("TLS_OK"))
        } finally { dir.deleteRecursively() }
    }

    @Test fun largeLogsRemainBoundedAndKeepTheirNewestMilestone() {
        val dir = Files.createTempDirectory("wa-log-limit").toFile()
        try {
            val file = dir.resolve("latest.txt")
            val log = DebugLog(file)
            repeat(80) { log.add("EVENT $it " + "data_".repeat(800)) }
            log.add("FINAL_STAGE")
            assertTrue(log.snapshot().contains("FINAL_STAGE"))
            assertTrue(log.snapshot().contains("older log entries removed"))
            assertTrue(file.length() <= 256 * 1024)
        } finally { dir.deleteRecursively() }
    }

    @Test fun diskFailureStillAllowsCopyingTheInMemoryTrace() {
        val dir = Files.createTempDirectory("wa-log-failure").toFile()
        try {
            val log = DebugLog(dir) // A directory cannot be opened as an output file.
            log.add("DNS_START")
            assertTrue(log.snapshot().contains("disk write failed"))
            assertTrue(log.snapshot().contains("DNS_START"))
        } finally { dir.deleteRecursively() }
    }
    @Test fun addressDiagnosticsKeepSuffixAndDomainWithoutFullIdentifiers() {
        assertEquals("lid:187136",DebugLog.addressHint("12345678187136:2@lid"))
        assertEquals("pn:567890",DebugLog.addressHint("34634567890@s.whatsapp.net"))
        assertEquals("unknown",DebugLog.addressHint("secret@example.com"))
        assertEquals("lid:short",DebugLog.addressHint("12345@lid"))
        assertEquals("unknown",DebugLog.addressHint("34634567890@s.whatsapp.net extra"))
        val dir=Files.createTempDirectory("wa-address-log").toFile()
        try {
            val log=DebugLog(dir.resolve("latest.txt"))
            log.hide("12345678187136")
            log.add("CHAT_IDENTITY address=${DebugLog.addressHint("12345678187136@lid")} name_source=identifier")
            assertTrue(log.snapshot().contains("lid:187136"))
            assertFalse(log.snapshot().contains("12345678187136"))
        } finally { dir.deleteRecursively() }
    }
}
