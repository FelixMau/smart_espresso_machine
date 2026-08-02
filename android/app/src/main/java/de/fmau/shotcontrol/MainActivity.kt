package de.fmau.shotcontrol

import android.Manifest
import android.app.KeyguardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.MotionEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.LocalCafe
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import de.fmau.shotcontrol.ui.HistoryScreen
import de.fmau.shotcontrol.ui.LiveScreen
import de.fmau.shotcontrol.ui.SettingsScreen
import de.fmau.shotcontrol.ui.ShotDetailScreen
import de.fmau.shotcontrol.data.MachineRepository
import de.fmau.shotcontrol.panel.PanelAdminReceiver
import de.fmau.shotcontrol.panel.PanelService
import de.fmau.shotcontrol.ui.ConnectionChip
import de.fmau.shotcontrol.ui.IdleOverlay
import de.fmau.shotcontrol.ui.theme.ShotControlTheme
import kotlinx.coroutines.launch
import java.io.File

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // Panel mode: the service relaunches this Activity to wake the display,
        // so it has to be able to come up over the lock screen and switch it on.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        }
        handleWakeIntent(intent)

        setContent {
            ShotControlTheme {
                ShotControlApp()
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleWakeIntent(intent)
    }

    private fun handleWakeIntent(intent: Intent?) {
        if (intent?.getBooleanExtra(EXTRA_WAKE, false) == true) {
            MachineRepository.wake(WakeReason.SCALE_CONNECTED)
            // The screen was really off, so get the keyguard out of the way.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                getSystemService(KeyguardManager::class.java)
                    ?.requestDismissKeyguard(this, null)
            }
        }
    }

    /** Every touch anywhere restarts the idle countdown. */
    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        MachineRepository.noteInteraction()
        return super.dispatchTouchEvent(event)
    }

    companion object {
        const val EXTRA_WAKE = "wake"
    }
}

private enum class Tab(val route: String, val label: String, val icon: ImageVector) {
    Live("live", "Live", Icons.Default.LocalCafe),
    Journal("journal", "Journal", Icons.Default.History),
    Settings("settings", "Settings", Icons.Default.Settings),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ShotControlApp(vm: MachineViewModel = viewModel()) {
    val state by vm.state.collectAsStateWithLifecycle()
    val shots by vm.shots.collectAsStateWithLifecycle()
    val navController = rememberNavController()
    val backStack by navController.currentBackStackEntryAsState()
    val route = backStack?.destination?.route
    val snackbar = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    val activity = context as? ComponentActivity

    // Without panel mode, polling follows the UI. With it, the service keeps the
    // connection up and this only reports whether anyone is looking.
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START -> vm.setUiVisible(true)
                Lifecycle.Event.ON_RESUME ->
                    vm.setDeviceAdminActive(PanelAdminReceiver.isActive(context))
                Lifecycle.Event.ON_STOP -> vm.setUiVisible(false)
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    // The service's notification is what makes it a legal foreground service.
    val notificationPermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { /* denied only costs the notification, the service still runs */ }

    // The panel service only needs to exist while panel mode is on.
    LaunchedEffect(state.kioskEnabled) {
        if (state.kioskEnabled) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED
            ) {
                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
            PanelService.start(context)
        } else {
            PanelService.stop(context)
        }
    }

    val dark = state.screen == ScreenState.IDLE
    val overlayDark = dark && state.idleStyle == IdleStyle.OVERLAY

