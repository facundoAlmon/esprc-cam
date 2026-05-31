import { setupUI } from './ui.js';
import { loadConfig } from './config.js';

// Load config before building the UI so state.config.statsEnabled is known
// by the time startStream() runs and decides whether to poll.
document.addEventListener('DOMContentLoaded', async () => {
    await loadConfig();
    setupUI();
});
