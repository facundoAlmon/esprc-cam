import { getMjpegUrl, fetchAPI } from './api.js';
import { state } from './state.js';

var statsInterval = null;

export function startStream() {
    var img = document.getElementById('stream-img');
    if (!img) return;
    img.src = getMjpegUrl();
    img.style.display = 'block';
    state.streaming = true;
    var badge = document.getElementById('stream-status');
    if (badge) { badge.textContent = 'Live'; badge.classList.add('live'); }
    syncStatsPolling();
}

export function stopStream() {
    var img = document.getElementById('stream-img');
    if (!img) return;
    img.src = '';
    state.streaming = false;
    var badge = document.getElementById('stream-status');
    if (badge) { badge.textContent = 'Stopped'; badge.classList.remove('live'); }
    syncStatsPolling();
}

export function syncStatsPolling() {
    var enabled = state.streaming && !!state.config.statsEnabled;
    var counter = document.getElementById('fps-counter');
    if (counter) counter.style.display = enabled ? '' : 'none';

    if (enabled && !statsInterval) {
        pollStats();
        statsInterval = setInterval(pollStats, 2000);
    } else if (!enabled && statsInterval) {
        clearInterval(statsInterval);
        statsInterval = null;
    }
}

async function pollStats() {
    var d = await fetchAPI('api/stats');
    if (!d || !d.enabled) return;
    var text = d.fps.toFixed(1) + ' fps';
    var el = document.getElementById('fps-counter');
    if (el) el.textContent = text;
    var full = document.getElementById('fpsFull');
    if (full) full.textContent = text;
}
