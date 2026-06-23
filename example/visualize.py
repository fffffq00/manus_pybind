#!/usr/bin/env python3
"""
Manus SDK Python Data Visualizer (v3.1.1)
================================================
Provides real-time interactive 3D visualization of Manus glove data.
Optimized for Manus SDK v3.1 Standard 21-joints Hand Model topology to fix
the skeletal distortion of the Ring and Pinky fingers.

Requirements:
    pip install matplotlib

Usage:
    python3 visualize.py [--ergo] [--host IP_ADDRESS] [--integrated]
"""

import sys
import os
import time
import argparse

# Add current folder to path to import the compiled module
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import manus_pybind

# Check for matplotlib dependency
try:
    import matplotlib
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    from mpl_toolkits.mplot3d import Axes3D
except ImportError:
    print("Error: matplotlib is required for this visualization.")
    print("Please install it by running: pip install matplotlib")
    sys.exit(1)


# Helper definitions mapping joint indices for ergonomics
FINGER_NAMES = ["Thumb", "Index", "Middle", "Ring", "Pinky"]

# ==================== v3.1 TOPOLOGY OPTIMIZATION ====================
# Disconnect the wrist node (0) from being the direct start of all 5 fingers.
# Each finger path now represents its true anatomical chain.
# Thumb: CMC(1) -> MCP(2) -> IP(3) -> Tip(4)
# Index: Metacarpal(5) -> MCP(6) -> PIP(7) -> DIP(8) -> Tip(9)
# ...
FINGER_PATHS = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12], [13, 14, 15, 16], [17, 18, 19, 20]]  # Thumb  # Index  # Middle  # Ring  # Pinky
# ====================================================================


