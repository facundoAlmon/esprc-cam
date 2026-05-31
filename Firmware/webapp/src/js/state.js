export const state = {
    hostname: localStorage.getItem('camHostname') || window.location.hostname,
    config: {},
    currentLanguage: localStorage.getItem('camLang') || 'en',
    activeTab: 'stream',
    streaming: false
};

export const elements = {};
