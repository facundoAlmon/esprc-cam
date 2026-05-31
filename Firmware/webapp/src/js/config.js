import { fetchAPI } from './api.js';
import { state } from './state.js';

export async function loadConfig() {
    const json = await fetchAPI('api/config');
    if (!json) return;
    state.config = json;
    applyToForm(json);
}

export async function saveConfig() {
    const body = serializeForm();
    await fetchAPI('api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
}

export async function saveWifi() {
    const ssid = document.getElementById('wifiSsid').value;
    const pass  = document.getElementById('wifiPass').value;
    const mode  = document.querySelector('input[name="wifiMode"]:checked')
                  ? document.querySelector('input[name="wifiMode"]:checked').value
                  : 'AP';
    const hostname = document.getElementById('hostname').value;
    await fetchAPI('api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ wifiSsid: ssid, wifiPass: pass, wifiMode: mode })
    });
    if (hostname) {
        await fetchAPI('api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ hostname })
        });
    }
}

export async function scanBrain() {
    const btn = document.getElementById('scanBrainBtn');
    if (btn) { btn.disabled = true; btn.textContent = '…'; }
    const json = await fetchAPI('api/scan-brain');
    if (btn) { btn.disabled = false; btn.textContent = btn.dataset.label || 'Sync from Brain'; }
    if (!json || !json.found) { alert('Brain not found on network.'); return; }
    if (json.wifiSsid) document.getElementById('wifiSsid').value = json.wifiSsid;
    if (json.wifiPass)  document.getElementById('wifiPass').value  = json.wifiPass;
}

function applyToForm(data) {
    const map = {
        framesize:     'framesize',
        camQuality:    'camQuality',
        fpsLimit:      'fpsLimit',
        camBright:     'camBright',
        camContrast:   'camContrast',
        camSaturation: 'camSaturation',
        camAELevel:    'camAELevel',
        camExpo:       'camExpo',
        camAGCGain:    'camAGCGain',
        camGainCeiling:'camGainCeiling',
        camEffect:     'camEffect',
        camWBMode:     'camWBMode',
        wifiSsid:      'wifiSsid',
        wifiPass:      'wifiPass',
        hostname:      'hostname',
        ip:            'espIP'
    };
    Object.entries(map).forEach(function(entry) {
        var k = entry[0], id = entry[1];
        var el = document.getElementById(id);
        if (el && data[k] !== undefined) {
            el.value = data[k];
            var valEl = document.getElementById(id + 'Value');
            if (valEl) valEl.textContent = data[k];
        }
    });
    if (data.wifiMode) {
        var radio = document.querySelector('input[name="wifiMode"][value="' + data.wifiMode + '"]');
        if (radio) radio.checked = true;
    }
    var bools = ['camAwb','camAwbGain','camAECSensor','camAECDSP','camAGC',
                 'camBPC','camWPC','camRAWGCMA','camLensCorr','camHmirror','camVFlip','camDCW','camColorBar',
                 'statsEnabled'];
    bools.forEach(function(k) {
        var el = document.getElementById(k);
        if (el && data[k] !== undefined) {
            el.checked = !!data[k];
        }
    });
}

function serializeForm() {
    var body = {};
    ['framesize','camQuality','fpsLimit','camBright','camContrast','camSaturation',
     'camAELevel','camExpo','camAGCGain','camGainCeiling','camEffect','camWBMode'].forEach(function(id) {
        var el = document.getElementById(id);
        if (el) body[id] = parseInt(el.value, 10);
    });
    ['camAwb','camAwbGain','camAECSensor','camAECDSP','camAGC',
     'camBPC','camWPC','camRAWGCMA','camLensCorr','camHmirror','camVFlip','camDCW','camColorBar',
     'statsEnabled'].forEach(function(id) {
        var el = document.getElementById(id);
        if (el) body[id] = el.checked ? 1 : 0;
    });
    return body;
}