class ManusVisualizer:
    def __init__(self, plot_ergo=False, host_ip="", use_integrated=False):
        self.plot_ergo = plot_ergo
        self.host_ip = host_ip
        self.connection_type = 1 if use_integrated else 2  # 1: Integrated, 2: Local/Remote

        self.client = manus_pybind.ManusClient()
        self.fig = None
        self.ani = None
        self.skeleton_hand_types = {}  # Tracks stable left/right glove assignment
        self.last_seen_times = {"Left": 0.0, "Right": 0.0}  # Timestamps for grace period
        self.active_skeleton_ids = {"Left": None, "Right": None}  # Currently active IDs

    def start_connection(self):
        print("Initializing Manus Core SDK (v3.1)...")
        if not self.client.initialize(connection_type=self.connection_type):
            print("Failed to initialize Manus SDK.")
            return False

        if self.connection_type != 1:
            print("Scanning for Manus Core hosts...")
            hosts = self.client.look_for_hosts(seconds=1, local_only=(self.host_ip == ""))
            if not hosts:
                print("No active Manus Core hosts found. Make sure Manus Core is running.")
                self.client.disconnect()
                return False

            target_ip = self.host_ip
            if not target_ip:
                target_ip = hosts[0].ip_address
                print(f"Auto-selected host: {hosts[0].hostname} ({target_ip})")

            print(f"Connecting to host: {target_ip}...")
            if not self.client.connect_to_host(ip_address=target_ip, connection_type=self.connection_type):
                print("Failed to connect to host.")
                self.client.disconnect()
                return False
        else:
            print("Running in Core Integrated mode...")
            if not self.client.connect_to_host(connection_type=1):
                print("Failed to initialize Integrated session.")
                self.client.disconnect()
                return False

        print("Connected and streaming data!")
        return True

    def setup_ergonomics_plot(self):
        """Set up 2D bar chart layout for left/right glove joint stretch angles with a premium dark theme."""
        self.fig, self.axes = plt.subplots(2, 5, figsize=(15, 8), sharey=True)
        self.fig.patch.set_facecolor("#0f172a")
        self.fig.suptitle("Manus Gloves Ergonomics Real-time Joint Stretch Angles", color="#f8fafc", fontsize=18, fontweight="bold", y=0.96)

        self.bars = []
        for hand_idx, hand_name in enumerate(["Left Hand", "Right Hand"]):
            for finger_idx, finger_name in enumerate(FINGER_NAMES):
                ax = self.axes[hand_idx, finger_idx]
                ax.set_facecolor("#0f172a")
                ax.set_title(f"{hand_name} - {finger_name}", color="#f1f5f9", fontsize=10, fontweight="semibold")
                ax.set_ylim(-30, 120)
                ax.grid(axis="y", linestyle=":", color="#334155", alpha=0.6)

                ax.tick_params(colors="#64748b", labelsize=8)
                for spine in ax.spines.values():
                    spine.set_color("#334155")

                x_labels = ["Spread", "MCP", "PIP", "DIP"]
                colors = ["#38bdf8", "#a855f7", "#ec4899", "#f43f5e"]
                rects = ax.bar(x_labels, [0, 0, 0, 0], color=colors, edgecolor="#1e293b", linewidth=0.5)
                self.bars.append((hand_idx, finger_idx, rects))

        plt.tight_layout()

    def update_ergonomics_plot(self, frame):
        """Timer callback to fetch data and refresh bar heights."""
        if not self.client.is_connected():
            return []

        ergo_list = self.client.get_ergonomics_data()
        left_data, right_data = None, None

        for ergo in ergo_list:
            if len(ergo.data) >= 40:
                left_data = ergo.data[0:20]
                right_data = ergo.data[20:40]

        updated_artists = []
        for hand_idx, finger_idx, rects in self.bars:
            data = left_data if hand_idx == 0 else right_data
            if data is not None and len(data) >= 20:
                offset = finger_idx * 4
                joint_vals = [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]]
                for r, val in zip(rects, joint_vals):
                    r.set_height(val)
                    updated_artists.append(r)
            else:
                for r in rects:
                    r.set_height(0)
                    updated_artists.append(r)

        return updated_artists

    def setup_skeleton_plot(self):
        """Set up side-by-side 3D plots to visualize Left and Right hand skeletons."""
        self.fig = plt.figure(figsize=(15, 8))
        self.fig.patch.set_facecolor("#0f172a")

        self.ax_left = self.fig.add_subplot(121, projection="3d")
        self.ax_left.set_facecolor("#0f172a")
        self.ax_left.set_title("Left Hand Skeleton", color="#f1f5f9", fontsize=14, fontweight="bold", pad=10)

        self.ax_right = self.fig.add_subplot(122, projection="3d")
        self.ax_right.set_facecolor("#0f172a")
        self.ax_right.set_title("Right Hand Skeleton", color="#f1f5f9", fontsize=14, fontweight="bold", pad=10)

        for ax in [self.ax_left, self.ax_right]:
            ax.set_xlabel("X (m)", color="#64748b", fontsize=9)
            ax.set_ylabel("Y (m)", color="#64748b", fontsize=9)
            ax.set_zlabel("Z (m)", color="#64748b", fontsize=9)
            ax.tick_params(colors="#64748b", labelsize=8)
            ax.view_init(elev=25, azim=-45)

            ax.xaxis.set_pane_color((0.0, 0.0, 0.0, 0.0))
            ax.yaxis.set_pane_color((0.0, 0.0, 0.0, 0.0))
            ax.zaxis.set_pane_color((0.0, 0.0, 0.0, 0.0))

            ax.grid(True, color="#334155", linestyle=":", alpha=0.5)
            ax.xaxis.line.set_color("#334155")
            ax.yaxis.line.set_color("#334155")
            ax.zaxis.line.set_color("#334155")

            ax.set_xlim3d([-0.16, 0.16])
            ax.set_ylim3d([-0.16, 0.16])
            ax.set_zlim3d([-0.16, 0.16])

            # Wrist Base Marker at (0, 0, 0)
            ax.plot([0], [0], [0], marker="o", markersize=10, color="#eab308", markeredgecolor="white", label="Wrist")

        self.fig.suptitle("Manus Gloves 3D Hand Skeletons - Real-time Visualization", color="#f8fafc", fontsize=18, fontweight="bold")
        self.fig.tight_layout(rect=[0, 0.03, 1, 0.95])

        # Extended key tracking for structure elements:
        # part_idx: 0..4 (fingers), 5 (knuckle), 6 (palm base), 7 (wrist anchor thumb), 8 (wrist anchor index)
        self.skeleton_artists = {}

        self.hud_texts = {
            "Left": self.ax_left.text2D(0.05, 0.95, "STATUS: DISCONNECTED", transform=self.ax_left.transAxes, color="#ef4444", fontsize=9, family="monospace", verticalalignment="top"),
            "Right": self.ax_right.text2D(0.05, 0.95, "STATUS: DISCONNECTED", transform=self.ax_right.transAxes, color="#ef4444", fontsize=9, family="monospace", verticalalignment="top"),
        }

        self.no_glove_messages = {
            "Left": self.ax_left.text(
                0,
                0,
                0,
                "NO LEFT GLOVE\nDETECTED",
                color="#ef4444",
                ha="center",
                va="center",
                fontsize=12,
                fontweight="bold",
                bbox=dict(facecolor="#1e293b", alpha=0.8, edgecolor="#ef4444", boxstyle="round,pad=0.8"),
            ),
            "Right": self.ax_right.text(
                0,
                0,
                0,
                "NO RIGHT GLOVE\nDETECTED",
                color="#ef4444",
                ha="center",
                va="center",
                fontsize=12,
                fontweight="bold",
                bbox=dict(facecolor="#1e293b", alpha=0.8, edgecolor="#ef4444", boxstyle="round,pad=0.8"),
            ),
        }

    def rotate_vector_by_quaternion(self, v, q):
        qw, qx, qy, qz = q
        vx, vy, vz = v
        tx = 2.0 * (qy * vz - qz * vy + qw * vx)
        ty = 2.0 * (qz * vx - qx * vz + qw * vy)
        tz = 2.0 * (qx * vy - qy * vx + qw * vz)
        rx = vx + qy * tz - qz * ty
        ry = vy + qz * tx - qx * tz
        rz = vz + qx * ty - qy * tx
        return (rx, ry, rz)

    def get_local_coords(self, wrist_node, target_node):
        wp = wrist_node.transform.position
        wq = wrist_node.transform.rotation
        rel_pos = (target_node.transform.position.x - wp.x, target_node.transform.position.y - wp.y, target_node.transform.position.z - wp.z)
        wq_conj = (wq.w, -wq.x, -wq.y, -wq.z)
        return self.rotate_vector_by_quaternion(rel_pos, wq_conj)

    def _update_line_artist(self, skeleton_id, part_idx, ax, x_data, y_data, z_data, active_keys, is_custom_style=False, **kwargs):
        """Helper to safely manage, reuse, or create line artists dynamically."""
        key = (skeleton_id, part_idx)
        active_keys.add(key)
        if key in self.skeleton_artists:
            line = self.skeleton_artists[key]
            line.set_data(x_data, y_data)
            line.set_3d_properties(z_data)
        else:
            if is_custom_style:
                (line,) = ax.plot(x_data, y_data, z_data, **kwargs)
            else:
                (line,) = ax.plot(x_data, y_data, z_data, marker="o", linewidth=3.5, markersize=5.5, markerfacecolor="white", markeredgewidth=1.2, **kwargs)
            self.skeleton_artists[key] = line

    def update_skeleton_plot(self, frame):
        """Timer callback to fetch and plot 3D skeleton joint positions with dynamic tracking."""
        if not self.client.is_connected():
            return []

        skeletons = self.client.get_skeletons() or self.client.get_raw_skeletons()
        current_time = time.time()
        active_artist_keys = set()

        if skeletons:
            for skeleton in skeletons:
                nodes = skeleton.nodes
                if not nodes or len(nodes) < 21:
                    continue

                nodes_by_id = {node.id: node for node in nodes}

                # Determine Layout Parameters dynamically (21-node vs 25-node hand skeleton)
                node_count = len(nodes)
                if node_count >= 25:
                    # 25-node layout: includes metacarpal joints for fingers, starting from wrist (0)
                    finger_paths = [
                        [0, 1, 2, 3, 4],            # Thumb (Wrist -> CMC -> MCP -> IP -> Tip)
                        [0, 5, 6, 7, 8, 9],         # Index (Wrist -> Metacarpal -> MCP -> PIP -> DIP -> Tip)
                        [0, 10, 11, 12, 13, 14],    # Middle (Wrist -> Metacarpal -> MCP -> PIP -> DIP -> Tip)
                        [0, 15, 16, 17, 18, 19],    # Ring (Wrist -> Metacarpal -> MCP -> PIP -> DIP -> Tip)
                        [0, 20, 21, 22, 23, 24]     # Pinky (Wrist -> Metacarpal -> MCP -> PIP -> DIP -> Tip)
                    ]
                    # Connect finger bases (Metacarpals) to outline palm width
                    palm_base_ids = [5, 10, 15, 20]
                    # Connect knuckles (MCPs) of the 4 fingers only (excl. Thumb 2 to avoid palm crossing lines)
                    knuckle_path = [6, 11, 16, 21]
                    pinky_idx = 21
                    index_idx = 6
                else:
                    # 21-node layout, starting from wrist (0)
                    finger_paths = [
                        [0, 1, 2, 3, 4],            # Thumb
                        [0, 5, 6, 7, 8],            # Index
                        [0, 9, 10, 11, 12],         # Middle
                        [0, 13, 14, 15, 16],        # Ring
                        [0, 17, 18, 19, 20]         # Pinky
                    ]
                    palm_base_ids = [5, 9, 13, 17]
                    knuckle_path = [5, 9, 13, 17]
                    pinky_idx = 17
                    index_idx = 5

                # Determine Hand Side Orientation (Left vs Right)
                if skeleton.id not in self.skeleton_hand_types:
                    wrist, pinky_mcp, index_mcp = nodes_by_id.get(0), nodes_by_id.get(pinky_idx), nodes_by_id.get(index_idx)
                    if wrist and pinky_mcp and index_mcp:
                        local_pinky = self.get_local_coords(wrist, pinky_mcp)
                        local_index = self.get_local_coords(wrist, index_mcp)
                        self.skeleton_hand_types[skeleton.id] = "Left" if (local_pinky[1] - local_index[1]) > 0 else "Right"
                    else:
                        continue

                hand_side = self.skeleton_hand_types[skeleton.id]
                self.last_seen_times[hand_side] = current_time
                self.active_skeleton_ids[hand_side] = skeleton.id

                is_left = hand_side == "Left"
                self.no_glove_messages[hand_side].set_visible(False)
                ax = self.ax_left if is_left else self.ax_right

                # Premium Hand Color Palettes
                colors = ["#f43f5e", "#ec4899", "#d946ef", "#a855f7", "#8b5cf6"] if is_left else ["#06b6d4", "#0ea5e9", "#3b82f6", "#6366f1", "#14b8a6"]
                accent_color = "#f472b6" if is_left else "#38bdf8"

                # Standardize Coordinates relative to Wrist (0, 0, 0)
                wrist_pos = nodes_by_id[0].transform.position
                node_coords = {nid: (n.transform.position.x - wrist_pos.x, n.transform.position.y - wrist_pos.y, n.transform.position.z - wrist_pos.z) for nid, n in nodes_by_id.items()}

                # 1. Update/Draw the 5 Independent Finger Chains (Wrist-attached)
                for f_idx, path in enumerate(finger_paths):
                    x_p, y_p, z_p = zip(*[node_coords[nid] for nid in path if nid in node_coords])
                    self._update_line_artist(skeleton.id, f_idx, ax, x_p, y_p, z_p, active_artist_keys, color=colors[f_idx], markeredgecolor=colors[f_idx])

                # 2. Update/Draw the Anatomical Palm Base Loop (connect finger bases)
                x_pb, y_pb, z_pb = zip(*[node_coords[nid] for nid in palm_base_ids if nid in node_coords])
                self._update_line_artist(
                    skeleton.id, 6, ax, x_pb, y_pb, z_pb, active_artist_keys, is_custom_style=True, color=accent_color, linewidth=2.0, marker="o", markersize=4, markerfacecolor="#0f172a", alpha=0.9
                )

                # 3. Update/Draw the Knuckle Loop Connection (Horizontal MCP knuckle tracking)
                x_k, y_k, z_k = zip(*[node_coords[nid] for nid in knuckle_path if nid in node_coords])
                self._update_line_artist(skeleton.id, 5, ax, x_k, y_k, z_k, active_artist_keys, is_custom_style=True, marker=None, linestyle="--", linewidth=1.2, color=accent_color, alpha=0.5)

                # 5. Update/Draw ID Labels next to each node for debugging
                for nid, coord in node_coords.items():
                    label_key = (skeleton.id, f"label_{nid}")
                    active_artist_keys.add(label_key)
                    label_pos = (coord[0], coord[1], coord[2] + 0.005)
                    if label_key in self.skeleton_artists:
                        txt = self.skeleton_artists[label_key]
                        txt.set_position((label_pos[0], label_pos[1]))
                        txt.set_3d_properties(label_pos[2], "z")
                    else:
                        txt = ax.text(label_pos[0], label_pos[1], label_pos[2], f"{nid}", color="#e2e8f0", fontsize=8, fontweight="bold", family="monospace", alpha=0.85)
                        self.skeleton_artists[label_key] = txt

                # 6. Update HUD Overlay Data
                hud_text = (
                    f"STATUS: STREAMING\nID    : {skeleton.id}\n"
                    f"WRIST : X={wrist_pos.x:+.3f}\n        Y={wrist_pos.y:+.3f}\n        Z={wrist_pos.z:+.3f} m\n"
                    f"TIME  : {skeleton.publish_time_seconds % 1000:07.3f}s"
                )
                self.hud_texts[hand_side].set_text(hud_text)
                self.hud_texts[hand_side].set_color("#10b981")

        # Hysteresis and Liveness Check for Disconnected Gloves
        for side in ["Left", "Right"]:
            if (current_time - self.last_seen_times[side]) >= 1.0:
                self.no_glove_messages[side].set_visible(True)
                self.hud_texts[side].set_text("STATUS: DISCONNECTED\nID    : -\nWRIST : X=-.---, Y=-.---, Z=-.--- m\nTIME  : -.---s")
                self.hud_texts[side].set_color("#ef4444")

        # Dynamic Garbage Collection for Obsolete Active Artists
        for key in list(self.skeleton_artists.keys()):
            sk_id, part_idx = key
            sk_side = self.skeleton_hand_types.get(sk_id, "Left")
            is_side_active = (current_time - self.last_seen_times[sk_side]) < 1.0

            # Deactivate if the hand side is dead
            should_remove = not (is_side_active and sk_id == self.active_skeleton_ids.get(sk_side))
            # Or if the frame was updated successfully but this specific part wasn't drawn
            if len(active_artist_keys) > 0 and key not in active_artist_keys and sk_id == self.active_skeleton_ids.get(sk_side):
                should_remove = True

            if should_remove:
                artist = self.skeleton_artists[key]
                if hasattr(artist, "set_data"):
                    artist.set_data([], [])
                    artist.set_3d_properties([])
                artist.remove()
                del self.skeleton_artists[key]

        return list(self.skeleton_artists.values())

    def run(self):
        if not self.start_connection():
            return

        if self.plot_ergo:
            self.setup_ergonomics_plot()
            update_func = self.update_ergonomics_plot
        else:
            self.setup_skeleton_plot()
            update_func = self.update_skeleton_plot

        self.ani = FuncAnimation(self.fig, update_func, interval=33, blit=False)

        try:
            plt.show()
        finally:
            print("Exiting visualizer. Shutting down client...")
            self.client.disconnect()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Manus SDK Python Real-time Visualizer")
    parser.add_argument("--ergo", action="store_true", help="Plot finger joint stretch/spread angles in 2D bars instead of 3D skeletons")
    parser.add_argument("--host", type=str, default="", help="Specific IP Address of Manus Core Server")
    parser.add_argument("--integrated", action="store_true", help="Run in Core Integrated standalone mode")

    args = parser.parse_args()
    vis = ManusVisualizer(plot_ergo=args.ergo, host_ip=args.host, use_integrated=args.integrated)
    vis.run()
