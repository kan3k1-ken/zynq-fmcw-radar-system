"""Radar Operations Console desktop application."""

from __future__ import annotations

import os
import math
import tkinter as tk
from tkinter import messagebox, ttk

from radar_host.models import EventKind, ReceiverEvent
from radar_host.protocol import (
    ANGLE_MAP_HORIZONTAL,
    ANGLE_MAP_VERTICAL,
    Detection,
    ScanSummary,
    Status,
)
from radar_host.reporting import AngleHeatmap
from radar_host.service import RadarReceiverService
from radar_host.storage import SessionRecorder
from radar_host.visualization import (
    heat_color,
    integrated_angle_peak,
    planar_angles_from_bins,
    point_cloud_projection,
)


BG = "#17191b"
PANEL = "#202326"
PANEL_ALT = "#292d30"
BORDER = "#3a3f43"
TEXT = "#f1f3f4"
MUTED = "#a8adb2"
GREEN = "#48c78e"
AMBER = "#f2b84b"
RED = "#ef6b73"
CYAN = "#55b8c9"


class MetricTile(ttk.Frame):
    def __init__(self, parent: tk.Misc, title: str, unit: str) -> None:
        super().__init__(parent, style="Metric.TFrame", padding=(12, 9))
        self.columnconfigure(0, weight=1)
        self.columnconfigure(1, weight=0)
        ttk.Label(self, text=title, style="MetricTitle.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(self, text=unit, style="MetricUnit.TLabel").grid(
            row=0, column=1, sticky="e", padx=(4, 0)
        )
        self.value = ttk.Label(self, text="--", style="MetricValue.TLabel")
        self.value.grid(row=1, column=0, columnspan=2, sticky="w", pady=(5, 0))

    def set(self, value: str) -> None:
        self.value.configure(text=value)


