#include <algorithm>
#include <cmath>
#include <cstdio>

#include "charts.hpp"

SeriesHistory::SeriesHistory(size_t capacity) : capacity_(capacity) {}

void SeriesHistory::push(double value, bool valid) {
    samples_.push_back({value, valid});
    while (samples_.size() > capacity_)
        samples_.pop_front();
}

void SeriesHistory::clear() { samples_.clear(); }

double SeriesHistory::latest_valid(bool *found) const {
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (it->valid) {
            if (found) *found = true;
            return it->value;
        }
    }
    if (found) *found = false;
    return 0.0;
}

double SeriesHistory::max_valid(double fallback) const {
    double top = fallback;
    for (const auto &sample : samples_) {
        if (sample.valid && sample.value > top)
            top = sample.value;
    }
    return top;
}

namespace {

// Ink colours, kept off the series hues: text never wears the series colour.
constexpr double kInkMuted[3]  = {0.49, 0.55, 0.64};
constexpr double kGridAlpha    = 0.10;

void set_ink(cairo_t *cr, const double rgb[3], double alpha) {
    cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
}

void draw_text(cairo_t *cr, double x, double y, const char *text, double size,
               bool bold) {
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
}

double text_width(cairo_t *cr, const char *text, double size) {
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    return extents.width;
}

} // namespace

void auto_y_range(const std::vector<ChartSeries> &series, double min_span,
                  double step, double floor_value,
                  double *y_min, double *y_max)
{
    bool found = false;
    double lo = 0.0, hi = 0.0;

    for (const auto &entry : series) {
        if (entry.history == nullptr || !entry.visible)
            continue;
        for (const auto &sample : entry.history->samples()) {
            if (!sample.valid)
                continue;
            if (!found) {
                lo = hi = sample.value;
                found = true;
            } else {
                lo = std::min(lo, sample.value);
                hi = std::max(hi, sample.value);
            }
        }
    }

    if (!found) {
        *y_min = floor_value;
        *y_max = floor_value + min_span;
        return;
    }

    double pad = std::max((hi - lo) * 0.15, min_span * 0.15);
    lo -= pad;
    hi += pad;

    // Widen symmetrically rather than stretching one end, so a flat trace sits
    // in the middle of the plot instead of hugging an edge.
    double span = hi - lo;
    if (span < min_span) {
        double extra = (min_span - span) / 2.0;
        lo -= extra;
        hi += extra;
    }

    lo = std::floor(lo / step) * step;
    hi = std::ceil(hi / step) * step;
    lo = std::max(lo, floor_value);
    if (hi - lo < step)
        hi = lo + step;

    *y_min = lo;
    *y_max = hi;
}

int chart_legend_index_at(const std::vector<ChartSeries> &series, double x, double y)
{
    for (size_t i = 0; i < series.size(); i++) {
        const auto &entry = series[i];
        if (entry.legend_w <= 0.0)
            continue;
        if (x >= entry.legend_x && x <= entry.legend_x + entry.legend_w &&
            y >= entry.legend_y && y <= entry.legend_y + entry.legend_h)
            return static_cast<int>(i);
    }
    return -1;
}

