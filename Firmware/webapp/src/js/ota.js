import { fetchAPI } from './api.js';
import { state } from './state.js';
import { translations } from './translations.js';

function t(key, fallback) {
    return (translations[state.currentLanguage] && translations[state.currentLanguage][key]) || fallback || key;
}

function fmtBytes(n) {
    if (n == null || isNaN(n)) return '-';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / (1024 * 1024)).toFixed(2) + ' MB';
}

export async function refreshOtaInfo() {
    var info = await fetchAPI('api/ota/info');
    var infoEl = document.getElementById('ota-info');
    if (!infoEl) return;
    if (!info) {
        infoEl.textContent = t('otaInfoUnavailable', 'OTA info unavailable.');
        return;
    }
    var rows = [];
    if (info.running) {
        var rAddr = info.running.address != null ? ' @ 0x' + info.running.address.toString(16) : '';
        var rSize = info.running.size != null ? ' (' + fmtBytes(info.running.size) + ')' : '';
        rows.push('<dt>' + t('otaRunningPartition', 'Running partition') + '</dt>' +
                  '<dd>' + info.running.label + rAddr + rSize + '</dd>');
    }
    if (info.next) {
        var nSize = info.next.size != null ? ' (' + fmtBytes(info.next.size) + ')' : '';
        rows.push('<dt>' + t('otaNextPartition', 'Target slot') + '</dt>' +
                  '<dd>' + info.next.label + nSize + '</dd>');
    }
    if (info.app) {
        if (info.app.version) {
            rows.push('<dt>' + t('otaAppVersion', 'App version') + '</dt><dd>' + info.app.version + '</dd>');
        }
        if (info.app.compile_date) {
            rows.push('<dt>' + t('otaBuildDate', 'Built') + '</dt>' +
                      '<dd>' + info.app.compile_date + ' ' + (info.app.compile_time || '') + '</dd>');
        }
        if (info.app.idf_version) {
            rows.push('<dt>IDF</dt><dd>' + info.app.idf_version + '</dd>');
        }
    }
    infoEl.innerHTML = '<dl class="ota-info-grid">' + rows.join('') + '</dl>';
}

function setOtaStatus(text, isError) {
    var el = document.getElementById('ota-status');
    if (!el) return;
    el.textContent = text || '';
    el.classList.toggle('error', !!isError);
}

function setOtaProgress(percent, label) {
    var bar = document.getElementById('ota-progress-bar');
    var wrap = document.getElementById('ota-progress');
    var text = document.getElementById('ota-progress-text');
    if (wrap) wrap.style.display = percent == null ? 'none' : 'block';
    if (bar) bar.style.width = Math.max(0, Math.min(100, percent || 0)) + '%';
    if (text) text.textContent = label || (percent != null ? percent.toFixed(1) + '%' : '');
}

export async function uploadFirmware() {
    var input = document.getElementById('ota-file-input');
    var uploadBtn = document.getElementById('ota-upload-btn');
    if (!input || !input.files || !input.files[0]) {
        alert(t('otaSelectFile', 'Please select a firmware .bin file first.'));
        return;
    }
    var file = input.files[0];

    if (!file.name.toLowerCase().endsWith('.bin')) {
        if (!confirm(t('otaNotBinConfirm', 'File does not end with .bin. Continue anyway?'))) return;
    }

    if (!confirm(t('otaConfirmStart', 'Upload firmware and reboot the device?'))) return;

    if (uploadBtn) uploadBtn.disabled = true;
    setOtaStatus(t('otaUploading', 'Uploading firmware...'));
    setOtaProgress(0, '0%');

    var url = window.location.protocol + '//' + state.hostname + '/api/ota';
    var xhr = new XMLHttpRequest();

    xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
            var pct = (e.loaded / e.total) * 100;
            setOtaProgress(pct, pct.toFixed(1) + '% (' + fmtBytes(e.loaded) + ' / ' + fmtBytes(e.total) + ')');
        }
    });

    xhr.onreadystatechange = function() {
        if (xhr.readyState !== 4) return;
        if (uploadBtn) uploadBtn.disabled = false;
        if (xhr.status >= 200 && xhr.status < 300) {
            setOtaProgress(100, '100%');
            setOtaStatus(t('otaSuccess', 'Firmware uploaded. The device is restarting...'));
            setTimeout(function() {
                setOtaStatus(t('otaPostReboot', 'Reconnect to the ESP and reload this page after a few seconds.'));
                refreshOtaInfo();
            }, 8000);
        } else {
            var detail = xhr.responseText || ('HTTP ' + xhr.status);
            setOtaStatus(t('otaFailed', 'OTA failed:') + ' ' + detail, true);
            setOtaProgress(null);
        }
    };
    xhr.onerror = function() {
        if (uploadBtn) uploadBtn.disabled = false;
        setOtaStatus(t('otaNetworkError', 'Network error during OTA upload.'), true);
        setOtaProgress(null);
    };

    xhr.open('POST', url, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.send(file);
}

export function initOta() {
    var refreshBtn = document.getElementById('ota-refresh-btn');
    if (refreshBtn) refreshBtn.addEventListener('click', refreshOtaInfo);

    var uploadBtn = document.getElementById('ota-upload-btn');
    if (uploadBtn) uploadBtn.addEventListener('click', uploadFirmware);

    var fileInput = document.getElementById('ota-file-input');
    if (fileInput) {
        fileInput.addEventListener('change', function() {
            var f = fileInput.files && fileInput.files[0];
            var label = document.getElementById('ota-file-label');
            if (label) {
                label.textContent = f
                    ? f.name + ' (' + fmtBytes(f.size) + ')'
                    : t('otaNoFile', 'No file selected');
            }
        });
    }
}

export async function restartDevice() {
    await fetchAPI('manage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ restartESP: 1 })
    });
}

export async function factoryReset() {
    var msg = document.documentElement.lang === 'es'
        ? '¿Esto borrará todas las configuraciones. ¿Continuar?'
        : 'This will erase all settings. Continue?';
    if (!confirm(msg)) return;
    await fetchAPI('manage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ clearPreferences: 1, restartESP: 1 })
    });
}
