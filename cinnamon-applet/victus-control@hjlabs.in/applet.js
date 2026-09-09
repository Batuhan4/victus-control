/* Victus Control - Cinnamon applet
 *
 * Fan mode, fan speed and keyboard lighting from the panel, so the desktop app
 * is not needed for everyday adjustments. Talks to victus-backend over its unix
 * socket using the same length-prefixed protocol as the GTK app.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

const Applet = imports.ui.applet;
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const St = imports.gi.St;
const Lang = imports.lang;
const PopupMenu = imports.ui.popupMenu;
const Settings = imports.ui.settings;
const Main = imports.ui.main;
const Util = imports.misc.util;

const UUID = 'victus-control@hjlabs.in';
const SOCKET_PATH = '/run/victus-control/victus_backend.sock';

const MIN_RPM = 2600;
const RPM_STEPS = 8;
const DEFAULT_FAN_MAX_RPM = [5800, 6100];

const FAN_MODE_LABELS = {
    AUTO: 'Auto',
    BETTER_AUTO: 'Better Auto',
    MANUAL: 'Manual',
    MAX: 'Max',
};

const EFFECT_LABELS = {
    STATIC: 'Static',
    RAINBOW: 'Rainbow',
    BREATHE: 'Breathe',
    FLOW: 'Flow',
};

// Menu-open refresh rate. Closed-menu rate comes from settings, because the
// panel only needs an occasional number and polling is not free.
const ACTIVE_POLL_SECONDS = 2;

function encodeUint32LE(value) {
    return new Uint8Array([
        value & 0xff,
        (value >> 8) & 0xff,
        (value >> 16) & 0xff,
        (value >> 24) & 0xff,
    ]);
}

function decodeUint32LE(bytes) {
    return (bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)) >>> 0;
}

function writeBytesAsync(stream, bytes) {
    return new Promise((resolve, reject) => {
        stream.write_bytes_async(GLib.Bytes.new(bytes), GLib.PRIORITY_DEFAULT, null, (source, result) => {
            try {
                resolve(source.write_bytes_finish(result));
            } catch (e) {
                reject(e);
            }
        });
    });
}

function readBytesAsync(stream, length) {
    return new Promise((resolve, reject) => {
        stream.read_bytes_async(length, GLib.PRIORITY_DEFAULT, null, (source, result) => {
            try {
                let bytes = source.read_bytes_finish(result);
                let data = bytes.get_data();
                resolve(data ? Uint8Array.from(data) : new Uint8Array());
            } catch (e) {
                reject(e);
            }
        });
    });
}

function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
}

/* A slider row with a leading icon, matching the layout of Cinnamon's own
 * brightness slider in the power applet. */
class LabelledSlider extends PopupMenu.PopupSliderMenuItem {
    constructor(iconName, onChanged) {
        super(0);

        this._onChanged = onChanged;
        this._suppress = false;

        this.icon = new St.Icon({
            icon_name: iconName,
            icon_type: St.IconType.SYMBOLIC,
            icon_size: 16,
        });
        this.removeActor(this._slider);
        this.addActor(this.icon, { span: 0 });
        this.addActor(this._slider, { span: -1, expand: true });

        this.connect('value-changed', Lang.bind(this, function (item, value) {
            if (this._suppress)
                return;
            this._onChanged(value);
        }));
    }

    // Set the position without triggering the change handler, so refreshing
    // from the backend does not echo a command straight back at it.
    setValueSilently(value) {
        this._suppress = true;
        this.setValue(clamp(value, 0, 1));
        this._suppress = false;
    }
}

