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
    @Test fun networkFailuresDuringHandshakeUseAllThreeRetries() {
        val policy=ReconnectPolicy()
        assertEquals(2_000L,policy.nextDelay(0))
        assertEquals(4_000L,policy.nextDelay(2_000))
        assertEquals(8_000L,policy.nextDelay(6_000))
        assertNull(policy.nextDelay(14_000))
    }
    @Test fun flappingConnectionsDoNotResetBackoff() {
        val policy=ReconnectPolicy()
        assertEquals(2_000L,policy.nextDelay(0))
        policy.connected(3_000)
        assertEquals(4_000L,policy.nextDelay(4_000))
        policy.connected(9_000)
        assertEquals(8_000L,policy.nextDelay(10_000))
        policy.connected(20_000)
        assertNull(policy.nextDelay(21_000))
        policy.connected(30_000)
        assertEquals(2_000L,policy.nextDelay(60_000))
        policy.reset()
        assertEquals(2_000L,policy.nextDelay(70_000))
    }
    @Test fun restoredNetworkGetsAFreshRetryBudget() {
        val policy=ReconnectPolicy()
        repeat(3) { assertNotNull(policy.nextDelay(it * 10_000L)) }
        assertNull(policy.nextDelay(40_000L))
        // MainActivity calls reset on a real unavailable -> available transition.
        policy.reset()
        assertEquals(2_000L,policy.nextDelay(50_000L))
    }
}
