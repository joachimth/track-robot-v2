// Tracked Robot Web Flasher
// Uses Web Serial API to flash ESP32 firmware

let port = null;
let reader = null;
let writer = null;

const connectBtn = document.getElementById('connectBtn');
const flashBtn = document.getElementById('flashBtn');
const disconnectBtn = document.getElementById('disconnectBtn');
const statusText = document.getElementById('statusText');
const statusDiv = document.getElementById('status');
const logDiv = document.getElementById('log');
const progressContainer = document.getElementById('progress-container');
const progressFill = document.getElementById('progress');
const progressText = document.getElementById('progressText');

// Check Web Serial API support
if (!('serial' in navigator)) {
    log('Web Serial API not supported. Please use Chrome, Edge, or Opera.', 'error');
    connectBtn.disabled = true;
} else {
    log('Web Serial API detected', 'success');
}

// Event listeners
connectBtn.addEventListener('click', connect);
flashBtn.addEventListener('click', flashFirmware);
disconnectBtn.addEventListener('click', disconnect);

// Logging function
function log(message, type = 'info') {
    const entry = document.createElement('div');
    entry.className = 'log-entry ' + type;
    const time = new Date().toLocaleTimeString();
    entry.textContent = '[' + time + '] ' + message;
    logDiv.appendChild(entry);
    logDiv.scrollTop = logDiv.scrollHeight;
    console.log(message);
}

// Update status
function updateStatus(text, icon = 'ª', connected = false) {
    statusText.textContent = text;
    const statusIcon = statusDiv.querySelector('.status-icon');
    statusIcon.textContent = icon;
    if (connected) {
        statusDiv.classList.add('connected');
    } else {
        statusDiv.classList.remove('connected');
    }
}

// Update progress
function updateProgress(percent, text = null) {
    progressFill.style.width = percent + '%';
    progressText.textContent = text || (percent + '%');
}

// Connect to ESP32
async function connect() {
    try {
        log('Requesting serial port...');
        port = await navigator.serial.requestPort();

        await port.open({ baudRate: 115200 });

        reader = port.readable.getReader();
        writer = port.writable.getWriter();

        updateStatus('Connected to ESP32', '=â', true);
        log('Connected successfully!', 'success');

        connectBtn.disabled = true;
        flashBtn.disabled = false;
        disconnectBtn.disabled = false;

    } catch (error) {
        log('Connection failed: ' + error.message, 'error');
        updateStatus('Connection failed', '=4');
    }
}

// Disconnect from ESP32
async function disconnect() {
    try {
        if (reader) {
            await reader.cancel();
            reader.releaseLock();
            reader = null;
        }

        if (writer) {
            writer.releaseLock();
            writer = null;
        }

        if (port) {
            await port.close();
            port = null;
        }

        updateStatus('Disconnected', 'ª');
        log('Disconnected', 'info');

        connectBtn.disabled = false;
        flashBtn.disabled = true;
        disconnectBtn.disabled = true;

    } catch (error) {
        log('Disconnect error: ' + error.message, 'error');
    }
}

// Reset ESP32 into bootloader mode
async function resetIntoBootloader() {
    log('Resetting ESP32 into bootloader mode...');

    // Set RTS and DTR to enter bootloader
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise(resolve => setTimeout(resolve, 100));
    await port.setSignals({ dataTerminalReady: true, requestToSend: false });
    await new Promise(resolve => setTimeout(resolve, 50));
    await port.setSignals({ dataTerminalReady: false });

    log('Bootloader mode activated', 'info');
}

// Flash firmware
async function flashFirmware() {
    try {
        flashBtn.disabled = true;
        progressContainer.style.display = 'block';
        updateProgress(0, 'Starting...');

        log('Starting firmware flash...', 'info');
        updateStatus('Flashing...', '=', true);

        // Step 1: Reset into bootloader
        await resetIntoBootloader();
        updateProgress(10, 'Bootloader ready');

        // Step 2: Download manifest
        log('Downloading firmware manifest...');
        const manifest = await fetch('manifest.json').then(r => r.json());
        log('Manifest loaded: v' + manifest.version, 'success');
        updateProgress(20, 'Manifest loaded');

        // Step 3: Download binaries
        log('Downloading firmware binaries...');
        const binaries = [];
        for (let i = 0; i < manifest.builds[0].parts.length; i++) {
            const part = manifest.builds[0].parts[i];
            log('Downloading: ' + part.path);
            const data = await fetch(part.path).then(r => r.arrayBuffer());
            binaries.push({ offset: part.offset, data: new Uint8Array(data) });
            updateProgress(20 + ((i + 1) / manifest.builds[0].parts.length) * 30,
                          'Downloaded ' + (i + 1) + '/' + manifest.builds[0].parts.length);
        }

        log('All binaries downloaded', 'success');
        updateProgress(50, 'Ready to flash');

        // NOTE: Steps 4-6 would require full ESP32 flash protocol implementation
        // This is a simplified version - production code would use esptool-js library
        log('  Full flashing requires esptool-js integration', 'error');
        log('Please use esptool manually for now', 'info');
        log('See documentation for flash commands', 'info');

        updateProgress(100, 'Manual flash required');
        updateStatus('See instructions below', ' ');

        log('Flash offsets:', 'info');
        log('  0x1000  - bootloader.bin', 'info');
        log('  0x8000  - partition-table.bin', 'info');
        log('  0x10000 - track-robot.bin', 'info');

    } catch (error) {
        log('Flash failed: ' + error.message, 'error');
        updateStatus('Flash failed', '=4');
    } finally {
        flashBtn.disabled = false;
    }
}

// Initialize
log('Web flasher loaded', 'info');
log('Click "Connect" to begin', 'info');
