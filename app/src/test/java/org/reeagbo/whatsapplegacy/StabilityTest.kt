package org.reeagbo.whatsapplegacy

import org.junit.Assert.*
import org.junit.Test

class StabilityTest {
    @Test fun delayedEventsCannotUndoRemoteConfirmation() {
        for (current in 2..4) for (late in listOf(-2,-1,0,1,2))
            assertEquals(current, MessageState.advance(current, late))
        assertEquals(4, MessageState.advance(3,4))
        assertEquals(-1, MessageState.advance(-1,0))
        assertEquals(3, MessageState.advance(-2,3))
    }
    @Test fun handshakeFailuresTransitionFromQuickToSpacedRetries() {
        val policy=ReconnectPolicy()
        assertEquals(2_000L,policy.nextDelay(0))
        assertEquals(4_000L,policy.nextDelay(2_000))
        assertEquals(8_000L,policy.nextDelay(6_000))
        assertEquals(60_000L,policy.nextDelay(14_000))
        assertEquals(120_000L,policy.nextDelay(74_000))
        assertEquals(300_000L,policy.nextDelay(194_000))
    }
    @Test fun flappingConnectionsDoNotResetBackoff() {
        val policy=ReconnectPolicy()
        assertEquals(2_000L,policy.nextDelay(0))
        policy.connected(3_000)
        assertEquals(4_000L,policy.nextDelay(4_000))
        policy.connected(9_000)
        assertEquals(8_000L,policy.nextDelay(10_000))
        policy.connected(20_000)
        assertEquals(60_000L,policy.nextDelay(21_000))
        policy.connected(30_000)
        assertEquals(2_000L,policy.nextDelay(60_000))
        policy.reset()
        assertEquals(2_000L,policy.nextDelay(70_000))
    }
    @Test fun restoredNetworkGetsAFreshRetryBudget() {
        val policy=ReconnectPolicy()
        repeat(3) { assertNotNull(policy.nextDelay(it * 10_000L)) }
        assertEquals(60_000L,policy.nextDelay(40_000L))
        // MainActivity calls reset on a real unavailable -> available transition.
        policy.reset()
        assertEquals(2_000L,policy.nextDelay(50_000L))
    }
    @Test fun longOutagesKeepRetryingWithoutOverflowOrBusyLoop() {
        val policy=ReconnectPolicy()
        var now=0L
        repeat(6) { now+=policy.nextDelay(now) }
        repeat(10_000) {
            val delay=policy.nextDelay(now)
            assertEquals(300_000L,delay)
            now+=delay
        }
        policy.connected(now)
        assertEquals(2_000L,policy.nextDelay(now+30_000L))
    }
}
