import { state, elements } from './state.js';

export async function fetchAPI(endpoint, options = {}) {
    const base = `${window.location.protocol}//${state.hostname}`;
    const url = `${base}/${endpoint}`;
    try {
        const response = await fetch(url, options);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const ct = response.headers.get('Content-Type') || '';
        if (ct.includes('application/json')) return await response.json();
        return null;
    } catch (err) {
        console.error(`API ${endpoint} failed:`, err);
        return null;
    }
}

export function getMjpegUrl() {
    return `${window.location.protocol}//${state.hostname}/mjpeg`;
}
