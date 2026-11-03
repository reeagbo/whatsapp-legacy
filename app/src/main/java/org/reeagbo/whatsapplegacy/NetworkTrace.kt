package org.reeagbo.whatsapplegacy

import okhttp3.Dns
import okhttp3.OkHttpClient
import java.net.Inet4Address
import java.net.Inet6Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketAddress
import java.security.KeyStore
import javax.net.SocketFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

/** WebSocket connections suppress OkHttp EventListener callbacks. Trace the actual
 * DNS/socket factories instead; leave certificate and hostname validation enabled. */
object NetworkTrace {
    fun client(base: OkHttpClient, log: (String) -> Unit): OkHttpClient {
        val trustFactory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        trustFactory.init(null as KeyStore?)
        val trust = trustFactory.trustManagers.filterIsInstance<X509TrustManager>().single()
        val ssl = SSLContext.getInstance("TLS").apply { init(null, arrayOf(trust), null) }
        return base.newBuilder()
            .dns(object : Dns {
                override fun lookup(hostname: String): List<InetAddress> {
                    log("DNS_START")
                    return try {
                        Dns.SYSTEM.lookup(hostname).also { addresses ->
                            log("DNS_OK ipv4=${addresses.count { it is Inet4Address }} ipv6=${addresses.count { it is Inet6Address }}")
                        }
                    } catch (error: Exception) {
                        log("DNS_FAILED exception=${error.javaClass.simpleName}")
                        throw error
                    }
                }
            })
            .socketFactory(TraceSocketFactory(log))
            .sslSocketFactory(TraceTlsFactory(ssl.socketFactory, log), trust)
            .addInterceptor { chain ->
                log("HTTP_UPGRADE_BEGIN")
                chain.proceed(chain.request()).also { log("HTTP_RESPONSE code=${it.code}") }
            }
            .build()
    }
}

internal class TraceSocketFactory(private val log: (String) -> Unit) : SocketFactory() {
    override fun createSocket(): Socket = object : Socket() {
        override fun connect(endpoint: SocketAddress, timeout: Int) {
            val address = endpoint as? InetSocketAddress
            log("TCP_CONNECT_START family=${if (address?.address is Inet6Address) "IPv6" else "IPv4"} port=${address?.port} timeout_ms=$timeout")
            try { super.connect(endpoint, timeout); log("TCP_CONNECT_OK") }
            catch (error: Exception) { log("TCP_CONNECT_FAILED exception=${error.javaClass.simpleName}"); throw error }
        }
    }
    override fun createSocket(host: String, port: Int): Socket = createSocket().apply { connect(InetSocketAddress(host, port)) }
    override fun createSocket(host: InetAddress, port: Int): Socket = createSocket().apply { connect(InetSocketAddress(host, port)) }
    override fun createSocket(host: String, port: Int, local: InetAddress, localPort: Int): Socket = createSocket().apply { bind(InetSocketAddress(local, localPort)); connect(InetSocketAddress(host, port)) }
    override fun createSocket(host: InetAddress, port: Int, local: InetAddress, localPort: Int): Socket = createSocket().apply { bind(InetSocketAddress(local, localPort)); connect(InetSocketAddress(host, port)) }
}

private class TraceTlsFactory(private val delegate: SSLSocketFactory, private val log: (String) -> Unit) : SSLSocketFactory() {
    override fun getDefaultCipherSuites(): Array<String> = delegate.defaultCipherSuites
    override fun getSupportedCipherSuites(): Array<String> = delegate.supportedCipherSuites
    override fun createSocket(socket: Socket, host: String, port: Int, autoClose: Boolean): Socket {
        log("TLS_SOCKET_CREATE")
        return delegate.createSocket(socket, host, port, autoClose)
    }
    override fun createSocket(): Socket = delegate.createSocket()
    override fun createSocket(host: String, port: Int): Socket = delegate.createSocket(host, port)
    override fun createSocket(host: InetAddress, port: Int): Socket = delegate.createSocket(host, port)
    override fun createSocket(host: String, port: Int, local: InetAddress, localPort: Int): Socket = delegate.createSocket(host, port, local, localPort)
    override fun createSocket(host: InetAddress, port: Int, local: InetAddress, localPort: Int): Socket = delegate.createSocket(host, port, local, localPort)
}