class VictusApplet extends Applet.TextIconApplet {
    _init(metadata, orientation, panelHeight, instanceId) {
        super._init(orientation, panelHeight, instanceId);

        this.setAllowedLayout(Applet.AllowedLayout.BOTH);
        this.set_applet_icon_symbolic_name('weather-windy');
        this.set_applet_label('');
        this.set_applet_tooltip('Victus Control');

        this._connection = null;
        this._inputStream = null;
        this._outputStream = null;
        this._commandQueue = Promise.resolve();
        this._destroyed = false;
        this._pollSource = 0;
        this._needsProbe = false;

        this._fanMode = 'AUTO';
        this._effect = 'STATIC';
        this._effectSpeed = 50;
        // SUPPORTED | UNSUPPORTED | DISABLED, from GET_FAN_TARGET_SUPPORT.
        this._fanSupport = 'SUPPORTED';
        this._keyboardZones = 'SINGLE_ZONE';
        this._fanMaxRpm = DEFAULT_FAN_MAX_RPM.slice();

        this.settings = new Settings.AppletSettings(this, UUID, instanceId);
        this.settings.bind('panel-display', 'panelDisplay', () => this._refresh());
        this.settings.bind('idle-poll-seconds', 'idlePollSeconds', () => this._reschedule());

        this.menuManager = new PopupMenu.PopupMenuManager(this);
        this.menu = new Applet.AppletPopupMenu(this, orientation);
        this.menuManager.addMenu(this.menu);

        this._buildMenu();

        this.menu.connect('open-state-changed', Lang.bind(this, function (menu, isOpen) {
            this._menuOpen = isOpen;
            this._reschedule();
            if (isOpen)
                this._refresh();
        }));

        // Ask the backend what this board can actually do before showing
        // controls for it, then start the slow panel refresh.
        this._probeCapabilities();
        this._reschedule();
    }

    // ---- menu ---------------------------------------------------------------

