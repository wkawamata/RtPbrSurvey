# Debug camera Arcball pitch limit

The Arcball object viewer pitch limit is 88 degrees. This keeps the camera away
from the 90-degree world-up singularity while allowing near top-down and
bottom-up inspection.

Horizontal orbit remains a rotation around world Y. Vertical orbit remains a
rotation around the right axis derived from world Y and the current view
direction. A vertical drag applies only the angle remaining before the limit,
so a large drag cannot cross the pole and reverse yaw.

Hosts that mirror the controller limit can use
`DebugCameraController::kObjectViewerPitchLimit` rather than duplicating the
numeric value.

The previous 1.4-radian value was introduced with the initial reusable camera
controller. No rationale or independent requirement for that value was found
in repository history or documentation.
