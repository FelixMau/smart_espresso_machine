package de.fmau.shotcontrol.data

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.withTimeoutOrNull
import java.util.concurrent.ConcurrentHashMap

data class DiscoveredMachine(val name: String, val host: String, val port: Int) {
    val address: String get() = if (port == 80) host else "$host:$port"
}

/**
 * Finds the machine on the LAN via mDNS (the firmware announces `_http._tcp`
 * as "espresso").
 *
 * Android does not resolve `*.local` names through the normal resolver, so the
 * app discovers the service and stores the numeric address it resolved to.
 */
object Discovery {

    private const val SERVICE_TYPE = "_http._tcp."

    /** Scans for [timeoutMs] and returns everything that answered, espresso first. */
    @Suppress("DEPRECATION") // resolveService(..) is the only API available below 34
    suspend fun find(context: Context, timeoutMs: Long = 6000L): List<DiscoveredMachine> {
        val nsd = context.getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return emptyList()
        val found = ConcurrentHashMap<String, DiscoveredMachine>()
        val failed = CompletableDeferred<Unit>()

        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String?) {}
            override fun onDiscoveryStopped(serviceType: String?) {}

            override fun onServiceFound(info: NsdServiceInfo?) {
                val service = info ?: return
                nsd.resolveService(service, resolveListener(nsd, service, found))
            }

            override fun onServiceLost(info: NsdServiceInfo?) {
                info?.serviceName?.let { found.remove(it) }
            }

            override fun onStartDiscoveryFailed(serviceType: String?, errorCode: Int) {
                failed.complete(Unit)
            }

            override fun onStopDiscoveryFailed(serviceType: String?, errorCode: Int) {}
        }

        return try {
            nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
            // Collect for the whole window, unless discovery could not start at all.
            withTimeoutOrNull(timeoutMs) { failed.await() }
            found.values.sortedByDescending { it.name.contains("espresso", ignoreCase = true) }
        } catch (t: Throwable) {
            emptyList()
        } finally {
            runCatching { nsd.stopServiceDiscovery(listener) }
        }
    }

    @Suppress("DEPRECATION")
    private fun resolveListener(
        nsd: NsdManager,
        service: NsdServiceInfo,
        into: MutableMap<String, DiscoveredMachine>,
    ) = object : NsdManager.ResolveListener {

        override fun onResolveFailed(info: NsdServiceInfo?, errorCode: Int) {
            // NsdManager handles one resolve at a time; retry once when it was busy.
            if (errorCode == NsdManager.FAILURE_ALREADY_ACTIVE) {
                runCatching { nsd.resolveService(service, this) }
            }
        }

        override fun onServiceResolved(info: NsdServiceInfo?) {
            val resolved = info ?: return
            val addr = resolved.host?.hostAddress ?: return
            val name = resolved.serviceName ?: addr
            into[name] = DiscoveredMachine(name, addr, resolved.port)
        }
    }
}