    _buildMenu() {
        this._statusItem = new PopupMenu.PopupMenuItem('Connecting to victus-backend…', { reactive: false });
        this.menu.addMenuItem(this._statusItem);

        this._tempItem = new PopupMenu.PopupMenuItem('', { reactive: false });
        this.menu.addMenuItem(this._tempItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // --- Cooling ---
        this._fanNoteItem = new PopupMenu.PopupMenuItem('', { reactive: false });
        this.menu.addMenuItem(this._fanNoteItem);
        this._fanNoteItem.actor.hide();

        this._fanModeMenu = new PopupMenu.PopupSubMenuMenuItem('Fan mode');
        this.menu.addMenuItem(this._fanModeMenu);

        this._fanModeItems = {};
        ['AUTO', 'BETTER_AUTO', 'MANUAL', 'MAX'].forEach(Lang.bind(this, function (mode) {
            let item = new PopupMenu.PopupMenuItem(FAN_MODE_LABELS[mode]);
            item.connect('activate', Lang.bind(this, function () {
                this._setFanMode(mode);
            }));
            this._fanModeItems[mode] = item;
            this._fanModeMenu.menu.addMenuItem(item);
        }));

        this._fanSpeedSlider = new LabelledSlider('weather-windy-symbolic', Lang.bind(this, function (value) {
            this._setFanSpeed(value);
        }));
        this.menu.addMenuItem(this._fanSpeedSlider);
        this._fanSpeedSlider.actor.hide();

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // --- Keyboard backlight ---
        this._kbdSwitch = new PopupMenu.PopupSwitchMenuItem('Keyboard backlight', false);
        this._kbdSwitch.connect('toggled', Lang.bind(this, function (item, state) {
            this._setKeyboardBrightness(state ? 255 : 0);
        }));
        this.menu.addMenuItem(this._kbdSwitch);

        this._kbdSlider = new LabelledSlider('keyboard-brightness-symbolic', Lang.bind(this, function (value) {
            this._setKeyboardBrightness(Math.round(value * 255));
        }));
        this.menu.addMenuItem(this._kbdSlider);

        this._effectMenu = new PopupMenu.PopupSubMenuMenuItem('Lighting effect');
        this.menu.addMenuItem(this._effectMenu);

        this._effectItems = {};
        ['STATIC', 'RAINBOW', 'BREATHE', 'FLOW'].forEach(Lang.bind(this, function (name) {
            let item = new PopupMenu.PopupMenuItem(EFFECT_LABELS[name]);
            item.connect('activate', Lang.bind(this, function () {
                this._setEffect(name);
            }));
            this._effectItems[name] = item;
            this._effectMenu.menu.addMenuItem(item);
        }));

        this._effectSpeedSlider = new LabelledSlider('media-playlist-repeat-symbolic', Lang.bind(this, function (value) {
            this._setEffectSpeed(Math.round(1 + value * 99));
        }));
        this.menu.addMenuItem(this._effectSpeedSlider);
        this._effectSpeedSlider.actor.hide();

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        let openItem = new PopupMenu.PopupIconMenuItem('Open Victus Control', 'preferences-system-symbolic', St.IconType.SYMBOLIC);
        openItem.connect('activate', function () {
            Util.spawnCommandLine('victus-control');
        });
        this.menu.addMenuItem(openItem);
    }

    on_applet_clicked() {
        this.menu.toggle();
    }

    // ---- backend ------------------------------------------------------------

    _ensureConnection() {
        return new Promise((resolve, reject) => {
            try {
                if (this._connection && !this._connection.is_closed()) {
                    resolve(this._connection);
                    return;
                }

                let client = new Gio.SocketClient();
                let address = new Gio.UnixSocketAddress({ path: SOCKET_PATH });

                client.connect_async(address, null, Lang.bind(this, function (source, result) {
                    try {
                        this._connection = source.connect_finish(result);
                        this._outputStream = this._connection.get_output_stream();
                        this._inputStream = this._connection.get_input_stream();
                        resolve(this._connection);
                    } catch (e) {
                        reject(e);
                    }
                }));
            } catch (e) {
                reject(e);
            }
        });
    }

    _resetConnection() {
        try {
            if (this._connection && !this._connection.is_closed())
                this._connection.close(null);
        } catch (e) {
            // Already gone; nothing useful to do.
        }
        this._connection = null;
        this._inputStream = null;
        this._outputStream = null;

        // A dropped connection usually means the backend restarted, and what it
        // accepts can differ afterwards - enabling or removing the fan-control
        // drop-in flips GET_FAN_TARGET_SUPPORT between DISABLED and UNSUPPORTED.
        // Re-probe rather than trusting what was learned at load.
        this._needsProbe = true;
    }

    async _writeAll(bytes) {
        let offset = 0;
        while (offset < bytes.length) {
            let written = await writeBytesAsync(this._outputStream, bytes.subarray(offset));
            if (written <= 0)
                throw new Error('Socket write returned no data');
            offset += written;
        }
    }

    async _readExact(length) {
        let out = new Uint8Array(length);
        let offset = 0;
        while (offset < length) {
            let chunk = await readBytesAsync(this._inputStream, length - offset);
            if (chunk.length === 0)
                throw new Error('Socket closed while reading');
            out.set(chunk, offset);
            offset += chunk.length;
        }
        return out;
    }

    // Commands are serialised: the backend answers one request per connection
    // at a time, and interleaving them would mismatch replies to senders.
    _send(command) {
        let run = Lang.bind(this, async function () {
            if (this._destroyed)
                throw new Error('Applet destroyed');

            await this._ensureConnection();
            try {
                let payload = new TextEncoder().encode(command);
                await this._writeAll(encodeUint32LE(payload.length));
                await this._writeAll(payload);

                let length = decodeUint32LE(await this._readExact(4));
                if (length > 4096)
                    throw new Error('Response too long: ' + length);

                return new TextDecoder().decode(await this._readExact(length));
            } catch (e) {
                this._resetConnection();
                throw e;
            }
        });

        this._commandQueue = this._commandQueue.then(run, run);
        return this._commandQueue;
    }

    // ---- capability probe ---------------------------------------------------

    _probeCapabilities() {
        (async () => {
            try {
                let support = (await this._send('GET_FAN_TARGET_SUPPORT')).trim();
                if (support === 'SUPPORTED' || support === 'UNSUPPORTED' || support === 'DISABLED')
                    this._fanSupport = support;
            } catch (e) {
                // Older backends do not know this command; assume supported and
                // let the set call report the failure instead.
                this._fanSupport = 'SUPPORTED';
            }

            try {
                let type = await this._send('GET_KEYBOARD_TYPE');
                if (type.indexOf('ERROR') === -1)
                    this._keyboardZones = type.trim();
            } catch (e) {
                // Leave the default.
            }

            for (let fan = 1; fan <= 2; fan++) {
                try {
                    let max = await this._send('GET_FAN_MAX_SPEED ' + fan);
                    let parsed = parseInt(max, 10);
                    if (!isNaN(parsed) && parsed > MIN_RPM)
                        this._fanMaxRpm[fan - 1] = parsed;
                } catch (e) {
                    // Keep the default maximum.
                }
            }

            // Match what the backend will actually accept, so the menu never
            // offers something that comes back as an error:
            //   DISABLED    - fan control is off entirely; no mode can be set.
            //   UNSUPPORTED - MANUAL and Better Auto both steer through
            //                 fan*_target, which this board does not expose.
            // Reset first: a re-probe after a backend restart may be relaxing
            // restrictions, not adding them.
            this._fanNoteItem.actor.hide();
            this._fanModeMenu.actor.show();
            this._fanModeItems['MANUAL'].actor.show();
            this._fanModeItems['BETTER_AUTO'].actor.show();

            if (this._fanSupport === 'DISABLED') {
                this._fanModeMenu.actor.hide();
                this._fanSpeedSlider.actor.hide();
                this._fanNoteItem.label.text = 'Fan control disabled; fans left to the firmware';
                this._fanNoteItem.actor.show();
            } else if (this._fanSupport === 'UNSUPPORTED') {
                this._fanModeItems['MANUAL'].actor.hide();
                this._fanModeItems['BETTER_AUTO'].actor.hide();
                this._fanSpeedSlider.actor.hide();
                this._fanNoteItem.label.text = 'This board has no fan speed targets';
                this._fanNoteItem.actor.show();
            }

            // FLOW needs zones for the colour to travel across.
            if (this._keyboardZones !== 'FOUR_ZONE')
                this._effectItems['FLOW'].actor.hide();

            this._refresh();
        })().catch(Lang.bind(this, function (e) {
            global.logError(UUID + ': capability probe failed: ' + e);
        }));
    }

    // ---- actions ------------------------------------------------------------

    _setFanMode(mode) {
        (async () => {
            let reply = await this._send('SET_FAN_MODE ' + mode);
            if (reply.indexOf('ERROR') === 0) {
                Main.notify('Victus Control', 'Could not set fan mode: ' + reply);
                return;
            }
            this._fanMode = mode;
            this._updateFanControls();
            this._refresh();
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    _setFanSpeed(value) {
        if (this._fanSupport !== 'SUPPORTED')
            return;

        let step = clamp(Math.round(value * RPM_STEPS), 1, RPM_STEPS);
        (async () => {
            for (let fan = 1; fan <= 2; fan++) {
                let max = this._fanMaxRpm[fan - 1];
                let rpm = Math.round(MIN_RPM + ((max - MIN_RPM) * step) / RPM_STEPS);
                let reply = await this._send('SET_FAN_SPEED ' + fan + ' ' + rpm);
                if (reply.indexOf('ERROR') === 0) {
                    Main.notify('Victus Control', 'Fan ' + fan + ': ' + reply);
                    return;
                }
            }
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    _setKeyboardBrightness(value) {
        (async () => {
            await this._send('SET_KBD_BRIGHTNESS ' + clamp(value, 0, 255));
            this._refresh();
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    _setEffect(name) {
        (async () => {
            let reply = await this._send('SET_KBD_EFFECT ' + name + ' ' + this._effectSpeed);
            if (reply.indexOf('ERROR') === 0) {
                Main.notify('Victus Control', 'Could not set effect: ' + reply);
                return;
            }
            this._effect = name;
            this._updateEffectControls();
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    _setEffectSpeed(speed) {
        this._effectSpeed = speed;
        if (this._effect === 'STATIC')
            return;

        (async () => {
            await this._send('SET_KBD_EFFECT ' + this._effect + ' ' + speed);
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    _reportFailure(e) {
        global.logError(UUID + ': ' + e);
        this._statusItem.label.text = 'victus-backend unavailable';
        this.set_applet_label('');
        this.set_applet_tooltip('Victus Control - backend not running');
    }

    // ---- refresh ------------------------------------------------------------

    _updateFanControls() {
        for (let mode in this._fanModeItems)
            this._fanModeItems[mode].setShowDot(mode === this._fanMode);

        this._fanModeMenu.label.text = 'Fan mode: ' + (FAN_MODE_LABELS[this._fanMode] || this._fanMode);

        // The speed slider only means anything in MANUAL, and only where the
        // firmware honours targets at all.
        if (this._fanSupport === 'SUPPORTED' && this._fanMode === 'MANUAL')
            this._fanSpeedSlider.actor.show();
        else
            this._fanSpeedSlider.actor.hide();
    }

    _updateEffectControls() {
        for (let name in this._effectItems)
            this._effectItems[name].setShowDot(name === this._effect);

        this._effectMenu.label.text = 'Lighting: ' + (EFFECT_LABELS[this._effect] || this._effect);

        if (this._effect === 'STATIC')
            this._effectSpeedSlider.actor.hide();
        else
            this._effectSpeedSlider.actor.show();
    }

    _refresh() {
        if (this._needsProbe) {
            this._needsProbe = false;
            this._probeCapabilities();
            return;
        }

        (async () => {
            let mode = (await this._send('GET_FAN_MODE')).trim();
            if (mode.indexOf('ERROR') === -1)
                this._fanMode = mode;

            let fan1 = parseInt(await this._send('GET_FAN_SPEED 1'), 10);
            let cpu = parseInt(await this._send('GET_CPU_TEMP'), 10);

            let fan2 = NaN, gpu = NaN, brightness = NaN, effect = null;
            if (this._menuOpen) {
                fan2 = parseInt(await this._send('GET_FAN_SPEED 2'), 10);
                gpu = parseInt(await this._send('GET_GPU_TEMP'), 10);
                brightness = parseInt(await this._send('GET_KBD_BRIGHTNESS'), 10);
                effect = (await this._send('GET_KBD_EFFECT')).trim();
            }

            // Panel
            if (this.panelDisplay === 'cpu-temp' && !isNaN(cpu))
                this.set_applet_label(cpu + '°C');
            else if (this.panelDisplay === 'fan-rpm' && !isNaN(fan1))
                this.set_applet_label(fan1 + '');
            else
                this.set_applet_label('');

            this.set_applet_tooltip('Victus Control  —  fan ' +
                (isNaN(fan1) ? '?' : fan1) + ' RPM, CPU ' + (isNaN(cpu) ? '?' : cpu) + '°C');

            // Menu contents are only worth updating while it is on screen.
            if (this._menuOpen) {
                this._statusItem.label.text = 'Fan 1: ' + (isNaN(fan1) ? '--' : fan1) +
                    ' RPM     Fan 2: ' + (isNaN(fan2) ? '--' : fan2) + ' RPM';
                this._tempItem.label.text = 'CPU: ' + (isNaN(cpu) ? '--' : cpu) +
                    '°C     GPU: ' + (isNaN(gpu) ? '--' : gpu) + '°C';

                if (!isNaN(brightness)) {
                    this._kbdSlider.setValueSilently(brightness / 255);
                    this._kbdSwitch.setToggleState(brightness > 0);
                }

                if (effect && effect.indexOf('ERROR') === -1) {
                    let parts = effect.split(' ');
                    this._effect = parts[0];
                    let speed = parseInt(parts[1], 10);
                    if (!isNaN(speed)) {
                        this._effectSpeed = speed;
                        this._effectSpeedSlider.setValueSilently((speed - 1) / 99);
                    }
                    this._updateEffectControls();
                }

                this._updateFanControls();
            }
        })().catch(Lang.bind(this, function (e) {
            this._reportFailure(e);
        }));
    }

    // Poll quickly only while the menu is visible; the panel number does not
    // need a fast refresh and polling costs CPU on a laptop that already runs hot.
    _reschedule() {
        if (this._pollSource) {
            GLib.source_remove(this._pollSource);
            this._pollSource = 0;
        }
        if (this._destroyed)
            return;

        let seconds = this._menuOpen ? ACTIVE_POLL_SECONDS : (this.idlePollSeconds || 10);
        this._pollSource = GLib.timeout_add_seconds(GLib.PRIORITY_LOW, seconds, Lang.bind(this, function () {
            this._refresh();
            return GLib.SOURCE_CONTINUE;
        }));
    }

    on_applet_removed_from_panel() {
        this._destroyed = true;
        if (this._pollSource) {
            GLib.source_remove(this._pollSource);
            this._pollSource = 0;
        }
        this._resetConnection();
        if (this.settings)
            this.settings.finalize();
    }
}

function main(metadata, orientation, panelHeight, instanceId) {
    return new VictusApplet(metadata, orientation, panelHeight, instanceId);
}
