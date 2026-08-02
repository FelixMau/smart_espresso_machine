package de.fmau.shotcontrol.panel

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import de.fmau.shotcontrol.MainActivity
import de.fmau.shotcontrol.R
import de.fmau.shotcontrol.ScreenState
import de.fmau.shotcontrol.WakeReason
import de.fmau.shotcontrol.data.MachineRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Keeps the machine polled while the panel screen is dark.
 *
 * Without this the Activity is stopped as soon as the screen goes off, and
 * nothing is left to notice the scale waking up. It also brings the Activity
 * back to the front (turning the screen on) when a wake event arrives.
 */
class PanelService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var wakeJob: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        MachineRepository.initialize(this)
        createChannel()
        startForeground(NOTIFICATION_ID, buildNotification())
        MachineRepository.setServiceRunning(true)

        wakeJob = scope.launch {
            MachineRepository.wakeEvents.collectLatest { reason ->
                // A touch already happened on a screen that was on; only the
                // machine-side triggers need to pull the panel back up.
                if (reason != WakeReason.TOUCH) bringPanelToFront()
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY

    override fun onDestroy() {
        MachineRepository.setServiceRunning(false)
        wakeJob?.cancel()
        scope.cancel()
        super.onDestroy()
    }

    /** Launches the panel with the flags that turn the display on. */
    private fun bringPanelToFront() {
        val intent = Intent(this, MainActivity::class.java).apply {
            addFlags(
                Intent.FLAG_ACTIVITY_NEW_TASK or
                    Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP
            )
            putExtra(MainActivity.EXTRA_WAKE, true)
        }
        runCatching { startActivity(intent) }
    }

    private fun createChannel() {
        val manager = getSystemService(NotificationManager::class.java) ?: return
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Espresso panel",
            NotificationManager.IMPORTANCE_MIN,
        ).apply {
            description = "Keeps the machine connection alive while the screen is dark"
            setShowBadge(false)
        }
        manager.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Espresso panel active")
            .setContentText("Watching for the scale")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(open)
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "panel"
        private const val NOTIFICATION_ID = 1

        fun start(context: Context) {
            val intent = Intent(context, PanelService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, PanelService::class.java))
        }

        /** True while the panel should be dark. */
        fun isDark(): Boolean = MachineRepository.state.value.screen == ScreenState.IDLE
    }
}
