/* Victus Control GNOME Shell Extension
 *
 * Integrates with victus-backend service for HP Victus laptop
 * fan control and keyboard RGB management.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import St from 'gi://St';
import Clutter from 'gi://Clutter';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as Slider from 'resource:///org/gnome/shell/ui/slider.js';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const SOCKET_PATH = '/run/victus-control/victus_backend.sock';
const MIN_RPM = 2000;
const FAN1_MAX_RPM = 5800;
const FAN2_MAX_RPM = 6100;
const RPM_STEPS = 8;

// Fan modes supported by the backend
const FAN_MODES = {
    AUTO: 'AUTO',
    BETTER_AUTO: 'BETTER_AUTO',
    MANUAL: 'MANUAL',
    MAX: 'MAX',
};

const FAN_MODE_LABELS = {
    AUTO: 'AUTO',
    BETTER_AUTO: 'Better Auto',
    MANUAL: 'MANUAL',
    MAX: 'MAX',
};

const COLOR_PRESETS = [
    { name: 'White', hex: 'FFFFFF' },
    { name: 'Red', hex: 'FF0000' },
    { name: 'Green', hex: '00FF00' },
    { name: 'Blue', hex: '0000FF' },
    { name: 'Cyan', hex: '00FFFF' },
    { name: 'Magenta', hex: 'FF00FF' },
    { name: 'Yellow', hex: 'FFFF00' },
    { name: 'Orange', hex: 'FF8000' },
    { name: 'Purple', hex: '8000FF' },
    { name: 'Off', hex: '000000' },
];

function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
}

function sliderValueToLevel(value) {
    return Math.round(clamp(value, 0, 1) * (RPM_STEPS - 1)) + 1;
}

function levelToRpm(level, maxRpm) {
    if (RPM_STEPS <= 1)
        return maxRpm;

    let stepSize = (maxRpm - MIN_RPM) / (RPM_STEPS - 1);
    return Math.round(MIN_RPM + (level - 1) * stepSize);
}

function sliderValueToRpm(value, maxRpm) {
    return levelToRpm(sliderValueToLevel(value), maxRpm);
}

function rgbTripletToHex(rgbTriplet) {
    let parts = rgbTriplet.trim().split(/\s+/);
    if (parts.length !== 3)
        return null;

    let values = parts.map(part => Number.parseInt(part, 10));
    if (values.some(value => Number.isNaN(value) || value < 0 || value > 255))
        return null;

    return values.map(value => value.toString(16).padStart(2, '0').toUpperCase()).join('');
}

function hexToRgbTriplet(hexColor) {
    let hex = hexColor.replace(/^#/, '');
    if (!/^[0-9A-Fa-f]{6}$/.test(hex))
        return null;

    let red = Number.parseInt(hex.slice(0, 2), 16);
    let green = Number.parseInt(hex.slice(2, 4), 16);
    let blue = Number.parseInt(hex.slice(4, 6), 16);
    return `${red} ${green} ${blue}`;
}

function colorNameForHex(hexColor) {
    let normalized = hexColor.replace(/^#/, '').toUpperCase();
    let preset = COLOR_PRESETS.find(color => color.hex === normalized);
    return preset ? preset.name : `#${normalized}`;
}

class VictusIndicator extends PanelMenu.Button {
    static {
        GObject.registerClass(this);
    }

    _init(extension) {
        super._init(0.0, 'Victus Control');

        this._extension = extension;
        this._currentFanMode = FAN_MODES.AUTO;
        this._currentFanSpeed = ['--', '--'];
        this._currentKeyboardColor = 'FFFFFF';
        this._currentKeyboardBrightness = 255;
        this._keyboardAvailable = true;
        this._updateTimeoutId = null;
        this._connection = null;
        this._inputStream = null;
        this._outputStream = null;
        this._commandQueue = Promise.resolve();

        // Panel icon
        this._icon = new St.Icon({
            icon_name: 'weather-windy-symbolic',
            style_class: 'system-status-icon'
        });
        this.add_child(this._icon);

        // Build the menu
        this._buildMenu();

        // Start status updates
        this._startStatusUpdates();
    }

    _buildMenu() {
        // Header
        let headerItem = new PopupMenu.PopupMenuItem('🌀 Victus Control', { reactive: false });
        headerItem.label.add_style_class_name('victus-header');
        this.menu.addMenuItem(headerItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // Fan Mode Section
        let fanModeLabel = new PopupMenu.PopupMenuItem('Fan Mode', { reactive: false });
        this.menu.addMenuItem(fanModeLabel);

        this._fanModeSubMenu = new PopupMenu.PopupSubMenuMenuItem('Mode: AUTO');
        this.menu.addMenuItem(this._fanModeSubMenu);

        // Add mode options
        for (let [modeName, modeValue] of Object.entries(FAN_MODES)) {
            let item = new PopupMenu.PopupMenuItem(FAN_MODE_LABELS[modeName]);
            item.connect('activate', () => this._setFanMode(modeValue));
            this._fanModeSubMenu.menu.addMenuItem(item);
        }

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // Fan Speed Slider (for manual mode)
        this._fanSpeedLabel = new PopupMenu.PopupMenuItem('Fan Speed', { reactive: false });
        this.menu.addMenuItem(this._fanSpeedLabel);

        // Fan 1 slider
        this._fan1SliderItem = new PopupMenu.PopupBaseMenuItem({ activate: false });
        this._fan1Label = new St.Label({ text: 'Fan 1: ', y_align: Clutter.ActorAlign.CENTER });
        this._fan1SliderItem.add_child(this._fan1Label);

        this._fan1Slider = new Slider.Slider(0);
        this._fan1Slider.connect('notify::value', () => this._onFan1SliderChanged());
        this._fan1SliderItem.add_child(this._fan1Slider);
        this.menu.addMenuItem(this._fan1SliderItem);

        // Fan 2 slider
        this._fan2SliderItem = new PopupMenu.PopupBaseMenuItem({ activate: false });
        this._fan2Label = new St.Label({ text: 'Fan 2: ', y_align: Clutter.ActorAlign.CENTER });
        this._fan2SliderItem.add_child(this._fan2Label);

        this._fan2Slider = new Slider.Slider(0);
        this._fan2Slider.connect('notify::value', () => this._onFan2SliderChanged());
        this._fan2SliderItem.add_child(this._fan2Slider);
        this.menu.addMenuItem(this._fan2SliderItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // Status Display
        this._statusItem = new PopupMenu.PopupMenuItem('Status: --', { reactive: false });
        this.menu.addMenuItem(this._statusItem);

        this._tempItem = new PopupMenu.PopupMenuItem('🌡️ Temp: -- °C', { reactive: false });
        this.menu.addMenuItem(this._tempItem);

        this._rpmItem = new PopupMenu.PopupMenuItem('💨 Fan: -- / -- RPM', { reactive: false });
        this.menu.addMenuItem(this._rpmItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // Keyboard RGB Section
        this._kbdLabel = new PopupMenu.PopupMenuItem('⌨️ Keyboard RGB', { reactive: false });
        this.menu.addMenuItem(this._kbdLabel);

        // Color presets
        this._colorSubMenu = new PopupMenu.PopupSubMenuMenuItem('Color: White');
        this.menu.addMenuItem(this._colorSubMenu);

        for (let color of COLOR_PRESETS) {
            let item = new PopupMenu.PopupMenuItem(color.name);
            item.connect('activate', () => this._setKeyboardColor(color.hex));
            this._colorSubMenu.menu.addMenuItem(item);
        }

        // Keyboard brightness slider
        this._kbdBrightnessItem = new PopupMenu.PopupBaseMenuItem({ activate: false });
        let brightnessLabel = new St.Label({ text: 'Brightness: ', y_align: Clutter.ActorAlign.CENTER });
        this._kbdBrightnessItem.add_child(brightnessLabel);

        this._kbdBrightnessSlider = new Slider.Slider(1.0);
        this._kbdBrightnessSlider.connect('notify::value', () => this._onKbdBrightnessChanged());
        this._kbdBrightnessItem.add_child(this._kbdBrightnessSlider);
        this.menu.addMenuItem(this._kbdBrightnessItem);

        // Hide manual sliders by default (only show in MANUAL mode)
        this._updateSliderVisibility();
    }

    _updateSliderVisibility() {
        let isManual = this._currentFanMode === FAN_MODES.MANUAL;
        this._fan1SliderItem.visible = isManual;
        this._fan2SliderItem.visible = isManual;
        this._fanSpeedLabel.visible = isManual;
    }

    _setKeyboardControlsVisible(visible) {
        this._keyboardAvailable = visible;
        this._kbdLabel.visible = visible;
        this._colorSubMenu.visible = visible;
        this._kbdBrightnessItem.visible = visible;
    }

    _resetConnection() {
        if (this._connection) {
            try {
                this._connection.close(null);
            } catch (e) {
                console.error('Victus: Failed to close backend connection:', e);
            }
        }

        this._connection = null;
        this._inputStream = null;
        this._outputStream = null;
    }

    _ensureConnection() {
        return new Promise((resolve, reject) => {
            try {
                if (this._connection && !this._connection.is_closed()) {
                    resolve(this._connection);
                    return;
                }

                let connection = new Gio.SocketClient();
                let socketAddress = new Gio.UnixSocketAddress({ path: SOCKET_PATH });

                connection.connect_async(socketAddress, null, (client, result) => {
                    try {
                        this._connection = client.connect_finish(result);
                        this._outputStream = this._connection.get_output_stream();
                        this._inputStream = this._connection.get_input_stream();
                        resolve(this._connection);
                    } catch (e) {
                        reject(e);
                    }
                });
            } catch (e) {
                reject(e);
            }
        });
    }

    _sendCommand(command) {
        let run = async () => {
            await this._ensureConnection();

            try {
                let encoder = new TextEncoder();
                let cmdBytes = encoder.encode(command);
                let lenBytes = new Uint8Array(4);
                let dv = new DataView(lenBytes.buffer);
                dv.setUint32(0, cmdBytes.length, true);

                this._outputStream.write_all(lenBytes, null);
                this._outputStream.write_all(cmdBytes, null);

                let responseLenBytes = this._inputStream.read_bytes(4, null).get_data();
                let responseLenDv = new DataView(responseLenBytes.buffer);
                let responseLen = responseLenDv.getUint32(0, true);

                let responseBytes = this._inputStream.read_bytes(responseLen, null).get_data();
                let decoder = new TextDecoder();
                return decoder.decode(responseBytes);
            } catch (e) {
                this._resetConnection();
                throw e;
            }
        };

        this._commandQueue = this._commandQueue.then(run, run);
        return this._commandQueue;
    }

    async _setFanMode(mode) {
        try {
            let response = await this._sendCommand(`SET_FAN_MODE ${mode}`);
            if (!response.startsWith('ERROR')) {
                this._currentFanMode = mode;
                this._fanModeSubMenu.label.text = `Mode: ${FAN_MODE_LABELS[mode] ?? mode}`;
                this._updateSliderVisibility();
                Main.notify('Victus Control', `Fan mode: ${FAN_MODE_LABELS[mode] ?? mode}`);
            }
        } catch (e) {
            console.error('Victus: Failed to set fan mode:', e);
        }
    }

    async _setFanSpeed(fan, value) {
        let targetRpm = sliderValueToRpm(value, fan === '1' ? FAN1_MAX_RPM : FAN2_MAX_RPM);
        try {
            let response = await this._sendCommand(`SET_FAN_SPEED ${fan} ${targetRpm}`);
            if (!response.startsWith('ERROR')) {
                this._currentFanSpeed[fan === '1' ? 0 : 1] = `${targetRpm}`;
                this._rpmItem.label.text = `💨 Fan: ${this._currentFanSpeed[0]} / ${this._currentFanSpeed[1]} RPM`;
            }
        } catch (e) {
            console.error('Victus: Failed to set fan speed:', e);
        }
    }

    _onFan1SliderChanged() {
        if (this._fan1SliderDebounce)
            GLib.source_remove(this._fan1SliderDebounce);

        this._fan1SliderDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setFanSpeed('1', this._fan1Slider.value);
            this._fan1SliderDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    _onFan2SliderChanged() {
        if (this._fan2SliderDebounce)
            GLib.source_remove(this._fan2SliderDebounce);

        this._fan2SliderDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setFanSpeed('2', this._fan2Slider.value);
            this._fan2SliderDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    async _setKeyboardColor(hexColor) {
        let rgbColor = hexToRgbTriplet(hexColor);
        if (!rgbColor)
            return;

        try {
            let response = await this._sendCommand(`SET_KEYBOARD_COLOR ${rgbColor}`);
            if (!response.startsWith('ERROR')) {
                this._currentKeyboardColor = hexColor.replace(/^#/, '').toUpperCase();
                this._colorSubMenu.label.text = `Color: ${colorNameForHex(this._currentKeyboardColor)}`;
                this._setKeyboardControlsVisible(true);
                Main.notify('Victus Control', `Keyboard color: ${colorNameForHex(this._currentKeyboardColor)}`);
            }
        } catch (e) {
            console.error('Victus: Failed to set keyboard color:', e);
        }
    }

    _onKbdBrightnessChanged() {
        if (this._kbdBrightnessDebounce)
            GLib.source_remove(this._kbdBrightnessDebounce);

        this._kbdBrightnessDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setKeyboardBrightness(Math.round(this._kbdBrightnessSlider.value * 255));
            this._kbdBrightnessDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    async _setKeyboardBrightness(value) {
        try {
            let response = await this._sendCommand(`SET_KBD_BRIGHTNESS ${clamp(value, 0, 255)}`);
            if (!response.startsWith('ERROR')) {
                this._currentKeyboardBrightness = clamp(value, 0, 255);
                this._setKeyboardControlsVisible(true);
            }
        } catch (e) {
            console.error('Victus: Failed to set keyboard brightness:', e);
        }
    }

    async _refreshKeyboardState() {
        try {
            let brightnessResponse = await this._sendCommand('GET_KBD_BRIGHTNESS');
            if (brightnessResponse.startsWith('ERROR')) {
                this._setKeyboardControlsVisible(false);
                return;
            }

            let brightness = Number.parseInt(brightnessResponse.trim(), 10);
            if (!Number.isNaN(brightness)) {
                this._currentKeyboardBrightness = clamp(brightness, 0, 255);
                this._kbdBrightnessSlider.value = this._currentKeyboardBrightness / 255;
            }

            let colorResponse = await this._sendCommand('GET_KEYBOARD_COLOR');
            if (!colorResponse.startsWith('ERROR')) {
                let hexColor = rgbTripletToHex(colorResponse);
                if (hexColor) {
                    this._currentKeyboardColor = hexColor;
                    this._colorSubMenu.label.text = `Color: ${colorNameForHex(hexColor)}`;
                }
            }

            this._setKeyboardControlsVisible(true);
        } catch (e) {
            console.error('Victus: Failed to refresh keyboard state:', e);
            this._setKeyboardControlsVisible(false);
        }
    }

    _startStatusUpdates() {
        this._refreshKeyboardState();
        this._updateStatus();
        this._updateTimeoutId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 3, () => {
            this._updateStatus();
            return GLib.SOURCE_CONTINUE;
        });
    }

    async _updateStatus() {
        try {
            // Get fan mode
            let modeResponse = await this._sendCommand('GET_FAN_MODE');
            if (!modeResponse.startsWith('ERROR')) {
                this._currentFanMode = modeResponse.trim();
                this._fanModeSubMenu.label.text = `Mode: ${FAN_MODE_LABELS[this._currentFanMode] || 'Unknown'}`;
                this._updateSliderVisibility();
            }

            // Get fan speed/RPM per fan
            let fan1Response = await this._sendCommand('GET_FAN_SPEED 1');
            let fan2Response = await this._sendCommand('GET_FAN_SPEED 2');
            if (!fan1Response.startsWith('ERROR') && !fan2Response.startsWith('ERROR')) {
                this._currentFanSpeed = [fan1Response.trim(), fan2Response.trim()];
                this._rpmItem.label.text = `💨 Fan: ${this._currentFanSpeed[0]} / ${this._currentFanSpeed[1]} RPM`;
            }

            // Get CPU temp (from sysfs)
            try {
                let [ok, contents] = GLib.file_get_contents('/sys/class/thermal/thermal_zone0/temp');
                if (ok) {
                    let temp = parseInt(new TextDecoder().decode(contents)) / 1000;
                    this._tempItem.label.text = `🌡️ CPU: ${temp.toFixed(0)} °C`;
                }
            } catch (e) {
                // Ignore temp read errors
            }

            // Update status
            this._statusItem.label.text = `Status: Connected`;

        } catch (e) {
            this._statusItem.label.text = 'Status: Backend unavailable';
            this._rpmItem.label.text = '💨 Fan: -- / -- RPM';
        }
    }

    _stopStatusUpdates() {
        if (this._updateTimeoutId) {
            GLib.source_remove(this._updateTimeoutId);
            this._updateTimeoutId = null;
        }
    }

    destroy() {
        this._stopStatusUpdates();

        if (this._fan1SliderDebounce)
            GLib.source_remove(this._fan1SliderDebounce);
        if (this._fan2SliderDebounce)
            GLib.source_remove(this._fan2SliderDebounce);
        if (this._kbdBrightnessDebounce)
            GLib.source_remove(this._kbdBrightnessDebounce);

        this._resetConnection();
        super.destroy();
    }
}

export default class VictusControlExtension extends Extension {
    enable() {
        this._indicator = new VictusIndicator(this);
        Main.panel.addToStatusArea('victus-control', this._indicator);
    }

    disable() {
        if (this._indicator) {
            this._indicator.destroy();
            this._indicator = null;
        }
    }
}