    // Panel mode holds the display on itself; the dark state is drawn, not slept.
    DisposableEffect(state.kioskEnabled) {
        val window = activity?.window
        if (state.kioskEnabled) {
            window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        onDispose { window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
    }

    // Backlight to zero while dark — this is what makes it dark on an LCD.
    LaunchedEffect(overlayDark) {
        val window = activity?.window ?: return@LaunchedEffect
        window.attributes = window.attributes.apply {
            screenBrightness = if (overlayDark) 0f
            else WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
        }
    }

    // Real screen-off variant: hand the display to the system and let the
    // service wake us again. Falls back to the overlay if admin was revoked.
    LaunchedEffect(dark, state.idleStyle) {
        if (dark && state.idleStyle == IdleStyle.SCREEN_OFF) {
            if (!PanelAdminReceiver.lockNow(context)) {
                vm.setIdleStyle(IdleStyle.OVERLAY)
                MachineRepository.message("Device admin not granted — using the dark overlay")
            }
        }
    }

    LaunchedEffect(state.message) {
        state.message?.let {
            snackbar.showSnackbar(it)
            vm.consumeMessage()
        }
    }

    fun share(file: File, mime: String) {
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = mime
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        context.startActivity(Intent.createChooser(intent, "Share shot data"))
    }

    val isDetail = route?.startsWith("shot/") == true

    Box(Modifier.fillMaxSize()) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (isDetail) "Shot" else "Shot Control") },
                navigationIcon = {
                    if (isDetail) {
                        IconButton(onClick = { navController.popBackStack() }) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                        }
                    }
                },
                actions = {
                    ConnectionChip(
                        connection = state.connection,
                        scaleConnected = state.status.scaleConnected,
                        modifier = Modifier.padding(end = 16.dp),
                    )
                },
            )
        },
        bottomBar = {
            if (!isDetail) {
                NavigationBar {
                    Tab.entries.forEach { tab ->
                        val selected = backStack?.destination?.hierarchy?.any { it.route == tab.route } == true
                        NavigationBarItem(
                            selected = selected,
                            onClick = {
                                navController.navigate(tab.route) {
                                    popUpTo(navController.graph.findStartDestination().id) {
                                        saveState = true
                                    }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            },
                            icon = { Icon(tab.icon, contentDescription = null) },
                            label = { Text(tab.label) },
                        )
                    }
                }
            }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = Tab.Live.route,
            modifier = Modifier.padding(padding),
        ) {
            composable(Tab.Live.route) {
                LiveScreen(
                    state = state,
                    onStart = vm::start,
                    onStop = vm::stop,
                    onGoalWeight = vm::setGoalWeight,
                    onWeightOffset = vm::setWeightOffset,
                    onSaveShot = { vm.saveCurrentShot() },
                )
            }
            composable(Tab.Journal.route) {
                HistoryScreen(
                    shots = shots,
                    onOpen = { navController.navigate("shot/${it.id}") },
                )
            }
            composable(Tab.Settings.route) {
                SettingsScreen(
                    state = state,
                    onHost = vm::setHost,
                    onDiscover = vm::discover,
                    onAutoSave = vm::setAutoSave,
                    onLiveInterval = vm::setLiveInterval,
                    onProfile = vm::applyProfile,
                    onExportJournal = {
                        scope.launch { share(vm.store.exportAllJson(), "application/json") }
                    },
                    onKiosk = vm::setKioskEnabled,
                    onIdleStyle = vm::setIdleStyle,
                    onIdleTimeout = vm::setIdleTimeoutMinutes,
                    onRequestDeviceAdmin = {
                        context.startActivity(PanelAdminReceiver.enableIntent(context))
                    },
                    onGoDarkNow = vm::goDark,
                )
            }
            composable("shot/{id}") { entry ->
                val id = entry.arguments?.getString("id")
                val shot = shots.firstOrNull { it.id == id }
                if (shot == null) {
                    Text("Shot not found", Modifier.padding(16.dp))
                } else {
                    ShotDetailScreen(
                        shot = shot,
                        onUpdate = { vm.updateShot(it) },
                        onDelete = { vm.deleteShot(shot.id); navController.popBackStack() },
                        onShare = { scope.launch { share(vm.store.exportCsv(shot), "text/csv") } },
                    )
                }
            }
        }
    }

        // Drawn over everything, so the panel goes dark whatever screen is open.
        if (overlayDark) {
            IdleOverlay(onWake = { vm.wake(WakeReason.TOUCH) })
        }
    }
}