void draw_history_chart(cairo_t *cr, int width, int height,
                        std::vector<ChartSeries> &series,
                        double y_min, double y_max,
                        const std::string &unit,
                        bool draw_time_axis,
                        double window_seconds) {
    if (series.empty() || y_max <= y_min)
        return;

    // Room on the left for axis labels, on the right for the direct labels that
    // sit beside the latest point.
    const double pad_left = 34.0;
    const double pad_right = 58.0;
    const double pad_top = 8.0;
    const double pad_bottom = draw_time_axis ? 30.0 : 16.0;

    const double plot_x = pad_left;
    const double plot_y = pad_top;
    const double plot_w = width - pad_left - pad_right;
    const double plot_h = height - pad_top - pad_bottom;
    if (plot_w <= 4.0 || plot_h <= 4.0)
        return;

    auto value_to_y = [&](double value) {
        double t = (value - y_min) / (y_max - y_min);
        t = std::clamp(t, 0.0, 1.0);
        return plot_y + plot_h - t * plot_h;
    };

    // Grid: recessive, and labelled only at the ends so the numbers do not
    // compete with the data.
    cairo_set_line_width(cr, 1.0);
    for (int step = 0; step <= 4; step++) {
        double y = plot_y + (plot_h * step) / 4.0;
        set_ink(cr, kInkMuted, kGridAlpha);
        cairo_move_to(cr, plot_x, std::round(y) + 0.5);
        cairo_line_to(cr, plot_x + plot_w, std::round(y) + 0.5);
        cairo_stroke(cr);
    }

    char label[32];
    set_ink(cr, kInkMuted, 0.75);
    std::snprintf(label, sizeof(label), "%g", y_max);
    draw_text(cr, 2.0, plot_y + 8.0, label, 9.0, false);
    std::snprintf(label, sizeof(label), "%g", y_min);
    draw_text(cr, 2.0, plot_y + plot_h, label, 9.0, false);

    // The window is fixed by capacity, so a partly filled buffer draws from the
    // right and grows leftwards instead of stretching a few points across.
    size_t capacity = series.front().history ? series.front().history->capacity() : 0;
    if (capacity < 2)
        return;
    double step_x = plot_w / static_cast<double>(capacity - 1);

    // Latest points are gathered first so their direct labels can be nudged
    // apart: two series at nearly the same value would otherwise print their
    // numbers on top of each other.
    struct EndPoint {
        bool present = false;
        double x = 0.0, y = 0.0, label_y = 0.0, value = 0.0;
    };
    std::vector<EndPoint> ends(series.size());

    for (size_t index = 0; index < series.size(); index++) {
        const auto &entry = series[index];
        if (entry.history == nullptr || entry.history->empty() || !entry.visible)
            continue;

        const auto &samples = entry.history->samples();
        size_t offset = capacity - samples.size();

        cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        // Invalid samples break the path so a gap stays a gap.
        bool drawing = false;
        for (size_t i = 0; i < samples.size(); i++) {
            if (!samples[i].valid) {
                if (drawing) {
                    cairo_stroke(cr);
                    drawing = false;
                }
                continue;
            }
            double x = plot_x + (offset + i) * step_x;
            double y = value_to_y(samples[i].value);
            if (!drawing) {
                cairo_move_to(cr, x, y);
                drawing = true;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        if (drawing)
            cairo_stroke(cr);

        bool have_latest = false;
        double latest = entry.history->latest_valid(&have_latest);
        if (!have_latest)
            continue;

        ends[index].present = true;
        ends[index].value = latest;
        ends[index].x = plot_x + (capacity - 1) * step_x;
        ends[index].y = value_to_y(latest);
        ends[index].label_y = ends[index].y + 3.0;
    }

    // Push overlapping labels apart, keeping them inside the plot.
    const double kLabelGap = 11.0;
    for (size_t a = 0; a < ends.size(); a++) {
        if (!ends[a].present)
            continue;
        for (size_t b = a + 1; b < ends.size(); b++) {
            if (!ends[b].present)
                continue;
            double gap = ends[b].label_y - ends[a].label_y;
            if (std::fabs(gap) >= kLabelGap)
                continue;
            double shift = (kLabelGap - std::fabs(gap)) / 2.0 + 0.5;
            if (gap >= 0.0) {
                ends[a].label_y -= shift;
                ends[b].label_y += shift;
            } else {
                ends[a].label_y += shift;
                ends[b].label_y -= shift;
            }
        }
    }

    for (size_t index = 0; index < series.size(); index++) {
        if (!ends[index].present)
            continue;
        const auto &entry = series[index];

        cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
        cairo_arc(cr, ends[index].x, ends[index].y, 3.0, 0, 2 * M_PI);
        cairo_fill(cr);

        std::snprintf(label, sizeof(label), "%.0f%s", ends[index].value, unit.c_str());
        double label_y = std::clamp(ends[index].label_y, plot_y + 9.0, plot_y + plot_h);
        draw_text(cr, ends[index].x + 6.0, label_y, label, 10.0, true);
    }

    // Shared time axis, labelled once beneath the stack. Every chart uses the
    // same window and padding, so a column means the same instant in all of them.
    if (draw_time_axis) {
        set_ink(cr, kInkMuted, 0.75);
        int minutes = static_cast<int>(std::lround(window_seconds / 60.0));
        for (int tick = 0; tick <= 4; tick++) {
            double x = plot_x + (plot_w * tick) / 4.0;

            set_ink(cr, kInkMuted, kGridAlpha * 2.0);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, std::round(x) + 0.5, plot_y + plot_h);
            cairo_line_to(cr, std::round(x) + 0.5, plot_y + plot_h + 3.0);
            cairo_stroke(cr);

            if (tick == 4)
                std::snprintf(label, sizeof(label), "now");
            else
                std::snprintf(label, sizeof(label), "-%dm",
                              (minutes * (4 - tick)) / 4);

            set_ink(cr, kInkMuted, 0.75);
            double w = text_width(cr, label, 9.0);
            double lx = std::clamp(x - w / 2.0, 0.0, static_cast<double>(width) - w);
            draw_text(cr, lx, plot_y + plot_h + 13.0, label, 9.0, false);
        }
    }

    // Legend: clickable, so a series can be taken off the chart. A hidden one
    // keeps its place and dims rather than disappearing, so the control does
    // not vanish along with the data it toggles.
    double legend_x = plot_x;
    double legend_y = height - 4.0;
    for (auto &entry : series) {
        double label_w = text_width(cr, entry.label.c_str(), 9.0);
        double box_w = 12.0 + label_w + 6.0;

        entry.legend_x = legend_x - 3.0;
        entry.legend_y = legend_y - 11.0;
        entry.legend_w = box_w;
        entry.legend_h = 15.0;

        double alpha = entry.visible ? 1.0 : 0.35;
        cairo_set_source_rgba(cr, entry.r, entry.g, entry.b, alpha);
        if (entry.visible) {
            cairo_rectangle(cr, legend_x, legend_y - 6.0, 8.0, 3.0);
            cairo_fill(cr);
        } else {
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, legend_x + 0.5, legend_y - 6.5, 7.0, 3.0);
            cairo_stroke(cr);
        }

        set_ink(cr, kInkMuted, entry.visible ? 0.9 : 0.4);
        draw_text(cr, legend_x + 12.0, legend_y, entry.label.c_str(), 9.0, false);
        legend_x += box_w + 10.0;
    }
}

