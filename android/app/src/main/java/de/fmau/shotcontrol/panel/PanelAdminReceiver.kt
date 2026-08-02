package de.fmau.shotcontrol.panel

import android.app.admin.DeviceAdminReceiver
import android.app.admin.DevicePolicyManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import de.fmau.shotcontrol.data.MachineRepository

/**
 * Device admin, used for exactly one thing: `lockNow()`, so the panel can really
 * turn the display off. Nothing else is requested — see res/xml/device_admin.xml.
 */
class PanelAdminReceiver : DeviceAdminReceiver() {

    override fun onEnabled(context: Context, intent: Intent) {
        MachineRepository.setDeviceAdminActive(true)
    }

    override fun onDisabled(context: Context, intent: Intent) {
        MachineRepository.setDeviceAdminActive(false)
    }

    companion object {
        fun component(context: Context) = ComponentName(context, PanelAdminReceiver::class.java)

        fun policyManager(context: Context): DevicePolicyManager? =
            context.getSystemService(DevicePolicyManager::class.java)

        fun isActive(context: Context): Boolean =
            policyManager(context)?.isAdminActive(component(context)) == true

        /** Intent that asks the user to grant device admin. */
        fun enableIntent(context: Context): Intent =
            Intent(DevicePolicyManager.ACTION_ADD_DEVICE_ADMIN).apply {
                putExtra(DevicePolicyManager.EXTRA_DEVICE_ADMIN, component(context))
                putExtra(
                    DevicePolicyManager.EXTRA_ADD_EXPLANATION,
                    "Lets Shot Control switch the display off when the machine is idle. " +
                        "It is used only to lock the screen — nothing else.",
                )
            }

        /** Turns the screen off. Returns false when admin was not granted. */
        fun lockNow(context: Context): Boolean {
            if (!isActive(context)) return false
            return runCatching { policyManager(context)?.lockNow() }.isSuccess
        }
    }
}
