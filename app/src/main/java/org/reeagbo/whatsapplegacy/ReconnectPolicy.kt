package org.reeagbo.whatsapplegacy

/** Platform-independent timing policy; the owner supplies a monotonic clock. */
internal class ReconnectPolicy {
    private var failures = 0
    private var connectedSince: Long? = null
    fun connected(now: Long) { if (connectedSince == null) connectedSince = now }
    fun reset() { failures = 0; connectedSince = null }
    fun nextDelay(now: Long): Long? {
        val since = connectedSince
        if (since != null && now >= since && now - since >= 30_000L) failures = 0
        connectedSince = null
        if (failures >= 3) return null
        return 2_000L shl failures++
    }
}
