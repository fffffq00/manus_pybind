#!/usr/bin/env python3
"""
Manus SDK Python Data Visualizer
================================
Provides real-time interactive 3D visualization of Manus glove data:
- Default: Real-time plotting of the 3D hand skeleton joints for BOTH hands,
           tracking them as they move in space.
- With `--ergo` flag: Dynamic bar charts showing finger joint stretch/spread angles.

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

# The 5 finger paths starting from Hand Root (0)
# Thumb: Root (0) -> CMC (1) -> MCP (2) -> IP (3) -> Tip (4)
# Index: Root (0) -> MCP (5) -> PIP (6) -> DIP (7) -> Tip (8)
# ...
FINGER_PATHS = [[0, 1, 2, 3, 4], [0, 5, 6, 7, 8], [0, 9, 10, 11, 12], [0, 13, 14, 15, 16], [0, 17, 18, 19, 20]]  # Thumb  # Index  # Middle  # Ring  # Pinky


class ManusVisualizer:
    def __init__(self, plot_ergo=False, host_ip="", use_integrated=False):
        self.plot_ergo = plot_ergo
        self.host_ip = host_ip
        self.connection_type = 1 if use_integrated else 2  # 1: Integrated, 2: Local/Remote

        self.client = manus_pybind.ManusClient()
        self.fig = None
        self.ani = None

    def start_connection(self):
        print("Initializing Manus Core SDK...")
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
        """Set up 2D bar chart layout for left/right glove joint stretch angles."""
        self.fig, self.axes = plt.subplots(2, 5, figsize=(15, 7), sharey=True)
        self.fig.suptitle("Manus Gloves Ergonomics Real-time Joint Stretch Angles", fontsize=16)

        self.bars = []
        # Setup each finger subplot
        for hand_idx, hand_name in enumerate(["Left Hand", "Right Hand"]):
            for finger_idx, finger_name in enumerate(FINGER_NAMES):
                ax = self.axes[hand_idx, finger_idx]
                ax.set_title(f"{hand_name} - {finger_name}")
                ax.set_ylim(-30, 120)  # Typical joint angles range
                ax.grid(axis="y", linestyle="--", alpha=0.7)

                # Bars represent: Spread, MCP Stretch, PIP Stretch, DIP Stretch
                x_labels = ["Spread", "MCP", "PIP", "DIP"]
                colors = ["#ff7f0e", "#1f77b4", "#2ca02c", "#d62728"]
                rects = ax.bar(x_labels, [0, 0, 0, 0], color=colors)
                self.bars.append((hand_idx, finger_idx, rects))

        plt.tight_layout()

    def update_ergonomics_plot(self, frame):
        """Timer callback to fetch data and refresh bar heights."""
        if not self.client.is_connected():
            return []

        ergo_list = self.client.get_ergonomics_data()

        left_data = None
        right_data = None

        for ergo in ergo_list:
            if len(ergo.data) >= 40:
                left_data = ergo.data[0:20]
                right_data = ergo.data[20:40]

        updated_artists = []
        for hand_idx, finger_idx, rects in self.bars:
            data = left_data if hand_idx == 0 else right_data
            if data is not None and len(data) >= 20:
                # Finger start index inside the hand ergonomics array
                offset = finger_idx * 4
                spread = data[offset]
                mcp = data[offset + 1]
                pip = data[offset + 2]
                dip = data[offset + 3]

                joint_vals = [spread, mcp, pip, dip]
                for r, val in zip(rects, joint_vals):
                    r.set_height(val)
                    updated_artists.append(r)
            else:
                for r in rects:
                    r.set_height(0)
                    updated_artists.append(r)

        return updated_artists

    def setup_skeleton_plot(self):
        """Set up 3D plot to visualize bones and joints."""
        self.fig = plt.figure(figsize=(12, 9))
        self.ax = self.fig.add_subplot(111, projection="3d")
        self.fig.suptitle("Manus Gloves 3D Hand Skeleton (Real-time Spatial Tracking)", fontsize=16)

        # Set labels
        self.ax.set_xlabel("X (m)")
        self.ax.set_ylabel("Y (m)")
        self.ax.set_zlabel("Z (m)")

        # Initial view angle
        self.ax.view_init(elev=20, azim=-60)

        # Lines representing bones
        # Key: (skeleton_id, finger_idx) -> Line3D object
        self.skeleton_lines = {}
        # Text labels for hand side
        self.hand_labels = {}

    def update_skeleton_plot(self, frame):
        """Timer callback to fetch and plot 3D skeleton joint positions with dynamic tracking."""
        if not self.client.is_connected():
            return []

        skeletons = self.client.get_skeletons()
        if not skeletons:
            skeletons = self.client.get_raw_skeletons()

        # If no skeletons found, hide current lines
        if not skeletons:
            for line in self.skeleton_lines.values():
                line.set_data([], [])
                line.set_3d_properties([])
            for text in self.hand_labels.values():
                text.set_text("")
            return []

        # Track overall bounding box to auto-center and keep aspect ratio 1:1:1
        all_x, all_y, all_z = [], [], []
        updated_artists = []
        active_line_keys = set()
        active_skeleton_ids = set()

        for skeleton in skeletons:
            nodes = skeleton.nodes
            if not nodes or len(nodes) < 21:
                continue

            active_skeleton_ids.add(skeleton.id)

            # Extract raw node coordinates
            node_coords = {}
            for node in nodes:
                node_coords[node.id] = (node.transform.position.x, node.transform.position.y, node.transform.position.z)

            # Heuristic to detect hand side (Left vs Right)
            # Left Hand Pinky MCP (ID 17) has positive Y offset relative to wrist root (0)
            # Right Hand Pinky MCP (ID 17) has negative Y offset relative to wrist root (0)
            is_left = True
            if 17 in node_coords:
                is_left = (node_coords[17][1] - node_coords[0][1]) > 0

            # Artificially offset Left hand to +Y (left) and Right hand to -Y (right) to prevent overlapping in space
            y_offset = 0.25 if is_left else -0.25

            # Apply the offset to coordinates for plotting and bounding box calculations
            for nid in list(node_coords.keys()):
                x, y, z = node_coords[nid]
                node_coords[nid] = (x, y + y_offset, z)
                all_x.append(x)
                all_y.append(y + y_offset)
                all_z.append(z)

            hand_label = "Left Hand" if is_left else "Right Hand"
            # Left hand: Warm colors (orange/red), Right hand: Cool colors (blue/cyan)
            colors = ["#ff4500", "#d62728", "#ff7f0e", "#d62728", "#ff4500"] if is_left else ["#1f77b4", "#00bfff", "#2ca02c", "#00bfff", "#1f77b4"]

            # Update/Draw the 5 fingers
            for f_idx, path in enumerate(FINGER_PATHS):
                x_line, y_line, z_line = [], [], []
                valid = True
                for node_id in path:
                    if node_id in node_coords:
                        coord = node_coords[node_id]
                        x_line.append(coord[0])
                        y_line.append(coord[1])
                        z_line.append(coord[2])
                    else:
                        valid = False
                        break

                if valid:
                    line_key = (skeleton.id, f_idx)
                    active_line_keys.add(line_key)

                    if line_key in self.skeleton_lines:
                        line = self.skeleton_lines[line_key]
                        line.set_data(x_line, y_line)
                        line.set_3d_properties(z_line)
                    else:
                        (line,) = self.ax.plot(x_line, y_line, z_line, marker="o", linewidth=3, markersize=5, color=colors[f_idx])
                        self.skeleton_lines[line_key] = line
                    updated_artists.append(line)

            # Draw a text label above the wrist to identify the hand
            wrist_pos = node_coords[0]
            label_pos = (wrist_pos[0], wrist_pos[1], wrist_pos[2] + 0.05)
            if skeleton.id in self.hand_labels:
                txt = self.hand_labels[skeleton.id]
                txt.set_text(hand_label)
                txt.set_position((label_pos[0], label_pos[1]))
                txt.set_3d_properties(label_pos[2], "z")
            else:
                txt = self.ax.text(label_pos[0], label_pos[1], label_pos[2], hand_label, color="red" if is_left else "blue", fontweight="bold", fontsize=10)
                self.hand_labels[skeleton.id] = txt
            updated_artists.append(txt)

        # Clear inactive lines (e.g. if one glove is disconnected)
        for key in list(self.skeleton_lines.keys()):
            if key not in active_line_keys:
                self.skeleton_lines[key].set_data([], [])
                self.skeleton_lines[key].set_3d_properties([])
                del self.skeleton_lines[key]

        for sk_id in list(self.hand_labels.keys()):
            if sk_id not in active_skeleton_ids:
                self.hand_labels[sk_id].set_text("")
                del self.hand_labels[sk_id]

        # Dynamically scale coordinates and axis limits to center hands in the 3D space
        if all_x:
            min_x, max_x = min(all_x), max(all_x)
            min_y, max_y = min(all_y), max(all_y)
            min_z, max_z = min(all_z), max(all_z)

            span_x = max_x - min_x
            span_y = max_y - min_y
            span_z = max_z - min_z

            # Set a minimum bounding size to prevent the camera from zooming in too tight when still
            max_range = max(span_x, span_y, span_z, 0.15)

            mid_x = (min_x + max_x) / 2
            mid_y = (min_y + max_y) / 2
            mid_z = (min_z + max_z) / 2

            # Apply equal scaling limits
            self.ax.set_xlim3d([mid_x - max_range / 2, mid_x + max_range / 2])
            self.ax.set_ylim3d([mid_y - max_range / 2, mid_y + max_range / 2])
            self.ax.set_zlim3d([mid_z - max_range / 2, mid_z + max_range / 2])

        return updated_artists

    def run(self):
        if not self.start_connection():
            return

        if self.plot_ergo:
            self.setup_ergonomics_plot()
            update_func = self.update_ergonomics_plot
        else:
            self.setup_skeleton_plot()
            update_func = self.update_skeleton_plot

        # Create animation updating at 30 FPS (interval = 33ms)
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

    # The default is now 3D skeleton visualization
    vis = ManusVisualizer(plot_ergo=args.ergo, host_ip=args.host, use_integrated=args.integrated)
    vis.run()
