#include <gtk/gtk.h>

#include "style.hpp"

namespace {

// Dark "performance" palette in the spirit of the Victus chassis: a near-black
// carbon base, cool grey panels, and an electric cyan accent, with amber and
// red reserved for temperature warnings so colour always carries meaning.
const char *kVictusCss = R"CSS(
@define-color victus_bg          #0a0d12;
@define-color victus_surface     #121822;
@define-color victus_surface_alt #18202c;
@define-color victus_border      #243040;
@define-color victus_accent      #00d9ff;
@define-color victus_accent_dim  #0596b3;
@define-color victus_text        #e6edf5;
@define-color victus_text_dim    #7d8da3;
@define-color victus_warn        #ffa726;
@define-color victus_hot         #ff4757;
@define-color victus_ok          #00e676;

window,
.victus-root {
  background-color: @victus_bg;
  color: @victus_text;
  font-family: "Inter", "Cantarell", "Segoe UI", sans-serif;
}

/* Title bar: a slim carbon strip with an accent hairline under it. */
headerbar {
  background: linear-gradient(180deg, #151d29 0%, #0d1219 100%);
  border-bottom: 1px solid @victus_accent_dim;
  box-shadow: 0 1px 12px rgba(0, 217, 255, 0.18);
  min-height: 42px;
  padding: 0 6px;
}

headerbar label {
  font-weight: 700;
  letter-spacing: 2px;
  color: @victus_text;
  text-shadow: 0 0 10px rgba(0, 217, 255, 0.55);
}

/* Tabs read as angular console buttons, with the active one underlined. */
notebook > header {
  background-color: @victus_bg;
  border-bottom: 1px solid @victus_border;
  padding-top: 4px;
}

notebook > header > tabs > tab {
  background-color: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  border-radius: 0;
  padding: 8px 22px;
  margin: 0 2px;
  min-height: 30px;
  color: @victus_text_dim;
  font-weight: 700;
  letter-spacing: 1.6px;
  transition: color 160ms ease, border-color 160ms ease, background-color 160ms ease;
}

notebook > header > tabs > tab:hover {
  color: @victus_text;
  background-color: rgba(0, 217, 255, 0.06);
}

notebook > header > tabs > tab:checked {
  color: @victus_accent;
  border-bottom-color: @victus_accent;
  background-color: rgba(0, 217, 255, 0.09);
}

notebook > stack,
scrolledwindow,
scrolledwindow > viewport {
  background-color: @victus_bg;
}

scrollbar {
  background-color: @victus_bg;
  border: none;
}

scrollbar slider {
  background-color: @victus_border;
  border-radius: 3px;
  min-width: 7px;
}

scrollbar slider:hover {
  background-color: @victus_accent_dim;
}

/* Grouped panels. */
.victus-card {
  background-color: @victus_surface;
  border: 1px solid @victus_border;
  border-radius: 4px;
  padding: 16px;
}

.victus-card:hover {
  border-color: alpha(@victus_accent, 0.35);
}

/* Small uppercase heading that labels a panel. */
.section-title {
  font-size: 11px;
  font-weight: 800;
  letter-spacing: 2.4px;
  color: @victus_accent;
  text-shadow: 0 0 8px rgba(0, 217, 255, 0.35);
}

.section-icon {
  color: @victus_accent;
  -gtk-icon-size: 16px;
}

.tile-icon {
  color: @victus_text_dim;
  -gtk-icon-size: 13px;
}

/* Explains a control that hardware will not honour. */
.notice {
  color: @victus_warn;
  font-size: 12px;
  background-color: rgba(255, 167, 38, 0.08);
  border-left: 3px solid @victus_warn;
  border-radius: 2px;
  padding: 8px 10px;
}

.field-label {
  color: @victus_text_dim;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 1.1px;
}

/* Numeric telemetry: monospace so digits do not jitter as values change. */
.readout {
  font-family: "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace;
  font-size: 22px;
  font-weight: 700;
  color: @victus_accent;
  text-shadow: 0 0 14px rgba(0, 217, 255, 0.45);
}

.readout.warn { color: @victus_warn; text-shadow: 0 0 14px rgba(255, 167, 38, 0.45); }
.readout.hot  { color: @victus_hot;  text-shadow: 0 0 16px rgba(255, 71, 87, 0.55); }
.readout.ok   { color: @victus_ok;   text-shadow: 0 0 14px rgba(0, 230, 118, 0.40); }

.readout-unit {
  font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace;
  font-size: 11px;
  font-weight: 600;
  color: @victus_text_dim;
}

.status-line {
  color: @victus_text_dim;
  font-size: 12px;
  letter-spacing: 0.6px;
}

/* Buttons: squared off, hairline border, accent wash on hover. */
button {
  background: linear-gradient(180deg, @victus_surface_alt 0%, @victus_surface 100%);
  border: 1px solid @victus_border;
  border-radius: 3px;
  color: @victus_text;
  padding: 9px 16px;
  font-weight: 700;
  letter-spacing: 1.2px;
  transition: all 160ms ease;
}

button:hover {
  border-color: @victus_accent;
  color: @victus_accent;
  box-shadow: 0 0 14px rgba(0, 217, 255, 0.22);
}

button:active {
  background: @victus_accent_dim;
  color: @victus_bg;
}

button:disabled {
  color: alpha(@victus_text_dim, 0.5);
  border-color: alpha(@victus_border, 0.6);
  box-shadow: none;
}

/* The one prominent action on a page. */
button.primary-action {
  background: linear-gradient(180deg, @victus_accent 0%, @victus_accent_dim 100%);
  border-color: @victus_accent;
  color: #06222b;
  font-weight: 800;
}

button.primary-action:hover {
  box-shadow: 0 0 22px rgba(0, 217, 255, 0.5);
  color: #06222b;
}

/* Power toggle reads as lit when the backlight is on. */
button.power-toggle {
  font-size: 13px;
  padding: 12px;
}

button.power-toggle.is-on {
  border-color: @victus_ok;
  color: @victus_ok;
  box-shadow: 0 0 16px rgba(0, 230, 118, 0.25) inset;
}

button.power-toggle.is-off {
  border-color: @victus_border;
  color: @victus_text_dim;
}

/* Sliders: thin dark rail, glowing accent fill, square-ish knob. */
scale {
  min-height: 26px;
}

scale trough {
  background-color: #0c1119;
  border: 1px solid @victus_border;
  border-radius: 2px;
  min-height: 6px;
}

scale highlight {
  background: linear-gradient(90deg, @victus_accent_dim 0%, @victus_accent 100%);
  border-radius: 2px;
  box-shadow: 0 0 12px rgba(0, 217, 255, 0.55);
}

scale slider {
  background: @victus_text;
  border: 2px solid @victus_accent;
  border-radius: 3px;
  min-width: 14px;
  min-height: 14px;
  margin: -6px;
  box-shadow: 0 0 10px rgba(0, 217, 255, 0.6);
  transition: box-shadow 160ms ease;
}

scale slider:hover {
  box-shadow: 0 0 18px rgba(0, 217, 255, 0.9);
}

scale value {
  font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace;
  font-weight: 700;
  color: @victus_accent;
}

/* Dropdowns. */
dropdown > button,
combobox button.combo {
  background: @victus_surface_alt;
  border: 1px solid @victus_border;
  border-radius: 3px;
  color: @victus_text;
  letter-spacing: 1px;
}

dropdown > button:hover,
combobox button.combo:hover {
  border-color: @victus_accent;
  color: @victus_accent;
}

popover > contents,
dropdown popover > contents {
  background-color: @victus_surface;
  border: 1px solid @victus_border;
  border-radius: 4px;
  color: @victus_text;
  padding: 4px;
}

popover listview > row:selected,
dropdown listview > row:selected {
  background-color: alpha(@victus_accent, 0.18);
  color: @victus_accent;
}

/* The drawn keyboard sits in its own recessed well. */
.keyboard-stage {
  background-color: #070a0e;
  border: 1px solid @victus_border;
  border-radius: 4px;
  padding: 10px;
}

separator {
  background-color: @victus_border;
  min-height: 1px;
  min-width: 1px;
}

dialog,
messagedialog,
aboutdialog,
.background {
  background-color: @victus_bg;
  color: @victus_text;
}

aboutdialog label,
dialog label {
  color: @victus_text;
}

tooltip {
  background-color: @victus_surface;
  border: 1px solid @victus_accent_dim;
  color: @victus_text;
}
)CSS";

} // namespace

void apply_victus_style() {
  // No colour-scheme is forced here on purpose. Asking GTK for a dark scheme
  // makes it load the system theme's dark variant, and themes that ship only a
  // gtk-dark.css (Yaru among them) fail that import and drop their whole base
  // stylesheet. The rules below paint every surface this app shows instead, so
  // it looks the same either way.

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, kVictusCss);

  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref(provider);
}
