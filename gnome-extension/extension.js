/* Victus Control GNOME Shell Extension
 * 
 * Integrates with victus-backend service for HP Victus laptop
 * fan control and keyboard RGB management.
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import St from 'gi://St';
import Clutter from 'gi://Clutter';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as Slider from 'resource:///org/gnome/shell/ui/slider.js';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const SOCKET_PATH = '/run/victus/victus.sock';

// Fan modes supported by the backend
const FAN_MODES = {
    AUTO: '0',
    BETTER_AUTO: '1',
    MANUAL: '2',
    MAX: '3'
};

const FAN_MODE_LABELS = {
    '0': 'AUTO',
    '1': 'Better Auto',
    '2': 'MANUAL',
    '3': 'MAX'
};

class VictusIndicator extends PanelMenu.Button {
    static {
        GLib.Object.registerClass(this);
    }

    _init(extension) {
        super._init(0.0, 'Victus Control');

        this._extension = extension;
        this._currentFanMode = '0';
        this._currentFanSpeed = [0, 0];
        this._currentKeyboardColor = '#FFFFFF';
        this._currentKeyboardBrightness = 100;
        this._updateTimeoutId = null;
        this._connection = null;

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
            let item = new PopupMenu.PopupMenuItem(modeName.replace('_', ' '));
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
        let kbdLabel = new PopupMenu.PopupMenuItem('⌨️ Keyboard RGB', { reactive: false });
        this.menu.addMenuItem(kbdLabel);

        // Color presets
        this._colorSubMenu = new PopupMenu.PopupSubMenuMenuItem('Color: White');
        this.menu.addMenuItem(this._colorSubMenu);

        const colors = [
            { name: 'White', value: 'FFFFFF' },
            { name: 'Red', value: 'FF0000' },
            { name: 'Green', value: '00FF00' },
            { name: 'Blue', value: '0000FF' },
            { name: 'Cyan', value: '00FFFF' },
            { name: 'Magenta', value: 'FF00FF' },
            { name: 'Yellow', value: 'FFFF00' },
            { name: 'Orange', value: 'FF8000' },
            { name: 'Purple', value: '8000FF' },
            { name: 'Off', value: '000000' }
        ];

        for (let color of colors) {
            let item = new PopupMenu.PopupMenuItem(color.name);
            item.connect('activate', () => this._setKeyboardColor(color.value));
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

    _sendCommand(command) {
        return new Promise((resolve, reject) => {
            try {
                let connection = new Gio.SocketClient();
                let socketAddress = new Gio.UnixSocketAddress({ path: SOCKET_PATH });

                connection.connect_async(socketAddress, null, (client, result) => {
                    try {
                        let conn = client.connect_finish(result);
                        let outputStream = conn.get_output_stream();
                        let inputStream = conn.get_input_stream();

                        // Send length-prefixed message
                        let encoder = new TextEncoder();
                        let cmdBytes = encoder.encode(command);
                        let lenBytes = new Uint8Array(4);
                        let dv = new DataView(lenBytes.buffer);
                        dv.setUint32(0, cmdBytes.length, true); // little-endian

                        outputStream.write_all(lenBytes, null);
                        outputStream.write_all(cmdBytes, null);

                        // Read response length
                        let responseLenBytes = inputStream.read_bytes(4, null).get_data();
                        let responseLenDv = new DataView(responseLenBytes.buffer);
                        let responseLen = responseLenDv.getUint32(0, true);

                        // Read response
                        let responseBytes = inputStream.read_bytes(responseLen, null).get_data();
                        let decoder = new TextDecoder();
                        let response = decoder.decode(responseBytes);

                        conn.close(null);
                        resolve(response);
                    } catch (e) {
                        reject(e);
                    }
                });
            } catch (e) {
                reject(e);
            }
        });
    }

    async _setFanMode(mode) {
        try {
            let response = await this._sendCommand(`SET_FAN_MODE ${mode}`);
            if (!response.startsWith('ERROR')) {
                this._currentFanMode = mode;
                this._fanModeSubMenu.label.text = `Mode: ${FAN_MODE_LABELS[mode]}`;
                this._updateSliderVisibility();
                Main.notify('Victus Control', `Fan mode: ${FAN_MODE_LABELS[mode]}`);
            }
        } catch (e) {
            console.error('Victus: Failed to set fan mode:', e);
        }
    }

    async _setFanSpeed(fan, value) {
        // Map slider value (0-1) to RPM steps (0-7)
        let step = Math.round(value * 7);
        try {
            await this._sendCommand(`SET_FAN_SPEED ${fan} ${step}`);
        } catch (e) {
            console.error('Victus: Failed to set fan speed:', e);
        }
    }

    _onFan1SliderChanged() {
        if (this._fan1SliderDebounce)
            GLib.source_remove(this._fan1SliderDebounce);

        this._fan1SliderDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setFanSpeed(0, this._fan1Slider.value);
            this._fan1SliderDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    _onFan2SliderChanged() {
        if (this._fan2SliderDebounce)
            GLib.source_remove(this._fan2SliderDebounce);

        this._fan2SliderDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setFanSpeed(1, this._fan2Slider.value);
            this._fan2SliderDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    async _setKeyboardColor(hexColor) {
        try {
            let response = await this._sendCommand(`SET_KEYBOARD_COLOR ${hexColor}`);
            if (!response.startsWith('ERROR')) {
                this._currentKeyboardColor = hexColor;
                Main.notify('Victus Control', `Keyboard color set`);
            }
        } catch (e) {
            console.error('Victus: Failed to set keyboard color:', e);
        }
    }

    _onKbdBrightnessChanged() {
        if (this._kbdBrightnessDebounce)
            GLib.source_remove(this._kbdBrightnessDebounce);

        this._kbdBrightnessDebounce = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
            this._setKeyboardBrightness(Math.round(this._kbdBrightnessSlider.value * 100));
            this._kbdBrightnessDebounce = null;
            return GLib.SOURCE_REMOVE;
        });
    }

    async _setKeyboardBrightness(value) {
        try {
            await this._sendCommand(`SET_KBD_BRIGHTNESS ${value}`);
        } catch (e) {
            console.error('Victus: Failed to set keyboard brightness:', e);
        }
    }

    _startStatusUpdates() {
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

            // Get fan speed/RPM
            let speedResponse = await this._sendCommand('GET_FAN_SPEED');
            if (!speedResponse.startsWith('ERROR')) {
                // Parse response like "2500 3000" (RPM values)
                let parts = speedResponse.trim().split(' ');
                if (parts.length >= 2) {
                    this._rpmItem.label.text = `💨 Fan: ${parts[0]} / ${parts[1]} RPM`;
                }
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
