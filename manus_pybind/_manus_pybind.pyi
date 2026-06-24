from typing import List, Dict, Any, Optional

class Vec3:
    """Represent a 3D Vector containing x, y, and z coordinates."""
    x: float
    y: float
    z: float
    def __init__(self) -> None: ...

class Quaternion:
    """Represent a quaternion containing w, x, y, and z elements for rotation."""
    w: float
    x: float
    y: float
    z: float
    def __init__(self) -> None: ...

class Transform:
    """Represent a 3D Transform containing position (Vec3), rotation (Quaternion), and scale (Vec3)."""
    position: Vec3
    rotation: Quaternion
    scale: Vec3
    def __init__(self) -> None: ...

class SkeletonNode:
    """Represent a node in a skeleton hierarchy."""
    id: int
    transform: Transform
    def __init__(self) -> None: ...

class Skeleton:
    """Represent a full retargeted skeleton containing multiple nodes."""
    id: int
    publish_time_seconds: int
    hand: int           # 0 for Left, 1 for Right
    nodes: List[SkeletonNode]
    def __init__(self) -> None: ...

class Tracker:
    """Represent a tracking device (e.g. HMD, controllers, Vive Trackers)."""
    id: str
    user_id: int
    is_hmd: bool
    type: int  # Maps to TrackerType enum (e.g., 0=Unknown, 1=Hmd, 2=Hand...)
    position: Vec3
    rotation: Quaternion
    quality: int  # Maps to TrackingQuality enum (e.g., 0=Untrackable, 1=Poor, 2=Ok, 3=Good)
    def __init__(self) -> None: ...

class Ergonomics:
    """Represent real-time finger joint angles ergonomics data."""
    id: int
    is_user_id: bool
    hand: int           # 0 for Left, 1 for Right
    data: List[float]   # Joint angles for the active hand (length 20)
    def __init__(self) -> None: ...

class Gesture:
    """Represent an individual gesture match result."""
    name: str
    probability: float
    def __init__(self) -> None: ...

class GestureData:
    """Represent gesture stream probabilities of a user or device."""
    id: int
    is_user_id: bool
    gestures: List[Gesture]
    def __init__(self) -> None: ...

class Host:
    """Represent a Manus Core Host server discovered on the network."""
    hostname: str
    ip_address: str
    version: str
    def __init__(self) -> None: ...

class ManusClient:
    """
    Python wrapper class for the Manus Core SDK Client, exposing real-time 
    data streams including skeletons, trackers, ergonomics, and gestures.
    """
    def __init__(self) -> None: ...

    def initialize(self, connection_type: int = 2) -> bool:
        """
        Initialize the Manus Core SDK wrapper and set up coordinate systems/callbacks.
        
        Args:
            connection_type: Connection mode to use.
                             1 = Integrated (Runs standalone without requiring Core server).
                             2 = Local (Connects to Manus Core running on the local host).
                             3 = Remote (Looks for Manus Core servers on the local network).
                             
        Returns:
            True if initialized successfully, False otherwise.
        """
        ...

    def look_for_hosts(self, seconds: int = 2, local_only: bool = True) -> List[Host]:
        """
        Scan the local network or host loopback interface for active Manus Core instances.
        
        Args:
            seconds: Time to spend scanning (in seconds).
            local_only: If True, scans only the localhost. If False, broadcasts to the LAN.
            
        Returns:
            A list of Host objects discovered.
        """
        ...

    def connect_to_host(self, ip_address: str = "", connection_type: int = 2) -> bool:
        """
        Connect to a specific Manus Core Host.
        
        Args:
            ip_address: Target IP address of the host. If left empty, connects to the first found host.
            connection_type: Specifies client connection wrapper type (Integrated, Local, Remote).
            
        Returns:
            True if connected successfully, False otherwise.
        """
        ...

    def disconnect(self) -> None:
        """Disconnect from Manus Core and release all internal SDK wrapper resources."""
        ...

    def is_connected(self) -> bool:
        """
        Check if the client is currently connected to Manus Core.
        
        Returns:
            True if connected, False otherwise.
        """
        ...

    def get_skeletons(self) -> List[Skeleton]:
        """
        Retrieve the latest retargeted skeletons stream data.
        This data is mapped to target character joint structures and requires 
        T-pose calibration and character assignment to be active in Manus Core.
        
        Returns:
            List of active retargeted Skeleton data structures.
        """
        ...

    def get_raw_skeletons(self) -> List[Skeleton]:
        """
        Retrieve the latest raw (non-retargeted) glove-driven hand skeleton joint transforms.
        This data stream is pure hand glove tracking inputs and is available 
        immediately upon connecting to Manus Core without requiring character pairing.
        
        Returns:
            List of raw hand Skeleton data structures.
        """
        ...

    def get_tracker_data(self) -> List[Tracker]:
        """
        Retrieve the latest external tracking hardware data.
        
        Returns:
            List of active Tracker data structures.
        """
        ...

    def get_ergonomics_data(self) -> List[Ergonomics]:
        """
        Retrieve the latest glove finger ergonomics joint angle measurements.
        
        Returns:
            List of Ergonomics data structures.
        """
        ...

    def get_gesture_data(self) -> List[GestureData]:
        """
        Retrieve the latest matching gesture probabilities.
        
        Returns:
            List of GestureData structures containing gesture name and probability mapping.
        """
        ...

    def set_log_level(self, level: int) -> None:
        """
        Set the console log filter level for Manus SDK outputs.
        
        Args:
            level: The severity threshold.
                   0 = Debug, 1 = Info, 2 = Warn, 3 = Error.
        """
        ...
