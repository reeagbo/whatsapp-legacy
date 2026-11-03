package org.reeagbo.whatsapplegacy

import okhttp3.OkHttpClient
import okhttp3.CertificatePinner
import javax.net.ssl.SSLPeerUnverifiedException
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager
import java.security.KeyStore
import org.junit.Assert.*
import org.junit.Test
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket

class NetworkTraceTest {
    @Test fun socketTraceRecordsConnectionWithoutAddress() {
        val events = mutableListOf<String>()
        val loopback = InetAddress.getLoopbackAddress()
        ServerSocket(0, 1, loopback).use { server ->
            TraceSocketFactory { events.add(it) }.createSocket().use { socket ->
                socket.connect(InetSocketAddress(loopback, server.localPort), 2000)
                server.accept().use { assertTrue(it.isConnected) }
            }
        }
        assertTrue(events.any { it.startsWith("TCP_CONNECT_START") })
        assertTrue(events.contains("TCP_CONNECT_OK"))
        assertFalse(events.joinToString().contains(loopback.hostAddress))
    }
    @Test fun tracingKeepsTheOriginalHostnameVerifier() {
        val pinner = CertificatePinner.Builder().add("web.whatsapp.com", "sha256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").build()
        val base = OkHttpClient.Builder().certificatePinner(pinner).build()
        val traced = NetworkTrace.client(base) { }
        assertSame(base.hostnameVerifier, traced.hostnameVerifier)
        try {
            val roots = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply { init(null as KeyStore?) }
            val trustedRoot = roots.trustManagers.filterIsInstance<X509TrustManager>().single().acceptedIssuers.first()
            traced.certificatePinner.check("web.whatsapp.com", listOf(trustedRoot))
            fail("Configured certificate pins must remain enforced")
        } catch (expected: SSLPeerUnverifiedException) { /* retained */ }
    }
}
