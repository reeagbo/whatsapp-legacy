package org.reeagbo.whatsapplegacy

/** Monotonic timing supplied by the platform; no Android or network dependency.
 * After a short burst, keep trying gently so DNS outages cannot strand a session.
 * The owner must cancel retries on manual disconnect, storage failure or shutdown.
 */
internal class ReconnectPolicy {
    private val delays = longArrayOf(2_000L,4_000L,8_000L,60_000L,120_000L,300_000L)
    private var failures = 0
    private var connectedSince: Long? = null
    fun connected(now: Long) { if (connectedSince == null) connectedSince = now }
    fun reset() { failures = 0; connectedSince = null }
    fun nextDelay(now: Long): Long {
        val since = connectedSince
        if (since != null && now >= since && now - since >= 30_000L) failures = 0
        connectedSince = null
        val delay=delays[failures]
        failures=(failures+1).coerceAtMost(delays.lastIndex)
        return delay
    }
}
