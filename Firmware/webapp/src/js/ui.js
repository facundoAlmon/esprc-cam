import { state, elements } from './state.js';
import { loadConfig, saveConfig, saveWifi, scanBrain } from './config.js';
import { startStream, stopStream, syncStatsPolling } from './stream.js';
import { initOta, refreshOtaInfo, restartDevice, factoryReset } from './ota.js';
import { translations } from './translations.js';

function t(key) { return (translations[state.currentLanguage] && translations[state.currentLanguage][key]) || key; }

function attachClick(id, fn) {
    var el = document.getElementById(id);
    if (el) el.addEventListener('click', fn);
}

export function setupUI() {
    // Sidebar toggle (mobile)
    var menuToggle = document.getElementById('menu-toggle');
    var sidebar = document.getElementById('sidebar');
    var mainWrapper = document.querySelector('.main-wrapper');
    if (menuToggle && sidebar) {
        menuToggle.addEventListener('click', function() {
            sidebar.classList.toggle('sidebar-hidden');
        });
        // Close sidebar when clicking outside on mobile
        document.addEventListener('click', function(e) {
            if (window.innerWidth <= 991 &&
                !sidebar.contains(e.target) &&
                e.target !== menuToggle &&
                !menuToggle.contains(e.target)) {
                sidebar.classList.add('sidebar-hidden');
            }
        });
    }

    // Language
    var langSel = document.getElementById('languageSelector');
    if (langSel) {
        langSel.value = state.currentLanguage;
        langSel.addEventListener('change', function(e) {
            state.currentLanguage = e.target.value;
            document.documentElement.lang = e.target.value;
            localStorage.setItem('camLang', e.target.value);
            applyTranslations();
        });
    }

    // Theme toggle
    var themeToggle = document.getElementById('lightModeToggle');
    if (themeToggle) {
        var saved = localStorage.getItem('camTheme');
        if (saved === 'light') {
            document.body.classList.add('light-mode');
            themeToggle.checked = true;
        }
        themeToggle.addEventListener('change', function() {
            document.body.classList.toggle('light-mode', themeToggle.checked);
            localStorage.setItem('camTheme', themeToggle.checked ? 'light' : 'dark');
        });
    }

    // Tab navigation
    document.querySelectorAll('.menu-link[data-tab]').forEach(function(link) {
        link.addEventListener('click', function() {
            openTab(link.dataset.tab);
            // Close sidebar on mobile after tab selection
            if (window.innerWidth <= 991 && sidebar) {
                sidebar.classList.add('sidebar-hidden');
            }
        });
    });

    // Slider value labels
    document.querySelectorAll('input[type="range"]').forEach(function(slider) {
        slider.addEventListener('input', function(e) {
            var lbl = document.getElementById(e.target.id + 'Value');
            if (lbl) lbl.textContent = e.target.value;
        });
    });

    // Stream buttons
    attachClick('startStreamBtn', startStream);
    attachClick('stopStreamBtn',  stopStream);

    // Config
    var saveBtn = document.getElementById('saveConfigBtn');
    if (saveBtn) saveBtn.addEventListener('click', async function() {
        await saveConfig();
        await loadConfig();
        syncStatsPolling();
    });

    // WiFi / Connection
    attachClick('saveWifiBtn',     saveWifi);
    attachClick('scanBrainBtn',    scanBrain);
    attachClick('restartBtn',      restartDevice);
    attachClick('factoryResetBtn', factoryReset);

    // OTA
    initOta();

    // Fullscreen
    var fsBtn     = document.getElementById('camFullscreenBtn');
    var fsOverlay = document.getElementById('camFsOverlay');
    var fsStream  = document.getElementById('camFsStream');
    var fsExit    = document.getElementById('camExitFsBtn');
    var streamImg = document.getElementById('stream-img');
    var camStreamEl = document.querySelector('.cam-stream');

    if (fsBtn && fsOverlay && fsStream && fsExit && streamImg) {
        fsBtn.addEventListener('click', function() {
            fsStream.appendChild(streamImg);
            fsOverlay.classList.add('active');
        });
        fsExit.addEventListener('click', function() {
            if (camStreamEl) camStreamEl.insertBefore(streamImg, camStreamEl.firstChild);
            fsOverlay.classList.remove('active');
        });
    }

    applyTranslations();
    openTab('stream');
}

export function openTab(tabId) {
    state.activeTab = tabId;
    document.querySelectorAll('.tab-content').forEach(function(el) { el.classList.remove('active'); });
    document.querySelectorAll('.menu-link').forEach(function(el) { el.classList.remove('active'); });
    var tab = document.getElementById(tabId);
    if (tab) tab.classList.add('active');
    document.querySelectorAll('.menu-link[data-tab="' + tabId + '"]').forEach(function(el) { el.classList.add('active'); });

    if (tabId === 'config')     loadConfig();
    if (tabId === 'update')     refreshOtaInfo();
    if (tabId === 'stream')     startStream();
    if (tabId === 'connection') loadConfig();
}

function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
        var key = el.dataset.i18n;
        var val = t(key);
        if (val && val !== key) el.textContent = val;
    });
}