class RadarMonitor:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Radar Operations Console")
        self.root.geometry("1440x880")
        self.root.minsize(1120, 720)
        self.root.configure(bg=BG)

        self.service: RadarReceiverService | None = None
        self.recorder = SessionRecorder()
        self.current_target: tuple[float, float] | None = None
        self.current_range: float | None = None
        self.current_target_estimated = False
        self.current_scan_id: int | None = None
        self.aoa_available = False
        self.heatmaps: dict[int, AngleHeatmap] = {}
        self.angle_estimates: dict[int, dict[int, int]] = {}
        self.point_cloud: list[tuple[float, float, float, float, bool, int]] = []
        self.scan_count = 0
        self.active_report_id: int | None = None
        self.report_conflicts = 0
        self._configure_styles()
        self._build_layout()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(100, self._poll_events)
        self.root.after(150, self.start_receiver)

    def _configure_styles(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, font=("Segoe UI", 10))
        style.configure("Main.TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("Metric.TFrame", background=PANEL_ALT, relief="solid", borderwidth=1)
        style.configure("Header.TLabel", background=BG, foreground=TEXT, font=("Segoe UI Semibold", 18))
        style.configure("Subtle.TLabel", background=BG, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("PanelTitle.TLabel", background=PANEL, foreground=TEXT, font=("Segoe UI Semibold", 11))
        style.configure("MetricTitle.TLabel", background=PANEL_ALT, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("MetricValue.TLabel", background=PANEL_ALT, foreground=TEXT, font=("Segoe UI Semibold", 20))
        style.configure("MetricUnit.TLabel", background=PANEL_ALT, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("Status.TLabel", background=BG, foreground=MUTED, font=("Consolas", 9))
        style.configure("TButton", background=PANEL_ALT, foreground=TEXT, borderwidth=1, padding=(12, 7))
        style.map("TButton", background=[("active", BORDER), ("pressed", "#44494d")])
        style.configure("Accent.TButton", background=GREEN, foreground="#101412")
        style.map("Accent.TButton", background=[("active", "#63d7a2")])
        style.configure("TEntry", fieldbackground="#101214", foreground=TEXT, insertcolor=TEXT, bordercolor=BORDER)
        style.configure("Treeview", background="#151719", fieldbackground="#151719", foreground=TEXT,
                        rowheight=27, borderwidth=0)
        style.configure("Treeview.Heading", background=PANEL_ALT, foreground=MUTED,
                        font=("Segoe UI Semibold", 9), relief="flat")
        style.map("Treeview", background=[("selected", "#375b62")])
        style.configure("TNotebook", background=PANEL, borderwidth=0)
        style.configure("TNotebook.Tab", background=PANEL_ALT, foreground=MUTED,
                        padding=(12, 6), borderwidth=0)
        style.map("TNotebook.Tab", background=[("selected", "#3a3f43")],
                  foreground=[("selected", TEXT)])

    def _build_layout(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        header = ttk.Frame(self.root, style="Main.TFrame", padding=(18, 13))
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)
        ttk.Label(header, text="Radar Operations Console", style="Header.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(header, text=f"Session  {self.recorder.session_id}", style="Subtle.TLabel").grid(
            row=1, column=0, sticky="w", pady=(2, 0)
        )

        controls = ttk.Frame(header, style="Main.TFrame")
        controls.grid(row=0, column=1, rowspan=2, sticky="e")
        ttk.Label(controls, text="Bind", style="Subtle.TLabel").pack(side="left", padx=(0, 6))
        self.bind_ip = tk.StringVar(value="0.0.0.0")
        ttk.Entry(controls, textvariable=self.bind_ip, width=13).pack(side="left")
        self.bind_port = tk.StringVar(value="9999")
        ttk.Entry(controls, textvariable=self.bind_port, width=7).pack(side="left", padx=(6, 10))
        self.start_button = ttk.Button(controls, text="Start", style="Accent.TButton", command=self.start_receiver)
        self.start_button.pack(side="left")
        ttk.Button(controls, text="Stop", command=self.stop_receiver).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="Clear", command=self.clear_view).pack(side="left", padx=(6, 0))
        ttk.Button(controls, text="Session Folder", command=self.open_session_folder).pack(side="left", padx=(6, 0))

        body = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg=BG, sashwidth=6,
                             sashrelief=tk.FLAT, bd=0)
        body.grid(row=1, column=0, sticky="nsew", padx=18, pady=(0, 10))

        overview = ttk.Frame(body, style="Panel.TFrame", padding=14)
        diagnostics = ttk.Frame(body, style="Panel.TFrame", padding=14)
        body.add(overview, minsize=680, stretch="always")
        body.add(diagnostics, minsize=420, stretch="always")
        overview.columnconfigure(0, weight=1)
        overview.rowconfigure(2, weight=3)
        overview.rowconfigure(4, weight=2)

        self.result_label = ttk.Label(overview, text="NO SCAN", style="PanelTitle.TLabel")
        self.result_label.grid(row=0, column=0, sticky="w", pady=(0, 10))

        metric_grid = ttk.Frame(overview, style="Panel.TFrame")
        metric_grid.grid(row=1, column=0, sticky="ew")
        for column in range(6):
            metric_grid.columnconfigure(column, weight=1, uniform="metric")
        definitions = (
            ("Range", "cm"), ("Azimuth", "deg"), ("Elevation", "deg"),
            ("SNR", "dB"), ("Detections", "count"), ("Saturated", "frames"),
        )
        self.metrics: dict[str, MetricTile] = {}
        for column, (title, unit) in enumerate(definitions):
            tile = MetricTile(metric_grid, title, unit)
            tile.grid(row=0, column=column, sticky="ew", padx=(0 if column == 0 else 4, 0))
            self.metrics[title] = tile

        visual_panel = ttk.Frame(overview, style="Panel.TFrame")
        visual_panel.grid(row=2, column=0, sticky="nsew", pady=(16, 0))
        visual_panel.columnconfigure(0, weight=1)
        visual_panel.rowconfigure(1, weight=1)
        ttk.Label(visual_panel, text="Range and Spatial Analysis", style="PanelTitle.TLabel").grid(
            row=0, column=0, sticky="w", pady=(0, 8)
        )
        visual_tabs = ttk.Notebook(visual_panel)
        visual_tabs.grid(row=1, column=0, sticky="nsew")

        top_view = ttk.Frame(visual_tabs, style="Panel.TFrame", padding=4)
        top_view.columnconfigure(0, weight=1)
        top_view.rowconfigure(0, weight=1)
        self.plot = tk.Canvas(top_view, bg="#111315", highlightthickness=1,
                              highlightbackground=BORDER)
        self.plot.grid(row=0, column=0, sticky="nsew")
        self.plot.bind("<Configure>", lambda _event: self._draw_plot())
        visual_tabs.add(top_view, text="Range View")

        heatmap_view = ttk.Frame(visual_tabs, style="Panel.TFrame", padding=4)
        heatmap_view.columnconfigure(0, weight=1, uniform="heatmap")
        heatmap_view.columnconfigure(1, weight=1, uniform="heatmap")
        heatmap_view.rowconfigure(1, weight=1)
        ttk.Label(heatmap_view, text="Horizontal / Azimuth", style="PanelTitle.TLabel").grid(
            row=0, column=0, sticky="w", padx=(2, 6), pady=(2, 6)
        )
        ttk.Label(heatmap_view, text="Vertical / Elevation", style="PanelTitle.TLabel").grid(
            row=0, column=1, sticky="w", padx=(6, 2), pady=(2, 6)
        )
        self.horizontal_heatmap = tk.Canvas(
            heatmap_view, bg="#111315", highlightthickness=1, highlightbackground=BORDER
        )
        self.vertical_heatmap = tk.Canvas(
            heatmap_view, bg="#111315", highlightthickness=1, highlightbackground=BORDER
        )
        self.horizontal_heatmap.grid(row=1, column=0, sticky="nsew", padx=(0, 4))
        self.vertical_heatmap.grid(row=1, column=1, sticky="nsew", padx=(4, 0))
        self.horizontal_heatmap.bind("<Configure>", lambda _event: self._draw_heatmaps())
        self.vertical_heatmap.bind("<Configure>", lambda _event: self._draw_heatmaps())
        self.angle_status = ttk.Label(
            heatmap_view,
            text="Awaiting complex AoA data / phase calibration pending",
            style="Subtle.TLabel",
        )
        self.angle_status.grid(row=2, column=0, columnspan=2, sticky="w", pady=(6, 0))
        visual_tabs.add(heatmap_view, text="AoA Heatmaps")

        cloud_view = ttk.Frame(visual_tabs, style="Panel.TFrame", padding=4)
        cloud_view.columnconfigure(0, weight=1)
        cloud_view.rowconfigure(0, weight=1)
        self.cloud = tk.Canvas(cloud_view, bg="#111315", highlightthickness=1,
                               highlightbackground=BORDER)
        self.cloud.grid(row=0, column=0, sticky="nsew")
        self.cloud.bind("<Configure>", lambda _event: self._draw_point_cloud())
        visual_tabs.add(cloud_view, text="3D Position")

        ttk.Separator(overview).grid(row=3, column=0, sticky="ew", pady=13)
        history_panel = ttk.Frame(overview, style="Panel.TFrame")
        history_panel.grid(row=4, column=0, sticky="nsew")
        history_panel.columnconfigure(0, weight=1)
        history_panel.rowconfigure(1, weight=1)
        ttk.Label(history_panel, text="Scan History", style="PanelTitle.TLabel").grid(
            row=0, column=0, sticky="w", pady=(0, 7)
        )
        columns = ("time", "scan", "result", "range", "az", "el", "snr", "peaks", "sat")
        self.history = ttk.Treeview(history_panel, columns=columns, show="headings", height=7)
        headings = ("Time", "Scan ID", "Result", "Range", "Az", "El", "SNR", "Peaks", "Sat")
        widths = (78, 105, 78, 74, 60, 60, 65, 65, 55)
        for column, heading, width in zip(columns, headings, widths):
            self.history.heading(column, text=heading)
            self.history.column(column, width=width, minwidth=45, anchor="center")
        self.history.grid(row=1, column=0, sticky="nsew")

        diagnostics.columnconfigure(0, weight=1)
        diagnostics.rowconfigure(2, weight=1)
        ttk.Label(diagnostics, text="Algorithm Analysis", style="PanelTitle.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        self.report_status = ttk.Label(diagnostics, text="Awaiting report", style="Subtle.TLabel")
        self.report_status.grid(row=1, column=0, sticky="w", pady=(4, 9))
        report_frame = ttk.Frame(diagnostics, style="Panel.TFrame")
        report_frame.grid(row=2, column=0, sticky="nsew")
        report_frame.columnconfigure(0, weight=1)
        report_frame.rowconfigure(0, weight=1)
        self.report = tk.Text(report_frame, bg="#101214", fg="#d7dbde", insertbackground=TEXT,
                              selectbackground="#375b62", relief="flat", wrap="none",
                              font=("Consolas", 9), padx=10, pady=10)
        self.report.tag_configure("section", foreground=GREEN, font=("Consolas", 9, "bold"))
        self.report.tag_configure("result", foreground="#ffe0a0", font=("Consolas", 9, "bold"))
        self.report.tag_configure("warning", foreground=AMBER)
        self.report.tag_configure("error", foreground=RED)
        self.report.tag_configure("muted", foreground=MUTED)
        report_scroll_y = ttk.Scrollbar(report_frame, orient="vertical", command=self.report.yview)
        report_scroll_x = ttk.Scrollbar(report_frame, orient="horizontal", command=self.report.xview)
        self.report.configure(yscrollcommand=report_scroll_y.set, xscrollcommand=report_scroll_x.set)
        self.report.grid(row=0, column=0, sticky="nsew")
        report_scroll_y.grid(row=0, column=1, sticky="ns")
        report_scroll_x.grid(row=1, column=0, sticky="ew")

        footer = ttk.Frame(self.root, style="Main.TFrame", padding=(18, 5, 18, 10))
        footer.grid(row=2, column=0, sticky="ew")
        footer.columnconfigure(1, weight=1)
        self.status_dot = tk.Canvas(footer, width=12, height=12, bg=BG, highlightthickness=0)
        self.status_dot.grid(row=0, column=0, padx=(0, 7))
        self.status_dot_id = self.status_dot.create_oval(2, 2, 10, 10, fill=MUTED, outline="")
        self.status_text = ttk.Label(footer, text="Stopped", style="Status.TLabel")
        self.status_text.grid(row=0, column=1, sticky="w")
        self.packet_text = ttk.Label(
            footer,
            text="Packets 0  |  Invalid 0  |  Integrity 0  |  Dropped 0",
            style="Status.TLabel",
        )
        self.packet_text.grid(row=0, column=2, sticky="e")

    def start_receiver(self) -> None:
        if self.service is not None and self.service.running:
            return
        try:
            port = int(self.bind_port.get())
            if not 1 <= port <= 65535:
                raise ValueError
        except ValueError:
            messagebox.showerror("Invalid Port", "Port must be between 1 and 65535.")
            return
        try:
            self.service = RadarReceiverService(self.bind_ip.get().strip(), port)
            self.service.start()
            self.start_button.state(["disabled"])
        except OSError as error:
            self._set_status(f"Listen failed: {error}", RED)
            messagebox.showerror("UDP Listen Failed", str(error))

    def stop_receiver(self) -> None:
        if self.service is not None:
            self.service.stop()
        self.start_button.state(["!disabled"])

    def _poll_events(self) -> None:
        if self.service is not None:
            for event in self.service.drain_events():
                self._handle_event(event)
            stats = self.service.stats()
            self.packet_text.configure(
                text=f"Packets {stats.packets_received}  |  Invalid {stats.invalid_packets}  |  "
                     f"Integrity {stats.integrity_warnings}  |  "
                     f"Dropped {stats.events_dropped}"
            )
        self.root.after(100, self._poll_events)

    def _handle_event(self, event: ReceiverEvent) -> None:
        if event.kind is EventKind.LISTENING:
            self._set_status(f"Listening on {event.message}", GREEN)
        elif event.kind is EventKind.STOPPED:
            self._set_status("Stopped", MUTED)
        elif event.kind is EventKind.WARNING:
            self._set_status(f"Protocol warning: {event.message}", RED)
            if event.report_id is not None and "duplicate payload mismatch" in (event.message or ""):
                if event.report_id != self.active_report_id:
                    self.active_report_id = event.report_id
                    self.report_conflicts = 0
                self.report_conflicts += 1
                self.report_status.configure(
                    text=f"Report {event.report_id} integrity warnings: {self.report_conflicts}"
                )
            self._append_report(f"\n[WARN] {event.message}\n")
        elif event.kind is EventKind.REPORT_PROGRESS and event.report_progress:
            if event.report_id != self.active_report_id:
                self.active_report_id = event.report_id
                self.report_conflicts = 0
            received, total = event.report_progress
            percent = received * 100 // total
            self.report_status.configure(
                text=f"Report {event.report_id}  {received}/{total} chunks  ({percent}%)"
            )
            self._set_status(f"Receiving algorithm report {percent}%", CYAN)
        elif event.kind is EventKind.REPORT and event.report_text is not None:
            if event.report_id != self.active_report_id:
                self.active_report_id = event.report_id
                self.report_conflicts = 0
            path = self.recorder.record_report(event.report_id or 0, event.report_text, event.received_at)
            self._render_report(event.report_text)
            self.report.xview_moveto(0.0)
            self.report.yview_moveto(0.0)
            self.root.after_idle(lambda: (self.report.xview_moveto(0.0), self.report.yview_moveto(0.0)))
            integrity = (
                f"integrity warnings {self.report_conflicts}"
                if self.report_conflicts else "integrity OK"
            )
            self.report_status.configure(
                text=f"Report {event.report_id} complete  |  {integrity}"
            )
            self._set_status(
                "Algorithm report complete" if not self.report_conflicts
                else "Algorithm report complete with integrity warnings",
                GREEN if not self.report_conflicts else RED,
            )
        elif event.kind is EventKind.HEATMAP_PROGRESS and event.heatmap_progress:
            received, total = event.heatmap_progress
            self._set_status(
                f"Receiving angle heatmap {received}/{total}", CYAN
            )
        elif event.kind is EventKind.HEATMAP and event.heatmap is not None:
            self._show_heatmap(event.heatmap, event)
        elif event.kind is EventKind.PACKET:
            if isinstance(event.packet, ScanSummary):
                self._show_summary(event.packet, event)
            elif isinstance(event.packet, Detection):
                self._show_detection(event.packet)
            elif isinstance(event.packet, Status):
                self._set_status(
                    f"Board status code={event.packet.code} session={event.packet.session_id}", CYAN
                )

    def _show_summary(self, summary: ScanSummary, event: ReceiverEvent) -> None:
        self.scan_count += 1
        self.recorder.record_summary(summary, event.received_at)
        degraded = summary.degraded or summary.saturated_frames > 0
        if not summary.target_valid:
            result = "NO TARGET"
            color = MUTED
        elif summary.cfar_confirmed and not degraded:
            result = "TARGET CONFIRMED"
            color = GREEN
        else:
            result = "RANGE CANDIDATE"
            color = AMBER
        self.result_label.configure(
            text=f"SCAN {self.scan_count:04d}  /  {result}  /  ID {summary.scan_id}",
            foreground=color,
        )
        self.metrics["Range"].set(f"{summary.range_cm:.1f}" if summary.target_valid else "--")
        self.current_scan_id = summary.scan_id
        self.heatmaps = {
            map_kind: heatmap
            for map_kind, heatmap in self.heatmaps.items()
            if heatmap.scan_id == summary.scan_id
        }
        self.aoa_available = summary.angle_available
        azimuth = summary.azimuth_deg
        elevation = summary.elevation_deg
        estimated = summary.angle_estimate and not summary.angle_valid
        prefix = "~" if estimated else ""
        if estimated:
            self.result_label.configure(
                text=f"SCAN {self.scan_count:04d}  /  {result}  /  ID {summary.scan_id}  /  ANGLE EST."
            )
        self.metrics["Azimuth"].set(f"{prefix}{azimuth:.1f}" if summary.angle_available else "--")
        self.metrics["Elevation"].set(f"{prefix}{elevation:.1f}" if summary.angle_available else "--")
        self.metrics["SNR"].set(f"{summary.snr_db:.1f}")
        self.metrics["Detections"].set(str(summary.peak_count))
        self.metrics["Saturated"].set(str(summary.saturated_frames))
        self.current_range = summary.range_cm if summary.target_valid else None
        if not summary.angle_available:
            self.heatmaps.clear()
            self.angle_estimates.pop(summary.scan_id, None)
            self.angle_status.configure(
                text="AoA unavailable: spatial quality gate not satisfied"
            )
        elif estimated:
            self.angle_status.configure(
                text="Complex AoA estimate available / phase calibration pending"
            )
        else:
            self.angle_status.configure(text="Calibrated complex AoA valid")
        if summary.target_valid:
            if summary.angle_available:
                azimuth_rad = math.radians(azimuth)
                elevation_rad = math.radians(elevation)
                cos_elevation = math.cos(elevation_rad)
                target_x = summary.range_cm * cos_elevation * math.sin(azimuth_rad)
                target_y = summary.range_cm * cos_elevation * math.cos(azimuth_rad)
                target_z = summary.range_cm * math.sin(elevation_rad)
            else:
                target_x = target_y = target_z = 0.0
            if summary.angle_available:
                self.current_target = (target_x, target_y)
                self.current_target_estimated = estimated
                self.point_cloud.append((
                    target_x, target_y, target_z, summary.snr_db,
                    estimated, summary.scan_id,
                ))
                self.point_cloud = self.point_cloud[-200:]
            else:
                self.current_target = None
                self.current_target_estimated = False
        else:
            self.current_target = None
            self.current_target_estimated = False
        self._draw_plot()
        self._draw_heatmaps()
        self._draw_point_cloud()
        history_result = (
            "CONFIRMED" if result == "TARGET CONFIRMED"
            else "CANDIDATE" if result == "RANGE CANDIDATE"
            else "NONE"
        )
        values = (
            event.received_at.strftime("%H:%M:%S"), summary.scan_id, history_result,
            f"{summary.range_cm:.1f}" if summary.target_valid else "--",
            f"{prefix}{azimuth:.1f}" if summary.angle_available else "--",
            f"{prefix}{elevation:.1f}" if summary.angle_available else "--",
            f"{summary.snr_db:.1f}",
            summary.peak_count, summary.saturated_frames,
        )
        self.history.insert("", 0, values=values)
        children = self.history.get_children()
        for item in children[100:]:
            self.history.delete(item)
        quality = "DEGRADED" if degraded else "VALID"
        self._set_status(
            f"Scan {summary.scan_id}: {result} / quality {quality} / "
            f"AoA {'valid' if summary.angle_valid else 'estimate' if summary.angle_estimate else 'unavailable'}",
            color,
        )

    def _show_detection(self, detection: Detection) -> None:
        self.current_range = detection.range_cm
        self.current_target = (detection.x_cm, detection.y_cm)
        self.current_target_estimated = False
        self.metrics["Range"].set(f"{detection.range_cm:.1f}")
        self.metrics["Azimuth"].set(f"{detection.azimuth_deg:.1f}")
        self.metrics["Elevation"].set(f"{detection.elevation_deg:.1f}")
        self.metrics["SNR"].set(f"{detection.snr_db:.1f}")
        self._draw_plot()

    def _draw_plot(self) -> None:
        canvas = self.plot
        canvas.delete("all")
        width = max(canvas.winfo_width(), 400)
        height = max(canvas.winfo_height(), 260)
        left, right, top, bottom = 52, width - 24, 28, height - 42
        center_x = (left + right) / 2
        max_range = 120.0

        for distance in (30, 60, 90, 120):
            y = bottom - (distance / max_range) * (bottom - top)
            canvas.create_line(left, y, right, y, fill="#303438", dash=(3, 5))
            canvas.create_text(left - 7, y, text=f"{distance}", fill=MUTED,
                               anchor="e", font=("Consolas", 8))
        cross_ranges = (-120, -60, 0, 60, 120)
        for index, cross_range in enumerate(cross_ranges):
            x = center_x + (cross_range / (2 * max_range)) * (right - left)
            canvas.create_line(x, top, x, bottom, fill="#292d30")
            anchor = "w" if index == 0 else "e" if index == len(cross_ranges) - 1 else "center"
            canvas.create_text(x, bottom + 14, text=str(cross_range), fill=MUTED,
                               anchor=anchor, font=("Consolas", 8))
        canvas.create_text(left, 10, text="Down-range Y (cm)", fill=MUTED,
                           anchor="w", font=("Segoe UI", 8))
        canvas.create_text(center_x, height - 10, text="Cross-range X (cm)", fill=MUTED,
                           anchor="center", font=("Segoe UI", 8))
        canvas.create_polygon(center_x, bottom - 9, center_x - 7, bottom + 3,
                              center_x + 7, bottom + 3, fill=CYAN, outline="")

        if self.current_target is not None:
            target_x, target_y = self.current_target
            x = center_x + (target_x / (2 * max_range)) * (right - left)
            y = bottom - (target_y / max_range) * (bottom - top)
            x = min(max(x, left), right)
            y = min(max(y, top), bottom)
            canvas.create_line(center_x, bottom, x, y, fill="#7a6437", dash=(4, 4))
            canvas.create_oval(x - 7, y - 7, x + 7, y + 7, fill=AMBER, outline="#ffe0a0", width=2)
            canvas.create_text(x + 11, y - 10, text=f"X {target_x:.1f}  Y {target_y:.1f}",
                               fill=TEXT, anchor="sw", font=("Consolas", 9))
            if self.current_target_estimated:
                canvas.create_text(x + 11, y + 8, text="ANGLE EST.", fill=AMBER,
                                   anchor="nw", font=("Segoe UI Semibold", 8))
        elif self.current_range is not None:
            radius_x = (self.current_range / (2 * max_range)) * (right - left)
            radius_y = (self.current_range / max_range) * (bottom - top)
            canvas.create_arc(
                center_x - radius_x, bottom - radius_y,
                center_x + radius_x, bottom + radius_y,
                start=0, extent=180, style="arc", outline=AMBER,
                width=2, dash=(5, 4),
            )
            label_y = max(top + 18, bottom - radius_y - 10)
            canvas.create_text(
                center_x, label_y,
                text=f"Range candidate {self.current_range:.1f} cm / bearing unavailable",
                fill="#ffe0a0", anchor="s", font=("Consolas", 9),
            )

    def _show_heatmap(self, heatmap: AngleHeatmap, event: ReceiverEvent) -> None:
        self.aoa_available = True
        self.heatmaps[heatmap.map_kind] = heatmap
        self.recorder.record_heatmap(heatmap, event.received_at)
        peak = integrated_angle_peak(heatmap)
        estimates = self.angle_estimates.setdefault(heatmap.scan_id, {})
        estimates[heatmap.map_kind] = peak.bin_index
        for scan_id in list(self.angle_estimates)[:-20]:
            del self.angle_estimates[scan_id]
        if heatmap.scan_id != self.current_scan_id:
            if (
                ANGLE_MAP_HORIZONTAL in estimates and
                ANGLE_MAP_VERTICAL in estimates
            ):
                angles = planar_angles_from_bins(
                    estimates[ANGLE_MAP_HORIZONTAL],
                    estimates[ANGLE_MAP_VERTICAL],
                    heatmap.cols,
                )
                if angles is None:
                    self.metrics["Azimuth"].set("--")
                    self.metrics["Elevation"].set("--")
                    self.angle_status.configure(
                        text=f"Scan {heatmap.scan_id} / non-physical direction-cosine pair"
                    )
                else:
                    azimuth, elevation = angles
                    self.metrics["Azimuth"].set(f"~{azimuth:.1f}")
                    self.metrics["Elevation"].set(f"~{elevation:.1f}")
                    self.angle_status.configure(
                        text=(
                            f"Scan {heatmap.scan_id} / range bin {heatmap.range_bin} / "
                            f"planar estimate Az {azimuth:+.1f} deg / "
                            f"El {elevation:+.1f} deg / UNCONFIRMED"
                        )
                    )
            else:
                axis = "horizontal" if heatmap.map_kind == ANGLE_MAP_HORIZONTAL else "vertical"
                self.angle_status.configure(
                    text=(
                        f"Scan {heatmap.scan_id} / {axis} spatial spectrum received / "
                        "awaiting orthogonal axis"
                    )
                )
        self._draw_heatmaps()
        self._set_status(f"Angle heatmap received for scan {heatmap.scan_id}", GREEN)

    def _draw_heatmaps(self) -> None:
        self._draw_heatmap(
            self.horizontal_heatmap,
            self.heatmaps.get(ANGLE_MAP_HORIZONTAL),
            "scan row",
        )
        self._draw_heatmap(
            self.vertical_heatmap,
            self.heatmaps.get(ANGLE_MAP_VERTICAL),
            "scan column",
        )

    def _draw_heatmap(
        self, canvas: tk.Canvas, heatmap: AngleHeatmap | None, row_label: str
    ) -> None:
        canvas.delete("all")
        width = max(canvas.winfo_width(), 280)
        height = max(canvas.winfo_height(), 190)
        if heatmap is None:
            message = "Awaiting complex AoA map"
            canvas.create_text(
                width / 2, height / 2, text=message, fill=MUTED,
                justify="center",
                font=("Segoe UI", 10),
            )
            return
        left, top, right, bottom = 38, 12, width - 10, height - 34
        cell_width = (right - left) / heatmap.cols
        cell_height = (bottom - top) / heatmap.rows
        half = heatmap.cols // 2
        for row in range(heatmap.rows):
            base = row * heatmap.cols
            y0 = top + row * cell_height
            y1 = top + (row + 1) * cell_height + 0.5
            for display_column in range(heatmap.cols):
                source_column = (display_column + half) % heatmap.cols
                value = heatmap.data[base + source_column]
                x0 = left + display_column * cell_width
                x1 = left + (display_column + 1) * cell_width + 0.5
                canvas.create_rectangle(
                    x0, y0, x1, y1, fill=heat_color(value), outline=""
                )
        peak = integrated_angle_peak(heatmap)
        display_peak = (peak.bin_index - half) % heatmap.cols
        peak_x = left + (display_peak + 0.5) * cell_width
        canvas.create_line(peak_x, top, peak_x, bottom, fill="#ffffff", width=1)
        for fraction, label in ((0.0, "-90"), (0.5, "0"), (1.0, "+90")):
            x = left + fraction * (right - left)
            anchor = "w" if fraction == 0.0 else "e" if fraction == 1.0 else "center"
            canvas.create_text(x, bottom + 12, text=label, fill=MUTED,
                               anchor=anchor, font=("Consolas", 8))
        canvas.create_text((left + right) / 2, height - 8, text="angle (deg)",
                           fill=MUTED, font=("Segoe UI", 8))
        canvas.create_text(5, (top + bottom) / 2, text=row_label, fill=MUTED,
                           anchor="w", angle=90, font=("Segoe UI", 8))

    def _draw_point_cloud(self) -> None:
        canvas = self.cloud
        canvas.delete("all")
        width = max(canvas.winfo_width(), 400)
        height = max(canvas.winfo_height(), 250)
        projection = point_cloud_projection(width, height)
        project = projection.project
        origin_x, origin_y = project(0.0, 0.0, 0.0)

        for y_value in (0, 30, 60, 90, 120):
            start = project(-120, y_value, 0)
            end = project(120, y_value, 0)
            canvas.create_line(*start, *end, fill="#303438")
        for x_value in (-120, -60, 0, 60, 120):
            start = project(x_value, 0, 0)
            end = project(x_value, 120, 0)
            canvas.create_line(*start, *end, fill="#292d30")
        x_axis = project(110, 0, 0)
        y_axis = project(0, 120, 0)
        positive_z_axis = project(0, 0, 110)
        negative_z_axis = project(0, 0, -110)
        canvas.create_line(origin_x, origin_y, *x_axis, fill=CYAN, width=2)
        canvas.create_line(origin_x, origin_y, *y_axis, fill=GREEN, width=2)
        canvas.create_line(origin_x, origin_y, *positive_z_axis, fill=AMBER, width=2)
        canvas.create_line(
            origin_x, origin_y, *negative_z_axis, fill=AMBER, width=2, dash=(4, 3)
        )
        canvas.create_text(*x_axis, text=" X", fill=CYAN, anchor="w")
        canvas.create_text(*y_axis, text=" Y", fill=GREEN, anchor="e")
        canvas.create_text(*positive_z_axis, text=" +Z", fill=AMBER, anchor="s")
        canvas.create_text(*negative_z_axis, text=" -Z", fill=AMBER, anchor="n")

        if not self.point_cloud:
            message = "Awaiting 3D position\nSpatial quality and a range target are required"
            canvas.create_text(width / 2, height / 2, text=message,
                               justify="center",
                               fill=MUTED, font=("Segoe UI", 10))
            return
        latest = self.point_cloud[-1]
        latest_ground = project(latest[0], latest[1], 0.0)
        latest_point = project(latest[0], latest[1], latest[2])
        canvas.create_line(
            *latest_ground, *latest_point, fill=BORDER, width=1, dash=(3, 3)
        )
        canvas.create_oval(
            latest_ground[0] - 2, latest_ground[1] - 2,
            latest_ground[0] + 2, latest_ground[1] + 2,
            fill=BORDER, outline="",
        )
        for x_value, y_value, z_value, snr, estimated, scan_id in self.point_cloud:
            point_x, point_y = project(x_value, y_value, z_value)
            radius = max(4.0, min(8.0, 4.0 + snr / 8.0))
            color = AMBER if estimated else GREEN
            canvas.create_oval(
                point_x - radius, point_y - radius,
                point_x + radius, point_y + radius,
                fill=color, outline="#ffffff" if scan_id == self.point_cloud[-1][5] else "",
            )
        latest_color = AMBER if latest[4] else GREEN
        halo_radius = max(9.0, min(13.0, 9.0 + latest[3] / 10.0))
        canvas.create_oval(
            latest_point[0] - halo_radius, latest_point[1] - halo_radius,
            latest_point[0] + halo_radius, latest_point[1] + halo_radius,
            outline=latest_color, width=2,
        )
        canvas.create_line(
            latest_point[0] - halo_radius - 4, latest_point[1],
            latest_point[0] + halo_radius + 4, latest_point[1],
            fill=latest_color,
        )
        canvas.create_line(
            latest_point[0], latest_point[1] - halo_radius - 4,
            latest_point[0], latest_point[1] + halo_radius + 4,
            fill=latest_color,
        )
        label_anchor = "e" if latest_point[0] > width - 170 else "w"
        label_offset = -halo_radius - 8 if label_anchor == "e" else halo_radius + 8
        canvas.create_text(
            latest_point[0] + label_offset, latest_point[1],
            text=f"SCAN {latest[5]}", fill=latest_color,
            anchor=label_anchor, font=("Consolas", 8),
        )
        canvas.create_text(
            12, 12,
            text=(f"Latest: X {latest[0]:.1f} / Y {latest[1]:.1f} / Z {latest[2]:.1f} cm"
                  + (" / ANGLE EST." if latest[4] else "")),
            fill=TEXT, anchor="nw", font=("Consolas", 9),
        )

    def _set_status(self, text: str, color: str) -> None:
        self.status_text.configure(text=text)
        self.status_dot.itemconfigure(self.status_dot_id, fill=color)

    def _append_report(self, text: str) -> None:
        self.report.insert(tk.END, text)
        self.report.see(tk.END)

    def _render_report(self, text: str) -> None:
        self.report.delete("1.0", tk.END)
        for line in text.splitlines(keepends=True):
            stripped = line.strip()
            tag = None
            if stripped.startswith("====="):
                tag = "section"
            elif "ERROR" in stripped or "FAILED" in stripped:
                tag = "error"
            elif "DEGRADED" in stripped or "WARNING" in stripped:
                tag = "warning"
            elif stripped.startswith("Target") or stripped.startswith("Result"):
                tag = "result"
            elif stripped.startswith("Use:"):
                tag = "muted"
            if tag is None:
                self.report.insert(tk.END, line)
            else:
                self.report.insert(tk.END, line, tag)

    def clear_view(self) -> None:
        self.report.delete("1.0", tk.END)
        for item in self.history.get_children():
            self.history.delete(item)
        self.current_target = None
        self.current_range = None
        self.current_target_estimated = False
        self.current_scan_id = None
        self.aoa_available = False
        self.heatmaps.clear()
        self.angle_estimates.clear()
        self.point_cloud.clear()
        self.angle_status.configure(
            text="Awaiting complex AoA data / phase calibration pending"
        )
        self._draw_plot()
        self._draw_heatmaps()
        self._draw_point_cloud()

    def open_session_folder(self) -> None:
        os.startfile(self.recorder.directory.resolve())

    def _on_close(self) -> None:
        if self.service is not None:
            self.service.stop()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    RadarMonitor(root)
    root.mainloop()


if __name__ == "__main__":
    main()
