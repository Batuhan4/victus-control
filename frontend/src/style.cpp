#include <gtk/gtk.h>

#include "style.hpp"

namespace {

// Dark palette in the spirit of the Victus chassis: a near-black carbon base,
// cool grey panels and one electric cyan accent. Cyan marks what is selected
// or interactive; amber and red are reserved for warnings and heat, so colour
// always carries meaning. Surfaces are flat - no glows - so the readings, not
// the chrome, are what the eye lands on.
const char *kVictusCss = R"CSS(
@define-color victus_bg          #0a0d12;
@define-color victus_surface     #111722;
@define-color victus_surface_alt #18202c;
@define-color victus_well        #0b1018;
@define-color victus_border      #222d3c;
@define-color victus_accent      #00d9ff;
@define-color victus_accent_dim  #0596b3;
@define-color victus_text        #e6edf5;
@define-color victus_text_dim    #8696ab;
@define-color victus_warn        #ffa726;
@define-color victus_hot         #ff4757;
@define-color victus_ok          #00e676;

/* GTK draws focus rings and selections from its own accent colours, which
   follow the desktop's accent (orange on Ubuntu) and would otherwise clash
   with everything here. Point them at the app accent. */
@define-color accent_color @victus_accent;
@define-color accent_bg_color @victus_accent;
@define-color accent_fg_color #06222b;
@define-color theme_selected_bg_color @victus_accent;
@define-color theme_selected_fg_color #06222b;

window,
.victus-root {
  background-color: @victus_bg;
  color: @victus_text;
  font-family: "Inter", "Cantarell", "Ubuntu", "Segoe UI", sans-serif;
}

/* Title bar: a slim carbon strip with an accent hairline under it. */
headerbar {
  background: @victus_surface;
  border-bottom: 1px solid alpha(@victus_accent, 0.35);
  box-shadow: none;
  min-height: 40px;
  padding: 0 8px;
}

headerbar label {
  font-size: 12px;
  font-weight: 800;
  letter-spacing: 2.4px;
  color: @victus_text;
}

/* Window controls and the menu button are small round targets, not the
   padded console buttons used in the cards. */
headerbar button,
windowcontrols > button {
  background: none;
  border: none;
  box-shadow: none;
  border-radius: 999px;
  padding: 0;
  margin: 0 2px;
  min-width: 28px;
  min-height: 28px;
  color: @victus_text_dim;
  letter-spacing: 0;
}

headerbar button:hover,
windowcontrols > button:hover {
  background-color: alpha(@victus_text, 0.10);
  color: @victus_text;
  box-shadow: none;
}

headerbar button:active,
headerbar button:checked {
  background-color: alpha(@victus_accent, 0.20);
  color: @victus_accent;
}

windowcontrols > button.close:hover {
  background-color: @victus_hot;
  color: #ffffff;
}

scrolledwindow,
scrolledwindow > viewport {
  background-color: @victus_bg;
}

scrollbar {
  background-color: transparent;
  border: none;
}

scrollbar slider {
  background-color: @victus_border;
  border-radius: 999px;
  min-width: 6px;
  min-height: 6px;
}

scrollbar slider:hover {
  background-color: @victus_accent_dim;
}

/* Grouped panels. */
.victus-card {
  background-color: @victus_surface;
  border: 1px solid @victus_border;
  border-radius: 12px;
  padding: 18px 20px 20px 20px;
}

/* Small uppercase heading that labels a panel. */
.section-title {
  font-size: 11px;
  font-weight: 800;
  letter-spacing: 2.2px;
  color: @victus_accent;
}

.section-icon {
  color: @victus_accent;
  -gtk-icon-size: 16px;
}

.tile-icon {
  color: @victus_text_dim;
  -gtk-icon-size: 13px;
}

/* A telemetry tile: one reading, in its own recessed panel. */
.stat-tile {
  background-color: @victus_well;
  border: 1px solid @victus_border;
  border-radius: 10px;
  padding: 12px 8px 10px 8px;
}

/* Something the user needs to know is limiting what they can do. */
.notice {
  color: @victus_warn;
  font-size: 12px;
  background-color: alpha(@victus_warn, 0.08);
  border-left: 3px solid @victus_warn;
  border-radius: 4px;
  padding: 8px 10px;
}

/* A standing fact about the hardware: present, but not shouting. */
.footnote {
  color: @victus_text_dim;
  font-size: 11.5px;
}

.error-text {
  color: @victus_hot;
}

.field-label {
  color: @victus_text_dim;
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 1.2px;
}

/* Row captions share a width so the controls beside them line up. */
.row-label {
  min-width: 62px;
}

/* Numeric telemetry: monospace so digits do not jitter as values change. */
.readout {
  font-family: "JetBrains Mono", "Fira Code", "Ubuntu Mono", "DejaVu Sans Mono", monospace;
  font-size: 22px;
  font-weight: 700;
  color: @victus_text;
}

.readout.warn { color: @victus_warn; }
.readout.hot  { color: @victus_hot; }
.readout.ok   { color: @victus_ok; }

.readout-unit {
  font-family: "JetBrains Mono", "Ubuntu Mono", "DejaVu Sans Mono", monospace;
  font-size: 11px;
  font-weight: 600;
  color: @victus_text_dim;
}

.status-line {
  color: @victus_text_dim;
  font-size: 12px;
}

.mono {
  font-family: "JetBrains Mono", "Ubuntu Mono", "DejaVu Sans Mono", monospace;
  font-size: 11px;
}

/* Buttons. */
button {
  background: @victus_surface_alt;
  border: 1px solid @victus_border;
  border-radius: 8px;
  box-shadow: none;
  color: @victus_text;
  padding: 8px 16px;
  font-weight: 700;
  letter-spacing: 0.4px;
  transition: all 140ms ease;
}

button:hover {
  border-color: alpha(@victus_accent, 0.7);
  color: @victus_accent;
}

button:active {
  background: alpha(@victus_accent, 0.22);
}

button:disabled {
  color: alpha(@victus_text_dim, 0.5);
  border-color: alpha(@victus_border, 0.6);
}

button:focus-visible {
  outline: 2px solid alpha(@victus_accent, 0.6);
  outline-offset: 1px;
}

button.flat {
  background: none;
  border-color: transparent;
}

/* The one prominent action on a page. */
button.primary-action {
  background: @victus_accent;
  border-color: @victus_accent;
  color: #06222b;
  font-weight: 800;
}

button.primary-action:hover {
  background: shade(@victus_accent, 1.1);
  color: #06222b;
}

/* Segmented control: a recessed track holding one button per choice, the
   active one raised and tinted. Used for the cooling profile and the lighting
   style, so "pick one of a few" looks the same everywhere. */
.segmented {
  background-color: @victus_well;
  border: 1px solid @victus_border;
  border-radius: 10px;
  padding: 3px;
}

.segmented > button {
  background: none;
  border: 1px solid transparent;
  border-radius: 7px;
  padding: 8px 10px;
  color: @victus_text_dim;
  font-weight: 700;
  letter-spacing: 0.3px;
}

.segmented > button:hover {
  background-color: alpha(@victus_text, 0.06);
  border-color: transparent;
  color: @victus_text;
}

.segmented > button:checked {
  background-color: alpha(@victus_accent, 0.16);
  border-color: alpha(@victus_accent, 0.55);
  color: @victus_accent;
}

/* A choice with a cost (MAX is loud) is amber when active, so it never looks
   like the resting state. */
.segmented > button.loud:checked {
  background-color: alpha(@victus_warn, 0.15);
  border-color: alpha(@victus_warn, 0.6);
  color: @victus_warn;
}

.segmented:disabled > button:checked {
  background-color: alpha(@victus_text_dim, 0.12);
  border-color: alpha(@victus_text_dim, 0.3);
  color: @victus_text_dim;
}

/* Switch: pill track, round knob, accent when on. */
switch {
  background-color: @victus_well;
  border: 1px solid @victus_border;
  border-radius: 999px;
  min-width: 44px;
  min-height: 24px;
  padding: 2px;
  transition: background-color 180ms ease, border-color 180ms ease;
}

switch:checked {
  background-color: @victus_accent_dim;
  border-color: @victus_accent;
}

switch > slider {
  background: @victus_text_dim;
  border: none;
  box-shadow: none;
  border-radius: 999px;
  min-width: 18px;
  min-height: 18px;
  margin: 0;
  transition: background 180ms ease;
}

switch:checked > slider {
  background: #ffffff;
}

switch > image {
  opacity: 0;
}

switch:disabled {
  opacity: 0.45;
}

/* Sliders: thin dark rail, accent fill, round knob. */
scale {
  min-height: 24px;
  padding: 6px 0;
}

scale trough {
  background-color: @victus_well;
  border: 1px solid @victus_border;
  border-radius: 999px;
  min-height: 4px;
}

scale highlight {
  background: @victus_accent;
  border: none;
  border-radius: 999px;
  min-height: 4px;
  margin: -1px;
}

scale slider {
  background: #ffffff;
  border: 3px solid @victus_accent;
  border-radius: 999px;
  box-shadow: none;
  min-width: 10px;
  min-height: 10px;
  margin: -7px;
  transition: border-color 140ms ease;
}

scale slider:hover {
  border-color: shade(@victus_accent, 1.15);
}

scale value {
  font-family: "JetBrains Mono", "Ubuntu Mono", "DejaVu Sans Mono", monospace;
  font-size: 12px;
  font-weight: 700;
  color: @victus_text;
  margin-left: 12px;
  min-width: 26px;
}

scale marks indicator {
  background-color: @victus_border;
  min-height: 4px;
  min-width: 1px;
}

scale:disabled {
  opacity: 0.45;
}

popover > contents {
  background-color: @victus_surface;
  border: 1px solid @victus_border;
  border-radius: 10px;
  color: @victus_text;
  padding: 4px;
}

popover > arrow {
  background-color: @victus_surface;
  border: 1px solid @victus_border;
}

/* The drawn keyboard sits in its own recessed well. */
.keyboard-stage {
  background-color: @victus_well;
  border: 1px solid @victus_border;
  border-radius: 10px;
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
  background-color: @victus_surface_alt;
  border: 1px solid @victus_border;
  border-radius: 6px;
  color: @victus_text;
}

tooltip label {
  color: @victus_text;
  font-size: 12px;
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