void draw_overlay_chart(cairo_t *cr, int width, int height,
                        std::vector<ChartSeries> &series,
                        double window_seconds)
{
    // Only visible series get an axis, so the gutter stays narrow and the
    // legend toggles control the clutter directly.
    std::vector<size_t> shown;
    for (size_t i = 0; i < series.size(); i++) {
        if (series[i].visible && series[i].history != nullptr &&
            !series[i].history->empty())
            shown.push_back(i);
    }

    const double axis_col = 38.0;
    const double pad_top = 10.0;
    const double pad_bottom = 42.0;   // time axis + legend
    // Wide enough for "<name> <value>" at the live end: colour alone is not
    // enough to tell eight overlaid traces apart.
    const double pad_right = 96.0;

    double gutter = std::max(axis_col, axis_col * static_cast<double>(shown.size()));
    double plot_x = gutter + 4.0;
    double plot_y = pad_top;
    double plot_w = width - plot_x - pad_right;
    double plot_h = height - pad_top - pad_bottom;
    if (plot_w <= 8.0 || plot_h <= 8.0)
        return;

    // Per-series range, computed once and reused by both the axis and the line.
    std::vector<double> lo(series.size(), 0.0), hi(series.size(), 1.0);
    for (size_t index : shown) {
        std::vector<ChartSeries> one{series[index]};
        auto_y_range(one, series[index].min_span, series[index].step,
                     series[index].floor_value, &lo[index], &hi[index]);
    }

    // Grid, drawn once for the shared plot.
    cairo_set_line_width(cr, 1.0);
    for (int step = 0; step <= 4; step++) {
        double y = plot_y + (plot_h * step) / 4.0;
        set_ink(cr, kInkMuted, kGridAlpha);
        cairo_move_to(cr, plot_x, std::round(y) + 0.5);
        cairo_line_to(cr, plot_x + plot_w, std::round(y) + 0.5);
        cairo_stroke(cr);
    }

    char label[48];

    // Parallel y axes: one column per visible series, in that series' colour so
    // a reader can tell at a glance which scale belongs to which line.
    for (size_t slot = 0; slot < shown.size(); slot++) {
        size_t index = shown[slot];
        const auto &entry = series[index];
        double x = 4.0 + slot * axis_col;

        cairo_set_source_rgba(cr, entry.r, entry.g, entry.b, 0.85);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, x + axis_col - 6.0, plot_y);
        cairo_line_to(cr, x + axis_col - 6.0, plot_y + plot_h);
        cairo_stroke(cr);

        for (int end = 0; end < 2; end++) {
            double value = (end == 0) ? hi[index] : lo[index];
            double y = (end == 0) ? plot_y + 7.0 : plot_y + plot_h;
            std::snprintf(label, sizeof(label), "%g", value);
            cairo_set_source_rgba(cr, entry.r, entry.g, entry.b, 0.95);
            draw_text(cr, x, y, label, 9.0, false);
        }
    }

    size_t capacity = series[shown.empty() ? 0 : shown.front()].history->capacity();
    if (capacity < 2)
        return;
    double step_x = plot_w / static_cast<double>(capacity - 1);

    struct EndPoint { bool present = false; double x = 0, y = 0, label_y = 0, value = 0; };
    std::vector<EndPoint> ends(series.size());

    for (size_t index : shown) {
        const auto &entry = series[index];
        const auto &samples = entry.history->samples();
        size_t offset = capacity - samples.size();
        double span = hi[index] - lo[index];
        if (span <= 0.0)
            continue;

        auto to_y = [&](double v) {
            double t = std::clamp((v - lo[index]) / span, 0.0, 1.0);
            return plot_y + plot_h - t * plot_h;
        };

        cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        bool drawing = false;
        for (size_t i = 0; i < samples.size(); i++) {
            if (!samples[i].valid) {
                if (drawing) { cairo_stroke(cr); drawing = false; }
                continue;
            }
            double x = plot_x + (offset + i) * step_x;
            double y = to_y(samples[i].value);
            if (!drawing) { cairo_move_to(cr, x, y); drawing = true; }
            else cairo_line_to(cr, x, y);
        }
        if (drawing)
            cairo_stroke(cr);

        bool have = false;
        double latest = entry.history->latest_valid(&have);
        if (!have)
            continue;
        ends[index].present = true;
        ends[index].value = latest;
        ends[index].x = plot_x + (capacity - 1) * step_x;
        ends[index].y = to_y(latest);
        ends[index].label_y = ends[index].y + 3.0;
    }

    // Overlaid lines collide far more often than separated ones, so the end
    // labels are spread rather than merely nudged.
    const double kLabelGap = 11.0;
    for (size_t a = 0; a < ends.size(); a++) {
        if (!ends[a].present) continue;
        for (size_t b = 0; b < ends.size(); b++) {
            if (a == b || !ends[b].present) continue;
            double gap = ends[b].label_y - ends[a].label_y;
            if (std::fabs(gap) >= kLabelGap) continue;
            double shift = (kLabelGap - std::fabs(gap)) / 2.0 + 0.5;
            if (gap >= 0.0) { ends[a].label_y -= shift; ends[b].label_y += shift; }
            else            { ends[a].label_y += shift; ends[b].label_y -= shift; }
        }
    }

    for (size_t index : shown) {
        if (!ends[index].present) continue;
        const auto &entry = series[index];
        cairo_set_source_rgb(cr, entry.r, entry.g, entry.b);
        cairo_arc(cr, ends[index].x, ends[index].y, 3.0, 0, 2 * M_PI);
        cairo_fill(cr);

        // Name first, then the reading: the label identifies the line without
        // the reader having to match a colour back to the legend.
        std::snprintf(label, sizeof(label), "%s %.0f%s", entry.label.c_str(),
                      ends[index].value, entry.unit.c_str());
        double ly = std::clamp(ends[index].label_y, plot_y + 9.0, plot_y + plot_h);
        draw_text(cr, ends[index].x + 6.0, ly, label, 9.5, true);
    }

    // The one shared axis: time.
    int minutes = static_cast<int>(std::lround(window_seconds / 60.0));
    for (int tick = 0; tick <= 4; tick++) {
        double x = plot_x + (plot_w * tick) / 4.0;
        set_ink(cr, kInkMuted, kGridAlpha * 2.0);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, std::round(x) + 0.5, plot_y + plot_h);
        cairo_line_to(cr, std::round(x) + 0.5, plot_y + plot_h + 3.0);
        cairo_stroke(cr);

        if (tick == 4) std::snprintf(label, sizeof(label), "now");
        else std::snprintf(label, sizeof(label), "-%dm", (minutes * (4 - tick)) / 4);

        set_ink(cr, kInkMuted, 0.75);
        double w = text_width(cr, label, 9.0);
        draw_text(cr, std::clamp(x - w / 2.0, 0.0, width - w),
                  plot_y + plot_h + 13.0, label, 9.0, false);
    }

    // Legend doubles as the on/off control for each trace and its axis.
    double legend_x = plot_x;
    double legend_y = height - 4.0;
    for (auto &entry : series) {
        double label_w = text_width(cr, entry.label.c_str(), 9.0);
        double box_w = 12.0 + label_w + 6.0;

        if (legend_x + box_w > width - 4.0) break;

        entry.legend_x = legend_x - 3.0;
        entry.legend_y = legend_y - 11.0;
        entry.legend_w = box_w;
        entry.legend_h = 15.0;

        cairo_set_source_rgba(cr, entry.r, entry.g, entry.b, entry.visible ? 1.0 : 0.35);
        if (entry.visible) {
            cairo_rectangle(cr, legend_x, legend_y - 6.0, 8.0, 3.0);
            cairo_fill(cr);
        } else {
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, legend_x + 0.5, legend_y - 6.5, 7.0, 3.0);
            cairo_stroke(cr);
        }

        set_ink(cr, kInkMuted, entry.visible ? 0.9 : 0.4);
        draw_text(cr, legend_x + 12.0, legend_y, entry.label.c_str(), 9.0, false);
        legend_x += box_w + 10.0;
    }
}
